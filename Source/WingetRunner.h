#pragma once

#include <wx/string.h>
#include <functional>
#include <vector>
#include <atomic>

// Runs a console program hidden (no window), captures stdout+stderr merged, and
// delivers output lines and completion on the UI thread via wxTheApp->CallAfter.
//
// One operation at a time: start() fails if a run is already in progress.
class WingetRunner
{
public:
    using LineCallback = std::function<void(const wxString& line)>;
    using DoneCallback = std::function<void(int exitCode, const wxString& fullOutput)>;

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

    // Same, for any program (PowerShell for the winget repair steps). Its
    // standard input is empty.
    bool startProgram(const wxString& program,
                      const std::vector<wxString>& args,
                      LineCallback onLine,
                      DoneCallback onDone);

    bool isBusy() const { return busy.load(); }

    // True when the last start()/startProgram() call could not create the
    // process at all (winget missing), as opposed to "busy".
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
                LineCallback onLine,
                DoneCallback onDone);

    std::atomic<bool>  busy { false };
    std::atomic<void*> processHandle { nullptr };  // HANDLE of the child
    bool               launchFailed = false;
};
