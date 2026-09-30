#include "MainWindow.h"
#include "Localization.h"
#include "Version.h"

#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/msgdlg.h>
#include <wx/clipbrd.h>
#include <wx/stattext.h>
#include <wx/tokenzr.h>
#include <wx/weakref.h>
#include <wx/time.h>
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

// A function, not a global constant: the language is only known once the
// application has started.
wxString busyMsg()
{
    return loc::tr("Busy, operation in progress", "Occupé, opération en cours");
}
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
    menuActions->Append(ID_UPGRADE_SELECTED, loc::tr("Update selection\tCtrl+U", "Mettre à jour la sélection\tCtrl+U"));
    menuActions->Append(ID_UPGRADE_ALL, loc::tr("Update all\tCtrl+Shift+U", "Tout mettre à jour\tCtrl+Shift+U"));
    menuActions->Append(ID_INSTALL_SELECTED, loc::tr("Install selection\tCtrl+I", "Installer la sélection\tCtrl+I"));
    menuActions->Append(ID_UNINSTALL_SELECTED, loc::tr("Uninstall selection", "Désinstaller la sélection"));
    menuActions->AppendSeparator();
    menuActions->Append(ID_COPY_ID, loc::tr("Copy ID\tCtrl+Shift+C", "Copier l'identifiant\tCtrl+Shift+C"));
    menuActions->Append(ID_REFRESH, loc::tr("Refresh\tF5", "Actualiser\tF5"));
    menuActions->AppendSeparator();
    menuActions->Append(wxID_EXIT, loc::tr("Exit\tAlt+F4", "Quitter\tAlt+F4"));

    auto* menuView = new wxMenu();
    menuView->Append(ID_TAB_INSTALLED, loc::tr("Installed\tCtrl+1", "Installés\tCtrl+1"));
    menuView->Append(ID_TAB_UPGRADES, loc::tr("Updates\tCtrl+2", "Mises à jour\tCtrl+2"));
    menuView->Append(ID_TAB_SEARCH, loc::tr("Search\tCtrl+3", "Recherche\tCtrl+3"));

    auto* menuHelp = new wxMenu();
    menuHelp->Append(ID_HELP_KEYS, loc::tr("Keyboard shortcuts\tCtrl+H", "Raccourcis clavier\tCtrl+H"));
    menuHelp->Append(ID_CHECK_UPDATE, loc::tr("Check for a WingetAccess update", "Rechercher une mise à jour de WingetAccess"));
    menuHelp->Append(wxID_ABOUT, loc::tr("About", "À propos"));

    auto* bar = new wxMenuBar();
    bar->Append(menuActions, "&Actions");
    bar->Append(menuView, loc::tr("&View", "Afficha&ge"));
    bar->Append(menuHelp, loc::tr("&Help", "&Aide"));
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
        auto* label = new wxStaticText(page, wxID_ANY, listLabel);
        sizer->Add(label, 0, wxLEFT | wxRIGHT | wxTOP, 6);
        listOut = new wxListView(page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxLC_REPORT | wxLC_SINGLE_SEL);
        sizer->Add(listOut, 1, wxEXPAND | wxALL, 6);

        // Description of the selected row, next in the tab order after the
        // list; filled asynchronously via `winget show`.
        sizer->Add(new wxStaticText(page, wxID_ANY, loc::tr("Description:", "Description :")), 0,
                   wxLEFT | wxRIGHT, 6);
        descBox[pageIndex] = new wxTextCtrl(page, wxID_ANY, wxEmptyString,
                                            wxDefaultPosition, wxSize(-1, 110),
                                            wxTE_MULTILINE | wxTE_READONLY);
        sizer->Add(descBox[pageIndex], 0, wxEXPAND | wxALL, 6);

        page->SetSizer(sizer);
        notebook->AddPage(page, title);
        return page;
    };

    // The list label carries its own colon: French puts a space before it.
    makeListPage(loc::tr("Installed", "Installés"), loc::tr("Installed:", "Installés :"),
                 listInstalled, 0);
    makeListPage(loc::tr("Updates", "Mises à jour"), loc::tr("Updates:", "Mises à jour :"),
                 listUpgrades, 1);

    // Search page: field + button above the list.
    {
        auto* page = new wxPanel(notebook);
        auto* top = new wxPanel(page);
        auto* topSizer = new wxBoxSizer(wxHORIZONTAL);
        auto* label = new wxStaticText(top, wxID_ANY, loc::tr("&Search:", "&Recherche :"));
        searchBox = new wxTextCtrl(top, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                   wxDefaultSize, wxTE_PROCESS_ENTER);
        auto* goBtn = new wxButton(top, ID_SEARCH_GO, loc::tr("Start search", "Lancer la recherche"));
        topSizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        topSizer->Add(searchBox, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        topSizer->Add(goBtn, 0, wxALIGN_CENTER_VERTICAL);
        top->SetSizer(topSizer);
        makeListPage(loc::tr("Search", "Recherche"), loc::tr("Results:", "Résultats :"),
                     listSearch, 2, top, page);
    }

    journal = new wxTextCtrl(root, wxID_ANY, wxEmptyString, wxDefaultPosition,
                             wxSize(-1, 160),
                             wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
    journal->SetName("Journal");

    auto* rootSizer = new wxBoxSizer(wxVERTICAL);
    rootSizer->Add(notebook, 1, wxEXPAND | wxALL, 4);
    rootSizer->Add(new wxStaticText(root, wxID_ANY, loc::tr("&Log:", "&Journal :")), 0,
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
            descBox[pageIndex]->SetValue(loc::tr("Loading description…", "Chargement de la description…"));
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
        delayedAnnouncement = wxString::Format(loc::tr("WingetAccess updated, version %s", "WingetAccess mis à jour, version %s"),
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

void MainWindow::announceProgress(const wxString& text)
{
    if (text.empty() || uiaProvider_ == nullptr)
        return;
    BSTR msg = SysAllocString(text.wc_str());
    BSTR activity = SysAllocString(L"WingetAccess.Progress");
    if (msg != nullptr && activity != nullptr)
        UiaRaiseNotificationEvent(static_cast<IRawElementProviderSimple*>(uiaProvider_),
                                  NotificationKind_ActionCompleted,
                                  NotificationProcessing_MostRecent,
                                  msg, activity);
    SysFreeString(msg);
    SysFreeString(activity);
}

// --- action progress ---------------------------------------------------------

void MainWindow::resetProgress()
{
    progressLabel.clear();
    lastProgressStep = 0;
    lastProgressPercent = 0;
    lastProgressTick = 0;
    progressCleared = false;
    sawDeterminate = false;
    phaseAnnounced = false;
}

void MainWindow::onActionLine(const wxString& line)
{
    log(line);

    // Phase names, only to label the percentages and say when the installer
    // starts. winget writes them in the Windows language; anything else still
    // gets bare percentages and the language-independent spinner cue below.
    const wxString lower = line.Lower();
    if (lower.StartsWith(wxString::FromUTF8("téléchargement en cours"))  // utf8-ok
        || lower.StartsWith("downloading "))
    {
        progressLabel = loc::tr("Downloading", "Téléchargement");
        lastProgressStep = 0;
    }
    else if (!phaseAnnounced
             && (lower.Contains(wxString::FromUTF8("désinstallation du package"))  // utf8-ok
                 || lower.Contains("starting package uninstall")))
    {
        phaseAnnounced = true;
        announceProgress(loc::tr("Uninstalling…", "Désinstallation en cours…"));
    }
    else if (!phaseAnnounced
             && (lower.Contains("installation du package")
                 || lower.Contains("starting package install")))
    {
        phaseAnnounced = true;
        announceProgress(loc::tr("Installing…", "Installation en cours…"));
    }
}

void MainWindow::onActionProgress(int state, int percent)
{
    // OSC 9;4 states: 0 cleared, 1 normal, 2 error, 3 indeterminate, 4 paused.
    if (state == 0)
    {
        progressCleared = true;
        return;
    }
    if (state == 3)
    {
        // A spinner after a percentage phase = the installer is running.
        if (sawDeterminate && !phaseAnnounced)
        {
            phaseAnnounced = true;
            announceProgress(loc::tr("Installing…", "Installation en cours…"));
        }
        return;
    }

    // A new percentage phase (another file) starts from the beginning.
    if (progressCleared && percent < lastProgressPercent)
        lastProgressStep = 0;
    progressCleared = false;
    sawDeterminate = true;
    lastProgressPercent = percent;

    const int step = percent / 5 * 5;
    if (step < 5 || step <= lastProgressStep)
        return;
    // Steps crossed faster than speech: skip to the next update, which will
    // carry a higher value, rather than queue stale percentages. 100 % always.
    const long long now = wxGetLocalTimeMillis().GetValue();
    if (step < 100 && now - lastProgressTick < 800)
        return;
    lastProgressStep = step;
    lastProgressTick = now;

    const wxString value = wxString::Format(loc::tr("%d%%", "%d %%"), step);
    announceProgress(progressLabel.empty() ? value : progressLabel + " " + value);
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
            announce(busyMsg());
    }
}

void MainWindow::ensureWinget(std::function<void()> then)
{
    log(loc::tr("Checking winget…", "Vérification de winget…"));
    probeWinget([this, then](const wingetsetup::Version& v)
    {
        if (v.recentEnough())
        {
            wingetReady = true;
            log(wxString::Format(loc::tr("winget %s ready.", "winget %s prêt."), v.text));
            then();
            return;
        }
        if (v.found)
            log(wxString::Format(loc::tr("winget %s is too old (minimum %d.%d), updating…", "winget %s est trop ancien (minimum %d.%d), mise à jour…"),
                                 v.text, wingetsetup::kMinMajor, wingetsetup::kMinMinor));
        else
            log(loc::tr("winget is missing or inactive on this machine, preparing…", "winget est introuvable ou inactif sur cette machine, préparation…"));
        announce(loc::tr("Preparing winget…", "Préparation de winget…"));
        runRepairStep(0, then);
    });
}

void MainWindow::runRepairStep(size_t step, std::function<void()> then)
{
    struct Step { const wchar_t* label; wxString (*script)(); };
    static const Step steps[] = {
        { loc::tr("Registering App Installer…", "Enregistrement du Programme d'installation d'application…"),
          &wingetsetup::scriptRegisterAppInstaller },
        { loc::tr("Repairing winget with Microsoft's tool (WinGet module)…", "Réparation de winget par l'outil de Microsoft (module WinGet)…"),
          &wingetsetup::scriptRepairWithModule },
        { loc::tr("Downloading the official winget package (about 220 MB)…", "Téléchargement du paquet officiel de winget (environ 220 Mo)…"),
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
                log(wxString::Format(loc::tr("winget %s could not be updated; some features "
                                             "may fail.",
                                             "winget %s n'a pas pu être mis à jour ; certaines "
                                             "fonctions peuvent échouer."), v.text));
                announce(loc::tr("Old winget, update impossible", "winget ancien, mise à jour impossible"));
                then();
                return;
            }
            wingetReady = false;
            log(loc::tr("winget could not be installed automatically.", "winget n'a pas pu être installé automatiquement."));
            announce(loc::tr("winget not found", "winget introuvable"));
            wxMessageBox(
                loc::tr("WingetAccess could not prepare winget on this computer.\n\n"
                        "Check the Internet connection, then press F5 to try again.\n"
                        "You can also install \"App Installer\" from the Microsoft Store, "
                        "then press F5.\n\n"
                        "The details of the errors are in the log.",
                        "WingetAccess n'a pas pu préparer winget sur cet ordinateur.\n\n"
                        "Vérifiez la connexion Internet, puis appuyez sur F5 pour réessayer.\n"
                        "Vous pouvez aussi installer « Programme d'installation d'application » "
                        "depuis le Microsoft Store, puis appuyer sur F5.\n\n"
                        "Le détail des erreurs est dans le journal."),
                loc::tr("winget not found", "winget introuvable"), wxOK | wxICON_WARNING, this);
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
                    log(wxString::Format(loc::tr("winget %s ready.", "winget %s prêt."), v.text));
                    announce(loc::tr("winget ready", "winget prêt"));
                    then();
                });
            });
        });
    if (!started)
    {
        log(loc::tr("Could not start PowerShell.", "Impossible de lancer PowerShell."));
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
        const wxString start = loc::tr("Updating winget (App Installer)…", "Mise à jour de winget (Programme d'installation d'application)…");
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
                        log(wxString::Format(loc::tr("winget updated: %s.", "winget mis à jour : %s."), after.text));
                        announce(wxString::Format(loc::tr("winget updated, version %s", "winget mis à jour, version %s"), after.text));
                    }
                    else
                    {
                        log(loc::tr("winget update failed.", "Échec de la mise à jour de winget."));
                        announce(loc::tr("winget update failed", "Échec de la mise à jour de winget"));
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
                log(loc::tr("Direct install failed, repairing with Microsoft's tool…", "Installation directe impossible, réparation par l'outil de Microsoft…"));
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
            log(loc::tr("Could not start PowerShell.", "Impossible de lancer PowerShell."));
            maintenance = false;
            then(false);
        }
    });
}

