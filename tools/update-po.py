#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""PC5's text for translators: lang/pc5.pot from the sources, merged into every lang/*.po.

    tools/update-po.py            make lang/pc5.pot again and merge it into lang/*.po
    tools/update-po.py --check    only check: lang/pc5.pot is what the sources give, and every
                                  .po has the template's entries and usable formats (exit 1 if not)
    tools/update-po.py --new CODE make lang/CODE.po for a language PC5 hasn't had yet

What is marked, in title/pc5*.c (pc5_i18n.h):
    T("...")  N_("...")  pc5_tf(out, size, "..." [: "..."], ...)   (every literal of the format)
with a comment holding TRANSLATORS: on the lines just before going to the translators. The web
page's text (title/pc5_web_page.html) is msgctxt "web": an element's text with data-i18n, its
data-i18n-placeholder and data-i18n-title, and the script's t("...").

Merging is msgmerge's: each entry keeps its translation; an English text that changed a little
gets the old translation marked fuzzy (PC5 shows English for it until a translator checks it);
entries no longer used stay at the end as obsolete (#~). No dates are written, so the files
change only when the text does.
"""
import difflib
import html
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LANG = ROOT / "lang"
POT = LANG / "pc5.pot"

# Each language's file, and the gettext code its header names (Poedit, Weblate and msgfmt read it).
LANGUAGES = {
    "ar": "ar",
    "cs": "cs", "da": "da", "de": "de", "el": "el", "es": "es", "es-419": "es_419", "fi": "fi", "fr": "fr",
    "fr-CA": "fr_CA", "hu": "hu", "id": "id", "it": "it", "ja": "ja", "ko": "ko", "nl": "nl", "no": "nb",
    "pl": "pl", "pt-BR": "pt_BR", "pt-PT": "pt_PT", "ro": "ro", "ru": "ru", "sv": "sv", "tr": "tr", "uk": "uk",
    "vi": "vi", "zh-Hans": "zh_Hans", "zh-Hant": "zh_Hant",
}
NAMES = {
    "ar": "Arabic",
    "cs": "Czech", "da": "Danish", "de": "German", "el": "Greek", "es": "Spanish (Spain)",
    "es-419": "Spanish (Latin America)", "fi": "Finnish", "fr": "French (France)", "fr-CA": "French (Canada)",
    "hu": "Hungarian", "id": "Indonesian", "it": "Italian", "ja": "Japanese", "ko": "Korean", "nl": "Dutch",
    "no": "Norwegian (Bokmål)", "pl": "Polish", "pt-BR": "Portuguese (Brazil)", "pt-PT": "Portuguese (Portugal)",
    "ro": "Romanian", "ru": "Russian", "sv": "Swedish", "tr": "Turkish", "uk": "Ukrainian", "vi": "Vietnamese",
    "zh-Hans": "Chinese (Simplified)", "zh-Hant": "Chinese (Traditional)",
}

# ---- reading C ------------------------------------------------------------------------------

C_LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def c_unescape(body):
    """A C literal's text as UTF-8 bytes, decoded (the sources write UTF-8 as \\x escapes)."""
    out = bytearray()
    i = 0
    while i < len(body):
        c = body[i]
        if c != "\\":
            out += c.encode("utf-8")
            i += 1
            continue
        e = body[i + 1]
        i += 2
        if e == "x":
            m = re.match(r"[0-9a-fA-F]{1,2}", body[i:])
            out.append(int(m.group(0), 16))
            i += len(m.group(0))
        elif e in "01234567":
            m = re.match(r"[0-7]{0,2}", body[i:])
            out.append(int(e + m.group(0), 8))
            i += len(m.group(0))
        else:
            out += {"n": b"\n", "t": b"\t", "r": b"\r", "\\": b"\\", '"': b'"', "'": b"'", "?": b"?",
                    "a": b"\a"}.get(e, e.encode())
    return out.decode("utf-8")


def strip_comments(text):
    """The code with comments and strings' insides kept apart: (code with comments blanked, comments by line)."""
    out, comments = [], {}
    i, line = 0, 1
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.index("*/", i + 2) + 2
            body = text[i:j]
            comments.setdefault(line, []).append(body)
            out.append("".join(ch if ch == "\n" else " " for ch in body))
            line += body.count("\n")
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            comments.setdefault(line, []).append(text[i:j])
            out.append(" " * (j - i))
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            line += text[i:j + 1].count("\n")
            i = j + 1
        else:
            out.append(c)
            line += c == "\n"
            i += 1
    return "".join(out), comments


