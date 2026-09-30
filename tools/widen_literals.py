# Prefixes L to every narrow C++ string literal that holds non-ASCII text.
#
# A non-ASCII char* literal handed to wxString is decoded as CP1252, not UTF-8:
# "Installés" comes out as "InstallÃ©s" on screen and to the screen reader.
# Wide literals (L"...") need no conversion.
#
# Usage: python tools/widen_literals.py Source/MainWindow.cpp [...]
# Files must use LF line endings; raw literals (R"...") are not handled.
import re
import sys

LITERAL = re.compile(rb'"(?:[^"\\\n]|\\.)*"')

for path in sys.argv[1:]:
    data = open(path, "rb").read()
    if b"\r\n" in data:
        sys.exit(path + ": CRLF line endings, convert to LF first")
    out, pos, count = [], 0, 0
    for m in LITERAL.finditer(data):
        literal = m.group(0)
        prefix = data[m.start() - 1:m.start()]
        needs = any(b > 0x7F for b in literal) and prefix not in (b"L", b"R")
        out.append(data[pos:m.start()])
        out.append(b"L" + literal if needs else literal)
        count += needs
        pos = m.end()
    out.append(data[pos:])
    open(path, "wb").write(b"".join(out))
    print(path, count, "literals widened")