void MainWindow::startRefreshChain(bool withAppInstallerCheck)
{
    autoUpdateAppInstaller = withAppInstallerCheck && !appInstallerAutoTried;
    log(loc::tr("Loading installed packages…", "Chargement des paquets installés…"));
    refreshUpgradesAfterInstalled = true;
    refreshInstalled();
}

void MainWindow::reportStartFailure()
{
    if (runner.lastLaunchFailed())
    {
        wingetReady = false;
        log(loc::tr("winget not found.", "winget est introuvable."));
        ensureWinget([this]() { startRefreshChain(true); });
    }
    else
        announce(busyMsg());
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
            announce(loc::tr("WingetAccess update already in progress", "Mise à jour de WingetAccess déjà en cours"));
        return;
    }
    if (manual)
    {
        log(loc::tr("Checking for a WingetAccess update…", "Recherche d'une mise à jour de WingetAccess…"));
        announce(loc::tr("Checking for an update…", "Recherche d'une mise à jour…"));
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
        log(wxString::Format(loc::tr("Could not check for a WingetAccess update: %s.", "Vérification de la mise à jour de WingetAccess impossible : %s."),
                             r.error));
        if (manual)
            announce(loc::tr("Check failed, see the log", "Vérification impossible, voir le journal"));
        return;
    }
    if (!selfupdate::isNewer(r))
    {
        if (manual)
        {
            const wxString msg = wxString::Format(loc::tr("WingetAccess is up to date, version %s", "WingetAccess est à jour, version %s"),
                                                  WINGETACCESS_VERSION_STR);
            log(msg + ".");
            announce(msg);
        }
        return;
    }

    log(wxString::Format(loc::tr("New WingetAccess version available: %s.", "Nouvelle version de WingetAccess disponible : %s."), r.tag));
    pendingRelease = r;
    selfUpdatePending = true;
    if (wingetIdle())
        applySelfUpdate();
    else
    {
        log(loc::tr("It will be installed once the current operation ends.", "Elle sera installée dès la fin de l'opération en cours."));
        selfUpdateTimer.Start(2000);
    }
}

