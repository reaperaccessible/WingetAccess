#include "ShortcutsDialog.h"
#include "Localization.h"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

ShortcutsDialog::ShortcutsDialog(wxWindow* parent, const std::vector<wxString>& lines)
    : wxDialog(parent, wxID_ANY, loc::tr("Keyboard shortcuts", "Raccourcis clavier"),
               wxDefaultPosition, wxSize(640, 420), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    announcer.attach(GetHandle());

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    // The static text before the list is its accessible name on wx/MSW.
    sizer->Add(new wxStaticText(this, wxID_ANY, loc::tr("&Shortcuts:", "&Raccourcis :")),
               0, wxLEFT | wxRIGHT | wxTOP, 8);
    wxArrayString items;
    for (const wxString& line : lines)
        items.Add(line);
    list = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, items, wxLB_SINGLE);
    sizer->Add(list, 1, wxEXPAND | wxALL, 8);
    auto* close = new wxButton(this, wxID_CLOSE, loc::tr("Close", "Fermer"));
    sizer->Add(close, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    SetSizer(sizer);

    SetEscapeId(wxID_CLOSE);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);
    Bind(wxEVT_CHAR_HOOK, &ShortcutsDialog::onCharHook, this);

    if (!items.IsEmpty())
        list->SetSelection(0);
    list->SetFocus();

    // Said once, after the screen reader has read the title, the list and the
    // first line.
    hintTimer.SetOwner(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&)
    {
        announcer.announce(wxString::Format(
            loc::tr("%u shortcuts. Up and Down arrows to browse, Escape to close.",
                    "%u raccourcis. Flèches haut et bas pour parcourir, Échap pour fermer."),
            list->GetCount()));
    });
    hintTimer.StartOnce(900);
    CentreOnParent();
}

WXLRESULT ShortcutsDialog::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    WXLRESULT result = 0;
    if (announcer.handleGetObject(nMsg, wParam, lParam, result))
        return result;
    return wxDialog::MSWWindowProc(nMsg, wParam, lParam);
}

void ShortcutsDialog::onCharHook(wxKeyEvent& e)
{
    const int key = e.GetKeyCode();
    if ((key == WXK_UP || key == WXK_DOWN) && !e.HasAnyModifiers() && FindFocus() == list)
    {
        const int count = static_cast<int>(list->GetCount());
        const int sel = list->GetSelection();
        const bool up = key == WXK_UP;
        if (count > 0 && sel >= 0 && ((up && sel == 0) || (!up && sel == count - 1)))
        {
            // The native list would not move: say where we are instead.
            announcer.announce((up ? loc::tr("First, ", "Premier, ")
                                   : loc::tr("Last, ", "Dernier, "))
                               + list->GetString(static_cast<unsigned>(sel)));
            return;
        }
    }
    e.Skip();
}
