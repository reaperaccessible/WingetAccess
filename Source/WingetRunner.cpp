#include "WingetRunner.h"

#include <wx/app.h>
#include <windows.h>
#include <string>
#include <thread>

namespace
{

// Quotes one argument for a CreateProcess command line (standard CRT rules).
std::wstring quoteArg(const wxString& arg)
{
    const std::wstring s = arg.ToStdWstring();
    if (!s.empty() && s.find_first_of(L" \t\"") == std::wstring::npos)
        return s;

    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : s)
    {
        if (c == L'\\')
            ++backslashes;
        else if (c == L'"')
        {
            out.append(backslashes * 2 + 1, L'\\');
            out += L'"';
            backslashes = 0;
        }
        else
        {
            out.append(backslashes, L'\\');
            out += c;
            backslashes = 0;
        }
        if (c == L'\\')
            continue;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

// winget emits UTF-8 when its output is redirected; strip CR, backspaces and
// stray VT escape sequences defensively before splitting into lines.
wxString decodeChunk(const std::string& bytes)
{
    wxString text = wxString::FromUTF8(bytes.c_str(), bytes.size());
    if (text.empty() && !bytes.empty())
        text = wxString(bytes.c_str(), wxConvLocal, bytes.size());
    return text;
}

} // namespace

WingetRunner::~WingetRunner()
{
    terminate();
}

void WingetRunner::terminate()
{
    HANDLE h = static_cast<HANDLE>(processHandle.load());
    if (h != nullptr)
        TerminateProcess(h, 1);
}

bool WingetRunner::start(const std::vector<wxString>& args,
                         LineCallback onLine,
                         DoneCallback onDone)
{
    bool expected = false;
    if (!busy.compare_exchange_strong(expected, true))
        return false;

    // Build the command line: winget <args...>
    std::wstring cmd = L"winget";
    for (const wxString& a : args)
    {
        cmd += L' ';
        cmd += quoteArg(a);
    }

    // Inheritable pipe for the child's stdout+stderr.
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0))
    {
        busy.store(false);
        if (onLine)
            onLine(L"Erreur interne : CreatePipe a échoué.");
        return false;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd;
    si.hStdError  = writeEnd;
    si.hStdInput  = INVALID_HANDLE_VALUE;

    PROCESS_INFORMATION pi {};
    std::wstring mutableCmd = cmd;  // CreateProcessW may modify the buffer
    const BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                                   TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                   &si, &pi);
    CloseHandle(writeEnd);

    if (!ok)
    {
        CloseHandle(readEnd);
        busy.store(false);
        if (onLine)
            onLine(L"Impossible de lancer winget. Vérifie que winget est installé (App Installer).");
        return false;
    }

    CloseHandle(pi.hThread);
    processHandle.store(pi.hProcess);

    std::thread([this, readEnd, process = pi.hProcess, onLine, onDone]()
    {
        std::string pending;   // bytes not yet terminated by \n
        std::string all;       // full raw output

        char buffer[4096];
        DWORD got = 0;
        while (ReadFile(readEnd, buffer, sizeof(buffer), &got, nullptr) && got > 0)
        {
            pending.append(buffer, got);
            all.append(buffer, got);

            // Deliver complete lines as they arrive.
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos)
            {
                std::string raw = pending.substr(0, nl);
                pending.erase(0, nl + 1);

                // Progress redraws use CR without LF; keep only the last
                // segment. Drop backspace/escape control bytes.
                const size_t cr = raw.find_last_of('\r');
                if (cr != std::string::npos)
                    raw.erase(0, cr + 1);
                std::string clean;
                clean.reserve(raw.size());
                for (char c : raw)
                    if (c != '\b' && c != '\x1b' && c != '\r')
                        clean += c;

                if (onLine != nullptr && !clean.empty() && wxTheApp != nullptr)
                {
                    const wxString line = decodeChunk(clean);
                    if (!line.Strip(wxString::both).empty())
                        wxTheApp->CallAfter([onLine, line]() { onLine(line); });
                }
            }
        }
        CloseHandle(readEnd);

        WaitForSingleObject(process, INFINITE);
        DWORD exitCode = static_cast<DWORD>(-1);
        GetExitCodeProcess(process, &exitCode);
        processHandle.store(nullptr);
        CloseHandle(process);

        const wxString fullOutput = decodeChunk(all);
        busy.store(false);
        if (onDone != nullptr && wxTheApp != nullptr)
        {
            const int code = static_cast<int>(exitCode);
            wxTheApp->CallAfter([onDone, code, fullOutput]() { onDone(code, fullOutput); });
        }
    }).detach();

    return true;
}
