#!/usr/bin/env python3
"""Builds the web pages in docs/ (GitHub Pages serves that folder):

    docs/index.html      English home page
    docs/hu/index.html   Hungarian home page
    docs/sitemap.xml     both pages, with hreflang alternates

Everything that changes with the project is read from the project, so the pages follow it: the
version from CMakeLists.txt, the factory presets from Source/PluginProcessor.cpp, the chord
symbols from Source/ChordSymbol.cpp. The FAQ is written once (FAQ below) and used for both the
visible questions and the FAQPage structured data, so they can't differ.

    python site/build-site.py

Python 3 only, no packages.
"""
import datetime
import html
import json
import os
import re
import struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DOCS = os.path.join(ROOT, "docs")

SITE = "https://miklosnemeth.github.io/mnkord/"
REPO = "https://github.com/miklosnemeth/mnkord"
AUTHOR = "Miklós Németh"
AUTHOR_HU = "Németh Miklós"


# ----------------------------------------------------------------------------------------------
# Facts read from the project
# ----------------------------------------------------------------------------------------------
def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read()


def project_version():
    m = re.search(r"project\(\s*mnKord\s+VERSION\s+([0-9.]+)", read("CMakeLists.txt"))
    return m.group(1) if m else ""


def factory_presets():
    """[(group, [(name, [tags])])] in the order the plugin lists them."""
    src = read("Source/PluginProcessor.cpp")

    tags = {}
    for m in re.finditer(r'name == "([^"]+)"\)\s*\n\s*preset\.tags = \{([^}]*)\}', src):
        tags[m.group(1)] = re.findall(r'"([^"]+)"', m.group(2))

    chord_items = []  # (position in the source, group, name, tags): the file lists them in order
    for m in re.finditer(r'chordPreset\("([^"]+)",\s*"([^"]+)",\s*\{([^}]*)\}', src):
        chord_items.append((m.start(), m.group(2), m.group(1), re.findall(r'"([^"]+)"', m.group(3))))
    chord_items.sort()

    # The six basic presets are built one by one in buildFactoryPresets(); Power Chords is built in a
    # block of its own, but is listed in the tag table above.
    basics = [m.group(1) for m in re.finditer(r'factory\.push_back\(makePreset\("([^"]+)",\s*"Factory"', src)]
    if "Power Chords" in tags and "Power Chords" not in basics:
        basics.insert(4, "Power Chords")

    ordered = [("Factory", n, tags.get(n, [])) for n in basics]
    ordered += [(g, n, t) for _, g, n, t in chord_items]

    groups = []
    for group, name, t in ordered:
        for g in groups:
            if g[0] == group:
                g[1].append((name, t))
                break
        else:
            groups.append((group, [(name, t)]))
    return groups


def chord_qualities():
    """The chord symbol suffixes the parser knows (the first table in ChordSymbol.cpp), in order."""
    src = read("Source/ChordSymbol.cpp")
    start = src.index("{ \"\",")
    end = src.index("};", start)
    seen = []
    for m in re.finditer(r'\{ "([^"]*)",\s*\{', src[start:end]):
        if m.group(1) not in seen:
            seen.append(m.group(1))
    return seen


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    return struct.unpack(">II", head[16:24])


# ----------------------------------------------------------------------------------------------
# Texts. UI names (INPUT, CHORD, Soft layer, ...) stay in English in both languages.
# ----------------------------------------------------------------------------------------------
QUALITY_NAMES = {
    "en": {
        "": "major triad", "m": "minor triad", "dim": "diminished triad", "aug": "augmented triad",
        "sus2": "suspended second", "sus4": "suspended fourth", "5": "power chord", "6": "major sixth",
        "m6": "minor sixth", "maj7": "major seventh", "m7": "minor seventh", "7": "dominant seventh",
        "m7b5": "half-diminished seventh", "dim7": "diminished seventh", "add9": "major add nine",
        "madd9": "minor add nine", "maj9": "major ninth", "m9": "minor ninth", "9": "dominant ninth",
        "7sus4": "dominant seventh, suspended fourth", "mmaj7": "minor-major seventh",
        "7#9": "dominant seventh, sharp nine", "7b9": "dominant seventh, flat nine", "6/9": "six-nine",
        "4th": "quartal chord (stacked fourths)",
    },
    "hu": {
        "": "dúr hármashangzat", "m": "moll hármashangzat", "dim": "szűkített hármashangzat",
        "aug": "bővített hármashangzat", "sus2": "szekund-késleltetéses (sus2)",
        "sus4": "kvart-késleltetéses (sus4)", "5": "power chord", "6": "dúr szext", "m6": "moll szext",
        "maj7": "nagy szeptim (maj7)", "m7": "moll szeptim", "7": "domináns szeptim",
        "m7b5": "fél-szűkített szeptim", "dim7": "szűkített szeptim", "add9": "dúr add9",
        "madd9": "moll add9", "maj9": "maj9 (nagy nóna)", "m9": "moll nóna", "9": "domináns nóna",
        "7sus4": "domináns szeptim, sus4", "mmaj7": "moll-nagyszeptim",
        "7#9": "domináns szeptim, felemelt nóna", "7b9": "domináns szeptim, leszállított nóna",
        "6/9": "six-nine (6/9)", "4th": "kvart-akkord (kvartokból építve)",
    },
}

SCALES = ["Major", "Natural minor", "Harmonic minor", "Melodic minor", "Major pentatonic", "Minor pentatonic"]
SCALES_HU = ["Dúr", "Természetes moll", "Harmonikus moll", "Dallamos moll", "Dúr pentaton", "Moll pentaton"]
TEMPLATES = ["Maj", "Min", "Sus2", "Sus4", "Dim", "7", "Maj7", "Min7", "Add9"]

GROUP_TITLES = {
    "en": {"Factory": "Basics"},
    "hu": {"Factory": "Alapok"},
}

