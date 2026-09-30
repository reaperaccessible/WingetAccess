#include "SelfUpdate.h"
#include "Version.h"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <functional>
#include <thread>
#include <vector>

namespace selfupdate
{

namespace
{

const wchar_t* kApiUrl =
    L"https://api.github.com/repos/reaperaccessible/WingetAccess/releases/latest";
const char* kAssetName = "WingetAccess.exe";

wxString exePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;)
    {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(),
                                           static_cast<DWORD>(buffer.size()));
        if (n == 0)
            return wxString();
        if (n < buffer.size())
            return wxString(buffer.data(), n);
        buffer.resize(buffer.size() * 2);
    }
}

wxString lastErrorText(DWORD code)
{
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                   | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    wxString msg = text != nullptr ? wxString(text) : wxString::Format("erreur %lu", code);
    if (text != nullptr)
        LocalFree(text);
    return msg.Strip(wxString::both);
}

// GET `url` (redirects followed), streaming the body into `sink`. Returns false
// with `error` set on a network error or a status other than 200.
bool httpGet(const wxString& url, const wchar_t* accept,
             const std::function<bool(const char*, DWORD)>& sink, wxString& error)
{
    const std::wstring wurl = url.ToStdWstring();
    URL_COMPONENTS parts {};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts))
    {
        error = L"adresse invalide";
        return false;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo != nullptr)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const bool https = parts.nScheme == INTERNET_SCHEME_HTTPS;

    const std::wstring agent =
        (wxString("WingetAccess/") + WINGETACCESS_VERSION_STR).ToStdWstring();
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
        session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
    {
        error = lastErrorText(GetLastError());
        return false;
    }

    bool ok = false;
    HINTERNET connect = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    HINTERNET request = connect == nullptr ? nullptr
        : WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (request != nullptr)
    {
        std::wstring headers = std::wstring(L"Accept: ") + accept + L"\r\n";
        if (WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1),
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            && WinHttpReceiveResponse(request, nullptr))
        {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                WINHTTP_NO_HEADER_INDEX);
            if (status != 200)
                error = wxString::Format(L"le serveur a répondu %lu", status);
            else
            {
                ok = true;
                std::vector<char> buffer(64 * 1024);
                for (;;)
                {
                    DWORD got = 0;
                    if (!WinHttpReadData(request, buffer.data(),
                                         static_cast<DWORD>(buffer.size()), &got))
                    {
                        error = lastErrorText(GetLastError());
                        ok = false;
                        break;
                    }
                    if (got == 0)
                        break;
                    if (!sink(buffer.data(), got))
                    {
                        error = L"écriture impossible";
                        ok = false;
                        break;
                    }
                }
            }
        }
        else
            error = lastErrorText(GetLastError());
    }
    else
        error = lastErrorText(GetLastError());

    if (request != nullptr) WinHttpCloseHandle(request);
    if (connect != nullptr) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

// Streaming SHA-256 through Windows CNG.
class Sha256
{
public:
    Sha256()
    {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0)
            BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
    }
    ~Sha256()
    {
        if (hash != nullptr) BCryptDestroyHash(hash);
        if (alg != nullptr) BCryptCloseAlgorithmProvider(alg, 0);
    }
    void update(const char* data, DWORD size)
    {
        if (hash != nullptr)
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data)), size, 0);
    }
    std::string hex()
    {
        unsigned char digest[32] = {};
        if (hash == nullptr || BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0)
            return std::string();
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (unsigned char b : digest)
        {
            out += digits[b >> 4];
            out += digits[b & 15];
        }
        return out;
    }

private:
    BCRYPT_ALG_HANDLE  alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
};

} // namespace

bool parseVersion(const wxString& text, int& major, int& minor)
{
    wxString t = text.Strip(wxString::both);
    if (t.StartsWith("v") || t.StartsWith("V"))
        t = t.Mid(1);
    long ma = 0, mi = 0;
    const wxString majorText = t.BeforeFirst('.');
    const wxString minorText = t.AfterFirst('.').BeforeFirst('.');
    if (!majorText.ToLong(&ma) || !minorText.ToLong(&mi))
        return false;
    major = static_cast<int>(ma);
    minor = static_cast<int>(mi);
    return true;
}

bool isNewer(const Release& r)
{
    int major = 0, minor = 0;
    if (!r.ok || !parseVersion(WINGETACCESS_VERSION_STR, major, minor))
        return false;
    return r.major > major || (r.major == major && r.minor > minor);
}

