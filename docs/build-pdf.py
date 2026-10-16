#!/usr/bin/env python3
"""Builds the PDF versions of the user guides (docs/*.pdf) from the Markdown files.

No packages needed beyond Python 3: a small converter handles the Markdown these guides use
(headings, paragraphs, lists, tables, code blocks, images, bold, italic, code, links), and Microsoft
Edge (or Chrome) prints the HTML to PDF in headless mode.

    python docs/build-pdf.py
"""
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile

DOCS = os.path.dirname(os.path.abspath(__file__))

GUIDES = [
    ("USER_GUIDE.md", "mnKord-User-Guide.pdf", "en"),
    ("USER_GUIDE.hu.md", "mnKord-Felhasznaloi-utmutato.pdf", "hu"),
]

BROWSERS = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
]

CSS = """
@page { size: A4; margin: 18mm 16mm; }
body { font-family: 'Segoe UI', Roboto, Arial, sans-serif; font-size: 10.5pt; line-height: 1.5; color: #1d2227; }
h1 { font-size: 24pt; margin: 0 0 0.4em; color: #0b6b3a; }
h2 { font-size: 16pt; margin: 1.5em 0 0.4em; border-bottom: 1px solid #cfd6dc; padding-bottom: 0.15em; break-after: avoid; }
h3 { font-size: 12.5pt; margin: 1.2em 0 0.3em; break-after: avoid; }
p { margin: 0.5em 0; }
ul, ol { margin: 0.4em 0 0.6em; padding-left: 1.5em; }
li { margin: 0.2em 0; }
code { font-family: Consolas, 'Cascadia Mono', monospace; font-size: 9.5pt; background: #eef1f4; padding: 0.05em 0.3em; border-radius: 3px; }
pre { font-family: Consolas, 'Cascadia Mono', monospace; font-size: 9.5pt; background: #eef1f4; padding: 0.7em 0.9em; border-radius: 4px; break-inside: avoid; }
pre code { background: none; padding: 0; }
table { border-collapse: collapse; width: 100%; margin: 0.6em 0; break-inside: avoid; }
th, td { border: 1px solid #cfd6dc; padding: 0.35em 0.6em; text-align: left; vertical-align: top; }
th { background: #eef1f4; }
figure { margin: 0.8em 0; break-inside: avoid; text-align: center; }
figure img { max-width: 100%; border: 1px solid #cfd6dc; border-radius: 4px; }
figcaption { font-size: 9pt; color: #5a6670; margin-top: 0.25em; }
a { color: #0b6b3a; text-decoration: none; }
"""


def inline(text):
    parts = re.split(r"(`[^`]*`)", text)
    out = []
    for part in parts:
        if part.startswith("`") and part.endswith("`") and len(part) >= 2:
            out.append("<code>%s</code>" % html.escape(part[1:-1]))
            continue
        part = html.escape(part, quote=False)
        part = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", part)
        part = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<em>\1</em>", part)

        def link(m):
            label, url = m.group(1), m.group(2)
            if url.startswith("http"):
                return '<a href="%s">%s</a>' % (url, label)
            return label  # a link to another file means nothing on paper

        part = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, part)
        out.append(part)
    return "".join(out)


def is_block_start(line):
    return bool(
        re.match(r"^(#{1,3}\s|```|\|)", line)
        or re.match(r"^\s*([-*]|\d+\.)\s+", line)
        or re.match(r"^!\[.*\]\(.*\)\s*$", line)
    )