def call_args(code, start):
    """The arguments of a call whose '(' is at start: [(text, offset)], and the end."""
    depth, args, at = 0, [], start + 1
    i = start
    while i < len(code):
        c = code[i]
        if c == '"':
            j = i + 1
            while code[j] != '"':
                j += 2 if code[j] == "\\" else 1
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                args.append((code[at:i], at))
                return args, i
        elif c == "," and depth == 1:
            args.append((code[at:i], at))
            at = i + 1
        i += 1
    raise ValueError("unclosed call")


def literal_chains(text):
    """The literals of an expression, each run of adjacent ones joined."""
    chains, last_end, current = [], None, None
    for m in C_LITERAL.finditer(text):
        if current is not None and text[last_end:m.start()].strip() == "":
            current += c_unescape(m.group(1))
        else:
            if current is not None:
                chains.append(current)
            current = c_unescape(m.group(1))
        last_end = m.end()
    if current is not None:
        chains.append(current)
    return chains


def hint_for(line_text, msgid):
    """What a translator should know from where the text is (the line it is on)."""
    notes = []
    s = line_text
    if "tab_names" in s:
        notes.append("One of the tabs along the top of the screen: one short word.")
    elif "list_names" in s:
        notes.append("A filter on the Library tab, drawn as a small pill: keep it short.")
    elif "page_titles" in s:
        notes.append("A page's title, at the top of the Settings or game menu drawer.")
    elif "Hint" in s:
        notes.append("A button hint along the bottom of the screen, beside a button's picture: keep it short "
                     "(one or two words).")
    elif re.search(r"\*label\s*=", s) or "label = " in s:
        notes.append("A menu row's name (about 30 characters fit).")
    elif re.search(r"\*value\s*=", s) or "pc5_copy(value" in s or "(value, value_size" in s:
        notes.append("A menu row's value, shown at its right: keep it short (about 15 characters).")
    elif "keyboard_open" in s:
        notes.append("The title of the console's on-screen keyboard.")
    elif "confirm_dialog" in s or "confirm3_dialog" in s:
        notes.append("In a question window: the title, its text, or a button's label (keep buttons short).")
    if msgid.startswith(" ") or msgid.endswith(" "):
        notes.append("Keep the space at the start or end: the text is joined to another.")
    if "\xc2\xb7".encode("latin-1").decode("utf-8") in msgid:
        notes.append("· separates parts of a line; keep it.")
    return notes


def extract_c(path):
    text = path.read_text(encoding="utf-8")
    code, comments = strip_comments(text)
    lines = text.split("\n")
    rel = path.relative_to(ROOT).as_posix()
    found = []

    def translators(line):
        for at in range(line, max(0, line - 4), -1):
            for body in comments.get(at, []):
                if "TRANSLATORS:" in body:
                    note = body.split("TRANSLATORS:", 1)[1]
                    note = re.sub(r"\*/\s*$", "", note)
                    note = " ".join(part.strip(" *") for part in note.split("\n")).strip()
                    return [note]
        return []

    for m in re.finditer(r"(?<![A-Za-z0-9_])(T|N_|NW_|pc5_tf)\s*\(", code):
        start = m.end() - 1
        try:
            args, _ = call_args(code, start)
        except (ValueError, IndexError):
            continue
        if m.group(1) == "pc5_tf":
            if len(args) < 3:
                continue
            expr, offset = args[2]
        else:
            expr, offset = args[0]
            if not expr.strip().startswith('"'):
                continue   # T(variable): translated at use, marked where it is made
        line = code.count("\n", 0, offset) + 1
        # A macro joined to the text ("..." PC5_VERSION) would make another string than the one
        # the template has: not allowed (pass it as a %s).
        bare = C_LITERAL.sub('""', expr)
        if re.search(r'""\s*[A-Za-z_]\w*\s*""|""\s*[A-Z_][A-Z0-9_]+\b(?!\s*\()|\b[A-Z_][A-Z0-9_]+\s*""', bare):
            raise SystemExit(f"update-po: {rel}:{line}: a macro joined to marked text; pass it as %s instead")
        for msgid in literal_chains(expr):
            if not msgid:
                continue
            notes = translators(line) + hint_for(lines[line - 1], msgid)
            found.append(("web" if m.group(1) == "NW_" else None, msgid, f"{rel}:{line}", notes))
    return found


