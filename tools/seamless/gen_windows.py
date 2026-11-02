#!/usr/bin/env python3
"""Turns Seamless Interiors' window data (by karsontr13, ported with the author's permission) into
src/seamless_windows_data.inc, the tables src/seamless_windows.inc compiles in:

  * third_party/seamless-interiors/SeamlessInteriorsMod.ExteriorFireEffects.cs: s_ShellWindows, the shells' window panes
    measured offline (shell-local centre, outward, size): the building half of every window pairing;
  * third_party/seamless-interiors/SeamlessInteriorsMod.BuildingWindowOverrides.cs: s_BuildingWindowOverrides, the hand
    data per shell. Carried: RoomTurnDegrees (TurnParkedRoomToShell), PairInPlaceWithinCm, PairedRoomWindows,
    ViewOutOffsets, ViewFromOutsideStartsCm, PicturePlaneCm(ByWindow), WindowsLeftAlone. A row that changes how the
    ROOM's windows are found (bands, joins, many-pane sheets, glass slots, hand openings, windows without backdrop...)
    is flagged `scan_altered`: the port finds room windows by SI's ordinary rule only, so the hand data that numbers
    room windows (PairedRoomWindows, ViewOutOffsets) is not used for it (the numbers would point at other windows).

Run from the repo root after changing the C# files:  python3 tools/seamless/gen_windows.py
"""
import importlib.util
import math
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TP = os.path.join(ROOT, "third_party", "seamless-interiors")
OUT = os.path.join(ROOT, "src", "seamless_windows_data.inc")

_spec = importlib.util.spec_from_file_location("gen_configs", os.path.join(os.path.dirname(__file__), "gen_configs.py"))
gc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gc)

NAN = float("nan")

TOKEN = re.compile(r'\s*(?:(?P<str>"(?:\\.|[^"\\])*")|(?P<num>\d+(?:\.\d+)?(?:[eE][-+]?\d+)?[fFdDmM]?)|'
                   r'(?P<id>[A-Za-z_][A-Za-z0-9_]*)|(?P<p>[{}()\[\]<>,=;.\-?:+*/]))')


def tokenize(text):
    toks, pos = [], 0
    while True:
        m = TOKEN.match(text, pos)
        if not m:
            if text[pos:].strip():
                raise SystemExit("cannot read C# at: %r" % text[pos:pos + 60])
            return toks
        pos = m.end()
        toks.append((m.lastgroup, m.group(m.lastgroup)))


class R:
    """A C# initializer reader: new T(args) { K = v }, new[] {..}, new T[] {..}, { a, b }, numbers, strings, calls."""

    def __init__(self, toks):
        self.t, self.i = toks, 0

    def peek(self, k=0):
        return self.t[self.i + k] if self.i + k < len(self.t) else (None, None)

    def take(self, v=None):
        tok = self.peek()
        if v is not None and tok[1] != v:
            raise SystemExit("expected %r, got %r at %d" % (v, tok, self.i))
        self.i += 1
        return tok

    def type_name(self):
        if self.peek()[1] == "[":  # new[]
            self.take("[")
            self.take("]")
            return "[]"
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
        while self.peek()[1] == "[" and self.peek(1)[1] == "]":
            self.take("[")
            self.take("]")
            name += "[]"
        return name

    def block(self):  # { a, b, K = v } -> (items, fields)
        self.take("{")
        items, fields = [], {}
        while self.peek()[1] != "}":
            if self.peek()[0] == "id" and self.peek(1)[1] == "=":
                key = self.take()[1]
                self.take("=")
                fields[key] = self.expr()
            else:
                items.append(self.expr())
            if self.peek()[1] == ",":
                self.take(",")
        self.take("}")
        return items, fields

    def args(self):
        out = []
        self.take("(")
        while self.peek()[1] != ")":
            if self.peek()[0] == "id" and self.peek(1)[1] == ":":  # named argument
                self.take()
                self.take(":")
            out.append(self.expr())
            if self.peek()[1] == ",":
                self.take(",")
        self.take(")")
        return out

    def expr(self):
        kind, v = self.peek()
        if v == "new":
            self.take()
            t = self.type_name()
            obj = {"__type__": t, "__args__": [], "__items__": []}
            if self.peek()[1] == "(":
                obj["__args__"] = self.args()
            if self.peek()[1] == "{":
                items, fields = self.block()
                obj["__items__"] = items
                obj.update(fields)
            if t.endswith("[]"):
                return obj["__items__"]
            return obj
        if v == "{":
            items, _ = self.block()
            return items
        if v == "-":
            self.take()
            return -self.expr()
        if kind == "num":
            self.take()
            return float(re.sub(r"[fFdDmM]$", "", v))
        if kind == "str":
            self.take()
            return v[1:-1]
        if v in ("true", "false"):
            self.take()
            return v == "true"
        if v == "null":
            self.take()
            return None
        if kind == "id":
            name = self.type_name()
            if name in ("float.NaN", "Single.NaN"):
                return NAN
            if self.peek()[1] == "(":
                return {"__call__": name, "__args__": self.args()}
            return {"__id__": name}
        raise SystemExit("unexpected %r" % (self.peek(),))


