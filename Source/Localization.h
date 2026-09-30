#pragma once

#include <wx/string.h>

//==============================================================================
// Minimal two-language helper (English / French), same contract as
// PromoAccess's and DrumAccess's loc::. The translation lives next to the
// original, where it cannot drift, and no .mo files need to ship.
//
// Both literals MUST be UTF-8 (the build passes /utf-8): tr() decodes them
// explicitly, which also avoids the CP1252 trap of a char* accented literal
// handed to wxString.
//
// The language comes from Windows at startup (any "fr" variant = French,
// everything else = English). App::OnInit calls wxUILocale::UseDefault()
// before the first tr(): without it wxUILocale reports the neutral "C" locale
// and a French Windows would get English. --lang=fr|en overrides it.
//==============================================================================
namespace loc
{
    enum class Language { English, French };

    // Overrides the system language; call before building any window.
    void setLanguage(Language language);

    bool isFrench();

    // The French literal when the language is French, the English one
    // otherwise.
    wxString tr(const char* en, const char* fr);
}
