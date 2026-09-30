# Opens the shortcuts list (menu command 6007) and the manual (6017) of a
# running WingetAccess by posting WM_COMMAND, then reads the list with
# LB_GETTEXT and checks the manual written to the temp folder. No UI Automation.
param([switch]$Manual)
Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class Help {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, int m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, StringBuilder l);
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, IntPtr l);
  public static IntPtr FindTop(uint pid, string title) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p);
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      if (p == pid && IsWindowVisible(h) && t.ToString() == title) { found = h; return false; } return true; }, IntPtr.Zero);
    return found; }
  public static List<string> ListItems(IntPtr dlg) {
    var r = new List<string>();
    EnumChildWindows(dlg, (h, l) => { var c = new StringBuilder(64); GetClassName(h, c, 64);
      if (c.ToString() == "ListBox") {
        int n = (int)SendMessage(h, 0x018B, IntPtr.Zero, IntPtr.Zero);
        for (int i = 0; i < n; i++) { int len = (int)SendMessage(h, 0x018A, (IntPtr)i, IntPtr.Zero);
          var sb = new StringBuilder(len + 1); SendMessage(h, 0x0189, (IntPtr)i, sb); r.Add(sb.ToString()); }
        return false; }
      return true; }, IntPtr.Zero);
    return r; } }
"@
$p = Get-Process WingetAccess -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { "WingetAccess ne tourne pas"; exit 1 }
[void][Help]::PostMessage($p.MainWindowHandle, 0x0111, [IntPtr]6007, [IntPtr]::Zero)
Start-Sleep -Milliseconds 1500
$title = "Raccourcis clavier"
$dlg = [Help]::FindTop([uint32]$p.Id, $title)
if ($dlg -eq [IntPtr]::Zero) { $title = "Keyboard shortcuts"; $dlg = [Help]::FindTop([uint32]$p.Id, $title) }
if ($dlg -eq [IntPtr]::Zero) { "fenetre des raccourcis introuvable" } else {
  "--- fenetre « $title » ---"
  [Help]::ListItems($dlg)
  [void][Help]::PostMessage($dlg, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_CLOSE
  Start-Sleep -Milliseconds 800
  "fermee : " + ([Help]::FindTop([uint32]$p.Id, $title) -eq [IntPtr]::Zero)
}
if ($Manual) {
  [void][Help]::PostMessage($p.MainWindowHandle, 0x0111, [IntPtr]6017, [IntPtr]::Zero)
  Start-Sleep -Seconds 2
  Get-ChildItem (Join-Path $env:TEMP "WingetAccess") -Filter *.html | ForEach-Object { "manuel ecrit : $($_.Name) $($_.Length) octets" }
}
