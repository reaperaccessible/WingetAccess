#include "MainWindow.h"
#include "Version.h"

#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/msgdlg.h>
#include <wx/clipbrd.h>
#include <wx/stattext.h>
#include <wx/tokenzr.h>
#include <wx/weakref.h>
#include <wx/app.h>

#include <algorithm>
#include <functional>
#include <thread>

#define NOMINMAX
#include <windows.h>
#include <uiautomation.h>   // IRawElementProviderSimple, UiaRaiseNotificationEvent
#include <oleauto.h>        // SysAllocString / SysFreeString

namespace
{
// Minimal UIA provider for the main-window HWND. Its only job is to exist so
// the window is a real UIA provider (returned from WM_GETOBJECT), which lets
// NVDA/JAWS receive the notification events we raise. Content is still read
// through the MSAA bridge via the host provider. A bare wx window is MSAA-only
// and NVDA silently drops notifications raised on a plain host provider.
class FrameUiaProvider : public IRawElementProviderSimple
{
public:
    explicit FrameUiaProvider(HWND hwnd) : hwnd_(hwnd) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (ppv == nullptr) return E_INVALIDARG;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple))
        {
            *ppv = static_cast<IRawElementProviderSimple*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* p) override
    {
        *p = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider
                                        | ProviderOptions_UseComThreading);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** p) override
    {
        *p = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* p) override
    {
        p->vt = VT_EMPTY;
        if (id == UIA_ControlTypePropertyId) { p->vt = VT_I4; p->lVal = UIA_PaneControlTypeId; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** p) override
    {
        return UiaHostProviderFromHwnd(hwnd_, p);
    }

private:
    LONG ref_ = 1;
    HWND hwnd_;
};

enum Ids
{
    ID_REFRESH = wxID_HIGHEST + 1,
    ID_UPGRADE_SELECTED,
    ID_UPGRADE_ALL,
    ID_INSTALL_SELECTED,
    ID_UNINSTALL_SELECTED,
    ID_COPY_ID,
    ID_HELP_KEYS,
    ID_TAB_INSTALLED,
    ID_TAB_UPGRADES,
    ID_TAB_SEARCH,
    ID_SEARCH_GO,
    ID_DESC_TIMER,
    ID_DELAY_TIMER,
    ID_CHECK_UPDATE,
    ID_SELFUPDATE_TIMER,
    ID_ANNOUNCE_TIMER,
};

const wxString kBusyMsg = L"Occupé, opération en cours";
} // namespace

