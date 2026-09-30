#include "TerminalStream.h"

#include <cstdio>

TerminalStream::TerminalStream(LineHandler onLine, ProgressHandler onProgress)
    : onLine(std::move(onLine)), onProgress(std::move(onProgress))
{
}

void TerminalStream::feed(const char* data, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(data[i]);
        switch (mode)
        {
            case Mode::Text:
                if (pendingCr)
                {
                    pendingCr = false;
                    if (c == '\n')
                    {
                        endLine();
                        continue;
                    }
                    // A carriage return not followed by a line feed redraws the
                    // line from its start (spinner, progress bar).
                    line.clear();
                }
                if (c == 0x1B)
                    mode = Mode::Escape;
                else if (c == '\r')
                    pendingCr = true;
                else if (c == '\n')
                    endLine();
                else if (c == '\t')
                    line += ' ';
                else if (c >= 0x20)
                    line += static_cast<char>(c);
                break;

            case Mode::Escape:
                if (c == '[')
                    mode = Mode::Csi;
                else if (c == ']')
                {
                    osc.clear();
                    mode = Mode::Osc;
                }
                else
                    mode = Mode::Text;   // two-byte sequence, ignored
                break;

            case Mode::Csi:
                // Parameters and intermediates, then one final byte.
                if (c >= 0x40 && c <= 0x7E)
                {
                    if (c == 'K')        // erase in line
                        line.clear();
                    mode = Mode::Text;
                }
                break;

            case Mode::Osc:
                if (c == 0x07)           // BEL terminator
                {
                    handleOsc();
                    mode = Mode::Text;
                }
                else if (c == 0x1B)
                    mode = Mode::OscEscape;
                else
                    osc += static_cast<char>(c);
                break;

            case Mode::OscEscape:         // ESC \ (string terminator)
                handleOsc();
                mode = Mode::Text;
                break;
        }
    }
}

void TerminalStream::finish()
{
    pendingCr = false;
    if (!line.empty())
        endLine();
}

void TerminalStream::handleOsc()
{
    // "9;4;<state>;<percent>"
    if (osc.rfind("9;4;", 0) != 0 || !onProgress)
        return;
    int state = -1, percent = 0;
    if (sscanf_s(osc.c_str() + 4, "%d;%d", &state, &percent) >= 1 && state >= 0)
        onProgress(state, percent < 0 ? 0 : (percent > 100 ? 100 : percent));
}

void TerminalStream::endLine()
{
    wxString text = wxString::FromUTF8(line.data(), line.size());
    line.clear();

    // The progress bar is drawn with block elements (U+2580-259F) or box
    // drawing (U+2500-257F): keep only what a screen reader can use,
    // "89.0 MB / 89.0 MB".
    wxString clean;
    for (wxString::const_iterator it = text.begin(); it != text.end(); ++it)
    {
        const unsigned int u = (*it).GetValue();
        if (u >= 0x2500 && u <= 0x259F)
            continue;
        clean += *it;
    }
    clean.Trim(true).Trim(false);

    // Spinner frames that survived as a line of their own.
    if (clean.empty() || clean == "-" || clean == "\\" || clean == "|" || clean == "/")
        return;
    if (onLine)
        onLine(clean);
}
