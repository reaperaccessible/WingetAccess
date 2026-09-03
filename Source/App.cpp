#include "App.h"
#include "MainWindow.h"

wxIMPLEMENT_APP(App);

bool App::OnInit()
{
    if (!wxApp::OnInit())
        return false;

    auto* window = new MainWindow();
    window->Show(true);
    return true;
}