MainWindow::MainWindow(bool justUpdated)
    : wxFrame(nullptr, wxID_ANY,
              wxString::Format("WingetAccess %s", WINGETACCESS_VERSION_STR),
              wxDefaultPosition, wxSize(1000, 700))
{
    // The frame's HWND exists now (base ctor created it); attach our UIA
    // provider so NVDA receives the notifications we raise (announce()).
    uiaProvider_ = new FrameUiaProvider(reinterpret_cast<HWND>(GetHandle()));

    // --- menu bar ------------------------------------------------------------
    auto* menuActions = new wxMenu();
    menuActions->Append(ID_UPGRADE_SELECTED, L"Mettre à jour la sélection\tCtrl+U");
    menuActions->Append(ID_UPGRADE_ALL, L"Tout mettre à jour\tCtrl+Shift+U");
    menuActions->Append(ID_INSTALL_SELECTED, L"Installer la sélection\tCtrl+I");
    menuActions->Append(ID_UNINSTALL_SELECTED, L"Désinstaller la sélection");
    menuActions->AppendSeparator();
    menuActions->Append(ID_COPY_ID, "Copier l'identifiant\tCtrl+Shift+C");
    menuActions->Append(ID_REFRESH, "Actualiser\tF5");
    menuActions->AppendSeparator();
    menuActions->Append(wxID_EXIT, "Quitter\tAlt+F4");

    auto* menuView = new wxMenu();
    menuView->Append(ID_TAB_INSTALLED, L"Installés\tCtrl+1");
    menuView->Append(ID_TAB_UPGRADES, L"Mises à jour\tCtrl+2");
    menuView->Append(ID_TAB_SEARCH, "Recherche\tCtrl+3");

    auto* menuHelp = new wxMenu();
    menuHelp->Append(ID_HELP_KEYS, "Raccourcis clavier\tCtrl+H");
    menuHelp->Append(ID_CHECK_UPDATE, L"Rechercher une mise à jour de WingetAccess");
    menuHelp->Append(wxID_ABOUT, L"À propos");

    auto* bar = new wxMenuBar();
    bar->Append(menuActions, "&Actions");
    bar->Append(menuView, "Afficha&ge");
    bar->Append(menuHelp, "&Aide");
    SetMenuBar(bar);

    // --- layout --------------------------------------------------------------
    auto* root = new wxPanel(this);
    notebook = new wxNotebook(root, wxID_ANY);

    // On wx/MSW the accessible name a screen reader announces for a native
    // control comes from the wxStaticText created just before it — SetName()
    // and SetAccessible() are inert (wx never handles WM_GETOBJECT). Giving
    // each list a static label carrying the tab name is what makes NVDA say
    // the tab on Ctrl+1/2/3, read before the focused row.
    auto makeListPage = [this](const wxString& title, const wxString& listLabel,
                               wxListView*& listOut, int pageIndex,
                               wxWindow* extraTop = nullptr, wxPanel* page = nullptr)
    {
        if (page == nullptr)
            page = new wxPanel(notebook);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        if (extraTop != nullptr)
            sizer->Add(extraTop, 0, wxEXPAND | wxALL, 6);
        auto* label = new wxStaticText(page, wxID_ANY, listLabel + " :");
        sizer->Add(label, 0, wxLEFT | wxRIGHT | wxTOP, 6);
        listOut = new wxListView(page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxLC_REPORT | wxLC_SINGLE_SEL);
        sizer->Add(listOut, 1, wxEXPAND | wxALL, 6);

        // Description of the selected row, next in the tab order after the
        // list; filled asynchronously via `winget show`.
        sizer->Add(new wxStaticText(page, wxID_ANY, "Description :"), 0,
                   wxLEFT | wxRIGHT, 6);
        descBox[pageIndex] = new wxTextCtrl(page, wxID_ANY, wxEmptyString,
                                            wxDefaultPosition, wxSize(-1, 110),
                                            wxTE_MULTILINE | wxTE_READONLY);
        sizer->Add(descBox[pageIndex], 0, wxEXPAND | wxALL, 6);

        page->SetSizer(sizer);
        notebook->AddPage(page, title);
        return page;
    };

    makeListPage(L"Installés", L"Installés", listInstalled, 0);
    makeListPage(L"Mises à jour", L"Mises à jour", listUpgrades, 1);

    // Search page: field + button above the list.
    {
        auto* page = new wxPanel(notebook);
        auto* top = new wxPanel(page);
        auto* topSizer = new wxBoxSizer(wxHORIZONTAL);
        auto* label = new wxStaticText(top, wxID_ANY, "&Recherche :");
        searchBox = new wxTextCtrl(top, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                   wxDefaultSize, wxTE_PROCESS_ENTER);
        auto* goBtn = new wxButton(top, ID_SEARCH_GO, "Lancer la recherche");
        topSizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        topSizer->Add(searchBox, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        topSizer->Add(goBtn, 0, wxALIGN_CENTER_VERTICAL);
        top->SetSizer(topSizer);
        makeListPage("Recherche", L"Résultats", listSearch, 2, top, page);
    }

    journal = new wxTextCtrl(root, wxID_ANY, wxEmptyString, wxDefaultPosition,
                             wxSize(-1, 160),
                             wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
    journal->SetName("Journal");

    auto* rootSizer = new wxBoxSizer(wxVERTICAL);
    rootSizer->Add(notebook, 1, wxEXPAND | wxALL, 4);
    rootSizer->Add(new wxStaticText(root, wxID_ANY, "&Journal :"), 0,
                   wxLEFT | wxRIGHT, 8);
    rootSizer->Add(journal, 0, wxEXPAND | wxALL, 4);
    root->SetSizer(rootSizer);

    // --- bindings ------------------------------------------------------------
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { onUpgradeSelected(); }, ID_UPGRADE_SELECTED);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { onUpgradeAll(); }, ID_UPGRADE_ALL);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { onInstallSelected(); }, ID_INSTALL_SELECTED);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { onUninstallSelected(); }, ID_UNINSTALL_SELECTED);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { onCopyId(); }, ID_COPY_ID);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { onRefresh(); }, ID_REFRESH);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { onHelpKeys(); }, ID_HELP_KEYS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { onAbout(); }, wxID_ABOUT);
    // Ctrl+1/2/3: the tab name is spoken through the static label that names
    // the focused control (see makeListPage) — a separate UIA notification
    // always loses the race against the focus event and stays silent.
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { notebook->SetSelection(0); listInstalled->SetFocus(); }, ID_TAB_INSTALLED);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { notebook->SetSelection(1); listUpgrades->SetFocus(); }, ID_TAB_UPGRADES);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { notebook->SetSelection(2); searchBox->SetFocus(); }, ID_TAB_SEARCH);

    // wxNotebook trap: the tab bar is the FIRST control of the frame, so a
    // backward navigation from it has nowhere to go and wx leaves the focus in
    // place — Shift+Tab does nothing forever, indistinguishable from a frozen
    // app for a screen-reader user. Send the focus to the last focusable
    // control of the current page instead.
    notebook->Bind(wxEVT_NAVIGATION_KEY, [this](wxNavigationKeyEvent& e)
    {
        if (!e.GetDirection() && FindFocus() == notebook)
        {
            std::function<wxWindow*(wxWindow*)> lastFocusable =
                [&lastFocusable](wxWindow* parent) -> wxWindow*
            {
                const auto& children = parent->GetChildren();
                for (auto it = children.rbegin(); it != children.rend(); ++it)
                {
                    wxWindow* child = *it;
                    if (!child->IsShown() || !child->IsEnabled())
                        continue;
                    if (wxWindow* deep = lastFocusable(child))
                        return deep;
                    if (child->AcceptsFocusFromKeyboard())
                        return child;
                }
                return nullptr;
            };
            if (wxWindow* target = lastFocusable(notebook->GetCurrentPage()))
            {
                target->SetFocus();
                return;
            }
        }
        e.Skip();
    });

    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { runSearch(); }, ID_SEARCH_GO);
    searchBox->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { runSearch(); });

    Bind(wxEVT_LIST_ITEM_ACTIVATED, &MainWindow::onItemActivated, this);

    // Selecting a row schedules its description fetch (debounced so arrowing
    // through a list does not spawn one winget per row).
    auto bindDescription = [this](wxListView* list, const wingetparser::Table* table,
                                  int pageIndex)
    {
        list->Bind(wxEVT_LIST_ITEM_SELECTED, [this, table, pageIndex](wxListEvent& e)
        {
            const long row = e.GetIndex();
            const int idCol = table->columnIndex("ID");
            if (idCol < 0 || row < 0 || row >= static_cast<long>(table->rows.size())
                || static_cast<size_t>(idCol) >= table->rows[row].size())
                return;
            const wxString id = table->rows[row][idCol];
            if (id == descShownId[pageIndex]
                || (id == descPendingId && pageIndex == descPendingPage))
                return;
            descPendingId = id;
            descPendingPage = pageIndex;
            descBox[pageIndex]->SetValue(L"Chargement de la description…");
            descTimer.Start(400, wxTIMER_ONE_SHOT);
        });
    };
    bindDescription(listInstalled, &tableInstalled, 0);
    bindDescription(listUpgrades, &tableUpgrades, 1);
    bindDescription(listSearch, &tableSearch, 2);
    descTimer.SetOwner(this, ID_DESC_TIMER);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { fetchDescription(); }, ID_DESC_TIMER);
    delayTimer.SetOwner(this, ID_DELAY_TIMER);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&)
    {
        auto fn = std::move(delayedCall);
        delayedCall = nullptr;
        if (fn)
            fn();
    }, ID_DELAY_TIMER);

    // Plain keys (Delete) are never delivered through accelerators when the
    // focus sits in a list or text control; route them with a CHAR_HOOK.
    Bind(wxEVT_CHAR_HOOK, &MainWindow::onCharHook, this);

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { checkSelfUpdate(true); }, ID_CHECK_UPDATE);
    selfUpdateTimer.SetOwner(this, ID_SELFUPDATE_TIMER);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { onSelfUpdateTimer(); }, ID_SELFUPDATE_TIMER);
    announceTimer.SetOwner(this, ID_ANNOUNCE_TIMER);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { announce(delayedAnnouncement); }, ID_ANNOUNCE_TIMER);

    log(wxString::Format(L"WingetAccess %s.", WINGETACCESS_VERSION_STR));
    if (justUpdated)
    {
        // Spoken once the window and its first focus have been read.
        delayedAnnouncement = wxString::Format(L"WingetAccess mis à jour, version %s",
                                               WINGETACCESS_VERSION_STR);
        log(delayedAnnouncement + ".");
        announceTimer.Start(1500, wxTIMER_ONE_SHOT);
    }
    selfupdate::cleanupOldCopy();
    checkSelfUpdate(false);
    ensureWinget([this]() { startRefreshChain(true); });
}

