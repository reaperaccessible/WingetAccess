#include "WingetRunner.h"
#include "Localization.h"
#include "TerminalStream.h"

#include <wx/app.h>
#include <wx/utils.h>
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

// winget emits UTF-8 when its output is redirected.
wxString decodeChunk(const std::string& bytes)
{
    wxString text = wxString::FromUTF8(bytes.c_str(), bytes.size());
    if (text.empty() && !bytes.empty())
        text = wxString(bytes.c_str(), wxConvLocal, bytes.size());
    return text;
}

// ConPTY exists from Windows 10 1809 (winget's own minimum); looked up at run
// time so the program still starts, on pipes, where it does not.
using CreatePseudoConsoleFn = HRESULT (WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
using ClosePseudoConsoleFn  = void (WINAPI*)(HPCON);

struct ConPtyApi
{
    CreatePseudoConsoleFn create = nullptr;
    ClosePseudoConsoleFn  close  = nullptr;
    ConPtyApi()
    {
        if (HMODULE k = GetModuleHandleW(L"kernel32.dll"))
        {
            create = reinterpret_cast<CreatePseudoConsoleFn>(GetProcAddress(k, "CreatePseudoConsole"));
            close  = reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(k, "ClosePseudoConsole"));
        }
    }
    bool available() const { return create != nullptr && close != nullptr; }
};

const ConPtyApi& conPty()
{
    static const ConPtyApi api;
    return api;
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

wxString WingetRunner::wingetPath()
{
    wxString localAppData;
    if (wxGetEnv("LOCALAPPDATA", &localAppData))
    {
        const wxString alias = localAppData + L"\\Microsoft\\WindowsApps\\winget.exe";
        // The alias is a reparse point: GetFileAttributes sees it, while
        // wxFileExists (which opens the target) can miss it.
        if (GetFileAttributesW(alias.wc_str()) != INVALID_FILE_ATTRIBUTES)
            return alias;
    }
    return "winget";
}

bool WingetRunner::start(const std::vector<wxString>& args,
                         LineCallback onLine,
                         DoneCallback onDone)
{
    return launch(wingetPath(), args, "Y\r\nY\r\nY\r\n", false, onLine, nullptr, onDone);
}

bool WingetRunner::startWithProgress(const std::vector<wxString>& args,
                                     LineCallback onLine,
                                     ProgressCallback onProgress,
                                     DoneCallback onDone)
{
    return launch(wingetPath(), args, "Y\r\nY\r\nY\r\n", conPty().available(),
                  onLine, onProgress, onDone);
}

bool WingetRunner::startProgram(const wxString& program,
                                const std::vector<wxString>& args,
                                LineCallback onLine,
                                DoneCallback onDone)
{
    return launch(program, args, std::string(), false, onLine, nullptr, onDone);
}

bool WingetRunner::launch(const wxString& program,
                          const std::vector<wxString>& args,
                          const std::string& stdinText,
                          bool pseudoConsole,
                          LineCallback onLine,
                          ProgressCallback onProgress,
                          DoneCallback onDone)
{
    launchFailed = false;
    bool expected = false;
    if (!busy.compare_exchange_strong(expected, true))
        return false;

    // Build the command line: <program> <args...>
    std::wstring cmd = quoteArg(program);
    for (const wxString& a : args)
    {
        cmd += L' ';
        cmd += quoteArg(a);
    }

    auto internalError = [&]()
    {
        busy.store(false);
        if (onLine)
            onLine(loc::tr("Internal error: CreatePipe failed.", "Erreur interne : CreatePipe a échoué."));
        return false;
    };

    // In a pseudo console the handles are not inherited: ConPTY owns them.
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = pseudoConsole ? FALSE : TRUE;

    // Output pipe (the child's stdout+stderr, or what the pseudo console
    // renders) and input pipe (prepared answers, then end of file).
    HANDLE outRead = nullptr, outWrite = nullptr, inRead = nullptr, inWrite = nullptr;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0))
        return internalError();
    if (!CreatePipe(&inRead, &inWrite, &sa, 0))
    {
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return internalError();
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    if (!stdinText.empty())
    {
        DWORD written = 0;
        WriteFile(inWrite, stdinText.data(), static_cast<DWORD>(stdinText.size()), &written, nullptr);
    }

    HPCON console = nullptr;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = nullptr;
    STARTUPINFOEXW si {};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    DWORD flags = CREATE_NO_WINDOW;

    if (pseudoConsole
        && SUCCEEDED(conPty().create(COORD { 250, 50 }, inRead, outWrite, 0, &console)))
    {
        // Wide enough that winget never wraps a line.
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        attrs = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, size));
        InitializeProcThreadAttributeList(attrs, 1, 0, &size);
        UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console,
                                  sizeof(console), nullptr, nullptr);
        si.lpAttributeList = attrs;
        // Null standard handles: otherwise a parent whose own output is
        // redirected hands them down and the child bypasses the console.
        flags = EXTENDED_STARTUPINFO_PRESENT;
    }
    else
    {
        pseudoConsole = false;
        si.StartupInfo.hStdOutput = outWrite;
        si.StartupInfo.hStdError  = outWrite;
        si.StartupInfo.hStdInput  = inRead;
    }

    PROCESS_INFORMATION pi {};
    std::wstring mutableCmd = cmd;  // CreateProcessW may modify the buffer
    const BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                                   pseudoConsole ? FALSE : TRUE, flags, nullptr, nullptr,
                                   &si.StartupInfo, &pi);
    // The child (or the pseudo console) has its own copies now.
    CloseHandle(outWrite);
    CloseHandle(inRead);
    if (attrs != nullptr)
    {
        DeleteProcThreadAttributeList(attrs);
        HeapFree(GetProcessHeap(), 0, attrs);
    }

    if (!ok)
    {
        if (console != nullptr)
            conPty().close(console);
        CloseHandle(outRead);
        CloseHandle(inWrite);
        launchFailed = true;
        busy.store(false);
        return false;
    }

    CloseHandle(pi.hThread);
    processHandle.store(pi.hProcess);

    std::thread([this, outRead, inWrite, console, process = pi.hProcess,
                 onLine, onProgress, onDone]()
    {
        std::string all;   // full raw output (the lists parse it)

        TerminalStream stream(
            [onLine](const wxString& line)
            {
                if (onLine != nullptr && wxTheApp != nullptr)
                    wxTheApp->CallAfter([onLine, line]() { onLine(line); });
            },
            [onProgress](int state, int percent)
            {
                if (onProgress != nullptr && wxTheApp != nullptr)
                    wxTheApp->CallAfter([onProgress, state, percent]() { onProgress(state, percent); });
            });

        // Output is read on its own thread: with a pseudo console the pipe only
        // ends once the console is closed, which must follow the process exit.
        std::thread reader([&]()
        {
            char buffer[4096];
            DWORD got = 0;
            while (ReadFile(outRead, buffer, sizeof(buffer), &got, nullptr) && got > 0)
            {
                all.append(buffer, got);
                stream.feed(buffer, got);
            }
        });

        WaitForSingleObject(process, INFINITE);
        DWORD exitCode = static_cast<DWORD>(-1);
        GetExitCodeProcess(process, &exitCode);
        processHandle.store(nullptr);
        CloseHandle(process);

        if (console != nullptr)
            conPty().close(console);   // flushes the last output, ends the reader
        reader.join();
        stream.finish();
        CloseHandle(outRead);
        CloseHandle(inWrite);

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
