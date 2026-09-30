// Runs a command line inside a pseudo console (ConPTY) and writes everything
// it prints, raw, to a file: what winget draws when it believes it is in a
// real terminal (progress bar included), unlike a plain redirected pipe.
//
//   ptytest <output file> <command line...>
#include <windows.h>
#include <cstdio>
#include <string>
#include <thread>

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3)
    {
        wprintf(L"usage: ptytest <output file> <command line...>\n");
        return 2;
    }
    std::wstring cmd;
    for (int i = 2; i < argc; ++i)
    {
        if (i > 2) cmd += L' ';
        cmd += argv[i];
    }

    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    CreatePipe(&inRead, &inWrite, nullptr, 0);
    CreatePipe(&outRead, &outWrite, nullptr, 0);

    HPCON console = nullptr;
    if (FAILED(CreatePseudoConsole(COORD { 250, 50 }, inRead, outWrite, 0, &console)))
    {
        wprintf(L"CreatePseudoConsole failed\n");
        return 2;
    }
    CloseHandle(inRead);
    CloseHandle(outWrite);

    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    auto* attrs = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, size));
    InitializeProcThreadAttributeList(attrs, 1, 0, &size);
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console,
                              sizeof(console), nullptr, nullptr);

    STARTUPINFOEXW si {};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attrs;
    // Without this, a parent whose own output is redirected hands those
    // redirected handles down and the child bypasses the pseudo console.
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nullptr;
    si.StartupInfo.hStdOutput = nullptr;
    si.StartupInfo.hStdError = nullptr;
    PROCESS_INFORMATION pi {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi))
    {
        wprintf(L"CreateProcess failed %lu\n", GetLastError());
        return 2;
    }

    FILE* out = _wfopen(argv[1], L"wb");
    std::thread reader([&]()
    {
        char buffer[4096];
        DWORD got = 0;
        while (ReadFile(outRead, buffer, sizeof(buffer), &got, nullptr) && got > 0)
            fwrite(buffer, 1, got, out);
    });

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    ClosePseudoConsole(console);   // ends the reader: the pipe closes
    reader.join();
    fclose(out);
    CloseHandle(inWrite);
    CloseHandle(outRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    DeleteProcThreadAttributeList(attrs);
    HeapFree(GetProcessHeap(), 0, attrs);
    wprintf(L"exit=%lu\n", code);
    return 0;
}