MainWindow::~MainWindow()
{
    runner.terminate();
    if (uiaProvider_ != nullptr)
    {
        static_cast<IRawElementProviderSimple*>(uiaProvider_)->Release();
        uiaProvider_ = nullptr;
    }
}

WXLRESULT MainWindow::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    // Answer WM_GETOBJECT for the UIA root so NVDA connects via UIA and receives
    // our notifications; everything else falls through to wx (which still
    // answers OBJID_CLIENT with its MSAA implementation for the controls).
    if (nMsg == WM_GETOBJECT && uiaProvider_ != nullptr
        && static_cast<long>(lParam) == static_cast<long>(UiaRootObjectId))
    {
        return UiaReturnRawElementProvider(
            reinterpret_cast<HWND>(GetHandle()), wParam, lParam,
            static_cast<IRawElementProviderSimple*>(uiaProvider_));
    }
    return wxFrame::MSWWindowProc(nMsg, wParam, lParam);
}

void MainWindow::announce(const wxString& text)
{
    if (text.empty() || uiaProvider_ == nullptr)
        return;
    BSTR msg = SysAllocString(text.wc_str());
    if (msg != nullptr)
    {
        UiaRaiseNotificationEvent(static_cast<IRawElementProviderSimple*>(uiaProvider_),
                                  NotificationKind_Other,
                                  NotificationProcessing_All,
                                  msg, /*activityId=*/nullptr);
        SysFreeString(msg);
    }
}

// --- helpers -----------------------------------------------------------------

wxListView* MainWindow::currentList() const
{
    switch (notebook->GetSelection())
    {
        case 0: return listInstalled;
        case 1: return listUpgrades;
        case 2: return listSearch;
        default: return nullptr;
    }
}

const wingetparser::Table* MainWindow::currentTable() const
{
    switch (notebook->GetSelection())
    {
        case 0: return &tableInstalled;
        case 1: return &tableUpgrades;
        case 2: return &tableSearch;
        default: return nullptr;
    }
}

wxString MainWindow::selectedId(wxString* nameOut, wxString* versionOut) const
{
    wxListView* list = currentList();
    const wingetparser::Table* table = currentTable();
    if (list == nullptr || table == nullptr)
        return wxEmptyString;

    const long row = list->GetFirstSelected();
    if (row < 0 || row >= static_cast<long>(table->rows.size()))
        return wxEmptyString;

    const int idCol = table->columnIndex("ID") >= 0 ? table->columnIndex("ID")
                                                    : table->columnIndex("Id");
    if (idCol < 0)
        return wxEmptyString;

    if (nameOut != nullptr && !table->rows[row].empty())
        *nameOut = table->rows[row][0];

    if (versionOut != nullptr)
    {
        const int verCol = table->columnIndex("Version");
        if (verCol >= 0 && static_cast<size_t>(verCol) < table->rows[row].size())
            *versionOut = table->rows[row][verCol];
    }

    return table->rows[row][idCol];
}

void MainWindow::log(const wxString& line)
{
    journal->AppendText(line + "\n");
}

