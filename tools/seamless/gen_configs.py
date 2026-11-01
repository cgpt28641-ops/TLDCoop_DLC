#!/usr/bin/env python3
"""Turns Seamless Interiors' building list (third_party/seamless-interiors/SeamlessInteriorsMod.Configs.cs, a C# list
initializer of `new InteriorConfig { ... }` by karsontr13, ported with the author's permission) into
src/seamless_configs.inc: plain C++ tables the mod compiles in (src/seamless_interiors.cpp), so the mod needs no C#
and no data files.

Run from the repo root after changing the C# file:  python3 tools/seamless/gen_configs.py

What is carried: every field the parked rooms use (see InteriorConfig in SI's Instances.cs). Not carried: the terrain
holes and ExternalObjectsToHide (only rooms standing on their shell use them; every SI room is parked 80 m up), and
the painted window backdrops in ObjectsToDisable: SI switches them off because it shows the real view through the
windows; without its window views the backdrops stay, or the windows would show the sky 80 m up. They are listed in
a comment per building so the choice can be checked.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "third_party", "seamless-interiors", "SeamlessInteriorsMod.Configs.cs")
OUT = os.path.join(ROOT, "src", "seamless_configs.inc")


# --- a small C# expression reader ------------------------------------------------------------------------------------

def strip_comments(text):
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
        elif text.startswith("/*", i):
            i = text.index("*/", i) + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


TOKEN = re.compile(r'\s*(?:(?P<str>"(?:\\.|[^"\\])*")|(?P<num>\d+(?:\.\d+)?(?:[eE][-+]?\d+)?[fFdDmM]?)|'
                   r'(?P<id>[A-Za-z_][A-Za-z0-9_]*)|(?P<p>[{}()\[\]<>,=;.\-?]))')


def tokenize(text):
    toks, pos = [], 0
    while True:
        m = TOKEN.match(text, pos)
        if not m:
            if text[pos:].strip():
                raise SystemExit("cannot read C# at: %r" % text[pos:pos + 60])
            return toks
        pos = m.end()
        kind = m.lastgroup
        toks.append((kind, m.group(kind)))


class Reader:
    def __init__(self, toks):
        self.t, self.i = toks, 0

    def peek(self, k=0):
        return self.t[self.i + k] if self.i + k < len(self.t) else (None, None)

    def take(self, value=None):
        tok = self.peek()
        if value is not None and tok[1] != value:
            raise SystemExit("expected %r, got %r (token %d)" % (value, tok, self.i))
        self.i += 1
        return tok

    def type_name(self):
        name = self.take()[1]
        while self.peek()[1] == ".":
            self.take(".")
            name += "." + self.take()[1]
        if self.peek()[1] == "<":
            depth = 0
            while True:
                v = self.take()[1]
                depth += v == "<"
                depth -= v == ">"
                if depth == 0:
                    break
        return name

    def expr(self):
        kind, v = self.peek()
        if v == "new":
            self.take()
            obj = {"__type__": self.type_name()}
            args = []
            if self.peek()[1] == "(":
                self.take("(")
                while self.peek()[1] != ")":
                    args.append(self.expr())
                    if self.peek()[1] == ",":
                        self.take(",")
                self.take(")")
            obj["__args__"] = args
            if self.peek()[1] == "{":
                self.take("{")
                items = []
                while self.peek()[1] != "}":
                    if self.peek()[0] == "id" and self.peek(1)[1] == "=":
                        key = self.take()[1]
                        self.take("=")
                        obj[key] = self.expr()
                    else:
                        items.append(self.expr())
                    if self.peek()[1] == ",":
                        self.take(",")
                self.take("}")
                obj["__items__"] = items
            return obj
        if v == "-":
            self.take()
            return -self.expr()
        if kind == "num":
            self.take()
            return float(re.sub(r"[fFdDmM]$", "", v))
        if kind == "str":
            self.take()
            return bytes(v[1:-1], "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")
        if v in ("true", "false"):
            self.take()
            return v == "true"
        if v == "null":
            self.take()
            return None
        if kind == "id":  # Vector3.zero, Vector3.one and the like
            name = self.type_name()
            if name in ("Vector3.zero",):
                return (0.0, 0.0, 0.0)
            if name in ("Vector3.one",):
                return (1.0, 1.0, 1.0)
            raise SystemExit("unknown value %r" % name)
        raise SystemExit("unexpected %r" % (self.peek(),))


def vec(v, default=(0.0, 0.0, 0.0)):
    if v is None:
        return default
    if isinstance(v, tuple):
        return v
    if isinstance(v, dict) and v["__type__"] == "Vector3":
        a = v["__args__"]
        return (a[0], a[1], a[2])
    raise SystemExit("not a Vector3: %r" % (v,))


def f(x):
    s = repr(float(x))
    return s + ("f" if ("." in s or "e" in s) else ".0f")


def v3(t):
    return "{%s, %s, %s}" % (f(t[0]), f(t[1]), f(t[2]))


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


# Painted window backdrops in ObjectsToDisable (SI shows the real view instead): kept on here.
def is_window_backdrop(name):
    low = name.lower()
    return "window" in low or "glow" in low or "matte" in low or name.endswith("BG")


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else SRC
    text = strip_comments(open(src, encoding="utf-8-sig").read())
    start = text.index("SupportedInteriors")
    start = text.index("new", start)
    r = Reader(tokenize(text[start:]))
    top = r.expr()
    configs = top["__items__"]

    names, doors, links, rows = [], [], [], []

    def name_list(lst):
        first = len(names)
        for s in lst:
            names.append(s)
        return first, len(lst)

    for c in configs:
        if c["__type__"] != "InteriorConfig":
            raise SystemExit("unexpected entry %r" % c["__type__"])
        scene = c["InteriorSceneBaseName"]
        cid = c.get("InstanceId") or scene
        destroy = [s for s in (c.get("ObjectsToDestroy") or {}).get("__items__", [])]
        disable_all = [s for s in (c.get("ObjectsToDisable") or {}).get("__items__", [])]
        disable = [s for s in disable_all if not is_window_backdrop(s)]
        kept = [s for s in disable_all if is_window_backdrop(s)]
        d_first, d_count = name_list(destroy)
        x_first, x_count = name_list(disable)
        door_first = len(doors)
        for d in (c.get("DoorSpawnPoints") or {}).get("__items__", []):
            doors.append((d.get("DoorName") or "", vec(d.get("DoorTransformPosition")), vec(d.get("EntryPosition")),
                          vec(d.get("ExitPosition"))))
        link_first = len(links)
        for l in (c.get("SubInteriorLinks") or {}).get("__items__", []):
            links.append((l["TargetInstanceId"], vec(l.get("ParentDoorPosition")), vec(l.get("ChildSpawnPosition")),
                          vec(l.get("ParentSpawnPosition"))))
        if "ParkOffset" in c:
            raise SystemExit("%s sets ParkOffset: the port parks every room 80 m up" % cid)
        rows.append(dict(
            id=cid, region=c["ExteriorSceneName"], scene=scene, shell=c.get("ExteriorShellPrefabName") or "",
            y=c.get("YOffset") or 0.0, scale=vec(c.get("ScaleAdjustment"), (1.0, 1.0, 1.0)),
            fallback=vec(c.get("FallbackPosition")), exact=bool(c.get("ForceExactPosition")),
            rotation=vec(c.get("RotationOffset")), entry=vec(c.get("EntrySpawnPosition")),
            exit=vec(c.get("ExitSpawnPosition")), doors=(door_first, len(doors) - door_first),
            links=(link_first, len(links) - link_first), destroy=(d_first, d_count), disable=(x_first, x_count),
            radius=c.get("BoundsRadius") or 30.0, kept=kept))

    out = []
    out.append("// Generated by tools/seamless/gen_configs.py from third_party/seamless-interiors/SeamlessInteriorsMod.Configs.cs")
    out.append("// (Seamless Interiors by karsontr13, https://github.com/karsontr13/TLD-Seamless-Interiors, ported with the")
    out.append("// author's permission; see third_party/seamless-interiors/NOTICE.txt). Do not edit.")
    out.append("// %d buildings. Painted window backdrops in ObjectsToDisable are left on (no window views yet)." % len(rows))
    out.append("")
    out.append("const char* const kNames[] = {")
    for s in names:
        out.append("    %s," % cstr(s))
    out.append("};")
    out.append("")
    out.append("const DoorSpawn kDoors[] = {")
    for d in doors:
        out.append("    {%s, %s, %s, %s}," % (cstr(d[0]), v3(d[1]), v3(d[2]), v3(d[3])))
    out.append("};")
    out.append("")
    out.append("const SubLink kLinks[] = {")
    for l in links:
        out.append("    {%s, %s, %s, %s}," % (cstr(l[0]), v3(l[1]), v3(l[2]), v3(l[3])))
    out.append("};")
    out.append("")
    out.append("const Config kConfigs[] = {")
    for i, c in enumerate(rows):
        if c["kept"]:
            out.append("    // %s: window backdrops kept on: %s" % (c["id"], ", ".join(c["kept"])))
        out.append("    {%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %d, %d, %d, %d, %d, %d, %d, %d, %s}," % (
            cstr(c["id"]), cstr(c["region"]), cstr(c["scene"]), cstr(c["shell"]), f(c["y"]), v3(c["scale"]),
            v3(c["fallback"]), "true" if c["exact"] else "false", v3(c["rotation"]), v3(c["entry"]), v3(c["exit"]),
            c["doors"][0], c["doors"][1], c["links"][0], c["links"][1], c["destroy"][0], c["destroy"][1],
            c["disable"][0], c["disable"][1], f(c["radius"])))
    out.append("};")
    out.append("")
    open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    print("wrote %s: %d buildings, %d doors, %d links, %d names" % (OUT, len(rows), len(doors), len(links), len(names)))


if __name__ == "__main__":
    main()
