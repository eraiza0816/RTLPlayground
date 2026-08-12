#!/usr/bin/env python3
"""Safe minifier for the WebUI sources (HTML/JS).

Removes comments and line-leading whitespace without touching string
literals, template literals or regex literals, so the output is
functionally identical to the input.  Used by the firmware Makefile
before the files are embedded in the flash; the raw sources in html/
stay untouched for development.

Usage: minify.py <src> <dst>
"""
import re
import sys

# Keywords after which a '/' starts a regex literal (not a division).
_REGEX_KEYWORDS = frozenset(
    "return typeof case in of new delete void do else yield await "
    "instanceof throw".split())


def _is_regex_start(s, i):
    """True if the '/' at i starts a regex literal.

    A '/' is a regex when the preceding token cannot end an expression
    (keyword like `return`, or one of ( , = : [ ! & | ? { } ; + - * % < > ^ ~)
    and not a value (identifier, number, ')', ']', '}').
    """
    j = i - 1
    while j >= 0 and s[j] in ' \t\r\n':
        j -= 1
    if j < 0:
        return True
    c = s[j]
    if c.isalnum() or c == '_':
        k = j
        while k >= 0 and (s[k].isalnum() or s[k] == '_'):
            k -= 1
        return s[k + 1:j + 1] in _REGEX_KEYWORDS
    return c not in ')]}'


def _skip_string(s, i, quote):
    """Index just past the closing quote; s[i] == quote."""
    j = i + 1
    n = len(s)
    while j < n:
        if s[j] == '\\':
            j += 2
            continue
        if s[j] == quote:
            return j + 1
        j += 1
    return n


def _skip_template(s, i):
    """Index just past the closing backtick of the template at i.

    Handles \\ escapes and ${...} expressions (which may contain nested
    strings/templates/comments, so a naive scan to the next backtick is
    not safe).
    """
    j = i + 1
    n = len(s)
    while j < n:
        c = s[j]
        if c == '\\':
            j += 2
            continue
        if c == '`':
            return j + 1
        if c == '$' and j + 1 < n and s[j + 1] == '{':
            depth = 1
            j += 2
            while j < n and depth:
                c = s[j]
                if c == '{':
                    depth += 1
                elif c == '}':
                    depth -= 1
                elif c in ('"', "'"):
                    j = _skip_string(s, j, c)
                    continue
                elif c == '`':
                    j = _skip_template(s, j)
                    continue
                elif c == '/' and j + 1 < n and s[j + 1] == '/':
                    e = s.find('\n', j)
                    j = e if e >= 0 else n
                    continue
                elif c == '/' and j + 1 < n and s[j + 1] == '*':
                    e = s.find('*/', j + 2)
                    j = e + 2 if e >= 0 else n
                    continue
                j += 1
            continue
        j += 1
    return n


def _skip_regex(s, i):
    """Index just past the closing '/' of the regex literal at i."""
    j = i + 1
    n = len(s)
    in_class = False
    while j < n:
        c = s[j]
        if c == '\\':
            j += 2
            continue
        if c == '[':
            in_class = True
        elif c == ']':
            in_class = False
        elif c == '/' and not in_class:
            return j + 1
        j += 1
    return n


def js_min(s):
    out = []
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        if c == '"' or c == "'":
            j = _skip_string(s, i, c)
            out.append(s[i:j])
            i = j
        elif c == '`':
            j = _skip_template(s, i)
            out.append(s[i:j])
            i = j
        elif c == '/' and i + 1 < n and s[i + 1] == '/':
            j = s.find('\n', i)
            i = j if j >= 0 else n
        elif c == '/' and i + 1 < n and s[i + 1] == '*':
            j = s.find('*/', i + 2)
            i = j + 2 if j >= 0 else n
        elif c == '/' and _is_regex_start(s, i):
            j = _skip_regex(s, i)
            out.append(s[i:j])
            i = j
        else:
            out.append(c)
            i += 1
    lines = [l.strip() for l in ''.join(out).split('\n')]
    if not out:
        return ''
    return '\n'.join(l for l in lines if l) + '\n'


def html_min(s):
    s = re.sub(r'<!--.*?-->', '', s, flags=re.S)
    lines = [l.strip() for l in s.split('\n')]
    return '\n'.join(l for l in lines if l) + '\n'


def main():
    if len(sys.argv) != 3:
        print('usage: minify.py <src> <dst>', file=sys.stderr)
        sys.exit(1)
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, 'r', encoding='utf-8', errors='replace') as f:
        s = f.read()
    if src.endswith('.js'):
        out = js_min(s)
    else:
        out = html_min(s)
    with open(dst, 'w', encoding='utf-8') as f:
        f.write(out)
    print('%s: %d -> %d bytes' % (src.split('/')[-1], len(s), len(out)))


if __name__ == '__main__':
    main()

