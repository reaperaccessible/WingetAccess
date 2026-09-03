#pragma once

#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/timer.h>

#include "WingetRunner.h"
#include "WingetParser.h"

class MainWindow : public wxFrame
{
public:
    MainWindow();
    ~MainWindow() override;

protected:
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;

private:
    // Screen-reader announcement of a dynamic message. Raised on our own UIA
    // provider: a raw wx window is MSAA-only and NVDA silently ignores
    // UiaRaiseNotificationEvent from a bare host provider (Manager 1.30 trap).
    void announce(const wxString& text);
    void* uiaProvider_ = nullptr;

    // --- UI ------------------------------------------------------------------
    wxNotebook* notebook       = nullptr;
    wxListView* listInstalled  = nullptr;
    wxListView* listUpgrades   = nullptr;
    wxListView* listSearch     = nullptr;
    wxTextCtrl* searchBox      = nullptr;
    wxTextCtrl* descriptionBox = nullptr;
    wxTextCtrl* journal        = nullptr;

    // Parsed tables backing each list (row index == list row index).
    wingetparser::Table tableInstalled;
    wingetparser::Table tableUpgrades;
    wingetparser::Table tableSearch;

    WingetRunner runner;
    bool refreshUpgradesAfterInstalled = false;  // startup chain
    bool actionInProgress = false;               // an install/upgrade/uninstall runs

    // Package description under the search results, fetched with `winget show`
    // on a debounce so arrowing through the list does not spawn one process per
    // row. Separate runner: never blocks real actions.
    WingetRunner showRunner;
    wxTimer      descTimer;
    wxString     descPendingId;   // selection waiting for its description
    wxString     descShownId;     // id whose description is displayed
    void fetchDescription();

    // --- helpers -------------------------------------------------------------
    wxListView* currentList() const;
    const wingetparser::Table* currentTable() const;
    wxString selectedId(wxString* nameOut = nullptr, wxString* versionOut = nullptr) const;

    void log(const wxString& line);
    void fillList(wxListView* list, const wingetparser::Table& table);

    void refreshInstalled();
    void refreshUpgrades();
    void runSearch();

    // Streams a winget action (install/upgrade/uninstall) into the journal and
    // refreshes the lists when done.
    void runAction(const std::vector<wxString>& args, const wxString& announceStart);

    // --- handlers ------------------------------------------------------------
    void onCharHook(wxKeyEvent& e);
    void onItemActivated(wxListEvent& e);
    void onRefresh();
    void onUpgradeSelected();
    void onUpgradeAll();
    void onInstallSelected();
    void onUninstallSelected();
    void onCopyId();
    void onHelpKeys();
    void onAbout();
};
