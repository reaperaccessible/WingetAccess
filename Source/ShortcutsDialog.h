#pragma once

#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/timer.h>
#include <vector>

#include "UiaAnnouncer.h"

// Ctrl+Shift+H: the keyboard shortcuts as a list browsed with the arrows, like
// the other products of the family. Short, fixed title (braille keeps the
// selected line in view); the count and how to use the list are said once, by
// voice, after the list itself has been read. Never a silent edge: Up on the
// first line says "First, …", Down on the last "Last, …". Escape closes.
class ShortcutsDialog : public wxDialog
{
public:
    ShortcutsDialog(wxWindow* parent, const std::vector<wxString>& lines);

protected:
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;

private:
    void onCharHook(wxKeyEvent& e);

    UiaAnnouncer announcer;
    wxListBox*   list = nullptr;
    wxTimer      hintTimer;
};