void MainWindow::applySelfUpdate()
{
    selfUpdatePending = false;
    selfUpdating = true;
    const wxString msg = wxString::Format(loc::tr("Updating WingetAccess to version %s…", "Mise à jour de WingetAccess vers la version %s…"),
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
                    self->log(wxString::Format(loc::tr("WingetAccess update failed: %s.", "Échec de la mise à jour de WingetAccess : %s."),
                                               error));
                    self->announce(loc::tr("WingetAccess update failed", "Échec de la mise à jour de WingetAccess"));
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
        log(wxString::Format(loc::tr("WingetAccess update failed: %s.", "Échec de la mise à jour de WingetAccess : %s."), error));
        announce(loc::tr("WingetAccess update failed", "Échec de la mise à jour de WingetAccess"));
        return;
    }
    log(loc::tr("Restarting WingetAccess…", "Redémarrage de WingetAccess…"));
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
                log(wxString::Format(loc::tr("winget list failed (code %d).", "winget list a échoué (code %d)."), exitCode));
            else
                log(wxString::Format(loc::tr("%zu installed packages.", "%zu paquets installés."), tableInstalled.rows.size()));

            if (refreshUpgradesAfterInstalled)
            {
                refreshUpgradesAfterInstalled = false;
                refreshUpgrades();
            }
            else
                announce(wxString::Format(loc::tr("%zu installed packages", "%zu paquets installés"), tableInstalled.rows.size()));
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
                log(wxString::Format(loc::tr("winget upgrade failed (code %d).", "winget upgrade a échoué (code %d)."), exitCode));
            else
                log(wxString::Format(loc::tr("%zu updates available.", "%zu mises à jour disponibles."), tableUpgrades.rows.size()));

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
            announce(wxString::Format(loc::tr("%zu updates", "%zu mises à jour"), tableUpgrades.rows.size()));
        });
    if (!started)
        reportStartFailure();
}

