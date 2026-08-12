#!/usr/bin/env python3
"""Tests for minify.py (template literals, regex literals, strings).

Run: python3 test_minify.py
"""
import sys
import minify

CASES = [
    # plain comment stripping still works
    ("var a = 1; // comment\nvar b = 2;\n",
     "var a = 1;\nvar b = 2;\n"),
    # block comments
    ("/* head */\nvar a = 1;\n", "var a = 1;\n"),
    # division is not a regex
    ("var x = a / b / 2;\n", "var x = a / b / 2;\n"),
    # regex with an escaped slash (would break the old scanner)
    ("var re = /http:\\/\\/x\\/y/;\nvar b = 2;\n",
     "var re = /http:\\/\\/x\\/y/;\nvar b = 2;\n"),
    # regex with a character class containing quotes
    ("s.replace(/[&<>\"']/g, f);\n", "s.replace(/[&<>\"']/g, f);\n"),
    # regex after 'return'
    ("function f() { return /a\\/b/.test(s); }\n",
     "function f() { return /a\\/b/.test(s); }\n"),
    # regex with a class containing '/'
    ("var re = /[/]\\/x/;\nvar b = 2;\n", "var re = /[/]\\/x/;\nvar b = 2;\n"),
    # string containing "//" must survive
    ('var s = "http://x"; // c\nvar b = 2;\n',
     'var s = "http://x";\nvar b = 2;\n'),
    # template literal with an embedded expression containing a string
    ("var t = `a${f('x//y')}b`;\nvar z = 1;\n",
     "var t = `a${f('x//y')}b`;\nvar z = 1;\n"),
    # template literal with a nested backtick inside ${}
    ("var t = `a${`b//c`}d`;\n", "var t = `a${`b//c`}d`;\n"),
    # comment inside ${} of a template is kept verbatim (safe: the
    # template is data; the minifier only strips comments outside)
    ("var t = `a${x /* c */ + y}`;\n", "var t = `a${x /* c */ + y}`;\n"),
    # template containing an escaped backtick
    ("var t = `a\\`b`;\nvar z = 1;\n", "var t = `a\\`b`;\nvar z = 1;\n"),
    # empty input
    ("", ""),
]


def main():
    failed = 0
    for i, (src, want) in enumerate(CASES):
        got = minify.js_min(src)
        if got != want:
            failed += 1
            print("FAIL case %d:\n  src : %r\n  want: %r\n  got : %r"
                  % (i, src, want, got))
    print("%d cases, %d failed" % (len(CASES), failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