T = {
    "en": {
        "lang": "en", "locale": "en_US", "dir": "", "base": "",
        "title": "mnKord – free MIDI chord plugin (VST3) for Windows",
        "description": "mnKord is a free MIDI chord plugin (VST3) for Windows: play one key and get a whole chord. "
                       "Chord symbols, velocity layers, scales and {presets} factory presets.",
        "og_alt": "The mnKord window with the INPUT and CHORD keyboards",
        "skip": "Skip to the content",
        "nav": [("#features", "Features"), ("#presets", "Presets"), ("#chords", "Chords"), ("#faq", "FAQ"), ("#get", "Get it")],
        "other_lang": ("HU", "Magyar", "hu/"),
        "this_lang": "EN",
        "eyebrow": "Free MIDI chord plugin · VST3",
        "h1_hidden": " – free MIDI chord plugin for Windows (VST3)",
        "tagline": "One key. A whole chord.",
        "lede": "Play a single note and mnKord sends the full chord to your synth. Map any key to any chord, with "
                "templates, chord symbols, velocity layers and {presets} ready-made presets.",
        "cta_download": "Download for Windows", "cta_guide": "User guide (PDF)", "cta_source": "Source on GitHub",
        "pdf": "mnKord-User-Guide.pdf", "guide_md": "USER_GUIDE.md",
        "facts": ["v{version}", "VST3 · Windows x64", "Free and open source (AGPL-3.0)"],
        "main_alt": "The mnKord window: INPUT and CHORD keyboards and the preset bar",
        "how_h": "How it works",
        "how_sub": "mnKord is a MIDI effect. It makes no sound itself: it sits in front of your synth.",
        "steps": [("Play one key", "Your MIDI keyboard sends a single note."),
                  ("mnKord expands it", "The key is looked up in the current preset and becomes a chord."),
                  ("Your synth plays it", "Put any synth after mnKord. All other MIDI passes straight through.")],
        "feat_h": "Made for fast chord playing",
        "feat_sub": "Everything you need to turn one finger into a full harmony, and nothing that gets in the way.",
        "cards": [
            ("Any key to any chord", "Click an INPUT key, then the chord tones. Or right-click for triad and seventh templates."),
            ("Chord symbols", "Type {c1}, {c2} or {c3} and the chord is voiced for you."),
            ("Velocity layers", "Play softly for one chord, hit harder and a fuller one takes over."),
            ("Scales", "Limit the keyboard to a scale and fill the whole preset with its triads or sevenths in one click."),
            ("{presets} presets, and a real browser", "Pop, jazz, soul, cinematic, dark, quartal. Search, tags, favourites and your own presets."),
            ("Learn, undo, transpose", "MIDI learn from your controller, undo and redo, copy a whole octave, transpose keys or chords."),
        ],
        "list_h": "Everything in the box",
        "list_sub": "The full feature list of mnKord {version}.",
        "feature_groups": [
            ("Building chords", [
                "Map any of the 128 MIDI keys to a chord of your own.",
                "Click the chord tones on the CHORD keyboard, or right-click an INPUT key for chord templates.",
                "Type a chord symbol (for example {c1}) to replace the selected chord; the name of the chord is shown for the selection.",
                "Invert a chord up or down with a right-click on a CHORD key.",
                "Copy the chords of a whole octave one octave up or down.",
                "Fit a chord to the selected scale, or clear it, from the right-click menu.",
            ]),
            ("Playing", [
                "Velocity layers: a soft layer for any velocity and a hard layer from velocity 80, 90, 100, 110 or 120.",
                "MIDI learn: play a key on your controller to select or create its mapping.",
                "Overlapping chords share notes (reference counted), so notes are not cut off or left stuck.",
                "The sustain pedal and all other non-note MIDI (CC, pitch bend, aftertouch) pass through to your synth.",
                "VST3 plugin and a Standalone program.",
            ]),
            ("Presets", [
                "{presets} read-only factory presets, and your own presets saved as files shared by every project.",
                "A preset browser with group, author and style columns, search and favourites.",
                "Preset metadata: name, group, author and style tags.",
                "XML export and import; import accepts a single preset or a whole bank file.",
                "The host program list mirrors the preset list (128 fixed programs), so program changes and automation pick the same presets.",
                "A project stores the preset you used, with your edits, so it sounds the same on another computer.",
            ]),
            ("Workflow", [
                "Undo and redo for every mapping edit (Ctrl+Z, Ctrl+Y).",
                "Transpose the whole preset, or only its chords, by a semitone or an octave.",
                "Restrict the keyboard to a scale and fill a preset with its triads or seventh chords.",
                "A dark, flat window with two keyboards and the version of the plugin under the title.",
                "Free and open source under the AGPL-3.0.",
            ]),
        ],
        "presets_h": "{presets} factory presets",
        "presets_sub": "Ready to play, grouped by style. All of them are read-only: edit one and use Save as to keep your version.",
        "chords_h": "Chord symbols, templates and scales",
        "chords_sub": "Type a chord symbol in the field above the CHORD keyboard and press Enter. Any root works (C, F#, Bb, ...), and a slash adds a bass note.",
        "symbols_h": "Chord symbol endings",
        "symbols_note": "Put one of these after the root note, for example {c1}. A slash and a note name add a bass note ({c4}).",
        "major_name": "(nothing after the root)",
        "templates_h": "Chord templates",
        "templates_sub": "In the right-click menu of an INPUT key: triads ({tri}) and sevenths ({sev}).",
        "scales_h": "Scales",
        "scales_sub": "Restrict the keyboard to a scale and fill the preset with its triads or sevenths.",
        "scr_h": "A look inside",
        "scr_sub": "A dark, flat window with two keyboards: the keys you play, and the chord they make.",
        "shots": [
            ("chord-templates.png", "Chord templates in the right-click menu of an INPUT key", "Right-click a key for triad and seventh templates"),
            ("preset-browser.png", "The preset browser with group, author and style columns", "The preset browser: group, author, style, search, favourites"),
            ("velocity-layers.png", "The velocity layer menu: soft layer and hard layers from velocity 80 to 120", "Velocity layers: a second chord for hard hits"),
        ],
        "req_h": "System requirements",
        "reqs": [
            "Windows 10 or 11, 64-bit (installer for the VST3).",
            "A DAW or host that loads VST3 plugins.",
            "A MIDI keyboard or controller (or the DAW's piano roll) to play the keys.",
            "A synth or sampler after mnKord, because mnKord itself makes no sound.",
            "Linux: build from the source code (instructions in the repository). macOS is not supported yet.",
        ],
        "faq_h": "Frequently asked questions",
        "get_h": "Get mnKord",
        "get_sub": "Free, with the source code open. The Windows installer is the quick way in.",
        "get_win": "Windows (64-bit)",
        "get_steps": [
            "Download <strong>mnKord-{version}-win-x64-setup.exe</strong> from the latest release.",
            "Run it. The VST3 goes to <code>Common Files\\VST3\\MNAudio</code>.",
            "Rescan plugins in your DAW and put mnKord on an instrument track, before your synth.",
        ],
        "get_btn": "Latest release",
        "docs_h": "Docs and source",
        "links": [
            ("{repo}/blob/master/docs/USER_GUIDE.md", "User guide (English)"),
            ("{base}mnKord-User-Guide.pdf", "User guide, PDF (English)"),
            ("{repo}/blob/master/docs/USER_GUIDE.hu.md", "User guide (Hungarian)"),
            ("{base}mnKord-Felhasznaloi-utmutato.pdf", "User guide, PDF (Hungarian)"),
            ("{repo}", "Source code and build instructions"),
            ("{repo}/blob/master/CHANGELOG.md", "Changelog"),
            ("{repo}/issues", "Report a bug or ask for a feature"),
            ("{repo}/blob/master/LICENSE", "License (AGPL-3.0)"),
        ],
        "mac": "macOS is not supported yet.",
        "by": "Vibecoded by",
        "name": AUTHOR, "name_html": "Miklós <b>Németh</b>",
        "about": "mnKord is a vibecoding project: the idea, the taste and the decisions are mine, the code is written together with an AI.",
        "built": 'Built with <a href="https://juce.com">JUCE</a>. Fonts: Montserrat (OFL) and Roboto Condensed (Apache 2.0).',
        "chord_examples": ["Dm9/G", "Bbmaj7", "C6/9", "G/B"],
    },
    "hu": {
        "lang": "hu", "locale": "hu_HU", "dir": "hu/", "base": "../",
        "title": "mnKord – ingyenes MIDI akkord-plugin (VST3) Windowsra",
        "description": "A mnKord egy ingyenes MIDI akkord-plugin (VST3) Windowsra: egy billentyűvel egész akkordot játszol. "
                       "Akkordszimbólumok, velocity-rétegek, skálák és {presets} gyári preset.",
        "og_alt": "A mnKord ablaka az INPUT és a CHORD billentyűzettel",
        "skip": "Ugrás a tartalomra",
        "nav": [("#features", "Funkciók"), ("#presets", "Presetek"), ("#chords", "Akkordok"), ("#faq", "GYIK"), ("#get", "Letöltés")],
        "other_lang": ("EN", "English", "../"),
        "this_lang": "HU",
        "eyebrow": "Ingyenes MIDI akkord-plugin · VST3",
        "h1_hidden": " – ingyenes MIDI akkord-plugin Windowsra (VST3)",
        "tagline": "Egy billentyű. Egy egész akkord.",
        "lede": "Nyomj meg egy billentyűt, és a mnKord az egész akkordot elküldi a szintetizátorodnak. Bármelyik billentyűhöz "
                "bármelyik akkord rendelhető: sablonokkal, akkordszimbólumokkal, velocity-rétegekkel és {presets} kész presettel.",
        "cta_download": "Letöltés Windowsra", "cta_guide": "Útmutató (PDF)", "cta_source": "Forráskód a GitHubon",
        "pdf": "mnKord-Felhasznaloi-utmutato.pdf", "guide_md": "USER_GUIDE.hu.md",
        "facts": ["v{version}", "VST3 · Windows x64", "Ingyenes és nyílt forráskódú (AGPL-3.0)"],
        "main_alt": "A mnKord ablaka: INPUT és CHORD billentyűzet, preset-sáv",
        "how_h": "Hogyan működik",
        "how_sub": "A mnKord MIDI-effekt. Hangot nem ad, a szintetizátorod elé kerül.",
        "steps": [("Nyomj meg egy billentyűt", "A MIDI-billentyűzeted egyetlen hangot küld."),
                  ("A mnKord kibővíti", "A billentyű az aktuális preset alapján akkorddá bővül."),
                  ("A szintetizátor lejátssza", "Tegyél bármilyen szintetizátort a mnKord után. Minden más MIDI változatlanul átmegy.")],
        "feat_h": "Gyors akkordjátékra tervezve",
        "feat_sub": "Minden, amivel egy ujjból teljes harmónia lesz, és semmi, ami útban van.",
        "cards": [
            ("Bármely billentyű, bármely akkord", "Kattints egy INPUT billentyűre, majd az akkordhangokra. Vagy jobb kattintással válassz hármas- és szeptimhangzat-sablont."),
            ("Akkordszimbólumok", "Írd be: {c1}, {c2} vagy {c3}, és az akkord kész."),
            ("Velocity-rétegek", "Halkan az egyik akkord szól, erősebb ütésre egy teltebb veszi át."),
            ("Skálák", "Korlátozd a billentyűzetet egy skálára, és egy kattintással töltsd ki a presetet a skála hármas- vagy szeptimhangzataival."),
            ("{presets} preset és igazi böngésző", "Pop, jazz, soul, filmzene, sötét, kvart. Keresés, címkék, kedvencek és saját presetek."),
            ("Learn, undo, transzponálás", "MIDI learn a kontrollerről, visszavonás és újra, egy egész oktáv másolása, billentyűk vagy akkordok transzponálása."),
        ],
        "list_h": "Minden, ami benne van",
        "list_sub": "A mnKord {version} teljes funkciólistája.",
        "feature_groups": [
            ("Akkordok építése", [
                "A 128 MIDI-billentyű bármelyikéhez saját akkordot rendelhetsz.",
                "Kattints az akkordhangokra a CHORD billentyűzeten, vagy jobb kattintással válassz akkordsablont egy INPUT billentyűn.",
                "Írj be egy akkordszimbólumot (például {c1}), és az a kiválasztott akkord helyére kerül; a kijelölés akkordjának nevét a program kiírja.",
                "Az akkordot jobb kattintással felfelé vagy lefelé megfordíthatod egy CHORD billentyűn.",
                "Egy egész oktáv akkordjait egy oktávval feljebb vagy lejjebb másolhatod.",
                "Az akkordot a jobb kattintású menüből a kiválasztott skálához igazíthatod, vagy törölheted.",
            ]),
            ("Játék", [
                "Velocity-rétegek: egy soft réteg bármilyen erősséghez, és egy hard réteg 80, 90, 100, 110 vagy 120 velocity-től.",
                "MIDI learn: játssz egy billentyűt a kontrolleren, és az kiválasztja (vagy létrehozza) a leképezését.",
                "Az átfedő akkordok osztoznak a hangokon (hivatkozásszámlálással), így nem vágódnak le és nem ragadnak be hangok.",
                "A szusztain pedál és minden más, nem hang típusú MIDI (CC, pitch bend, aftertouch) átmegy a szintetizátorra.",
                "VST3 plugin és Standalone program.",
            ]),
            ("Presetek", [
                "{presets} csak olvasható gyári preset, és a saját presetjeid, amelyek fájlként mentődnek, és minden projekt közösen használja őket.",
                "Preset-böngésző csoport, szerző és stílus oszlopokkal, kereséssel és kedvencekkel.",
                "Preset-adatok: név, csoport, szerző és stílus-címkék.",
                "XML export és import; az import egy presetet vagy egy teljes bank-fájlt is elfogad.",
                "A hoszt programlistája tükrözi a presetlistát (128 fix program), így a programváltás és az automatizálás ugyanazt a presetet választja.",
                "A projekt a használt presetet a szerkesztéseivel együtt tárolja, ezért egy másik gépen is ugyanúgy szól.",
            ]),
            ("Munkafolyamat", [
                "Visszavonás és újra minden leképezés-szerkesztéshez (Ctrl+Z, Ctrl+Y).",
                "A teljes preset, vagy csak az akkordjai transzponálása félhanggal vagy oktávval.",
                "A billentyűzet korlátozása egy skálára, és a preset kitöltése a skála hármas- vagy szeptimhangzataival.",
                "Sötét, letisztult ablak két billentyűzettel, a cím alatt a plugin verziójával.",
                "Ingyenes és nyílt forráskódú, AGPL-3.0 alatt.",
            ]),
        ],
        "presets_h": "{presets} gyári preset",
        "presets_sub": "Azonnal játszhatók, stílus szerint csoportosítva. Mind csak olvasható: szerkeszd, és a Save as gombbal mentsd el a saját változatodat.",
        "chords_h": "Akkordszimbólumok, sablonok és skálák",
        "chords_sub": "Írj be egy akkordszimbólumot a CHORD billentyűzet feletti mezőbe, és nyomj Entert. Bármilyen alaphang megy (C, F#, Bb, ...), a perjel pedig basszushangot ad.",
        "symbols_h": "Akkordszimbólum-végződések",
        "symbols_note": "Az alaphang után írd az egyiket, például {c1}. A perjel és egy hangnév basszushangot ad ({c4}).",
        "major_name": "(semmi az alaphang után)",
        "templates_h": "Akkordsablonok",
        "templates_sub": "Egy INPUT billentyű jobb kattintású menüjében: hármashangzatok ({tri}) és szeptimek ({sev}).",
        "scales_h": "Skálák",
        "scales_sub": "Korlátozd a billentyűzetet egy skálára, és töltsd ki a presetet a skála hármas- vagy szeptimhangzataival.",
        "scr_h": "Egy pillantás belülről",
        "scr_sub": "Egy sötét, letisztult ablak két billentyűzettel: amit játszol, és az akkord, amit kapsz.",
        "shots": [
            ("chord-templates.png", "Akkordsablonok egy INPUT billentyű jobb kattintású menüjében", "Jobb kattintás egy billentyűn: hármas- és szeptimhangzat-sablonok"),
            ("preset-browser.png", "A preset-böngésző csoport, szerző és stílus oszlopokkal", "A preset-böngésző: csoport, szerző, stílus, keresés, kedvencek"),
            ("velocity-layers.png", "A velocity-réteg menü: soft réteg és hard rétegek 80-tól 120 velocity-ig", "Velocity-rétegek: második akkord erős ütésre"),
        ],
        "req_h": "Rendszerkövetelmények",
        "reqs": [
            "Windows 10 vagy 11, 64 bites (telepítő a VST3-hoz).",
            "VST3 pluginokat betöltő DAW vagy hoszt.",
            "MIDI-billentyűzet vagy kontroller (vagy a DAW zongorasávja) a billentyűk megszólaltatásához.",
            "Egy szintetizátor vagy sampler a mnKord után, mert a mnKord maga nem ad hangot.",
            "Linux: fordítás a forráskódból (útmutató a tárolóban). macOS-t egyelőre nem támogat.",
        ],
        "faq_h": "Gyakori kérdések",
        "get_h": "Szerezd meg a mnKordot",
        "get_sub": "Ingyenes, nyílt forráskóddal. A leggyorsabb út a Windows-telepítő.",
        "get_win": "Windows (64 bites)",
        "get_steps": [
            "Töltsd le a <strong>mnKord-{version}-win-x64-setup.exe</strong> fájlt a legfrissebb kiadásból.",
            "Futtasd. A VST3 a <code>Common Files\\VST3\\MNAudio</code> mappába kerül.",
            "Indíts új pluginkeresést a DAW-ban, és tedd a mnKordot egy hangszersávra, a szintetizátor elé.",
        ],
        "get_btn": "Legfrissebb kiadás",
        "docs_h": "Dokumentáció és forrás",
        "links": [
            ("{repo}/blob/master/docs/USER_GUIDE.hu.md", "Felhasználói útmutató (magyar)"),
            ("{base}mnKord-Felhasznaloi-utmutato.pdf", "Felhasználói útmutató, PDF (magyar)"),
            ("{repo}/blob/master/docs/USER_GUIDE.md", "Felhasználói útmutató (angol)"),
            ("{base}mnKord-User-Guide.pdf", "Felhasználói útmutató, PDF (angol)"),
            ("{repo}", "Forráskód és fordítási útmutató"),
            ("{repo}/blob/master/CHANGELOG.md", "Változásnapló (angolul)"),
            ("{repo}/issues", "Hibabejelentés, ötletek"),
            ("{repo}/blob/master/LICENSE", "Licenc (AGPL-3.0)"),
        ],
        "mac": "macOS-t egyelőre nem támogat.",
        "by": "Vibecoder",
        "name": AUTHOR_HU, "name_html": "<b>Németh</b> Miklós",
        "about": "A mnKord egy vibecoding-projekt: az ötlet, az ízlés és a döntések az enyémek, a kódot egy MI-vel együtt írom.",
        "built": '<a href="https://juce.com">JUCE</a> alapon. Betűk: Montserrat (OFL) és Roboto Condensed (Apache 2.0).',
        "chord_examples": ["Dm9/G", "Bbmaj7", "C6/9", "G/B"],
    },
}

