#include "App.h"
#include "MainWindow.h"
#include "Localization.h"

#include <wx/uilocale.h>

wxIMPLEMENT_APP(App);

void App::OnInitCmdLine(wxCmdLineParser& parser)
{
    wxApp::OnInitCmdLine(parser);
    parser.AddLongSwitch("updated", "started by the previous version after a self-update");
    parser.AddLongOption("lang", "interface language: fr or en (default: Windows language)");
}

bool App::OnCmdLineParsed(wxCmdLineParser& parser)
{
    justUpdated = parser.FoundSwitch("updated") == wxCMD_SWITCH_ON;
    parser.Found("lang", &forcedLanguage);
    return wxApp::OnCmdLineParsed(parser);
}

bool App::OnInit()
{
    // Adopt the Windows UI language before the first loc::tr(), which resolves
    // and caches it (see Localization.h).
    wxUILocale::UseDefault();

    if (!wxApp::OnInit())
        return false;

    if (forcedLanguage.CmpNoCase("fr") == 0)
        loc::setLanguage(loc::Language::French);
    else if (forcedLanguage.CmpNoCase("en") == 0)
        loc::setLanguage(loc::Language::English);

    auto* window = new MainWindow(justUpdated);
    window->Show(true);
    return true;
}
