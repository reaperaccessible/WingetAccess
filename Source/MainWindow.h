#pragma once

#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/timer.h>

#include <deque>
#include <functional>

#include "WingetRunner.h"
#include "WingetParser.h"
#include "WingetSetup.h"
#include "SelfUpdate.h"

class MainWindow : public wxFrame
{
public:
    // `justUpdated`: started by the previous version after a self-update.
    explicit MainWindow(bool justUpdated = false);
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
    wxTextCtrl* descBox[3]     = { nullptr, nullptr, nullptr };  // one per tab
    wxTextCtrl* journal        = nullptr;

    // Parsed tables backing each list (row index == list row index).
    wingetparser::Table tableInstalled;
    wingetparser::Table tableUpgrades;
    wingetparser::Table tableSearch;

    WingetRunner runner;
    bool refreshUpgradesAfterInstalled = false;  // startup chain
    bool actionInProgress = false;               // an install/upgrade/uninstall runs

    // --- winget readiness ----------------------------------------------------
    // At startup (and on F5 while winget is unusable) WingetAccess checks
    // `winget --version`, repairs or updates App Installer on its own, and
    // updates App Installer first when an update is listed for it.
    bool wingetReady = false;
    bool maintenance = false;              // App Installer being repaired/updated
    bool autoUpdateAppInstaller = false;   // pending check after the refresh chain
    bool appInstallerAutoTried = false;    // once per session, never loops
    void ensureWinget(std::function<void()> then);
    void probeWinget(std::function<void(const wingetsetup::Version&)> then);
    void runRepairStep(size_t step, std::function<void()> then);
    void updateAppInstaller(std::function<void(bool ok)> then);
    void startRefreshChain(bool withAppInstallerCheck);
    bool appInstallerUpdateListed() const;
    void reportStartFailure();

    // Delayed call (lets a freshly registered App Installer settle before the
    // next `winget --version`).
    wxTimer               delayTimer;
    std::function<void()> delayedCall;
    void callLater(int ms, std::function<void()> fn);

    // "Upgrade all" runs one package at a time: App Installer first (outside
    // winget), then each listed package, with a spoken "N of M".
    // --- self-update (GitHub releases) ---------------------------------------
    // Checked quietly at startup and on demand; the swap only happens while no
    // winget operation runs, so an install is never cut short.
    selfupdate::Release pendingRelease;
    bool     selfUpdatePending = false;   // newer release known, not yet downloaded
    bool     selfUpdating = false;        // download or swap in progress
    wxString readyFile;                   // verified new exe waiting for the swap
    wxTimer  selfUpdateTimer;             // waits for winget to be idle
    wxTimer  announceTimer;               // spoken "updated" message at startup
    wxString delayedAnnouncement;
    bool wingetIdle() const;
    void checkSelfUpdate(bool manual);
    void onSelfUpdateInfo(const selfupdate::Release& r, bool manual);
    void applySelfUpdate();
    void swapAndRestart();

    struct PendingUpgrade { wxString id; wxString name; };
    std::deque<PendingUpgrade> upgradeQueue;
    size_t upgradeTotal = 0, upgradeDone = 0, upgradeFailed = 0;
    void runNextQueuedUpgrade();

    // Package description under the search results, fetched with `winget show`
    // on a debounce so arrowing through the list does not spawn one process per
    // row. Separate runner: never blocks real actions.
    WingetRunner showRunner;
    wxTimer      descTimer;
    wxString     descPendingId;      // selection waiting for its description
    int          descPendingPage = -1;
    wxString     descShownId[3];     // id displayed per tab
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

    // Streams a winget action (install/upgrade/uninstall) into the journal.
    // Without `then`, announces the result and refreshes the lists; with it,
    // hands the exit code over instead (queued upgrades).
    void runAction(const std::vector<wxString>& args, const wxString& announceStart,
                   std::function<void(int exitCode)> then = nullptr);

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
    void onSelfUpdateTimer();
};
