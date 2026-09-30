#include "App.h"
#include "MainWindow.h"

wxIMPLEMENT_APP(App);

bool App::OnInit()
{
    if (!wxApp::OnInit())
        return false;

    // --updated: started by the previous version right after a self-update.
    bool justUpdated = false;
    for (int i = 1; i < argc; ++i)
        if (argv[i] == "--updated")
            justUpdated = true;

    auto* window = new MainWindow(justUpdated);
    window->Show(true);
    return true;
}
