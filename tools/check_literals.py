# Flags accented string literals that do not go through loc::tr().
#
# Visible text must be written loc::tr("English", "Français") with plain UTF-8
# literals: tr() decodes them explicitly. An accented char* literal handed to
# wxString any other way is decoded as CP1252 ("InstallÃ©s"), and an L"..."
# one cannot be passed to tr() at all.
#
# A literal is accepted when the statement it belongs to (from the previous
# ';', '{' or '}' up to it) contains loc::tr( or psText(, or when the line is
# marked with the comment "utf8-ok" (text decoded with wxString::FromUTF8).
# Usage: python tools/check_literals.py Source/*.cpp
import re
import sys

LITERAL = re.compile(r'(?<![A-Za-z0-9_])(L|LR|R)?"((?:[^"\\\n]|\\.)*)"')
problems = 0
for path in sys.argv[1:]:
    text = open(path, encoding="utf-8").read()
    # Same text with the inside of every literal blanked, so a ';' or '}'
    # written in a message is not taken for the end of a statement.
    masked = LITERAL.sub(lambda m: (m.group(1) or "") + '"' + " " * len(m.group(2)) + '"', text)
    for m in LITERAL.finditer(text):
        body = m.group(2)
        if not re.search(r"[^\x00-\x7f]", body) or (m.group(1) or "").endswith("R"):
            continue
        line_no = text.count("\n", 0, m.start()) + 1
        line = text.splitlines()[line_no - 1]
        start = max(masked.rfind(";", 0, m.start()), masked.rfind("{", 0, m.start()),
                    masked.rfind("}", 0, m.start()))
        statement = masked[start + 1:m.start()]
        ok = (m.group(1) is None
              and ("loc::tr(" in statement or "psText(" in statement or "utf8-ok" in line))
        if not ok:
            problems += 1
            print(f"{path}:{line_no}: {m.group(0)[:70]}")
print("problèmes :", problems)
sys.exit(1 if problems else 0)