# One FAQ for both the visible questions and the structured data. Answers may contain <code>.
FAQ = {
    "en": [
        ("What is mnKord?",
         "mnKord is a free MIDI chord plugin (VST3) for Windows. You play one key and it sends a whole chord to your synth. "
         "Every key can have its own chord, set with templates, chord symbols or by clicking the notes."),
        ("Is mnKord free?",
         "Yes. mnKord is free software under the GNU Affero General Public License v3.0, and the source code is open on GitHub."),
        ("Does mnKord make sound by itself?",
         "No. It is a MIDI effect: it turns one note into a chord, and the synth or sampler after it plays the sound. "
         "Put it on an instrument track, before your synth."),
        ("Which operating systems does it run on?",
         "There is an installer for Windows 10 and 11 (64-bit). On Linux you can build it from the source code. macOS is not supported yet."),
        ("Which DAWs can I use it in?",
         "mnKord is a standard VST3 plugin, so a DAW that hosts VST3 plugins on Windows can load it. "
         "How MIDI is routed to the next plugin differs between DAWs; the user guide has the steps."),
        ("How do I make my own chord?",
         "Click a key on the INPUT keyboard, then click the notes on the CHORD keyboard. You can also right-click an INPUT key for "
         "chord templates, or type a chord symbol such as <code>Dm9/G</code> in the field above the CHORD keyboard."),
        ("Which chord symbols can I type?",
         "Triads, suspended, power chords, sixths, sevenths, ninths, add9, altered sevenths (7b9, 7#9), 6/9, quartal chords, and "
         "slash chords with a bass note, for any root note. The full list is on this page."),
        ("What are velocity layers?",
         "A key can have a second, hard layer with its own chord. It plays instead of the soft layer when you hit the key at least as "
         "hard as the threshold you choose (80, 90, 100, 110 or 120)."),
        ("Where are my presets saved?",
         "Your presets are files in <code>Documents\\MNAudio\\mnKord\\User</code>, shared by every project. A DAW project also stores the "
         "preset it used, with its edits, so it sounds the same on another computer."),
        ("Who made mnKord?",
         "mnKord is made by Miklós Németh as a vibecoding project: the idea, the taste and the decisions are his, and the code is written together with an AI."),
    ],
    "hu": [
        ("Mi a mnKord?",
         "A mnKord egy ingyenes MIDI akkord-plugin (VST3) Windowsra. Megnyomsz egy billentyűt, és egy egész akkordot küld a szintetizátorodnak. "
         "Minden billentyűnek saját akkordja lehet, amit sablonokkal, akkordszimbólumokkal vagy a hangokra kattintva állítasz be."),
        ("Ingyenes a mnKord?",
         "Igen. A mnKord szabad szoftver a GNU Affero General Public License v3.0 alatt, a forráskódja nyílt a GitHubon."),
        ("Ad hangot a mnKord magától?",
         "Nem. MIDI-effekt: egy hangból akkordot csinál, a hangot a mögötte lévő szintetizátor vagy sampler adja. "
         "Tedd egy hangszersávra, a szintetizátor elé."),
        ("Milyen operációs rendszereken fut?",
         "Windows 10-re és 11-re (64 bites) van telepítő. Linuxon a forráskódból lefordíthatod. macOS-t egyelőre nem támogat."),
        ("Melyik DAW-ban használhatom?",
         "A mnKord szabványos VST3 plugin, ezért minden DAW betöltheti, amelyik Windowson VST3 pluginokat hosztol. "
         "Hogy a MIDI hogyan jut a következő pluginhoz, DAW-onként más; a lépések az útmutatóban vannak."),
        ("Hogyan készítek saját akkordot?",
         "Kattints egy billentyűre az INPUT billentyűzeten, majd a hangokra a CHORD billentyűzeten. Jobb kattintással akkordsablonokat is kapsz, "
         "vagy beírhatsz egy akkordszimbólumot (például <code>Dm9/G</code>) a CHORD billentyűzet feletti mezőbe."),
        ("Milyen akkordszimbólumokat írhatok be?",
         "Hármashangzatokat, sus akkordokat, power chordot, szextet, szeptimeket, nónákat, add9-et, módosított szeptimeket (7b9, 7#9), 6/9-et, "
         "kvart-akkordokat és basszushangos (perjeles) akkordokat, bármilyen alaphanggal. A teljes lista ezen az oldalon van."),
        ("Mik azok a velocity-rétegek?",
         "Egy billentyűnek lehet egy második, hard rétege saját akkorddal. A soft réteg helyett az szól, ha legalább olyan erősen ütöd le a billentyűt, "
         "mint a választott küszöb (80, 90, 100, 110 vagy 120)."),
        ("Hol vannak a presetjeim?",
         "A saját presetjeid fájlok a <code>Documents\\MNAudio\\mnKord\\User</code> mappában, minden projekt közösen használja őket. A DAW-projekt a "
         "használt presetet a szerkesztéseivel együtt is tárolja, ezért egy másik gépen is ugyanúgy szól."),
        ("Ki készítette a mnKordot?",
         "A mnKordot Németh Miklós készítette vibecoding-projektként: az ötlet, az ízlés és a döntések az övéi, a kódot egy MI-vel együtt írja."),
    ],
}


