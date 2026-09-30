#pragma once

#include <wx/string.h>
#include <functional>
#include <string>

// Decodes what winget prints into clean text lines and progress events.
//
// In a pseudo console winget draws for a terminal: colours and cursor moves
// (CSI sequences), a spinner and a bar redrawn with carriage returns, and its
// progress as the Windows Terminal taskbar sequence OSC 9;4;<state>;<percent>
// (state 1 = percent valid, 3 = indeterminate, 0 = cleared; language
// independent). Through a plain pipe it prints CRLF-terminated lines. Both go
// through this decoder.
class TerminalStream
{
public:
    using LineHandler     = std::function<void(const wxString& line)>;
    using ProgressHandler = std::function<void(int state, int percent)>;

    TerminalStream(LineHandler onLine, ProgressHandler onProgress);

    void feed(const char* data, size_t size);
    void finish();   // flushes a last line without a line feed

private:
    enum class Mode { Text, Escape, Csi, Osc, OscEscape };

    void endLine();
    void handleOsc();

    LineHandler     onLine;
    ProgressHandler onProgress;
    Mode            mode = Mode::Text;
    std::string     line;      // UTF-8 bytes of the line being built
    std::string     osc;       // body of the OSC sequence being read
    bool            pendingCr = false;
};