def convert(markdown):
    lines = markdown.splitlines()
    body = []
    title = "mnKord"
    i = 0
    while i < len(lines):
        line = lines[i]

        if not line.strip():
            i += 1
            continue

        # The language switch under the title means nothing on paper.
        if re.match(r"^(\*\*English\*\*|\[English\]\(|\*\*Magyar\*\*|\[Magyar\]\()", line):
            i += 1
            continue

        if line.startswith("```"):
            code = []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                code.append(lines[i])
                i += 1
            i += 1
            body.append("<pre><code>%s</code></pre>" % html.escape("\n".join(code)))
            continue

        m = re.match(r"^(#{1,3})\s+(.*)$", line)
        if m:
            level = len(m.group(1))
            if level == 1:
                title = m.group(2)
            body.append("<h%d>%s</h%d>" % (level, inline(m.group(2)), level))
            i += 1
            continue

        m = re.match(r"^!\[(.*)\]\((.*)\)\s*$", line)
        if m:
            body.append('<figure><img src="%s" alt="%s"><figcaption>%s</figcaption></figure>'
                        % (html.escape(m.group(2)), html.escape(m.group(1)), inline(m.group(1))))
            i += 1
            continue

        if line.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            header, data = rows[0], [r for r in rows[2:]]
            t = ["<table><thead><tr>%s</tr></thead><tbody>" % "".join("<th>%s</th>" % inline(c) for c in header)]
            for r in data:
                t.append("<tr>%s</tr>" % "".join("<td>%s</td>" % inline(c) for c in r))
            t.append("</tbody></table>")
            body.append("".join(t))
            continue

        m = re.match(r"^(\s*)([-*]|\d+\.)\s+(.*)$", line)
        if m:
            ordered = m.group(2)[0].isdigit()
            items = []
            while i < len(lines):
                m = re.match(r"^(\s*)([-*]|\d+\.)\s+(.*)$", lines[i])
                if not m:
                    break
                text = [m.group(3)]
                i += 1
                while i < len(lines) and lines[i].strip() and not is_block_start(lines[i]):
                    text.append(lines[i].strip())
                    i += 1
                items.append(" ".join(text))
                while i < len(lines) and not lines[i].strip():
                    # A blank line inside a list: the list goes on if the next line is an item.
                    j = i
                    while j < len(lines) and not lines[j].strip():
                        j += 1
                    if j < len(lines) and re.match(r"^\s*([-*]|\d+\.)\s+", lines[j]):
                        i = j
                    break
            tag = "ol" if ordered else "ul"
            body.append("<%s>%s</%s>" % (tag, "".join("<li>%s</li>" % inline(t) for t in items), tag))
            continue

        text = [line.strip()]
        i += 1
        while i < len(lines) and lines[i].strip() and not is_block_start(lines[i]):
            text.append(lines[i].strip())
            i += 1
        body.append("<p>%s</p>" % inline(" ".join(text)))

    return title, "\n".join(body)


def find_browser():
    for path in BROWSERS:
        if os.path.exists(path):
            return path
    return shutil.which("msedge") or shutil.which("chrome")


def main():
    browser = find_browser()
    if not browser:
        sys.exit("Microsoft Edge or Google Chrome is needed to print the PDF.")

    for source, target, lang in GUIDES:
        with open(os.path.join(DOCS, source), encoding="utf-8") as f:
            title, body = convert(f.read())

        page = ('<!doctype html><html lang="%s"><head><meta charset="utf-8"><title>%s</title>'
                "<style>%s</style></head><body>%s</body></html>") % (lang, html.escape(title), CSS, body)

        # Next to the Markdown, so that the relative image paths work.
        tmp = os.path.join(DOCS, ".build-%s.html" % lang)
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(page)

        out = os.path.join(DOCS, target)
        profile = tempfile.mkdtemp(prefix="mnkord-pdf-")
        try:
            subprocess.run(
                [browser, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                 "--user-data-dir=" + profile, "--print-to-pdf=" + out,
                 "file:///" + tmp.replace("\\", "/")],
                check=True, timeout=120, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        finally:
            os.remove(tmp)
            shutil.rmtree(profile, ignore_errors=True)

        print("wrote", os.path.relpath(out, os.path.dirname(DOCS)))


if __name__ == "__main__":
    main()
