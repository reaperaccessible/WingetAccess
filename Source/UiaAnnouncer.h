#pragma once

#include <wx/string.h>
#include <wx/defs.h>

// Screen-reader announcements for one top-level window.
//
// A bare wx window is MSAA-only, and NVDA silently ignores a UIA notification
// raised on a mere host provider (ReaperAccessible Installer Manager 1.30
// trap). The window must answer WM_GETOBJECT for the UIA root with a real
// provider, and notifications are raised on that same provider. Every window
// that announces anything (main window, shortcuts list) owns one of these.
class UiaAnnouncer
{
public:
    UiaAnnouncer() = default;
    ~UiaAnnouncer();
    UiaAnnouncer(const UiaAnnouncer&) = delete;
    UiaAnnouncer& operator=(const UiaAnnouncer&) = delete;

    // Call once the window's HWND exists.
    void attach(WXHWND hwnd);

    // From the window's MSWWindowProc: true when the message was the UIA root
    // request, answered in `result`.
    bool handleGetObject(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam, WXLRESULT& result);

    // Queued announcement.
    void announce(const wxString& text);

    // Progress: "most recent" of one activity, so a screen reader may drop a
    // stale percentage instead of queueing it.
    void announceProgress(const wxString& text);

private:
    void raise(const wxString& text, bool progress);

    WXHWND hwnd = nullptr;
    void*  provider = nullptr;   // IRawElementProviderSimple*
};