void MainWindow::fillList(wxListView* list, const wingetparser::Table& table)
{
    // DeleteAllItems wipes the selection; remember and restore it so a focused
    // list never ends up with rows but nothing selected.
    long previous = list->GetFirstSelected();

    list->Freeze();
    list->ClearAll();
    for (size_t c = 0; c < table.headers.size(); ++c)
        list->AppendColumn(table.headers[c], wxLIST_FORMAT_LEFT, c == 0 ? 380 : 180);

    for (size_t r = 0; r < table.rows.size(); ++r)
    {
        const long item = list->InsertItem(static_cast<long>(r),
                                           table.rows[r].empty() ? wxString() : table.rows[r][0]);
        for (size_t c = 1; c < table.rows[r].size(); ++c)
            list->SetItem(item, static_cast<int>(c), table.rows[r][c]);
    }
    list->Thaw();

    if (list->GetItemCount() > 0)
    {
        if (previous < 0)
            previous = 0;
        if (previous >= list->GetItemCount())
            previous = list->GetItemCount() - 1;
        list->Select(previous);
        list->Focus(previous);
    }
}

// --- winget readiness --------------------------------------------------------

void MainWindow::callLater(int ms, std::function<void()> fn)
{
    delayedCall = std::move(fn);
    delayTimer.Start(ms, wxTIMER_ONE_SHOT);
}

void MainWindow::probeWinget(std::function<void(const wingetsetup::Version&)> then)
{
    const bool started = runner.start(
        { "--version" },
        nullptr,
        [then](int exitCode, const wxString& output)
        {
            wingetsetup::Version v;
            if (exitCode == 0)
                v = wingetsetup::parseVersion(output);
            then(v);
        });
    if (!started)
    {
        if (runner.lastLaunchFailed())
            then(wingetsetup::Version {});   // winget absent: not found
        else
            announce(kBusyMsg);
    }
}

void MainWindow::ensureWinget(std::function<void()> then)
{
    log(L"Vérification de winget…");
    probeWinget([this, then](const wingetsetup::Version& v)
    {
        if (v.recentEnough())
        {
            wingetReady = true;
            log(wxString::Format(L"winget %s prêt.", v.text));
            then();
            return;
        }
        if (v.found)
            log(wxString::Format(L"winget %s est trop ancien (minimum %d.%d), mise à jour…",
                                 v.text, wingetsetup::kMinMajor, wingetsetup::kMinMinor));
        else
            log(L"winget est introuvable ou inactif sur cette machine, préparation…");
        announce(L"Préparation de winget…");
        runRepairStep(0, then);
    });
}

void MainWindow::runRepairStep(size_t step, std::function<void()> then)
{
    struct Step { const wchar_t* label; wxString (*script)(); };
    static const Step steps[] = {
        { L"Enregistrement du Programme d'installation d'application…",
          &wingetsetup::scriptRegisterAppInstaller },
        { L"Réparation de winget par l'outil de Microsoft (module WinGet)…",
          &wingetsetup::scriptRepairWithModule },
        { L"Téléchargement du paquet officiel de winget (environ 220 Mo)…",
          &wingetsetup::scriptInstallOfficialBundle },
    };
    const size_t stepCount = sizeof(steps) / sizeof(steps[0]);

    if (step >= stepCount)
    {
        // Every repair failed: keep going with an old winget if there is one,
        // otherwise explain and wait for F5.
        maintenance = false;
        probeWinget([this, then](const wingetsetup::Version& v)
        {
            if (v.found)
            {
                wingetReady = true;
                log(wxString::Format(L"winget %s n'a pas pu être mis à jour ; certaines fonctions "
                                     L"peuvent échouer.", v.text));
                announce(L"winget ancien, mise à jour impossible");
                then();
                return;
            }
            wingetReady = false;
            log(L"winget n'a pas pu être installé automatiquement.");
            announce(L"winget introuvable");
            wxMessageBox(
                L"WingetAccess n'a pas pu préparer winget sur cette machine.\n\n"
                L"Vérifie la connexion Internet, puis appuie sur F5 pour réessayer.\n"
                L"Tu peux aussi installer « Programme d'installation d'application » "
                L"depuis le Microsoft Store, puis appuyer sur F5.\n\n"
                L"Le détail des erreurs est dans le journal.",
                L"winget introuvable", wxOK | wxICON_WARNING, this);
        });
        return;
    }

    maintenance = true;
    const wxString label = steps[step].label;
    log(label);
    announce(label);

    const bool started = runner.startProgram(
        wingetsetup::powershellPath(),
        wingetsetup::powershellArgs(steps[step].script()),
        [this](const wxString& line) { log(line); },
        [this, step, then](int, const wxString&)
        {
            // Let a freshly registered package expose its winget alias.
            callLater(1500, [this, step, then]()
            {
                probeWinget([this, step, then](const wingetsetup::Version& v)
                {
                    if (!v.recentEnough())
                    {
                        runRepairStep(step + 1, then);
                        return;
                    }
                    maintenance = false;
                    wingetReady = true;
                    log(wxString::Format(L"winget %s prêt.", v.text));
                    announce(L"winget prêt");
                    then();
                });
            });
        });
    if (!started)
    {
        log(L"Impossible de lancer PowerShell.");
        runRepairStep(step + 1, then);
    }
}

bool MainWindow::appInstallerUpdateListed() const
{
    const int idCol = tableUpgrades.columnIndex("ID");
    if (idCol < 0)
        return false;
    for (const auto& row : tableUpgrades.rows)
        if (static_cast<size_t>(idCol) < row.size() && wingetsetup::isAppInstallerId(row[idCol]))
            return true;
    return false;
}