# ----------------------------------------------------------------------------------------------
# HTML
# ----------------------------------------------------------------------------------------------
def esc(s):
    return html.escape(s, quote=True)


def plain(s):
    return html.unescape(re.sub(r"<[^>]+>", "", s))


def logo_svg(extra=""):
    return ('<svg %sviewBox="0 0 32 32" aria-hidden="true"><rect width="32" height="32" rx="7" fill="#13211a"/>'
            '<rect x="6" y="7" width="5" height="18" rx="1.5" fill="#3ddc84"/><rect x="13.5" y="7" width="5" height="18" rx="1.5" fill="#fbfcfb"/>'
            '<rect x="21" y="7" width="5" height="18" rx="1.5" fill="#3ddc84"/></svg>') % extra


ICONS = [
    '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="M8 5v8M12 5v8M16 5v8"/>',
    '<path d="M4 7h16M4 12h10M4 17h6"/>',
    '<path d="M3 18c3 0 3-12 6-12s3 12 6 12 3-8 6-8"/>',
    '<path d="M4 20 12 4l8 16M7 14h10"/>',
    '<path d="m12 3 2.7 5.6 6.1.9-4.4 4.3 1 6.1L12 17l-5.4 2.9 1-6.1L3.2 9.5l6.1-.9z"/>',
    '<path d="M9 14 4 9l5-5M4 9h10a6 6 0 0 1 0 12h-3"/>',
]


