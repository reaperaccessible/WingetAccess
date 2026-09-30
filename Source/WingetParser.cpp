#include "WingetParser.h"

#include <wx/tokenzr.h>

namespace wingetparser
{

int Table::columnIndex(const wxString& name) const
{
    for (size_t i = 0; i < headers.size(); ++i)
        if (headers[i].CmpNoCase(name) == 0)
            return static_cast<int>(i);
    return -1;
}

namespace
{

// Display width of one code point: 2 cells for East-Asian wide/fullwidth
// ranges, 1 otherwise. Mirrors the padding rule winget uses to align columns.
int charWidth(wxUniChar ch)
{
    const unsigned int c = ch.GetValue();
    if (c < 0x1100)
        return 1;
    if ((c >= 0x1100 && c <= 0x115F) ||                     // Hangul Jamo
        (c >= 0x2E80 && c <= 0xA4CF && c != 0x303F) ||      // CJK ... Yi
        (c >= 0xAC00 && c <= 0xD7A3) ||                     // Hangul syllables
        (c >= 0xF900 && c <= 0xFAFF) ||                     // CJK compat
        (c >= 0xFE30 && c <= 0xFE4F) ||                     // CJK compat forms
        (c >= 0xFF00 && c <= 0xFF60) ||                     // Fullwidth forms
        (c >= 0xFFE0 && c <= 0xFFE6) ||
        (c >= 0x20000 && c <= 0x3FFFD))
        return 2;
    return 1;
}

// Slice `line` between display-width offsets [from, to); to < 0 = end of line.
wxString sliceByWidth(const wxString& line, int from, int to)
{
    wxString out;
    int pos = 0;
    for (wxString::const_iterator it = line.begin(); it != line.end(); ++it)
    {
        if (to >= 0 && pos >= to)
            break;
        if (pos >= from)
            out += *it;
        pos += charWidth(*it);
    }
    out.Trim(true).Trim(false);
    return out;
}

bool isSeparatorLine(const wxString& line)
{
    if (line.length() < 10)
        return false;
    for (wxString::const_iterator it = line.begin(); it != line.end(); ++it)
        if (*it != '-')
            return false;
    return true;
}

} // namespace

Table parseFirstTable(const wxString& output)
{
    Table table;

    // Split into lines, keeping empty lines (they end the table).
    std::vector<wxString> lines;
    {
        wxString current;
        for (wxString::const_iterator it = output.begin(); it != output.end(); ++it)
        {
            if (*it == '\n')
            {
                lines.push_back(current);
                current.clear();
            }
            else if (*it != '\r')
                current += *it;
        }
        if (!current.empty())
            lines.push_back(current);
    }

    // Find the all-dashes separator; the header is the line right before.
    size_t sep = 0;
    bool found = false;
    for (size_t i = 1; i < lines.size(); ++i)
    {
        if (isSeparatorLine(lines[i]))
        {
            sep = i;
            found = true;
            break;
        }
    }
    if (!found)
        return table;

    const wxString& header = lines[sep - 1];

    // Column start offsets (display width): a token starts where a non-space
    // follows the line start or 2+ spaces. winget separates titles that way.
    std::vector<int> starts;
    {
        int pos = 0;
        int spaceRun = 2;  // treat line start as preceded by spaces
        for (wxString::const_iterator it = header.begin(); it != header.end(); ++it)
        {
            if (*it == ' ')
                ++spaceRun;
            else
            {
                if (spaceRun >= 2)
                    starts.push_back(pos);
                spaceRun = 0;
            }
            pos += charWidth(*it);
        }
    }
    if (starts.empty())
        return table;

    for (size_t k = 0; k < starts.size(); ++k)
    {
        const int from = starts[k];
        const int to   = (k + 1 < starts.size()) ? starts[k + 1] : -1;
        table.headers.push_back(sliceByWidth(header, from, to));
    }

    // A real package always has an ID and a version (list, upgrade, search).
    // winget prints a summary right under the table, with no blank line in
    // between (« 2 mises à niveau disponibles. »): sliced into the columns it
    // would become a bogus row with an empty version, so such a row ends the
    // table.
    const int idCol = table.columnIndex("ID");
    const int versionCol = table.columnIndex("Version");

    // Data rows run from the separator to the first empty line.
    for (size_t i = sep + 1; i < lines.size(); ++i)
    {
        wxString trimmed = lines[i];
        trimmed.Trim(true).Trim(false);
        if (trimmed.empty())
            break;
        if (isSeparatorLine(lines[i]))
            break;

        std::vector<wxString> row;
        for (size_t k = 0; k < starts.size(); ++k)
        {
            const int from = starts[k];
            const int to   = (k + 1 < starts.size()) ? starts[k + 1] : -1;
            row.push_back(sliceByWidth(lines[i], from, to));
        }
        if ((idCol >= 0 && row[idCol].empty()) || (versionCol >= 0 && row[versionCol].empty()))
            break;
        table.rows.push_back(std::move(row));
    }

    return table;
}

} // namespace wingetparser
