// Console harness for WingetSetup.
//
//   setuptest version "<winget --version output>"   parse a version string
//   setuptest dump <step> <file>                     write a script (UTF-8)
//   setuptest run <step>                             run it as the app does
//
// <step>: register | module | bundle | update
#include "WingetSetup.h"

#include <wx/init.h>
#include <wx/file.h>
#include <windows.h>
#include <cstdio>
#include <string>

namespace
{

wxString scriptFor(const wxString& step)
{
    if (step == "register") return wingetsetup::scriptRegisterAppInstaller();
    if (step == "module")   return wingetsetup::scriptRepairWithModule();
    if (step == "bundle")   return wingetsetup::scriptInstallOfficialBundle();
    if (step == "update")   return wingetsetup::scriptUpdateAppInstaller("winget");
    return wxString();
}

std::wstring quote(const wxString& s)
{
    return L"\"" + s.ToStdWstring() + L"\"";
}

} // namespace

int main(int argc, char** argv)
{
    wxInitializer init;
    if (argc < 3)
    {
        printf("usage: setuptest version <text> | dump <step> <file> | run <step>\n");
        return 2;
    }
    const wxString command = wxString::FromUTF8(argv[1]);
    const wxString arg = wxString::FromUTF8(argv[2]);

    if (command == "version")
    {
        const wingetsetup::Version v = wingetsetup::parseVersion(arg);
        printf("found=%d major=%d minor=%d text=[%s] recentEnough=%d\n",
               v.found, v.major, v.minor, (const char*)v.text.utf8_str(), v.recentEnough());
        return 0;
    }

    const wxString script = scriptFor(arg);
    if (script.empty())
    {
        printf("unknown step\n");
        return 2;
    }

    if (command == "dump" && argc >= 4)
    {
        wxFile out(wxString::FromUTF8(argv[3]), wxFile::write);
        out.Write(script, wxConvUTF8);
        return 0;
    }

    if (command == "run")
    {
        std::wstring cmd = quote(wingetsetup::powershellPath());
        for (const wxString& a : wingetsetup::powershellArgs(script))
            cmd += L" " + quote(a);

        STARTUPINFOW si {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi {};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0,
                            nullptr, nullptr, &si, &pi))
        {
            printf("CreateProcess failed\n");
            return 2;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        printf("exit=%lu\n", code);
        return static_cast<int>(code);
    }

    printf("unknown command\n");
    return 2;
}
