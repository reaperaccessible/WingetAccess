#include "WingetSetup.h"
#include "Localization.h"

#include <wx/base64.h>
#include <wx/utils.h>
#include <string>

namespace wingetsetup
{

namespace
{

// Common frame of every script: stop on the first error, report it as one
// plain "ERREUR : ..." line on stdout (read by the journal), UTF-8 output
// without BOM, TLS 1.2 for downloads on older .NET defaults.
wxString wrapScript(const wxString& body)
{
    return wxString(LR"PS($ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false } catch { }
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
try {
)PS")
        + body
        + wxString(LR"PS(
} catch {
    Write-Output ('@ERROR@' + $_.Exception.Message)
    exit 1
}
exit 0
)PS");
}

// Inserts a translated message into a PowerShell single-quoted string.
wxString psText(const char* en, const char* fr)
{
    wxString text = loc::tr(en, fr);
    text.Replace("'", "''");
    return text;
}

wxString finish(wxString script)
{
    script.Replace("@ERROR@", psText("ERROR: ", "ERREUR : "));
    script.Replace("@DOWNLOAD_FAILED@", psText("winget download failed, code ",
                                               "winget download a échoué, code "));
    script.Replace("@NO_PACKAGE@", psText("package not found after the download",
                                          "paquet introuvable après le téléchargement"));
    return script;
}

bool isDigit(wxUniChar c)
{
    return c >= '0' && c <= '9';
}

} // namespace

Version parseVersion(const wxString& output)
{
    Version v;
    // First "<digits>.<digits>" in the output, e.g. "v1.29.380".
    const size_t n = output.length();
    for (size_t i = 0; i < n; ++i)
    {
        if (!isDigit(output[i]))
            continue;
        size_t j = i;
        long major = 0;
        while (j < n && isDigit(output[j]))
            major = major * 10 + (output[j++].GetValue() - '0');
        if (j + 1 < n && output[j] == '.' && isDigit(output[j + 1]))
        {
            ++j;
            long minor = 0;
            while (j < n && isDigit(output[j]))
                minor = minor * 10 + (output[j++].GetValue() - '0');
            v.found = true;
            v.major = static_cast<int>(major);
            v.minor = static_cast<int>(minor);
            // Keep the whole token ("v1.29.380", "v1.10.340-preview"): the
            // update check compares it before and after.
            size_t from = (i > 0 && (output[i - 1] == 'v' || output[i - 1] == 'V')) ? i - 1 : i;
            size_t to = j;
            while (to < n && !wxIsspace(output[to]))
                ++to;
            v.text = output.Mid(from, to - from);
            break;
        }
        i = j;
    }
    return v;
}

bool isAppInstallerId(const wxString& id)
{
    return id.CmpNoCase("Microsoft.AppInstaller") == 0
        || id.CmpNoCase("9NBLGGH4NNS1") == 0;
}

wxString powershellPath()
{
    wxString root;
    if (!wxGetEnv("SystemRoot", &root) || root.empty())
        root = "C:\\Windows";
    return root + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
}

std::vector<wxString> powershellArgs(const wxString& script)
{
    // -EncodedCommand expects UTF-16LE; wchar_t is UTF-16LE on Windows.
    const std::wstring wide = script.ToStdWstring();
    const wxString encoded = wxBase64Encode(wide.data(), wide.size() * sizeof(wchar_t));
    return { "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
             "-EncodedCommand", encoded };
}

wxString scriptRegisterAppInstaller()
{
    return finish(wrapScript(LR"PS(
    Add-AppxPackage -RegisterByFamilyName -MainPackage Microsoft.DesktopAppInstaller_8wekyb3d8bbwe
)PS"));
}

wxString scriptRepairWithModule()
{
    return finish(wrapScript(LR"PS(
    $nuget = Get-PackageProvider -ListAvailable -Name NuGet -ErrorAction SilentlyContinue |
             Where-Object { $_.Version -ge [version]'2.8.5.201' }
    if (-not $nuget) {
        Install-PackageProvider -Name NuGet -MinimumVersion 2.8.5.201 -Force -Scope CurrentUser | Out-Null
    }
    Install-Module -Name Microsoft.WinGet.Client -Repository PSGallery -Force -Scope CurrentUser -AllowClobber | Out-Null
    Import-Module Microsoft.WinGet.Client -Force
    Repair-WinGetPackageManager -Latest -Force | Out-Null
)PS"));
}

wxString scriptInstallOfficialBundle()
{
    return finish(wrapScript(LR"PS(
    $dir = Join-Path $env:TEMP 'WingetAccess_AppInstaller'
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    try {
        $file = Join-Path $dir 'AppInstaller.msixbundle'
        Invoke-WebRequest -Uri 'https://aka.ms/getwinget' -OutFile $file -UseBasicParsing
        Add-AppxPackage -Path $file -ForceApplicationShutdown
    } finally {
        Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
    }
)PS"));
}

wxString scriptUpdateAppInstaller(const wxString& wingetExe)
{
    // PowerShell single-quoted string: a quote is escaped by doubling it.
    wxString exe = wingetExe;
    exe.Replace("'", "''");

    wxString body = LR"PS(
    $dir = Join-Path $env:TEMP 'WingetAccess_AppInstaller'
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    try {
        & '@WINGET@' download --id Microsoft.AppInstaller --exact --source winget --skip-dependencies --download-directory $dir --accept-source-agreements --accept-package-agreements --disable-interactivity
        if ($LASTEXITCODE -ne 0) { throw ('@DOWNLOAD_FAILED@' + $LASTEXITCODE) }
        # winget names the bundle after the localized package name, with a
        # .msix extension: pick the package file whatever its name.
        $pkg = Get-ChildItem -Path $dir -File |
               Where-Object { $_.Extension -in '.msix', '.msixbundle', '.appx', '.appxbundle' } |
               Select-Object -First 1
        if (-not $pkg) { throw '@NO_PACKAGE@' }
        Add-AppxPackage -Path $pkg.FullName -ForceApplicationShutdown
    } finally {
        Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
    }
)PS";
    body.Replace("@WINGET@", exe);
    return finish(wrapScript(body));
}

} // namespace wingetsetup