Release fetchLatest()
{
    Release r;
    std::string body;
    if (!httpGet(kApiUrl, L"application/vnd.github+json",
                 [&body](const char* data, DWORD size) { body.append(data, size); return true; },
                 r.error))
        return r;

    try
    {
        const nlohmann::json release = nlohmann::json::parse(body);
        r.tag = wxString::FromUTF8(release.value("tag_name", std::string()));
        if (!parseVersion(r.tag, r.major, r.minor))
        {
            r.error = wxString::Format(L"étiquette de version illisible : %s", r.tag);
            return r;
        }
        for (const auto& asset : release.value("assets", nlohmann::json::array()))
        {
            if (wxString::FromUTF8(asset.value("name", std::string())).CmpNoCase(kAssetName) != 0)
                continue;
            r.assetUrl = wxString::FromUTF8(asset.value("browser_download_url", std::string()));
            r.assetSize = asset.value("size", 0LL);
            std::string digest = asset.contains("digest") && asset["digest"].is_string()
                                     ? asset["digest"].get<std::string>() : std::string();
            if (digest.rfind("sha256:", 0) == 0)
                r.sha256 = digest.substr(7);
            break;
        }
        if (r.assetUrl.empty())
        {
            r.error = wxString::Format(L"la version %s ne contient pas WingetAccess.exe", r.tag);
            return r;
        }
        r.ok = true;
    }
    catch (const std::exception&)
    {
        r.error = L"réponse de GitHub illisible";
    }
    return r;
}

wxString download(const Release& r, wxString& error)
{
    const wxString target = exePath() + ".new";
    HANDLE file = CreateFileW(target.wc_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = wxString::Format(L"impossible d'écrire dans le dossier de WingetAccess (%s)",
                                 lastErrorText(GetLastError()));
        return wxString();
    }

    Sha256 sha;
    long long written = 0;
    const bool ok = httpGet(r.assetUrl, L"application/octet-stream",
        [&](const char* data, DWORD size)
        {
            DWORD out = 0;
            if (!WriteFile(file, data, size, &out, nullptr) || out != size)
                return false;
            sha.update(data, size);
            written += size;
            return true;
        }, error);
    CloseHandle(file);

    if (ok && r.assetSize > 0 && written != r.assetSize)
        error = wxString::Format(L"fichier incomplet (%lld octets sur %lld)", written, r.assetSize);
    else if (ok && !r.sha256.empty() && sha.hex() != r.sha256)
        error = L"l'empreinte SHA-256 ne correspond pas";
    else if (ok)
        return target;

    DeleteFileW(target.wc_str());
    return wxString();
}

bool installAndRestart(const wxString& newFile, wxString& error)
{
    const wxString exe = exePath();
    const wxString old = exe + ".old";

    // A running exe cannot be overwritten, but it can be renamed.
    DeleteFileW(old.wc_str());
    if (!MoveFileExW(exe.wc_str(), old.wc_str(), MOVEFILE_REPLACE_EXISTING))
    {
        error = wxString::Format(L"impossible de renommer WingetAccess.exe (%s)",
                                 lastErrorText(GetLastError()));
        DeleteFileW(newFile.wc_str());
        return false;
    }
    if (!MoveFileExW(newFile.wc_str(), exe.wc_str(), 0))
    {
        error = wxString::Format(L"impossible de mettre la nouvelle version en place (%s)",
                                 lastErrorText(GetLastError()));
        MoveFileExW(old.wc_str(), exe.wc_str(), 0);
        DeleteFileW(newFile.wc_str());
        return false;
    }

    std::wstring cmd = L"\"" + exe.ToStdWstring() + L"\" --updated";
    const wxString dir = exe.BeforeLast('\\');
    STARTUPINFOW si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};
    if (!CreateProcessW(exe.wc_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        dir.wc_str(), &si, &pi))
    {
        error = wxString::Format(L"impossible de lancer la nouvelle version (%s)",
                                 lastErrorText(GetLastError()));
        // Put the running version back under its name.
        MoveFileExW(exe.wc_str(), newFile.wc_str(), MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(old.wc_str(), exe.wc_str(), 0);
        DeleteFileW(newFile.wc_str());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void cleanupOldCopy()
{
    const wxString exe = exePath();
    if (exe.empty())
        return;
    const std::wstring old = (exe + ".old").ToStdWstring();
    const std::wstring stale = (exe + ".new").ToStdWstring();
    std::thread([old, stale]()
    {
        DeleteFileW(stale.c_str());   // interrupted download
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            if (GetFileAttributesW(old.c_str()) == INVALID_FILE_ATTRIBUTES)
                return;
            if (DeleteFileW(old.c_str()))
                return;
            Sleep(500);   // the previous process may still be exiting
        }
    }).detach();
}

} // namespace selfupdate
