# Reads the WingetAccess journal (the rich edit at the bottom of the window)
# with WM_GETTEXT: no UI Automation, so it never disturbs a running screen
# reader. Usage: powershell -File tools\read_journal.ps1
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class JournalReader {
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, StringBuilder l);
    [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, IntPtr l);
    public static string Read(IntPtr top) {
        string result = null;
        EnumChildWindows(top, (h, l) => {
            var cls = new StringBuilder(64);
            GetClassName(h, cls, 64);
            if (cls.ToString().StartsWith("RICHEDIT")) {
                int len = (int)SendMessage(h, 0x000E, IntPtr.Zero, IntPtr.Zero);
                var sb = new StringBuilder(len + 1);
                SendMessage(h, 0x000D, (IntPtr)(len + 1), sb);
                result = sb.ToString();
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
}
"@
$p = Get-Process WingetAccess -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { "WingetAccess ne tourne pas"; exit 1 }
[JournalReader]::Read($p.MainWindowHandle)