void MainWindow::updateAppInstaller(std::function<void(bool ok)> then)
{
    maintenance = true;
    probeWinget([this, then](const wingetsetup::Version& before)
    {
        const wxString start = L"Mise à jour de winget (Programme d'installation d'application)…";
        log(start);
        announce(start);

        // Shared end: compare the version, report, hand over.
        auto finish = [this, then, before](int exitCode)
        {
            callLater(1500, [this, then, before, exitCode]()
            {
                probeWinget([this, then, before, exitCode](const wingetsetup::Version& after)
                {
                    maintenance = false;
                    const bool ok = after.recentEnough()
                                 && (exitCode == 0 || after.text != before.text);
                    if (ok)
                    {
                        log(wxString::Format(L"winget mis à jour : %s.", after.text));
                        announce(wxString::Format(L"winget mis à jour, version %s", after.text));
                    }
                    else
                    {
                        log(L"Échec de la mise à jour de winget.");
                        announce(L"Échec de la mise à jour de winget");
                    }
                    then(ok);
                });
            });
        };

        const bool started = runner.startProgram(
            wingetsetup::powershellPath(),
            wingetsetup::powershellArgs(
                wingetsetup::scriptUpdateAppInstaller(WingetRunner::wingetPath())),
            [this](const wxString& line) { log(line); },
            [this, finish](int exitCode, const wxString&)
            {
                if (exitCode == 0)
                {
                    finish(0);
                    return;
                }
                // The new version may need dependencies the direct install
                // skipped: Microsoft's repair tool installs them.
                log(L"Installation directe impossible, réparation par l'outil de Microsoft…");
                const bool again = runner.startProgram(
                    wingetsetup::powershellPath(),
                    wingetsetup::powershellArgs(wingetsetup::scriptRepairWithModule()),
                    [this](const wxString& line) { log(line); },
                    [finish](int code, const wxString&) { finish(code); });
                if (!again)
                    finish(exitCode);
            });
        if (!started)
        {
            log(L"Impossible de lancer PowerShell.");
            maintenance = false;
            then(false);
        }
    });
}

void MainWindow::startRefreshChain(bool withAppInstallerCheck)
{
    autoUpdateAppInstaller = withAppInstallerCheck && !appInstallerAutoTried;
    log(L"Chargement des paquets installés…");
    refreshUpgradesAfterInstalled = true;
    refreshInstalled();
}

void MainWindow::reportStartFailure()
{
    if (runner.lastLaunchFailed())
    {
        wingetReady = false;
        log(L"winget est introuvable.");
        ensureWinget([this]() { startRefreshChain(true); });
    }
    else
        announce(kBusyMsg);
}

// --- self-update -------------------------------------------------------------

bool MainWindow::wingetIdle() const
{
    return !runner.isBusy() && !actionInProgress && !maintenance && upgradeQueue.empty();
}

void MainWindow::checkSelfUpdate(bool manual)
{
    if (selfUpdating || selfUpdatePending)
    {
        if (manual)
            announce(L"Mise à jour de WingetAccess déjà en cours");
        return;
    }
    if (manual)
    {
        log(L"Recherche d'une mise à jour de WingetAccess…");
        announce(L"Recherche d'une mise à jour…");
    }
    wxWeakRef<MainWindow> self(this);
    std::thread([self, manual]()
    {
        const selfupdate::Release r = selfupdate::fetchLatest();
        if (wxTheApp != nullptr)
            wxTheApp->CallAfter([self, r, manual]()
            {
                if (self)
                    self->onSelfUpdateInfo(r, manual);
            });
    }).detach();
}

void MainWindow::onSelfUpdateInfo(const selfupdate::Release& r, bool manual)
{
    if (!r.ok)
    {
        log(wxString::Format(L"Vérification de la mise à jour de WingetAccess impossible : %s.",
                             r.error));
        if (manual)
            announce(L"Vérification impossible, voir le journal");
        return;
    }
    if (!selfupdate::isNewer(r))
    {
        if (manual)
        {
            const wxString msg = wxString::Format(L"WingetAccess est à jour, version %s",
                                                  WINGETACCESS_VERSION_STR);
            log(msg + ".");
            announce(msg);
        }
        return;
    }

    log(wxString::Format(L"Nouvelle version de WingetAccess disponible : %s.", r.tag));
    pendingRelease = r;
    selfUpdatePending = true;
    if (wingetIdle())
        applySelfUpdate();
    else
    {
        log(L"Elle sera installée dès la fin de l'opération en cours.");
        selfUpdateTimer.Start(2000);
    }
}

void MainWindow::applySelfUpdate()
{
    selfUpdatePending = false;
    selfUpdating = true;
    const wxString msg = wxString::Format(L"Mise à jour de WingetAccess vers la version %s…",
                                          pendingRelease.tag);
    log(msg);
    announce(msg);

    wxWeakRef<MainWindow> self(this);
    const selfupdate::Release r = pendingRelease;
    std::thread([self, r]()
    {
        wxString error;
        const wxString file = selfupdate::download(r, error);
        if (wxTheApp != nullptr)
            wxTheApp->CallAfter([self, file, error]()
            {
                if (!self)
                    return;
                if (file.empty())
                {
                    self->selfUpdating = false;
                    self->log(wxString::Format(L"Échec de la mise à jour de WingetAccess : %s.",
                                               error));
                    self->announce(L"Échec de la mise à jour de WingetAccess");
                    return;
                }
                self->readyFile = file;
                // An action may have been started during the download.
                if (self->wingetIdle())
                    self->swapAndRestart();
                else
                    self->selfUpdateTimer.Start(2000);
            });
    }).detach();
}