# ---- reading the web page ---------------------------------------------------------------------

def extract_web(path):
    text = path.read_text(encoding="utf-8")
    rel = path.relative_to(ROOT).as_posix()
    found = []
    # Elements with data-i18n: their inner HTML (only <b> and <code> are allowed in it), whitespace made one.
    for m in re.finditer(r"<(\w+)([^>]*\sdata-i18n(?=[\s>=])[^>]*)>(.*?)</\1>", text, re.S):
        inner = " ".join(m.group(3).split())
        if re.search(r"<(?!/?(b|code)>)", inner):
            raise SystemExit(f"update-po: {rel}: a data-i18n element holds markup other than <b> and <code>: {inner[:60]}")
        line = text.count("\n", 0, m.start()) + 1
        notes = ["Web page text."]
        if "<b>" in inner or "<code>" in inner:
            notes.append("Keep <b>...</b> and <code>...</code> around the same words.")
        found.append(("web", html.unescape(inner) if "<" not in inner else inner, f"{rel}:{line}", notes))
    for m in re.finditer(r'data-i18n-(placeholder|title)="([^"]*)"', text):
        line = text.count("\n", 0, m.start()) + 1
        what = "A text box's hint, shown in it while it is empty." if m.group(1) == "placeholder" else "A tooltip."
        found.append(("web", html.unescape(m.group(2)), f"{rel}:{line}", ["Web page text.", what]))
    script = text[text.index("<script>"):] if "<script>" in text else ""
    base = text.count("\n", 0, text.index("<script>")) if script else 0
    for m in re.finditer(r'(?<![\w.$])(?:t|tf)\(\s*"((?:[^"\\]|\\.)*)"((?:\s*\+\s*"(?:[^"\\]|\\.)*")*)', script):
        line = base + script.count("\n", 0, m.start()) + 1
        parts = [m.group(1)] + re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))
        msgid = "".join(p.encode().decode("unicode_escape").encode("latin-1").decode("utf-8") for p in parts)
        notes = ["Web page text."]
        if "%" in msgid:
            notes.append("%s and %d are filled in by the page; %1$s, %2$s put them in another order.")
        found.append(("web", msgid, f"{rel}:{line}", notes))
    return found


# ---- the template ------------------------------------------------------------------------------

def collect():
    entries = {}   # (ctxt, id) -> [refs, notes]
    order = []
    sources = sorted((ROOT / "title").glob("pc5*.c")) + sorted((ROOT / "title").glob("pc5_ui_enhancements*.h"))
    sources += [ROOT / "title" / "pc5_web_localize.h"]
    found = []
    for path in sources:
        found += extract_c(path)
    web = ROOT / "title" / "pc5_web_page.html"
    if web.exists():
        found += extract_web(web)
    for ctxt, msgid, ref, notes in found:
        key = (ctxt, msgid)
        if key not in entries:
            entries[key] = [[], []]
            order.append(key)
        entries[key][0].append(ref)
        for note in notes:
            if note not in entries[key][1]:
                entries[key][1].append(note)
    return [(k, entries[k][0], entries[k][1]) for k in order]


def po_quote(text, width=77):
    """A text as .po lines: one "..." line, or "" then lines broken after spaces and \\n."""
    def esc(s):
        return s.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n")
    whole = esc(text)
    if len(whole) + 9 <= width and "\n" not in text[:-1]:
        return [f'"{whole}"']
    pieces = []
    for part in re.split(r"(?<=\n)", text):
        if not part:
            continue
        current = ""
        for word in re.split(r"(?<= )", part):
            if current and len(esc(current + word)) > width - 2:
                pieces.append(current)
                current = word
            else:
                current += word
        if current:
            pieces.append(current)
    return ['""'] + [f'"{esc(p)}"' for p in pieces]