def read_dict(path, marker):
    text = gc.strip_comments(open(path, encoding="utf-8-sig").read())
    start = text.index(marker)
    start = text.index("new", start)
    r = R(tokenize(text[start:text.index("};", start) + 1]))
    return r.expr()["__items__"]  # [[key, value], ...]


def f(x):
    if isinstance(x, float) and math.isnan(x):
        return "kNaN"
    return gc.f(x)


SCAN_FIELDS = ("RoomBands", "RoomWindowsFromBuilding", "RoomManyPaneObjects", "RoomWindowsWithoutBackdrop",
               "RoomGlassViewOutOnly", "RoomGlassSlotObjects", "RoomWindowJoins", "RoomOpeningsByHand",
               "OpeningsWithNoGlass", "RoomManyPaneJoinWithinCm", "RoomViewOutsideGlassCm")


def main():
    spots = read_dict(os.path.join(TP, "SeamlessInteriorsMod.ExteriorFireEffects.cs"), "s_ShellWindows")
    rows = read_dict(os.path.join(TP, "SeamlessInteriorsMod.BuildingWindowOverrides.cs"), "s_BuildingWindowOverrides")

    out = []
    out.append("// Generated by tools/seamless/gen_windows.py from Seamless Interiors' window data (karsontr13, with his")
    out.append("// permission): SeamlessInteriorsMod.ExteriorFireEffects.cs s_ShellWindows and")
    out.append("// SeamlessInteriorsMod.BuildingWindowOverrides.cs. Do not edit by hand.")
    out.append("")
    spot_rows, shells = [], []
    for key, value in spots:
        first = len(spot_rows)
        for s in value:
            a = s["__args__"]
            spot_rows.append("    {%s}," % ", ".join(f(x) for x in a))
        shells.append("    {%s, %d, %d}," % (gc.cstr(key), first, len(value)))
    out.append("constexpr WinSpot kWinSpots[] = {")
    out += spot_rows
    out.append("};")
    out.append("constexpr ShellSpots kShellSpots[] = {")
    out += shells
    out.append("};")

    ints, offs, orows = [], [], []

    def int_list(lst):
        first = len(ints)
        for v in lst or []:
            ints.append(int(v))
        return first, len(lst or [])

    for key, value in rows:
        if isinstance(value, dict) and "__call__" in value:
            if value["__call__"] == "MiltonHouseFRow":
                value = {"RoomTurnDegrees": 0.0, "WindowsLeftAlone": [1.0, 2.0]}
            else:
                raise SystemExit("unknown row helper %r" % value["__call__"])
        turn = value.get("RoomTurnDegrees", NAN)
        in_place = value.get("PairInPlaceWithinCm", NAN)
        view_from_out = value.get("ViewFromOutsideStartsCm", NAN)
        plane = value.get("PicturePlaneCm", NAN)
        scan = any(k in value and value[k] not in (None, False) for k in SCAN_FIELDS)
        pf, pc = int_list(value.get("PairedRoomWindows"))
        lf, lc = int_list(value.get("WindowsLeftAlone"))
        of = len(offs)
        for o in value.get("ViewOutOffsets") or []:
            offs.append("    {%d, %s, %s, %s}," % (int(o.get("RoomWindow", 0)), f(o.get("RightCm", 0.0)),
                                                    f(o.get("UpCm", 0.0)), f(o.get("OutCm", 0.0))))
        oc = len(offs) - of
        planes = value.get("PicturePlaneCmByWindow") or []
        plf = len(ints)
        # by-window picture planes are floats; kept in their own table below
        orows.append("    {%s, %s, %s, %s, %s, %s, %d, %d, %d, %d, %d, %d}," % (
            gc.cstr(key), f(turn), f(in_place), f(view_from_out), f(plane), "true" if scan else "false",
            pf, pc, lf, lc, of, oc))
        _ = (planes, plf)

    out.append("constexpr int kWinInts[] = {%s};" % (", ".join(str(i) for i in ints) if ints else "0"))
    out.append("constexpr WinViewOut kWinViewOuts[] = {")
    out += offs if offs else ["    {-1, 0.0f, 0.0f, 0.0f},"]
    out.append("};")
    out.append("constexpr WinRow kWinRows[] = {")
    out += orows
    out.append("};")
    data = "\n".join(out) + "\n"
    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(data)
    print("wrote %s: %d shells (%d panes), %d hand rows" % (OUT, len(shells), len(spot_rows), len(orows)))


if __name__ == "__main__":
    main()