void MainWindow::swapAndRestart()
{
    selfUpdateTimer.Stop();
    showRunner.terminate();
    wxString error;
    if (!selfupdate::installAndRestart(readyFile, error))
    {
        selfUpdating = false;
        readyFile.clear();
        log(wxString::Format(L"Échec de la mise à jour de WingetAccess : %s.", error));
        announce(L"Échec de la mise à jour de WingetAccess");
        return;
    }
    log(L"Redémarrage de WingetAccess…");
    Close(true);
}

void MainWindow::onSelfUpdateTimer()
{
    if (!wingetIdle())
        return;
    selfUpdateTimer.Stop();
    if (selfUpdatePending)
        applySelfUpdate();
    else if (!readyFile.empty())
        swapAndRestart();
}

// --- refreshes ---------------------------------------------------------------

void MainWindow::refreshInstalled()
{
    const bool started = runner.start(
        { "list", "--disable-interactivity", "--accept-source-agreements" },
        nullptr,
        [this](int exitCode, const wxString& output)
        {
            tableInstalled = wingetparser::parseFirstTable(output);

            // Keep only the packages a winget source knows (Source column
            // non-empty: winget, msstore) — the rest is installed software
            // winget merely sees but cannot manage.
            const int srcCol = tableInstalled.columnIndex("Source");
            if (srcCol >= 0)
            {
                auto& rows = tableInstalled.rows;
                rows.erase(std::remove_if(rows.begin(), rows.end(),
                               [srcCol](const std::vector<wxString>& row)
                               {
                                   return static_cast<size_t>(srcCol) >= row.size()
                                       || row[srcCol].empty();
                               }),
                           rows.end());
            }

            fillList(listInstalled, tableInstalled);
            if (exitCode != 0 && tableInstalled.empty())
                log(wxString::Format(L"winget list a échoué (code %d).", exitCode));
            else
                log(wxString::Format(L"%zu paquets installés.", tableInstalled.rows.size()));

            if (refreshUpgradesAfterInstalled)
            {
                refreshUpgradesAfterInstalled = false;
                refreshUpgrades();
            }
            else
                announce(wxString::Format(L"%zu paquets installés", tableInstalled.rows.size()));
        });
    if (!started)
        reportStartFailure();
}

void MainWindow::refreshUpgrades()
{
    const bool started = runner.start(
        { "upgrade", "--disable-interactivity", "--accept-source-agreements" },
        nullptr,
        [this](int exitCode, const wxString& output)
        {
            tableUpgrades = wingetparser::parseFirstTable(output);
            fillList(listUpgrades, tableUpgrades);
            if (exitCode != 0 && tableUpgrades.empty())
                log(wxString::Format(L"winget upgrade a échoué (code %d).", exitCode));
            else
                log(wxString::Format(L"%zu mises à jour disponibles.", tableUpgrades.rows.size()));

            // winget itself first: an outdated App Installer is updated on its
            // own at startup (once per session), then the lists are reloaded.
            if (autoUpdateAppInstaller)
            {
                autoUpdateAppInstaller = false;
                if (appInstallerUpdateListed())
                {
                    appInstallerAutoTried = true;
                    updateAppInstaller([this](bool) { startRefreshChain(false); });
                    return;
                }
            }
            announce(wxString::Format(L"%zu mises à jour", tableUpgrades.rows.size()));
        });
    if (!started)
        reportStartFailure();
}

void MainWindow::runSearch()
{
    wxString terms = searchBox->GetValue().Strip(wxString::both);
    if (terms.empty())
    {
        announce("Termes de recherche vides");
        return;
    }

    const bool started = runner.start(
        { "search", terms, "--disable-interactivity", "--accept-source-agreements" },
        nullptr,
        [this](int exitCode, const wxString& output)
        {
            tableSearch = wingetparser::parseFirstTable(output);
            fillList(listSearch, tableSearch);
            if (tableSearch.empty())
            {
                log(exitCode == 0 ? wxString(L"Recherche : aucun résultat.")
                                  : wxString::Format(L"Recherche : aucun résultat (code %d).", exitCode));
                announce(L"Aucun résultat");
            }
            else
            {
                log(wxString::Format(L"Recherche : %zu résultats.", tableSearch.rows.size()));
                announce(wxString::Format(L"%zu résultats", tableSearch.rows.size()));
                listSearch->SetFocus();
            }
        });
    if (started)
    {
        log(wxString::Format(L"Recherche de « %s »…", terms));
        announce(L"Recherche…");
    }
    else
        reportStartFailure();
}

