#pragma once

#include <wx/string.h>
#include <string>

// Self-update from the GitHub releases of reaperaccessible/WingetAccess.
//
// WingetAccess is a single portable exe, so no installer and no helper batch:
// Windows lets a running exe be RENAMED. The new exe is downloaded next to the
// current one (".new"), checked against the size and SHA-256 GitHub publishes,
// then the running exe is renamed ".old", the new one takes its name, it is
// started with --updated and the old process exits. The next start deletes the
// ".old" file.
//
// Every release MUST carry an asset named exactly "WingetAccess.exe" and a tag
// "vX.YY" (also what the permanent download link relies on).
namespace selfupdate
{
    struct Release
    {
        bool     ok = false;       // the API answered and a usable asset exists
        wxString error;            // why not, when !ok
        wxString tag;              // "v1.02"
        int      major = 0;
        int      minor = 0;
        wxString assetUrl;
        long long assetSize = 0;
        std::string sha256;        // lowercase hex, empty if GitHub gave none
    };

    // Parses "1.02" / "v1.02" into major/minor; false if not a version.
    bool parseVersion(const wxString& text, int& major, int& minor);

    // True when `r` is newer than the running build (WINGETACCESS_VERSION_STR).
    bool isNewer(const Release& r);

    // Blocking (call from a worker thread): reads releases/latest.
    Release fetchLatest();

    // Blocking: downloads the asset to "<exe>.new" and verifies size and hash.
    // Returns the path of the verified file, or empty with `error` set.
    wxString download(const Release& r, wxString& error);

    // Swaps the verified file in, starts it with --updated, returns true; the
    // caller then closes the application. On failure the current exe is left
    // (or put back) in place and `error` is set.
    bool installAndRestart(const wxString& newFile, wxString& error);

    // At startup: removes the "<exe>.old" left by the previous update (the old
    // process may still be exiting, so a few retries on a worker thread).
    void cleanupOldCopy();
}
