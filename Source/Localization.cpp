#include "Localization.h"

#include <wx/intl.h>
#include <wx/uilocale.h>

namespace loc
{
namespace
{
    Language systemLanguage()
    {
        // BCP-47 tag of the preferred UI language ("fr", "fr-FR", "fr-CA"...).
        const wxString tag = wxUILocale::GetCurrent().GetName();
        if (tag.Lower().StartsWith("fr"))
            return Language::French;

        const wxLanguageInfo* info = wxUILocale::GetLanguageInfo(wxLANGUAGE_DEFAULT);
        if (info != nullptr && info->CanonicalName.Lower().StartsWith("fr"))
            return Language::French;

        return Language::English;
    }

    Language current  = Language::English;
    bool     resolved = false;

    Language& state()
    {
        if (!resolved)
        {
            current  = systemLanguage();
            resolved = true;
        }
        return current;
    }
}

void setLanguage(Language language)
{
    state() = language;
}

bool isFrench()
{
    return state() == Language::French;
}

wxString tr(const char* en, const char* fr)
{
    return wxString::FromUTF8(isFrench() ? fr : en);
}

} // namespace loc