def is_c_format(msgid):
    return bool(re.search(r"%(\d+\$)?[-+ #0']*(\d+|\*)?(\.(\d+|\*))?(hh|h|ll|l|z|j|t|L)?[diouxXcsfFeEgGaAp]", msgid))


def write_entry(out, entry, obsolete=False):
    p = "#~ " if obsolete else ""
    for note in entry.get("translator", []):
        out.append(f"# {note}" if note else "#")
    if not obsolete:
        for note in entry.get("notes", []):
            out.append(f"#. {note}")
        refs = entry.get("refs", [])
        line = "#:"
        for ref in refs:
            if len(line) + len(ref) + 1 > 79 and line != "#:":
                out.append(line)
                line = "#:"
            line += " " + ref
        if line != "#:":
            out.append(line)
    flags = entry.get("flags", [])
    if entry.get("previous") is not None and not obsolete:
        lines = po_quote(entry["previous"])
        if "fuzzy" in flags:
            out.append(f"#| msgid {lines[0]}")
            out += ["#| " + l for l in lines[1:]]
    if flags:
        out.append(f"{p}#, " + ", ".join(flags) if not obsolete else "#~ #, " + ", ".join(flags))
    if entry.get("ctxt") is not None:
        lines = po_quote(entry["ctxt"])
        out.append(f"{p}msgctxt {lines[0]}")
        out += [p + l for l in lines[1:]]
    lines = po_quote(entry["id"])
    out.append(f"{p}msgid {lines[0]}")
    out += [p + l for l in lines[1:]]
    lines = po_quote(entry.get("str", ""))
    out.append(f"{p}msgstr {lines[0]}")
    out += [p + l for l in lines[1:]]
    out.append("")


POT_HEADER = """# PC5's text in English, for translators: lang/<language>.po holds each language's.
# Made by tools/update-po.py from title/pc5*.c and title/pc5_web_page.html: don't edit it.
# A translation that is empty, or marked fuzzy, shows in English.
# This file is distributed under the same licence as PC5 (LGPL 2.1 or later).
#
#, fuzzy
msgid ""
msgstr ""
"Project-Id-Version: PC5\\n"
"Report-Msgid-Bugs-To: https://github.com/bornaradusin/PC5/issues\\n"
"Language: \\n"
"MIME-Version: 1.0\\n"
"Content-Type: text/plain; charset=UTF-8\\n"
"Content-Transfer-Encoding: 8bit\\n"
"""


def make_pot():
    out = POT_HEADER.rstrip("\n").split("\n") + [""]
    for (ctxt, msgid), refs, notes in collect():
        flags = ["c-format"] if is_c_format(msgid) else []
        write_entry(out, {"ctxt": ctxt, "id": msgid, "refs": refs, "notes": notes, "flags": flags})
    return "\n".join(out)


# ---- reading .po ------------------------------------------------------------------------------------