def build_page(lang, ctx):
    t = T[lang]
    fmt = dict(presets=ctx["n_presets"], version=ctx["version"],
               c1=ctx["ex"][0], c2=ctx["ex"][1], c3=ctx["ex"][2], c4=ctx["ex"][3],
               tri=", ".join(TEMPLATES[:5]), sev=", ".join(TEMPLATES[5:]),
               repo=REPO, base=t["base"])

    def f(s):
        return s.format(**fmt)

    def code(s):
        return "<code>%s</code>" % esc(s)

    def fc(s):
        """The text escaped, with {c1}..{c4} (and the other fields) filled in; the chords as <code>."""
        c = dict(fmt)
        for k in ("c1", "c2", "c3", "c4"):
            c[k] = code(fmt[k])
        return esc(s).format(**c)

    url = SITE + t["dir"]
    en_url, hu_url = SITE, SITE + "hu/"
    title = t["title"]
    description = f(t["description"])
    image = SITE + "images/main.png"
    iw, ih = ctx["image_size"]
    base = t["base"]
    cta_download = REPO + "/releases/latest"

    faq = FAQ[lang]

    # ---- structured data
    graph = [
        {"@type": "WebSite", "@id": SITE + "#website", "url": SITE, "name": "mnKord",
         "inLanguage": ["en", "hu"], "publisher": {"@id": SITE + "#author"}},
        {"@type": "Person", "@id": SITE + "#author", "name": AUTHOR, "alternateName": AUTHOR_HU,
         "url": "https://github.com/miklosnemeth", "jobTitle": "Vibecoder"},
        {"@type": "SoftwareApplication", "@id": SITE + "#app", "name": "mnKord",
         "url": url, "description": description, "inLanguage": lang,
         "applicationCategory": "MultimediaApplication", "applicationSubCategory": "MIDI effect, VST3 audio plugin",
         "operatingSystem": "Windows 10, Windows 11", "softwareVersion": ctx["version"],
         "downloadUrl": cta_download, "installUrl": cta_download, "fileFormat": "application/vnd.steinberg.vst3",
         "softwareRequirements": "A DAW that hosts VST3 plugins",
         "license": "https://www.gnu.org/licenses/agpl-3.0.html", "isAccessibleForFree": True,
         "offers": {"@type": "Offer", "price": "0", "priceCurrency": "USD"},
         "author": {"@id": SITE + "#author"}, "image": image,
         "screenshot": [SITE + "images/" + s[0] for s in t["shots"]] + [image],
         "featureList": [plain(fc(x)) for _, items in t["feature_groups"] for x in items]},
        {"@type": "FAQPage", "@id": url + "#faq", "inLanguage": lang,
         "mainEntity": [{"@type": "Question", "name": q,
                         "acceptedAnswer": {"@type": "Answer", "text": plain(a)}} for q, a in faq]},
    ]
    jsonld = json.dumps({"@context": "https://schema.org", "@graph": graph}, ensure_ascii=False, indent=1)
    jsonld = jsonld.replace("</", "<\\/")

    o = []
    w = o.append

    w("<!doctype html>")
    w('<html lang="%s">' % lang)
    w("<head>")
    w('<meta charset="utf-8">')
    w('<meta name="viewport" content="width=device-width, initial-scale=1">')
    w("<title>%s</title>" % esc(title))
    w('<meta name="description" content="%s">' % esc(description))
    w('<meta name="robots" content="index, follow, max-image-preview:large">')
    w('<meta name="author" content="%s">' % esc(t["name"]))
    w('<link rel="canonical" href="%s">' % url)
    w('<link rel="alternate" hreflang="en" href="%s">' % en_url)
    w('<link rel="alternate" hreflang="hu" href="%s">' % hu_url)
    w('<link rel="alternate" hreflang="x-default" href="%s">' % en_url)
    w('<meta property="og:type" content="website">')
    w('<meta property="og:site_name" content="mnKord">')
    w('<meta property="og:title" content="%s">' % esc(title))
    w('<meta property="og:description" content="%s">' % esc(description))
    w('<meta property="og:url" content="%s">' % url)
    w('<meta property="og:locale" content="%s">' % t["locale"])
    w('<meta property="og:locale:alternate" content="%s">' % T["hu" if lang == "en" else "en"]["locale"])
    w('<meta property="og:image" content="%s">' % image)
    w('<meta property="og:image:width" content="%d">' % iw)
    w('<meta property="og:image:height" content="%d">' % ih)
    w('<meta property="og:image:alt" content="%s">' % esc(t["og_alt"]))
    w('<meta name="twitter:card" content="summary_large_image">')
    w('<meta name="twitter:title" content="%s">' % esc(title))
    w('<meta name="twitter:description" content="%s">' % esc(description))
    w('<meta name="twitter:image" content="%s">' % image)
    w('<meta name="color-scheme" content="dark">')
    w('<meta name="theme-color" content="#0b1510">')
    w('<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns=\'http://www.w3.org/2000/svg\' viewBox=\'0 0 32 32\'%3E%3Crect width=\'32\' height=\'32\' rx=\'7\' fill=\'%230b1510\'/%3E%3Crect x=\'6\' y=\'7\' width=\'5\' height=\'18\' rx=\'1.5\' fill=\'%233ddc84\'/%3E%3Crect x=\'13.5\' y=\'7\' width=\'5\' height=\'18\' rx=\'1.5\' fill=\'%23fbfcfb\'/%3E%3Crect x=\'21\' y=\'7\' width=\'5\' height=\'18\' rx=\'1.5\' fill=\'%233ddc84\'/%3E%3C/svg%3E">')
    w('<link rel="preload" href="%sassets/fonts/Montserrat.ttf" as="font" type="font/ttf" crossorigin>' % base)
    w("<style>")
    w(ctx["css"].replace("{{BASE}}", base))
    w("</style>")
    w('<script type="application/ld+json">')
    w(jsonld)
    w("</script>")
    w("</head>")
    w("<body>")
    w('<a class="skip" href="#main">%s</a>' % esc(t["skip"]))

    # ---- header
    w('<div class="wrap">')
    w('  <header class="top">')
    w('    <a class="brand display" href="%s" aria-label="mnKord">%smnKord</a>' % (base or "./", logo_svg()))
    w('    <nav class="nav" aria-label="%s">' % ("Main" if lang == "en" else "Főmenü"))
    for href, label in t["nav"]:
        w('      <a class="hide-sm" href="%s">%s</a>' % (href, esc(label)))
    code_, name_, path_ = t["other_lang"]
    w('      <span class="lang" role="group" aria-label="Language / Nyelv"><span class="cur" aria-current="true">%s</span>'
      '<a href="%s" hreflang="%s" lang="%s" title="%s">%s</a></span>'
      % (t["this_lang"], path_, code_.lower(), code_.lower(), esc(name_), code_))
    w("    </nav>")
    w("  </header>")

    # ---- hero
    w('  <main id="main">')
    w('    <div class="hero">')
    w('      <p class="eyebrow">%s</p>' % esc(t["eyebrow"]))
    w('      <h1 class="display">mn<span>Kord</span><span class="sr-only">%s</span></h1>' % esc(t["h1_hidden"]))
    w('      <p class="tagline">%s</p>' % esc(t["tagline"]))
    w('      <p class="lede">%s</p>' % esc(f(t["lede"])))
    w('      <div class="cta">')
    w('        <a class="btn primary" href="%s"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" '
      'stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 3v12m0 0-5-5m5 5 5-5M4 20h16"/></svg>%s</a>'
      % (cta_download, esc(t["cta_download"])))
    w('        <a class="btn" href="%s%s"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" '
      'stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M6 3h8l4 4v14H6z"/><path d="M14 3v4h4M9 13h6M9 17h6"/></svg>%s</a>'
      % (base, t["pdf"], esc(t["cta_guide"])))
    w('        <a class="btn" href="%s"><svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M12 .5a11.5 11.5 0 0 0-3.64 '
      '22.41c.58.1.79-.25.79-.56v-2c-3.2.7-3.88-1.37-3.88-1.37-.52-1.33-1.28-1.69-1.28-1.69-1.04-.71.08-.7.08-.7 1.15.08 1.76 1.19 '
      '1.76 1.19 1.03 1.76 2.69 1.25 3.35.96.1-.75.4-1.25.73-1.54-2.55-.29-5.24-1.28-5.24-5.69 0-1.26.45-2.28 1.19-3.09-.12-.29-.52-1.46.11-3.05 '
      '0 0 .97-.31 3.17 1.18a11 11 0 0 1 5.78 0c2.2-1.49 3.17-1.18 3.17-1.18.63 1.59.23 2.76.11 3.05.74.81 1.19 1.83 1.19 3.09 0 4.42-2.69 '
      '5.39-5.25 5.68.41.36.78 1.06.78 2.14v3.17c0 .31.21.67.8.56A11.5 11.5 0 0 0 12 .5Z"/></svg>%s</a>' % (REPO, esc(t["cta_source"])))
    w("      </div>")
    w('      <ul class="facts">')
    for fact in t["facts"]:
        w("        <li>%s</li>" % esc(f(fact)))
    w("      </ul>")
    w('      <div class="shot"><img src="%simages/main.png" width="%d" height="%d" alt="%s"></div>' % (base, iw, ih, esc(t["main_alt"])))
    w("    </div>")

    # ---- how it works
    w('    <section aria-labelledby="how">')
    w('      <h2 id="how" class="display">%s</h2>' % esc(t["how_h"]))
    w('      <p class="sub">%s</p>' % esc(t["how_sub"]))
    w('      <ol class="steps">')
    for h, p in t["steps"]:
        w('        <li class="step"><h3>%s</h3><p>%s</p></li>' % (esc(h), esc(p)))
    w("      </ol>")
    w("    </section>")

    # ---- features: cards, then the full list
    w('    <section id="features" aria-labelledby="feat">')
    w('      <h2 id="feat" class="display">%s</h2>' % esc(t["feat_h"]))
    w('      <p class="sub">%s</p>' % esc(t["feat_sub"]))
    w('      <ul class="cards">')
    for i, (h, p) in enumerate(t["cards"]):
        w('        <li class="card"><div class="ic"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" '
          'stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">%s</svg></div><h3>%s</h3><p>%s</p></li>'
          % (ICONS[i], esc(f(h)), fc(p)))
    w("      </ul>")
    w('      <h3 class="listh">%s</h3>' % esc(t["list_h"]))
    w('      <p class="sub">%s</p>' % esc(f(t["list_sub"])))
    w('      <div class="featlists">')
    for gh, items in t["feature_groups"]:
        w('        <div class="fl"><h4>%s</h4><ul>' % esc(gh))
        for item in items:
            w("          <li>%s</li>" % fc(item))
        w("        </ul></div>")
    w("      </div>")
    w("    </section>")

    # ---- presets
    w('    <section id="presets" aria-labelledby="pre">')
    w('      <h2 id="pre" class="display">%s</h2>' % esc(f(t["presets_h"])))
    w('      <p class="sub">%s</p>' % esc(t["presets_sub"]))
    w('      <div class="presetgroups">')
    for group, presets in ctx["groups"]:
        w('        <div class="pg"><h3>%s</h3><ul>' % esc(GROUP_TITLES[lang].get(group, group)))
        for name, tags in presets:
            extra = ' <span class="tags">%s</span>' % esc(", ".join(tags)) if tags else ""
            w("          <li><strong>%s</strong>%s</li>" % (esc(name), extra))
        w("        </ul></div>")
    w("      </div>")
    w("    </section>")

    # ---- chords: symbols, templates, scales
    w('    <section id="chords" aria-labelledby="cho">')
    w('      <h2 id="cho" class="display">%s</h2>' % esc(t["chords_h"]))
    w('      <p class="sub">%s</p>' % esc(t["chords_sub"]))
    w('      <h3 class="listh">%s</h3>' % esc(t["symbols_h"]))
    w('      <p class="sub">%s</p>' % fc(t["symbols_note"]))
    w('      <ul class="symbols">')
    names = QUALITY_NAMES[lang]
    for q in ctx["qualities"]:
        w('        <li><code>C%s</code> <span>%s</span></li>' % (esc(q), esc(names.get(q, ""))))
    w("      </ul>")
    w('      <div class="twocol">')
    w('        <div><h3 class="listh">%s</h3><p class="sub">%s</p><ul class="chips">' % (esc(t["templates_h"]), esc(f(t["templates_sub"]))))
    for x in TEMPLATES:
        w("          <li>%s</li>" % esc(x))
    w("        </ul></div>")
    w('        <div><h3 class="listh">%s</h3><p class="sub">%s</p><ul class="chips">' % (esc(t["scales_h"]), esc(t["scales_sub"])))
    for x in (SCALES if lang == "en" else SCALES_HU):
        w("          <li>%s</li>" % esc(x))
    w("        </ul></div>")
    w("      </div>")
    w("    </section>")

    # ---- screenshots
    w('    <section id="screens" aria-labelledby="scr">')
    w('      <h2 id="scr" class="display">%s</h2>' % esc(t["scr_h"]))
    w('      <p class="sub">%s</p>' % esc(t["scr_sub"]))
    w('      <div class="gallery">')
    for fn, alt, cap in t["shots"]:
        sw, sh = ctx["shot_sizes"][fn]
        w('        <figure><img src="%simages/%s" loading="lazy" width="%d" height="%d" alt="%s"><figcaption>%s</figcaption></figure>'
          % (base, fn, sw, sh, esc(alt), esc(cap)))
    w("      </div>")
    w("    </section>")

    # ---- requirements
    w('    <section id="requirements" aria-labelledby="req">')
    w('      <h2 id="req" class="display">%s</h2>' % esc(t["req_h"]))
    w('      <ul class="plain">')
    for r in t["reqs"]:
        w("        <li>%s</li>" % esc(r))
    w("      </ul>")
    w("    </section>")

    # ---- FAQ
    w('    <section id="faq" aria-labelledby="faqh">')
    w('      <h2 id="faqh" class="display">%s</h2>' % esc(t["faq_h"]))
    w('      <div class="faq">')
    for q, a in faq:
        w("        <details><summary>%s</summary><p>%s</p></details>" % (esc(q), a))
    w("      </div>")
    w("    </section>")

    # ---- get
    w('    <section id="get" aria-labelledby="getit">')
    w('      <h2 id="getit" class="display">%s</h2>' % esc(t["get_h"]))
    w('      <p class="sub">%s</p>' % esc(t["get_sub"]))
    w('      <div class="get">')
    w('        <div class="box"><h3>%s</h3><ol>' % esc(t["get_win"]))
    for s in t["get_steps"]:
        w("          <li>%s</li>" % f(s))
    w('        </ol><a class="btn primary" href="%s">%s</a></div>' % (cta_download, esc(t["get_btn"])))
    w('        <div class="box"><h3>%s</h3><ul>' % esc(t["docs_h"]))
    for href, label in t["links"]:
        w('          <li><a href="%s">%s</a></li>' % (esc(f(href)), esc(label)))
    w("        </ul><p>%s</p></div>" % esc(t["mac"]))
    w("      </div>")
    w("    </section>")
    w("  </main>")
    w("</div>")

    # ---- footer
    w('<footer class="author">')
    w('  <div class="wrap">')
    w('    <div class="tag"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" '
      'stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m8 8-5 4 5 4M16 8l5 4-5 4M14 5l-4 14"/></svg>%s</div>' % esc(t["by"]))
    w('    <p class="name display">%s</p>' % t["name_html"])
    w("    <p>%s</p>" % esc(t["about"]))
    w('    <p class="legal">© %d %s · <a href="%s/blob/master/LICENSE">AGPL-3.0</a> · %s</p>'
      % (ctx["year"], esc(t["name"]), REPO, t["built"]))
    w("  </div>")
    w("</footer>")
    w("</body>")
    w("</html>")
    return "\n".join(o) + "\n"