void MainWindow::fetchDescription()
{
    if (descPendingId.empty() || descPendingPage < 0)
        return;
    // While App Installer is replaced, winget can be shut down mid-run: wait.
    if (showRunner.isBusy() || maintenance)
    {
        descTimer.Start(maintenance ? 1500 : 400, wxTIMER_ONE_SHOT);
        return;
    }
    const wxString id = descPendingId;
    const int page = descPendingPage;

    const bool started = showRunner.start(
        { "show", "--id", id, "--exact",
          "--disable-interactivity", "--accept-source-agreements" },
        nullptr,
        [this, id, page](int exitCode, const wxString& output)
        {
            // The selection may have moved on while winget ran.
            if (id != descPendingId || page != descPendingPage)
            {
                descTimer.Start(1, wxTIMER_ONE_SHOT);
                return;
            }
            descPendingId.clear();
            descPendingPage = -1;
            descShownId[page] = id;

            // `winget show` prints localized "Field : value" lines; keep the
            // few that matter for a spoken summary.
            wxString text;
            const wchar_t* keys[] = { L"Description", L"Auteur", L"Publisher",
                                      L"Page d", L"Licence", L"Version" };
            wxStringTokenizer lines(output, "\n");
            while (lines.HasMoreTokens())
            {
                wxString line = lines.GetNextToken();
                line.Trim(true).Trim(false);
                for (const wchar_t* key : keys)
                {
                    if (line.StartsWith(key) && line.Contains(":"))
                    {
                        text += line + "\n";
                        break;
                    }
                }
            }
            if (text.empty())
                text = exitCode == 0 ? wxString(L"Aucune description disponible.")
                                     : wxString::Format(L"Description indisponible (code %d).", exitCode);

            descBox[page]->SetValue(text);
            // If the user already tabbed onto the field while it said
            // "loading", speak the real content now.
            if (FindFocus() == descBox[page])
                announce(text);
        });
    if (!started)
    {
        if (showRunner.lastLaunchFailed())
        {
            descBox[page]->SetValue(L"Description indisponible : winget est introuvable.");
            descPendingId.clear();
            descPendingPage = -1;
        }
        else
            descTimer.Start(400, wxTIMER_ONE_SHOT);
    }
}

// --- actions -----------------------------------------------------------------

void MainWindow::runAction(const std::vector<wxString>& args, const wxString& announceStart,
                           std::function<void(int exitCode)> then)
{
    const bool started = runner.start(
        args,
        [this](const wxString& line) { log(line); },
        [this, then](int exitCode, const wxString&)
        {
            actionInProgress = false;
            if (exitCode == 0)
                log(L"Terminé.");
            else
                log(wxString::Format(L"Échec, code %d (0x%08X).", exitCode,
                                     static_cast<unsigned int>(exitCode)));
            if (then)
            {
                then(exitCode);
                return;
            }
            announce(exitCode == 0 ? wxString(L"Terminé")
                                   : wxString::Format(L"Échec, code %d", exitCode));
            // Refresh both stateful lists after any action.
            startRefreshChain(false);
        });

    if (started)
    {
        actionInProgress = true;
        log(announceStart);
        announce(announceStart);
    }
    else
        reportStartFailure();
}

void MainWindow::onRefresh()
{
    if (runner.isBusy())
    {
        announce(kBusyMsg);
        return;
    }
    if (!wingetReady)
    {
        ensureWinget([this]() { startRefreshChain(true); });
        return;
    }
    switch (notebook->GetSelection())
    {
        case 0: log(L"Actualisation des paquets installés…"); refreshInstalled(); break;
        case 1: log(L"Actualisation des mises à jour…"); refreshUpgrades(); break;
        case 2: runSearch(); break;
    }
}

void MainWindow::onUpgradeSelected()
{
    wxString name;
    const wxString id = selectedId(&name);
    if (id.empty())
    {
        announce(L"Aucune sélection");
        return;
    }
    if (runner.isBusy())
    {
        announce(kBusyMsg);
        return;
    }
    // App Installer is winget itself: never upgraded through winget.
    if (wingetsetup::isAppInstallerId(id))
    {
        updateAppInstaller([this](bool) { startRefreshChain(false); });
        return;
    }
    runAction({ "upgrade", "--id", id, "--exact", "--silent",
                "--accept-source-agreements", "--accept-package-agreements",
                "--disable-interactivity" },
              wxString::Format(L"Mise à jour de %s…", name));
}

void MainWindow::onUpgradeAll()
{
    if (runner.isBusy())
    {
        announce(kBusyMsg);
        return;
    }
    if (tableUpgrades.empty())
    {
        announce(L"Aucune mise à jour");
        return;
    }
    const int reply = wxMessageBox(
        wxString::Format(L"Mettre à jour les %zu paquets ?", tableUpgrades.rows.size()),
        L"Tout mettre à jour", wxYES_NO | wxICON_QUESTION, this);
    if (reply != wxYES)
        return;

    // One package at a time rather than `upgrade --all`: App Installer can be
    // taken out and done first (through winget it would kill winget halfway
    // through the whole batch), and each package is announced "N of M".
    upgradeQueue.clear();
    bool appInstaller = false;
    const int idCol = tableUpgrades.columnIndex("ID");
    for (const auto& row : tableUpgrades.rows)
    {
        if (idCol < 0 || static_cast<size_t>(idCol) >= row.size() || row[idCol].empty())
            continue;
        if (wingetsetup::isAppInstallerId(row[idCol]))
            appInstaller = true;
        else
            upgradeQueue.push_back({ row[idCol], row.empty() ? row[idCol] : row[0] });
    }
    upgradeTotal = upgradeQueue.size() + (appInstaller ? 1 : 0);
    upgradeDone = 0;
    upgradeFailed = 0;

    if (appInstaller)
    {
        updateAppInstaller([this](bool ok)
        {
            ++upgradeDone;
            if (!ok)
                ++upgradeFailed;
            runNextQueuedUpgrade();
        });
    }
    else
        runNextQueuedUpgrade();
}

void MainWindow::runNextQueuedUpgrade()
{
    if (upgradeQueue.empty())
    {
        const wxString summary = upgradeFailed == 0
            ? wxString::Format(L"Mises à jour terminées : %zu sur %zu.", upgradeDone, upgradeTotal)
            : wxString::Format(L"Mises à jour terminées : %zu réussies, %zu échecs.",
                               upgradeDone - upgradeFailed, upgradeFailed);
        log(summary);
        announce(summary);
        startRefreshChain(false);
        return;
    }

    const PendingUpgrade next = upgradeQueue.front();
    upgradeQueue.pop_front();
    runAction({ "upgrade", "--id", next.id, "--exact", "--silent",
                "--accept-source-agreements", "--accept-package-agreements",
                "--disable-interactivity" },
              wxString::Format(L"Mise à jour %zu sur %zu : %s…",
                               upgradeDone + 1, upgradeTotal, next.name),
              [this, name = next.name](int exitCode)
              {
                  ++upgradeDone;
                  if (exitCode != 0)
                  {
                      ++upgradeFailed;
                      announce(wxString::Format(L"Échec : %s", name));
                  }
                  runNextQueuedUpgrade();
              });
}

