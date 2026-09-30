#pragma once

#include <wx/string.h>
#include <functional>
#include <vector>
#include <atomic>

// Runs a console program hidden (no window), and delivers its output lines,
// progress and completion on the UI thread via wxTheApp->CallAfter.
//
// Two ways to run winget:
//  * start(): through a pipe. winget then prints plain lines and no progress.
//    Used for the lists and the search, whose columns are parsed.
//  * startWithProgress(): inside a pseudo console (ConPTY). winget believes it
//    is in a terminal and reports its progress (OSC 9;4), turned into
//    onProgress calls. Used for install / upgrade / uninstall. Falls back to a
//    pipe where ConPTY does not exist.
// Either way the output goes through TerminalStream, so the lines handed to
// onLine are clean text.
//
// One operation at a time: a start fails if a run is already in progress.
class WingetRunner
{
public:
    using LineCallback     = std::function<void(const wxString& line)>;
    using ProgressCallback = std::function<void(int state, int percent)>;
    using DoneCallback     = std::function<void(int exitCode, const wxString& fullOutput)>;

    WingetRunner() = default;
    ~WingetRunner();

    // Runs winget with `args` (each one quoted as needed). Its standard input
    // is pre-filled with a few "Y" answers: a first run on a fresh machine can
    // still stop on a source-agreement prompt that --accept-source-agreements
    // does not cover on older winget builds.
    // Returns false if busy or if the process failed to start (then `onDone`
    // is NOT called; lastLaunchFailed() tells the two cases apart).
    bool start(const std::vector<wxString>& args,
               LineCallback onLine,
               DoneCallback onDone);

    // Same, inside a pseudo console, with progress events.
    bool startWithProgress(const std::vector<wxString>& args,
                           LineCallback onLine,
                           ProgressCallback onProgress,
                           DoneCallback onDone);

    // Same as start(), for any program (PowerShell for the winget repair
    // steps). Its standard input is empty.
    bool startProgram(const wxString& program,
                      const std::vector<wxString>& args,
                      LineCallback onLine,
                      DoneCallback onDone);

    bool isBusy() const { return busy.load(); }

    // True when the last start call could not create the process at all
    // (winget missing), as opposed to "busy".
    bool lastLaunchFailed() const { return launchFailed; }

    // Kills a running process (used at shutdown only).
    void terminate();

    // Full path of winget.exe: the App Installer execution alias in the user's
    // WindowsApps folder when present (it appears there right after App
    // Installer is registered, before PATH is refreshed), else "winget".
    static wxString wingetPath();

private:
    bool launch(const wxString& program,
                const std::vector<wxString>& args,
                const std::string& stdinText,
                bool pseudoConsole,
                LineCallback onLine,
                ProgressCallback onProgress,
                DoneCallback onDone);

    std::atomic<bool>  busy { false };
    std::atomic<void*> processHandle { nullptr };  // HANDLE of the child
    bool               launchFailed = false;
};