def sitemap(lastmod):
    def entry(url, other_url, lang_self, lang_other):
        return (
            "  <url>\n    <loc>%s</loc>\n    <lastmod>%s</lastmod>\n    <changefreq>monthly</changefreq>\n    <priority>%s</priority>\n"
            '    <xhtml:link rel="alternate" hreflang="%s" href="%s"/>\n    <xhtml:link rel="alternate" hreflang="%s" href="%s"/>\n'
            '    <xhtml:link rel="alternate" hreflang="x-default" href="%s"/>\n  </url>\n'
            % (url, lastmod, "1.0" if url == SITE else "0.9", lang_self, url, lang_other, other_url, SITE))
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:xhtml="http://www.w3.org/1999/xhtml">\n'
            + entry(SITE, SITE + "hu/", "en", "hu") + entry(SITE + "hu/", SITE, "hu", "en") + "</urlset>\n")


def main():
    with open(os.path.join(os.path.dirname(__file__), "site.css"), encoding="utf-8") as f:
        css = f.read()

    shots = {}
    for fn in ("main.png", "chord-templates.png", "preset-browser.png", "velocity-layers.png", "preset-menu.png"):
        shots[fn] = png_size(os.path.join(DOCS, "images", fn))

    groups = factory_presets()
    n = sum(len(p) for _, p in groups)
    ctx = dict(
        version=project_version(), css=css, groups=groups, n_presets=n,
        qualities=chord_qualities(), ex=T["en"]["chord_examples"],
        image_size=shots["main.png"], shot_sizes=shots, year=datetime.date.today().year,
    )

    os.makedirs(os.path.join(DOCS, "hu"), exist_ok=True)
    for lang, rel in (("en", "index.html"), ("hu", os.path.join("hu", "index.html"))):
        with open(os.path.join(DOCS, rel), "w", encoding="utf-8", newline="\n") as f:
            f.write(build_page(lang, ctx))
        print("wrote docs/" + rel.replace("\\", "/"))

    with open(os.path.join(DOCS, "sitemap.xml"), "w", encoding="utf-8", newline="\n") as f:
        f.write(sitemap(datetime.date.today().isoformat()))
    print("wrote docs/sitemap.xml  (%d presets, %d chord endings, version %s)" % (n, len(ctx["qualities"]), ctx["version"]))


if __name__ == "__main__":
    main()
