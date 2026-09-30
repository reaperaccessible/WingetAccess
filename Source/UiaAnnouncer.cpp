#include "UiaAnnouncer.h"

#define NOMINMAX
#include <windows.h>
#include <uiautomation.h>   // IRawElementProviderSimple, UiaRaiseNotificationEvent
#include <oleauto.h>        // SysAllocString / SysFreeString

namespace
{
// Minimal UIA provider for a top-level HWND. Its only job is to exist, so the
// window is a real UIA provider and receives the notifications we raise;
// content is still read through the MSAA bridge via the host provider.
class WindowUiaProvider : public IRawElementProviderSimple
{
public:
    explicit WindowUiaProvider(HWND hwnd) : hwnd_(hwnd) {}

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
} // namespace

UiaAnnouncer::~UiaAnnouncer()
{
    if (provider != nullptr)
        static_cast<IRawElementProviderSimple*>(provider)->Release();
}

void UiaAnnouncer::attach(WXHWND window)
{
    if (provider != nullptr)
        return;
    hwnd = window;
    provider = new WindowUiaProvider(static_cast<HWND>(window));
}

bool UiaAnnouncer::handleGetObject(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam, WXLRESULT& result)
{
    if (msg != WM_GETOBJECT || provider == nullptr
        || static_cast<long>(lParam) != static_cast<long>(UiaRootObjectId))
        return false;
    result = UiaReturnRawElementProvider(static_cast<HWND>(hwnd), wParam, lParam,
                                         static_cast<IRawElementProviderSimple*>(provider));
    return true;
}

void UiaAnnouncer::announce(const wxString& text)
{
    raise(text, false);
}

void UiaAnnouncer::announceProgress(const wxString& text)
{
    raise(text, true);
}

void UiaAnnouncer::raise(const wxString& text, bool progress)
{
    if (text.empty() || provider == nullptr)
        return;
    BSTR msg = SysAllocString(text.wc_str());
    BSTR activity = progress ? SysAllocString(L"WingetAccess.Progress") : nullptr;
    if (msg != nullptr)
        UiaRaiseNotificationEvent(static_cast<IRawElementProviderSimple*>(provider),
                                  progress ? NotificationKind_ActionCompleted : NotificationKind_Other,
                                  progress ? NotificationProcessing_MostRecent
                                           : NotificationProcessing_All,
                                  msg, activity);
    SysFreeString(msg);
    SysFreeString(activity);
}
