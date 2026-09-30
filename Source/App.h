#pragma once

#include <wx/app.h>
#include <wx/cmdline.h>

class App : public wxApp
{
public:
    bool OnInit() override;

    // wxApp::OnInit parses the command line and REJECTS unknown options with an
    // error box, then quits: --updated must be declared here.
    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;

private:
    bool     justUpdated = false;   // --updated: started by the previous version
    wxString forcedLanguage;        // --lang=fr|en, else the Windows language
};
