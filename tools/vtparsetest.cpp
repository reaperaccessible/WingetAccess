// Feeds a captured winget output (pseudo console or pipe) to TerminalStream,
// in small chunks like a real pipe, and prints the lines and progress events.
//   vtparsetest <capture file>
#include "TerminalStream.h"

#include <wx/init.h>
#include <cstdio>
#include <vector>

int main(int argc, char** argv)
{
    wxInitializer init;
    if (argc < 2)
    {
        printf("usage: vtparsetest <capture file>\n");
        return 2;
    }
    FILE* f = fopen(argv[1], "rb");
    if (f == nullptr)
    {
        printf("cannot open\n");
        return 2;
    }
    std::vector<char> data;
    char buffer[4096];
    size_t got;
    while ((got = fread(buffer, 1, sizeof(buffer), f)) > 0)
        data.insert(data.end(), buffer, buffer + got);
    fclose(f);

    TerminalStream stream(
        [](const wxString& line) { printf("LIGNE  [%s]\n", (const char*)line.utf8_str()); },
        [](int state, int percent) { printf("PROGRES etat=%d %d%%\n", state, percent); });
    // Odd chunk size: sequences get split across reads, as they do for real.
    for (size_t i = 0; i < data.size(); i += 7)
        stream.feed(data.data() + i, std::min<size_t>(7, data.size() - i));
    stream.finish();
    return 0;
}
