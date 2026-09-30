# Dumps the visible texts of the running WingetAccess window: menu bar, child
# controls (labels, buttons, edit boxes). WM_GETTEXT and menu APIs only, no UI
# Automation, so it never disturbs a running screen reader.
# Usage: powershell -File tools\dump_ui.ps1
Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class UiDump {
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, StringBuilder l);
    [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern IntPtr GetMenu(IntPtr h);
    [DllImport("user32.dll")] static extern int GetMenuItemCount(IntPtr m);
    [DllImport("user32.dll")] static extern IntPtr GetSubMenu(IntPtr m, int i);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetMenuString(IntPtr m, uint id, StringBuilder s, int n, uint flags);
    static string Text(IntPtr h) {
        int len = (int)SendMessage(h, 0x000E, IntPtr.Zero, IntPtr.Zero);
        var sb = new StringBuilder(len + 1);
        SendMessage(h, 0x000D, (IntPtr)(len + 1), sb);
        return sb.ToString();
    }
    public static List<string> Menus(IntPtr top) {
        var r = new List<string>();
        IntPtr bar = GetMenu(top);
        for (int i = 0; i < GetMenuItemCount(bar); i++) {
            var s = new StringBuilder(256); GetMenuString(bar, (uint)i, s, 256, 0x400);
            var items = new List<string>();
            IntPtr sub = GetSubMenu(bar, i);
            for (int j = 0; j < GetMenuItemCount(sub); j++) {
                var t = new StringBuilder(256); GetMenuString(sub, (uint)j, t, 256, 0x400);
                if (t.Length > 0) items.Add(t.ToString().Replace("\t", " [") + "]");
            }
            r.Add(s + " : " + string.Join(" | ", items));
        }
        return r;
    }
    public static List<string> Controls(IntPtr top) {
        var r = new List<string>();
        EnumChildWindows(top, (h, l) => {
            var c = new StringBuilder(64); GetClassName(h, c, 64);
            string cls = c.ToString();
            if (cls == "Static" || cls == "Button" || cls == "Edit" || cls.StartsWith("RICHEDIT")) {
                string t = Text(h);
                if (t.Length > 0) r.Add(cls + " : " + t.Replace("\r\n", " / ").Replace("\n", " / "));
            }
            return true;
        }, IntPtr.Zero);
        return r;
    }
}
"@
$p = Get-Process WingetAccess -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { "WingetAccess ne tourne pas"; exit 1 }
"--- menus ---"
[UiDump]::Menus($p.MainWindowHandle)
"--- controles ---"
[UiDump]::Controls($p.MainWindowHandle)