void MainWindow::onInstallSelected()
{
    wxString name;
    const wxString id = selectedId(&name);
    if (id.empty())
    {
        announce(L"Aucune sélection");
        return;
    }
    runAction({ "install", "--id", id, "--exact", "--silent",
                "--accept-source-agreements", "--accept-package-agreements",
                "--disable-interactivity" },
              wxString::Format(L"Installation de %s…", name));
}

void MainWindow::onUninstallSelected()
{
    if (notebook->GetSelection() == 2)
    {
        announce(L"Désinstallation impossible depuis la recherche");
        return;
    }
    wxString name, version;
    const wxString id = selectedId(&name, &version);
    if (id.empty())
    {
        announce(L"Aucune sélection");
        return;
    }
    if (wingetsetup::isAppInstallerId(id))
    {
        log(L"Le Programme d'installation d'application contient winget : le désinstaller "
            L"rendrait WingetAccess inutilisable. Désinstallation refusée.");
        announce(L"Désinstallation de winget refusée");
        return;
    }
    const int reply = wxMessageBox(
        wxString::Format(L"Désinstaller %s ?", name),
        L"Désinstaller", wxYES_NO | wxICON_QUESTION, this);
    if (reply != wxYES)
        return;

    // Same winget ID can cover several installed entries (e.g. AIDA64 7.70 and
    // 8.35 both map to FinalWire.AIDA64.Extreme); without the version winget
    // answers "multiple packages found" and does nothing. Target the exact
    // version of the selected row when it is usable.
    std::vector<wxString> args = { "uninstall", "--id", id, "--exact", "--silent",
                                   "--accept-source-agreements", "--disable-interactivity" };
    if (!version.empty() && version.CmpNoCase("Unknown") != 0
        && version.CmpNoCase("Inconnu") != 0)
    {
        args.push_back("--version");
        args.push_back(version);
    }
    runAction(args, wxString::Format(L"Désinstallation de %s %s…", name, version));
}

void MainWindow::onCopyId()
{
    const wxString id = selectedId();
    if (id.empty())
    {
        announce(L"Aucune sélection");
        return;
    }
    if (wxTheClipboard->Open())
    {
        wxTheClipboard->SetData(new wxTextDataObject(id));
        wxTheClipboard->Close();
        announce(L"Identifiant copié");
    }
}

// --- key handling ------------------------------------------------------------

void MainWindow::onCharHook(wxKeyEvent& e)
{
    const int key = e.GetKeyCode();
    const wxWindow* focus = FindFocus();

    // Delete / numpad Delete on a list row = uninstall (accelerators for plain
    // keys are never translated when a list or text control has the focus).
    if ((key == WXK_DELETE || key == WXK_NUMPAD_DELETE) && !e.HasAnyModifiers()
        && (focus == listInstalled || focus == listUpgrades))
    {
        onUninstallSelected();
        return;
    }

    e.Skip();
}

void MainWindow::onItemActivated(wxListEvent& e)
{
    // Enter on a row: contextual default action.
    switch (notebook->GetSelection())
    {
        case 0:  // installed: upgrade if an update is available
        case 1:
            onUpgradeSelected();
            break;
        case 2:
            onInstallSelected();
            break;
    }
    e.Skip(false);
}

// --- help --------------------------------------------------------------------

void MainWindow::onHelpKeys()
{
    wxMessageBox(
        L"Ctrl+1 : Installés\n"
        L"Ctrl+2 : Mises à jour\n"
        "Ctrl+3 : Recherche (le focus va au champ)\n"
        L"Entrée dans une liste : mettre à jour (Installés, Mises à jour) ou installer (Recherche)\n"
        L"Ctrl+U : mettre à jour la sélection\n"
        L"Ctrl+Maj+U : tout mettre à jour\n"
        L"Ctrl+I : installer la sélection\n"
        L"Suppr : désinstaller la sélection (avec confirmation)\n"
        "Ctrl+Maj+C : copier l'identifiant du paquet\n"
        L"F5 : actualiser l'onglet courant (ou relancer la préparation de winget)\n"
        L"Le journal en bas de la fenêtre garde la sortie complète de winget.\n"
        L"Au démarrage, WingetAccess prépare winget tout seul : il l'installe ou le répare "
        L"s'il manque, accepte les conditions des sources et le met à jour en premier "
        L"quand une nouvelle version existe.\n"
        L"WingetAccess se met aussi à jour tout seul depuis GitHub, au démarrage, dès "
        L"qu'aucune opération n'est en cours (menu Aide : Rechercher une mise à jour "
        L"de WingetAccess).",
        "Raccourcis clavier", wxOK | wxICON_INFORMATION, this);
}

void MainWindow::onAbout()
{
    wxMessageBox(
        wxString::Format("WingetAccess %s\n"
                         "Gestionnaire Winget accessible (NVDA), portable.\n"
                         "Interface wxWidgets, annonces UI Automation natives.",
                         WINGETACCESS_VERSION_STR),
        L"À propos", wxOK | wxICON_INFORMATION, this);
}
