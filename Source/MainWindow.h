#pragma once

#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/textctrl.h>
#include <wx/button.h>

#include "WingetRunner.h"
#include "WingetParser.h"

class MainWindow : public wxFrame
{
public:
    MainWindow();

private:
    // --- UI ------------------------------------------------------------------
    wxNotebook* notebook       = nullptr;
    wxListView* listInstalled  = nullptr;
    wxListView* listUpgrades   = nullptr;
    wxListView* listSearch     = nullptr;
    wxTextCtrl* searchBox      = nullptr;
    wxTextCtrl* journal        = nullptr;

    // Parsed tables backing each list (row index == list row index).
    wingetparser::Table tableInstalled;
    wingetparser::Table tableUpgrades;
    wingetparser::Table tableSearch;

    WingetRunner runner;
    bool refreshUpgradesAfterInstalled = false;  // startup chain
    bool actionInProgress = false;               // an install/upgrade/uninstall runs

    // --- helpers -------------------------------------------------------------
    wxListView* currentList() const;
    const wingetparser::Table* currentTable() const;
    wxString selectedId(wxString* nameOut = nullptr) const;

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
