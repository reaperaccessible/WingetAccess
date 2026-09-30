# Lists the string literals of C++ sources (line, prefix, text) to inventory
# what the user can see before a translation pass.
# Usage: python tools/list_literals.py Source/MainWindow.cpp [...]
import re
import sys

LITERAL = re.compile(r'(L|LR|R|u8)?"((?:[^"\\\n]|\\.)*)"')
# Literals that are code, never shown: winget arguments, column keys, ids.
CODE_ONLY = re.compile(r'^(-{1,2}[a-z-]+|[A-Z][a-z]*|ID|Id|\s*|[a-z]+|%s|%d|\\n|\\t|:)$')

for path in sys.argv[1:]:
    for number, line in enumerate(open(path, encoding="utf-8"), 1):
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("#include"):
            continue
        for m in LITERAL.finditer(line):
            text = m.group(2)
            if CODE_ONLY.match(text) and not re.search(r"[^\x00-\x7f]", text):
                continue
            print(f"{path}:{number}: {m.group(1) or ''}\"{text}\"")
