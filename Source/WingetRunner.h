#pragma once

#include <wx/string.h>
#include <functional>
#include <vector>
#include <atomic>

// Runs winget.exe hidden (no console), captures stdout+stderr merged, and
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

    // `args` are the winget arguments (without the program name); each one is
    // quoted as needed. Returns false if busy or if the process failed to
    // start (then `onDone` is NOT called and an error line is sent to
    // `onLine`).
    bool start(const std::vector<wxString>& args,
               LineCallback onLine,
               DoneCallback onDone);

    bool isBusy() const { return busy.load(); }

    // Kills a running winget (used at shutdown only).
    void terminate();

private:
    std::atomic<bool>  busy { false };
    std::atomic<void*> processHandle { nullptr };  // HANDLE of the child
};
