// Drives the real WingetRunner (pseudo console + TerminalStream) on a winget
// command that changes nothing on the machine, e.g. a download to a temp
// folder, and prints what the window would receive: lines and progress.
//   runnertest winget-args...
#include "WingetRunner.h"

#include <wx/app.h>
#include <wx/init.h>
#include <cstdio>
#include <vector>

class RunnerTestApp : public wxAppConsole
{
public:
    bool OnInit() override
    {
        std::vector<wxString> args;
        for (int i = 1; i < argc; ++i)
            args.push_back(argv[i]);
        const bool started = runner.startWithProgress(
            args,
            [](const wxString& line) { printf("LIGNE    %s\n", (const char*)line.utf8_str()); fflush(stdout); },
            [](int state, int percent) { printf("PROGRES  etat=%d %d%%\n", state, percent); fflush(stdout); },
            [this](int code, const wxString&) { printf("FIN      code=%d\n", code); ExitMainLoop(); });
        if (!started)
        {
            printf("lancement impossible\n");
            return false;
        }
        return true;
    }

private:
    WingetRunner runner;
};

wxIMPLEMENT_APP_CONSOLE(RunnerTestApp);
