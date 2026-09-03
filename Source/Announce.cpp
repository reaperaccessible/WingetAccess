#include "Announce.h"

#include <windows.h>
#include <uiautomation.h>   // UiaHostProviderFromHwnd, UiaRaiseNotificationEvent
#include <oleauto.h>        // SysAllocString / SysFreeString

namespace a11y
{

void announce(const wxString& text)
{
    if (text.empty())
        return;

    HWND hwnd = GetActiveWindow();
    if (hwnd == nullptr)
        hwnd = GetForegroundWindow();
    if (hwnd == nullptr)
        return;

    // A UIA host provider for the HWND is enough to raise a notification; no
    // custom provider implementation is required.
    IRawElementProviderSimple* provider = nullptr;
    if (FAILED(UiaHostProviderFromHwnd(hwnd, &provider)) || provider == nullptr)
        return;

    BSTR message = SysAllocString(text.wc_str());
    if (message != nullptr)
    {
        UiaRaiseNotificationEvent(provider,
                                  NotificationKind_Other,
                                  NotificationProcessing_All,
                                  message,
                                  /*activityId=*/nullptr);
        SysFreeString(message);
    }

    provider->Release();
}

} // namespace a11y
