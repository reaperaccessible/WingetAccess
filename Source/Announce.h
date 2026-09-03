#pragma once

#include <wx/string.h>

// Screen-reader announcements for dynamic messages that are NOT tied to a focus
// change (operation progress, results, errors).
//
// Implemented with the native Windows UI Automation notification event
// (UiaRaiseNotificationEvent) raised on our active window, so it works with
// NVDA, JAWS and Narrator alike and needs no third-party DLL. All static UI and
// focus-driven reading is handled by wxWidgets' native MSAA/UIA controls with no
// code at all.
namespace a11y
{
    // Announces `text` on the thread's active top-level window. No-op if empty
    // or if there is no active window to attach the notification to.
    void announce(const wxString& text);
}
