#pragma once

#include <wx/string.h>
#include <vector>

// Parses winget's fixed-width column output (list / upgrade / search).
//
// winget aligns columns on DISPLAY width (East-Asian wide characters count for
// two cells), so rows are sliced by display-width offsets computed from the
// header line, not by character index.
namespace wingetparser
{
    struct Table
    {
        std::vector<wxString>              headers;  // column titles, trimmed
        std::vector<std::vector<wxString>> rows;     // one cell per header

        bool empty() const { return rows.empty(); }

        // Index of the column whose header matches `name` case-insensitively
        // ("id" matches both "ID" and "Id"); -1 if absent.
        int columnIndex(const wxString& name) const;
    };

    // Extracts the FIRST table found in `output` (the block delimited by the
    // all-dashes separator line). Returns an empty table if none is found.
    Table parseFirstTable(const wxString& output);
}
