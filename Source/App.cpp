#include "App.h"
#include "MainWindow.h"

wxIMPLEMENT_APP(App);

void App::OnInitCmdLine(wxCmdLineParser& parser)
{
    wxApp::OnInitCmdLine(parser);
    parser.AddLongSwitch("updated", "started by the previous version after a self-update");
}

bool App::OnCmdLineParsed(wxCmdLineParser& parser)
{
    justUpdated = parser.FoundSwitch("updated") == wxCMD_SWITCH_ON;
    return wxApp::OnCmdLineParsed(parser);
}

bool App::OnInit()
{
    if (!wxApp::OnInit())
        return false;

    auto* window = new MainWindow(justUpdated);
    window->Show(true);
    return true;
}