def po_unquote(s):
    s = s.strip()
    assert s.startswith('"') and s.endswith('"'), s
    body = s[1:-1]
    out, i = [], 0
    while i < len(body):
        c = body[i]
        if c == "\\" and i + 1 < len(body):
            e = body[i + 1]
            out.append({"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}.get(e, e))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def read_po(path):
    """A .po's entries: dicts with ctxt, id, str, flags, translator (comments), obsolete. The header first."""
    entries, cur, part = [], None, None

    def finish():
        nonlocal cur, part
        if cur is not None and "id" in cur:
            entries.append(cur)
        cur, part = None, None

    for raw in path.read_text(encoding="utf-8-sig").split("\n"):
        line = raw.rstrip("\r")
        obsolete = line.startswith("#~")
        if obsolete:
            line = line[2:].lstrip()
            if line.startswith("#"):
                continue
        if not line.strip():
            finish()
            continue
        if line.startswith("#"):
            if cur is not None and "id" in cur:
                finish()
            if cur is None:
                cur = {"flags": [], "translator": [], "obsolete": False}
            if line.startswith("#,"):
                cur["flags"] += [f.strip() for f in line[2:].split(",") if f.strip()]
            elif line.startswith("# ") or line == "#":
                cur["translator"].append(line[2:])
            continue
        if cur is None:
            cur = {"flags": [], "translator": [], "obsolete": False}
        if obsolete:
            cur["obsolete"] = True
        m = re.match(r"(msgctxt|msgid_plural|msgid|msgstr(?:\[\d+\])?)\s+(.*)", line)
        if m:
            if m.group(1) in ("msgctxt",) and "id" in cur:
                finish()
                cur = {"flags": [], "translator": [], "obsolete": obsolete}
            if m.group(1) == "msgid" and "id" in cur:
                finish()
                cur = {"flags": [], "translator": [], "obsolete": obsolete}
            key = {"msgctxt": "ctxt", "msgid": "id", "msgid_plural": "plural", "msgstr": "str", "msgstr[0]": "str"}.get(m.group(1))
            part = key
            if key:
                cur[key] = po_unquote(m.group(2))
            continue
        if line.startswith('"') and part:
            cur[part] += po_unquote(line)
            continue
        raise SystemExit(f"update-po: {path}: can't read the line: {raw}")
    finish()
    return entries


# ---- formats, as pc5_i18n.c checks them -----------------------------------------------------------

CONV = re.compile(r"%(?:(\d+)\$)?[-+ #0']*(\*|\d+)?(?:\.(\*|\d+))?(hh|h|ll|l|z|j|t|L)?([diouxXcsfFeEgGaApn%])")


def convs(fmt):
    out = []
    for m in CONV.finditer(fmt):
        if m.group(5) == "%":
            continue
        length = m.group(4) or ""
        letter = m.group(5)
        if letter == "n":
            return None
        kind = ("i" if letter in "diouxXc" else "f" if letter in "fFeEgGaA" else letter) + \
            ("" if length in ("", "h", "hh") or letter not in "diouxXc" else length)
        if letter in "fFeEgGaA" and length == "L":
            kind += "L"
        stars = (m.group(2) == "*") + (m.group(3) == "*")
        out.append((int(m.group(1)) if m.group(1) else 0, kind, stars))
    return out


def format_ok(english, translated):
    want, got = convs(english), convs(translated)
    if want is None or got is None:
        return False
    if not got or not got[0][0]:
        if any(g[0] for g in got):
            return False
        return [(k, s) for _, k, s in got] == [(k, s) for _, k, s in want]
    if any(not g[0] for g in got) or any(w[2] for w in want) or any(g[2] for g in got):
        return False
    used = set()
    for pos, kind, _ in got:
        if pos > len(want) or want[pos - 1][1] != kind:
            return False
        used.add(pos)
    return used == set(range(1, len(want) + 1))


# ---- merging ----------------------------------------------------------------------------------

def header_for(code):
    return {"ctxt": None, "id": "", "flags": [], "translator": [
        f"PC5's text in {NAMES.get(code, code)}.",
        "Edit it with Poedit (or any text editor): fill in each msgstr, leave it empty to keep English.",
        "How to send a fix: https://github.com/bornaradusin/PC5/wiki/Translations",
        "This file is distributed under the same licence as PC5 (LGPL 2.1 or later)."],
        "str": ("Project-Id-Version: PC5\n"
                "Report-Msgid-Bugs-To: https://github.com/bornaradusin/PC5/issues\n"
                f"Language: {LANGUAGES[code]}\n"
                "MIME-Version: 1.0\n"
                "Content-Type: text/plain; charset=UTF-8\n"
                "Content-Transfer-Encoding: 8bit\n")}


def merge(code, path, template):
    old = read_po(path) if path.exists() else []
    header = next((e for e in old if e["id"] == "" and e.get("ctxt") is None and not e["obsolete"]), None) or header_for(code)
    if f"Language: {LANGUAGES[code]}\n" not in header["str"]:
        lines = [l for l in header["str"].split("\n") if not l.startswith("Language:")]
        lines.insert(min(2, len(lines)), f"Language: {LANGUAGES[code]}")
        header["str"] = "\n".join(lines)
    header["flags"] = [f for f in header["flags"] if f != "fuzzy"]
    known = {(e.get("ctxt"), e["id"]): e for e in old if e["id"] != "" or e.get("ctxt") is not None}
    used = set()
    out = []
    write_entry(out, header)
    unused_with_text = [e for k, e in known.items() if e.get("str")]
    for t in template:
        key = (t["ctxt"], t["id"])
        entry = dict(t)
        was = known.get(key)
        flags = list(t["flags"])
        if was:
            used.add(key)
            entry["str"] = was.get("str", "")
            entry["translator"] = was.get("translator", [])
            if "fuzzy" in was.get("flags", []) and entry["str"]:
                flags.append("fuzzy")
        else:
            # An English text changed a little: its old translation, to be checked (fuzzy).
            near = difflib.get_close_matches(t["id"], [e["id"] for e in unused_with_text
                                                      if e.get("ctxt") == t["ctxt"]], n=1, cutoff=0.8)
            if near:
                was = next(e for e in unused_with_text if e["id"] == near[0] and e.get("ctxt") == t["ctxt"])
                entry["str"] = was["str"]
                entry["previous"] = was["id"]
                flags.append("fuzzy")
            else:
                entry["str"] = ""
        entry["flags"] = flags
        write_entry(out, entry)
    for key, e in known.items():
        if key not in used and e.get("str") and "plural" not in e:
            write_entry(out, {"ctxt": e.get("ctxt"), "id": e["id"], "str": e["str"], "flags": [], "translator": []},
                        obsolete=True)
    text = "\n".join(out)
    return text


def template_entries():
    entries = read_po(POT)
    return [{"ctxt": e.get("ctxt"), "id": e["id"], "flags": e["flags"],
             "notes": [], "refs": []} for e in entries if e["id"] != "" or e.get("ctxt") is not None]


def template_full():
    """The template's entries with their notes and references, from the sources."""
    out = []
    for (ctxt, msgid), refs, notes in collect():
        out.append({"ctxt": ctxt, "id": msgid, "refs": refs, "notes": notes,
                    "flags": ["c-format"] if is_c_format(msgid) else []})
    return out


def check_po(path, template_keys):
    problems = []
    for e in read_po(path):
        if e["obsolete"] or (e["id"] == "" and e.get("ctxt") is None):
            continue
        if (e.get("ctxt"), e["id"]) not in template_keys:
            problems.append(f"{path.name}: not in the template: {e['id'][:60]!r}")
        s = e.get("str", "")
        if s and "%" in e["id"] and not format_ok(e["id"], s):
            problems.append(f"{path.name}: its % conversions aren't the English's (PC5 shows English): {e['id'][:60]!r}")
    return problems


def main(argv):
    LANG.mkdir(exist_ok=True)
    if argv[1:2] == ["--new"]:
        code = argv[2]
        if code not in LANGUAGES:
            raise SystemExit(f"update-po: {code} isn't one of PC5's languages ({', '.join(sorted(LANGUAGES))})")
        path = LANG / f"{code}.po"
        if path.exists():
            raise SystemExit(f"update-po: {path} is there already")
        path.write_text(merge(code, path, template_full()), encoding="utf-8")
        print(f"update-po: made {path}")
        return 0
    pot = make_pot()
    if argv[1:2] == ["--check"]:
        problems = []
        if not POT.exists() or POT.read_text(encoding="utf-8") != pot:
            problems.append("lang/pc5.pot isn't what the sources give: run tools/update-po.py")
        keys = {(t["ctxt"], t["id"]) for t in template_full()}
        for code in sorted(LANGUAGES):
            path = LANG / f"{code}.po"
            if not path.exists():
                problems.append(f"lang/{code}.po is missing: tools/update-po.py --new {code}")
                continue
            if path.read_text(encoding="utf-8") != merge(code, path, template_full()):
                problems.append(f"lang/{code}.po isn't merged with the template: run tools/update-po.py")
            problems += check_po(path, keys)
        for p in problems:
            print("update-po:", p, file=sys.stderr)
        if problems:
            return 1
        print(f"update-po: lang/pc5.pot has {len(keys)} texts; {len(LANGUAGES)} languages checked")
        return 0
    POT.write_text(pot, encoding="utf-8")
    template = template_full()
    for code in sorted(LANGUAGES):
        path = LANG / f"{code}.po"
        path.write_text(merge(code, path, template), encoding="utf-8")
    print(f"update-po: {POT.relative_to(ROOT)}: {len(template)} texts; merged into {len(LANGUAGES)} languages")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
