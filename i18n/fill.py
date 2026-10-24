# -*- coding: utf-8 -*-
# Refill i18n/zuuned_zh_CN.ts from the Chinese in i18n/zh_CN.tsv.
#
#   python i18n/fill.py            write i18n/zuuned_zh_CN.ts.tmp and report
#   python i18n/fill.py --write    publish it over the .ts
#
# Run i18n/update.ps1 first to refresh the <source> strings from the QML and C++
# that carry translatable text, then this to put the Chinese back, then build -
# CMake compiles the .ts into a .qm inside the executable.  The tsv is one message
# per line, written as  source ~=> Chinese , and a literal backslash-n stands for a
# newline inside a message.  A message with no line here stays unfinished, which is
# what Qt renders as English.  See docs/I18N.md.
import os
import re
import sys
from xml.sax.saxutils import escape, unescape

def norm(t):
    return re.sub(r"\s+", " ", t).strip()

mapping = {}
ctx_mapping = {}
contexts = set()
HERE = os.path.dirname(os.path.abspath(__file__))
TS = os.path.join(HERE, "zuuned_zh_CN.ts")


def key_of(src):
    return norm(src).replace("\\n", " ")


def lookup(ctx, src):
    """Chinese for one <source>, preferring a context-qualified line.

    A tsv line is either `source ~=> Chinese` or `Context!source ~=> Chinese`.
    The second form exists because the same English word means two different
    things in two pages -- `Albums` is 专辑 beside artists and songs and 相册
    beside photos -- and one bare key per word cannot answer both.  A prefix is
    only read as a context when it names a <context> that really exists in the
    .ts, so wording that legitimately contains `!` still resolves as itself.
    """
    if ctx and (ctx + "!" + src) in ctx_mapping:
        return ctx_mapping[ctx + "!" + src]
    return mapping.get(src) or mapping.get(key_of(src))


s = open(TS, encoding="utf-8").read()
for m in re.finditer(r"<context>\s*<name>(.*?)</name>", s, re.S):
    contexts.add(unescape(m.group(1), {"&apos;": "'"}))

for line in open(os.path.join(HERE, "zh_CN.tsv"), encoding="utf-8"):
    line = line.rstrip("\n").rstrip("\r")
    if not line.strip():
        continue
    assert " ~=> " in line, line[:60]
    src, zh = line.split(" ~=> ", 1)
    zh = zh.strip()
    head, sep, tail = src.partition("!")
    if sep and unescape(head, {"&apos;": "'"}) in contexts:
        src = norm(tail).replace("&quot;", chr(34)).replace("&apos;", "'")
        ctx_mapping[head + "!" + src] = zh
        ctx_mapping[head + "!" + key_of(tail)] = zh
        continue
    key = key_of(src)
    # older tsv lines were written before unescape() handled entities
    mapping[key.replace("&quot;", chr(34)).replace("&apos;", "'")] = zh
    mapping[key] = zh

missing = []
count = [0]


def repl(m):
    ctx = repl.ctx
    src_raw = m.group(1)
    # saxutils omits &apos; from its default entities; lupdate writes it.
    src = norm(unescape(src_raw, {"&apos;": "'"}))
    zh = lookup(ctx, src)
    if zh is None:
        missing.append((ctx, src))
        return m.group(0)
    count[0] += 1
    out = zh.replace("\\n", "\n")
    sep = "\n" + (m.group(2).rsplit("\n", 1)[-1] or "    ")
    return "<source>%s</source>%s<translation>%s</translation>" % (src_raw, sep, escape(out))


pat = re.compile(r"<source>(.*?)</source>(\s*)<translation(?:\s+type=\"unfinished\")?\s*(?:/>|></translation>|>.*?</translation>)", re.S)


def fill_block(block, ctx):
    repl.ctx = ctx
    return pat.sub(repl, block)


out_parts = []
pos = 0
block_pat = re.compile(r"(<context>\s*<name>)(.*?)(</name>)", re.S)
for m in block_pat.finditer(s):
    ctx = unescape(m.group(2), {"&apos;": "'"})
    end = s.find("</context>", m.end())
    if end < 0:
        end = len(s)
    out_parts.append(s[pos:m.start()])
    out_parts.append(fill_block(s[m.start():end], ctx))
    pos = end
out_parts.append(s[pos:])
s2 = "".join(out_parts)
out = TS + (".tmp" if "--write" not in sys.argv else "")
open(out, "w", encoding="utf-8", newline="").write(s2)
print("wrote", out)
print("translated", count[0], "missing", len(missing))
for ctx, src in missing:
    print("  MISSING:", (ctx + "!" if ctx else "") + src[:104])