void MainWindow::runSearch()
{
    wxString terms = searchBox->GetValue().Strip(wxString::both);
    if (terms.empty())
    {
        announce(loc::tr("Empty search terms", "Termes de recherche vides"));
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
                log(exitCode == 0 ? wxString(loc::tr("Search: no results.", "Recherche : aucun résultat."))
                                  : wxString::Format(loc::tr("Search: no results (code %d).", "Recherche : aucun résultat (code %d)."), exitCode));
                announce(loc::tr("No results", "Aucun résultat"));
            }
            else
            {
                log(wxString::Format(loc::tr("Search: %zu results.", "Recherche : %zu résultats."), tableSearch.rows.size()));
                announce(wxString::Format(loc::tr("%zu results", "%zu résultats"), tableSearch.rows.size()));
                listSearch->SetFocus();
            }
        });
    if (started)
    {
        log(wxString::Format(loc::tr("Searching for \"%s\"…", "Recherche de « %s »…"), terms));
        announce(loc::tr("Searching…", "Recherche…"));
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

            // `winget show` prints "Field: value" lines in the Windows
            // language; keep the few that matter for a spoken summary. The
            // colon must follow the field name directly, or "Publisher" would
            // also catch "Publisher Url" and "Publisher Support Url".
            static const char* const keys[] = {
                "Description", "Version", "Publisher", "Author", "Auteur",
                "Homepage", "Page d’accueil", "Page d'accueil", "License", "Licence" };  // utf8-ok
            wxString text;
            bool continuation = false;   // field with its value on the next lines
            wxStringTokenizer lines(output, "\n");
            while (lines.HasMoreTokens())
            {
                wxString line = lines.GetNextToken();
                line.Trim(true);
                // A field with an empty value (Git's description) continues on
                // the following indented lines.
                if (continuation)
                {
                    if (!line.empty() && (line[0] == ' ' || line[0] == '\t'))
                    {
                        text += line.Strip(wxString::leading) + "\n";
                        continue;
                    }
                    continuation = false;
                }
                line.Trim(false);
                for (const char* key : keys)
                {
                    wxString rest;
                    if (!line.StartsWith(wxString::FromUTF8(key), &rest))
                        continue;
                    // French winget puts a no-break space (U+00A0) before
                    // most colons, a plain one elsewhere.
                    size_t i = 0;
                    while (i < rest.length()
                           && (rest[i] == ' ' || rest[i] == '\t'
                               || rest[i] == wxUniChar(0x00A0) || rest[i] == wxUniChar(0x202F)))
                        ++i;
                    if (i < rest.length() && rest[i] == ':')
                    {
                        text += line + "\n";
                        continuation = rest.Mid(i + 1).Strip(wxString::both).empty();
                        break;
                    }
                }
            }
            if (text.empty())
                text = exitCode == 0 ? wxString(loc::tr("No description available.", "Aucune description disponible."))
                                     : wxString::Format(loc::tr("Description unavailable (code %d).", "Description indisponible (code %d)."), exitCode);

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
            descBox[page]->SetValue(loc::tr("Description unavailable: winget not found.", "Description indisponible : winget est introuvable."));
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
    resetProgress();
    const bool started = runner.startWithProgress(
        args,
        [this](const wxString& line) { onActionLine(line); },
        [this](int state, int percent) { onActionProgress(state, percent); },
        [this, then](int exitCode, const wxString&)
        {
            actionInProgress = false;
            if (exitCode == 0)
                log(loc::tr("Done.", "Terminé."));
            else
                log(wxString::Format(loc::tr("Failed, code %d (0x%08X).", "Échec, code %d (0x%08X)."), exitCode,
                                     static_cast<unsigned int>(exitCode)));
            if (then)
            {
                then(exitCode);
                return;
            }
            announce(exitCode == 0 ? wxString(loc::tr("Done", "Terminé"))
                                   : wxString::Format(loc::tr("Failed, code %d", "Échec, code %d"), exitCode));
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
        announce(busyMsg());
        return;
    }
    if (!wingetReady)
    {
        ensureWinget([this]() { startRefreshChain(true); });
        return;
    }
    switch (notebook->GetSelection())
    {
        case 0: log(loc::tr("Refreshing installed packages…", "Actualisation des paquets installés…")); refreshInstalled(); break;
        case 1: log(loc::tr("Refreshing updates…", "Actualisation des mises à jour…")); refreshUpgrades(); break;
        case 2: runSearch(); break;
    }
}

void MainWindow::onUpgradeSelected()
{
    wxString name;
    const wxString id = selectedId(&name);
    if (id.empty())
    {
        announce(loc::tr("No selection", "Aucune sélection"));
        return;
    }
    if (runner.isBusy())
    {
        announce(busyMsg());
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
              wxString::Format(loc::tr("Updating %s…", "Mise à jour de %s…"), name));
}

void MainWindow::onUpgradeAll()
{
    if (runner.isBusy())
    {
        announce(busyMsg());
        return;
    }
    if (tableUpgrades.empty())
    {
        announce(loc::tr("No updates", "Aucune mise à jour"));
        return;
    }
    const int reply = wxMessageBox(
        wxString::Format(loc::tr("Update the %zu packages?", "Mettre à jour les %zu paquets ?"), tableUpgrades.rows.size()),
        loc::tr("Update all", "Tout mettre à jour"), wxYES_NO | wxICON_QUESTION, this);
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
            ? wxString::Format(loc::tr("Updates finished: %zu of %zu.", "Mises à jour terminées : %zu sur %zu."), upgradeDone, upgradeTotal)
            : wxString::Format(loc::tr("Updates finished: %zu succeeded, %zu failed.", "Mises à jour terminées : %zu réussies, %zu échecs."),
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
              wxString::Format(loc::tr("Update %zu of %zu: %s…", "Mise à jour %zu sur %zu : %s…"),
                               upgradeDone + 1, upgradeTotal, next.name),
              [this, name = next.name](int exitCode)
              {
                  ++upgradeDone;
                  if (exitCode != 0)
                  {
                      ++upgradeFailed;
                      announce(wxString::Format(loc::tr("Failed: %s", "Échec : %s"), name));
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
        announce(loc::tr("No selection", "Aucune sélection"));
        return;
    }
    runAction({ "install", "--id", id, "--exact", "--silent",
                "--accept-source-agreements", "--accept-package-agreements",
                "--disable-interactivity" },
              wxString::Format(loc::tr("Installing %s…", "Installation de %s…"), name));
}

void MainWindow::onUninstallSelected()
{
    if (notebook->GetSelection() == 2)
    {
        announce(loc::tr("Cannot uninstall from the search", "Désinstallation impossible depuis la recherche"));
        return;
    }
    wxString name, version;
    const wxString id = selectedId(&name, &version);
    if (id.empty())
    {
        announce(loc::tr("No selection", "Aucune sélection"));
        return;
    }
    if (wingetsetup::isAppInstallerId(id))
    {
        log(loc::tr("App Installer contains winget: uninstalling it would make WingetAccess "
                    "unusable. Uninstall refused.",
                    "Le Programme d'installation d'application contient winget : le désinstaller "
                    "rendrait WingetAccess inutilisable. Désinstallation refusée."));
        announce(loc::tr("Uninstalling winget refused", "Désinstallation de winget refusée"));
        return;
    }
    const int reply = wxMessageBox(
        wxString::Format(loc::tr("Uninstall %s?", "Désinstaller %s ?"), name),
        loc::tr("Uninstall", "Désinstaller"), wxYES_NO | wxICON_QUESTION, this);
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
    runAction(args, wxString::Format(loc::tr("Uninstalling %s %s…", "Désinstallation de %s %s…"), name, version));
}

void MainWindow::onCopyId()
{
    const wxString id = selectedId();
    if (id.empty())
    {
        announce(loc::tr("No selection", "Aucune sélection"));
        return;
    }
    if (wxTheClipboard->Open())
    {
        wxTheClipboard->SetData(new wxTextDataObject(id));
        wxTheClipboard->Close();
        announce(loc::tr("ID copied", "Identifiant copié"));
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
        loc::tr("Ctrl+1: Installed\n"
                "Ctrl+2: Updates\n"
                "Ctrl+3: Search (the focus goes to the field)\n"
                "Enter in a list: update (Installed, Updates) or install (Search)\n"
                "Tab from a list: description of the selected package\n"
                "Ctrl+U: update the selection\n"
                "Ctrl+Shift+U: update all\n"
                "Ctrl+I: install the selection\n"
                "Delete: uninstall the selection (with confirmation)\n"
                "Ctrl+Shift+C: copy the package ID\n"
                "F5: refresh the current tab (or prepare winget again)\n"
                "The log at the bottom of the window keeps winget's full output.\n"
                "At startup, WingetAccess prepares winget on its own: it installs or repairs "
                "it when missing, accepts the source agreements and updates it first when a "
                "new version exists.\n"
                "WingetAccess also updates itself from GitHub, at startup, as soon as no "
                "operation is running (Help menu: Check for a WingetAccess update).",
                "Ctrl+1 : Installés\n"
                "Ctrl+2 : Mises à jour\n"
                "Ctrl+3 : Recherche (le focus va au champ)\n"
                "Entrée dans une liste : mettre à jour (Installés, Mises à jour) ou installer "
                "(Recherche)\n"
                "Tab depuis une liste : description du paquet sélectionné\n"
                "Ctrl+U : mettre à jour la sélection\n"
                "Ctrl+Maj+U : tout mettre à jour\n"
                "Ctrl+I : installer la sélection\n"
                "Suppr : désinstaller la sélection (avec confirmation)\n"
                "Ctrl+Maj+C : copier l'identifiant du paquet\n"
                "F5 : actualiser l'onglet courant (ou relancer la préparation de winget)\n"
                "Le journal en bas de la fenêtre garde la sortie complète de winget.\n"
                "Au démarrage, WingetAccess prépare winget tout seul : il l'installe ou le "
                "répare s'il manque, accepte les conditions des sources et le met à jour en "
                "premier quand une nouvelle version existe.\n"
                "WingetAccess se met aussi à jour tout seul depuis GitHub, au démarrage, dès "
                "qu'aucune opération n'est en cours (menu Aide : Rechercher une mise à jour "
                "de WingetAccess)."),
        loc::tr("Keyboard shortcuts", "Raccourcis clavier"), wxOK | wxICON_INFORMATION, this);
}

void MainWindow::onAbout()
{
    wxMessageBox(
        wxString::Format(loc::tr("WingetAccess %s\n"
                                 "Accessible, portable Winget manager for screen readers "
                                 "(NVDA, JAWS, Narrator).\n"
                                 "By ReaperAccessible.",
                                 "WingetAccess %s\n"
                                 "Gestionnaire Winget accessible et portable pour lecteurs "
                                 "d'écran (NVDA, JAWS, Narrateur).\n"
                                 "Par ReaperAccessible."),
                         WINGETACCESS_VERSION_STR),
        loc::tr("About", "À propos"), wxOK | wxICON_INFORMATION, this);
}
