#include "WingetParser.h"
#include <wx/init.h>
#include <wx/file.h>
#include <cstdio>

int main(int argc, char** argv)
{
    wxInitializer init;
    if (argc < 2) { printf("usage: parsetest <file>\n"); return 2; }
    wxFile f(wxString::FromUTF8(argv[1]));
    wxString content;
    if (!f.ReadAll(&content, wxConvUTF8)) { printf("read failed\n"); return 2; }
    auto table = wingetparser::parseFirstTable(content);
    printf("headers (%zu): ", table.headers.size());
    for (auto& h : table.headers) printf("[%s] ", (const char*)h.utf8_str());
    printf("\nrows: %zu\n", table.rows.size());
    for (size_t i = 0; i < table.rows.size() && i < 5; ++i)
    {
        for (auto& c : table.rows[i]) printf("| %s ", (const char*)c.utf8_str());
        printf("\n");
    }
    const int id = table.columnIndex("ID");
    printf("id column: %d\n", id);
    return table.empty() ? 1 : 0;
}
