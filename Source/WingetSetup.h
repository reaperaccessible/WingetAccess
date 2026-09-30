#pragma once

#include <wx/string.h>
#include <vector>

// Everything WingetAccess needs to make winget usable on its own: version
// check, App Installer repair on a fresh machine, and App Installer update.
//
// App Installer IS winget: updating it through `winget upgrade` makes winget
// replace its own running package and get killed halfway. Its update therefore
// runs outside winget, through PowerShell's Add-AppxPackage.
namespace wingetsetup
{
    // Oldest winget WingetAccess relies on (`winget download`, used to fetch an
    // App Installer update, appeared in 1.8; --disable-interactivity earlier).
    constexpr int kMinMajor = 1;
    constexpr int kMinMinor = 8;

    struct Version
    {
        bool found = false;   // `winget --version` ran and printed a version
        int  major = 0;
        int  minor = 0;
        wxString text;        // as printed, e.g. "v1.29.380"

        bool recentEnough() const
        {
            return found && (major > kMinMajor || (major == kMinMajor && minor >= kMinMinor));
        }
    };

    // Parses the output of `winget --version`.
    Version parseVersion(const wxString& output);

    // True for the App Installer package, under its winget and Store IDs.
    bool isAppInstallerId(const wxString& id);

    // Full path of Windows PowerShell 5.1 (always present on Windows 10/11).
    wxString powershellPath();

    // Arguments running `script` hidden and non-interactive. The script is
    // passed as -EncodedCommand (UTF-16LE base64): no quoting pitfalls.
    std::vector<wxString> powershellArgs(const wxString& script);

    // Repair steps for a missing or too old winget, tried in this order; after
    // each one the caller checks `winget --version` again.
    //  1. Register the App Installer already staged on the machine (the usual
    //     state of a freshly installed Windows: present but not registered).
    //  2. Microsoft's own repair: Repair-WinGetPackageManager from the
    //     Microsoft.WinGet.Client module, which also installs dependencies.
    //  3. Last resort: the official bundle from https://aka.ms/getwinget.
    wxString scriptRegisterAppInstaller();
    wxString scriptRepairWithModule();
    wxString scriptInstallOfficialBundle();

    // Update of App Installer while winget works: winget downloads the new
    // package (hash verified, dependencies skipped), then Add-AppxPackage
    // installs it once winget has exited.
    wxString scriptUpdateAppInstaller(const wxString& wingetExe);
}
