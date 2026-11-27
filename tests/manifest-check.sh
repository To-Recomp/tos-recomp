#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Quick check of packaging/recomplauncher*.json, no build and no network:
#  1. they hold the overlay's shared settings fragments (To-Recomp/overlay tools/sync-manifest.py --check);
#  2. they obey the rules the launcher enforces when it loads a manifest (launcher/src-tauri/src/settings/manifest.rs),
#     so that a mistake fails here and not on a player's screen.
#
#   tests/manifest-check.sh [--overlay DIR]      (default: ../overlay next to this repository)
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
overlay="$here/../overlay"
while [ $# -gt 0 ]; do
  case "$1" in
    --overlay) overlay="$2"; shift 2 ;;
    -h|--help) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
PYTHON="${PYTHON:-python3}"

"$overlay/tools/manifest-check.sh" --bindings "$overlay/packaging/bindings-gamecube.json" "$here/packaging/recomplauncher.json" "$here/packaging/recomplauncher.windows.json"

"$PYTHON" - "$overlay/packaging/settings-manifest.json" "$overlay/packaging/bindings-gamecube.json" "$here/packaging/recomplauncher.json" "$here/packaging/recomplauncher.windows.json" <<'PY'
import json, re, sys

TABS = {"Display", "Graphics", "Audio", "Controls", "Saves", "Game"}
NAMED = {"Space", "Return", "Escape", "Tab", "Backspace", "Shift", "Ctrl", "Alt", "Up", "Down", "Left", "Right",
         "Mouse Left", "Mouse Right", "Mouse Middle", "Mouse 4", "Mouse 5", "Wheel Up", "Wheel Down", "Insert",
         "Delete", "Home", "End", "Page Up", "Page Down", "Caps Lock"}
PAD = {"A", "B", "X", "Y", "Cross", "Circle", "Square", "Triangle", "LB", "RB", "LT", "RT", "Start", "Back",
       "Left Stick", "Right Stick", "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "L3", "R3"}
CONTROLS = ("keyboard", "gamepad")
KEYS = ["a", "b", "x", "y", "start", "z", "l", "r", "dpad_up", "dpad_down", "dpad_left", "dpad_right",
        "stick_up", "stick_down", "stick_left", "stick_right", "cstick_up", "cstick_down", "cstick_left", "cstick_right"]
BUTTONS = KEYS[:12]
failures = []


def bad(where, msg):
    failures.append("%s: %s" % (where, msg))


def valid_key(name, pad):
    return (re.fullmatch(r"[A-Za-z0-9]", name) or re.fullmatch(r"F([1-9]|1[0-2])", name) or name in NAMED
            or (pad and name in PAD))


def unquote(v):
    v = str(v).strip()
    return v[1:-1] if len(v) >= 2 and v[0] == v[-1] == '"' else v


def check(path, m):
    configs = [c.get("id", "main") for c in m.get("config", [])]
    if len(configs) != len(set(configs)):
        bad(path, "a config id is declared twice")
    for c in m.get("config", []):
        if c.get("format") == "ini":
            bad(path, "config %s: ini is rejected by the launcher" % c.get("id"))
    seen = set()
    for s in m.get("settings", []):
        sid = s.get("id", "")
        where = "%s: setting %r" % (path, sid)
        if not sid or sid in seen:
            bad(where, "empty or duplicate id")
        seen.add(sid)
        if len([k for k in ("choices", "slider", "bindings", "text") if k in s]) != 1:
            bad(where, "needs exactly one of choices, slider, bindings, text")
            continue
        if "label" not in s:
            bad(where, "no label")
        if s.get("tab", "Game") not in TABS:
            bad(where, "unknown tab %r" % s.get("tab"))
        target = s.get("in", "main")
        if target != "args" and target not in configs:
            bad(where, "config %r is not declared" % target)
        if "choices" in s:
            ch = s["choices"]
            if not ch:
                bad(where, "choices is empty")
            if target != "args" and any(not c.get("set") for c in ch):
                bad(where, "every choice needs a set")
            if s.get("control") == "toggle" and len(ch) != 2:
                bad(where, "a toggle needs exactly two choices")
            if s.get("control") not in (None, "toggle", "segmented", "dropdown"):
                bad(where, "unknown control %r" % s.get("control"))
            # A default that matches no choice would show "custom" in the launcher.
            for key, dv in s.get("defaults", {}).items():
                if not any(unquote(c["set"].get(key)) == unquote(dv) for c in ch if key in c.get("set", {})):
                    bad(where, "default of %s matches no choice" % key)
        elif "control" in s:
            bad(where, "control only applies to choices")
        if "slider" in s:
            sl = s["slider"]
            if not (sl["min"] < sl["max"] and sl["step"] > 0):
                bad(where, "slider needs min < max and step > 0")
            if "format" in sl and not re.fullmatch(r"%g|%d|%\.(\d|10)f", sl["format"]):
                bad(where, "unsupported number format %r" % sl["format"])
            if target != "args" and "key" not in sl:
                bad(where, "slider needs a key")
        if "bindings" in s:
            b = s["bindings"]
            if not b.get("actions"):
                bad(where, "bindings.actions is empty")
            if b.get("max") == 0:
                bad(where, "bindings.max must be at least 1")
            keys = set()
            for a in b.get("actions", []):
                keys.add(a["key"])
                if "label" not in a:
                    bad(where, "action %s has no label" % a["key"])
            for name in b.get("names", {}):
                if not valid_key(name, any(a.get("pad") for a in b["actions"])):
                    bad(where, "names: %r is not a key name the launcher knows" % name)
            for key, dv in s.get("defaults", {}).items():
                if key not in keys:
                    bad(where, "default %s is not an action key" % key)
                parts = [p for p in unquote(dv).split(b.get("separator", "+")) if p]
                if len(parts) > b.get("max", 99):
                    bad(where, "default of %s has more than max keys" % key)
                pad = any(a["key"] == key and a.get("pad") for a in b["actions"])
                launcher_names = {v: k for k, v in b.get("names", {}).items()}
                for p in parts:
                    if not valid_key(launcher_names.get(p, p), pad):
                        bad(where, "default %s: %r is not a valid key name" % (key, p))


canon = json.load(open(sys.argv[1], encoding="utf-8"))
canon["settings"] = canon["settings"] + json.load(open(sys.argv[2], encoding="utf-8"))["settings"]
check("canonical block", canon)

# The fragments must cover exactly the keys the game reads (gcrecomp docs/SETTINGS.md).
by_id = {s["id"]: s for s in canon["settings"]}
for sid, ctl, keys in (("keyboard", "keyboard", KEYS), ("gamepad", "gamepad", BUTTONS)):
    got = [a["key"] for a in by_id[sid]["bindings"]["actions"]]
    if got != ["%s/%s" % (ctl, k) for k in keys]:
        bad("canonical block", "%s actions differ from the control keys" % sid)
    if sid == "gamepad" and not all(a.get("pad") for a in by_id[sid]["bindings"]["actions"]):
        bad("canonical block", "gamepad actions must have pad: true")

canon_ids = {s["id"] for s in canon["settings"]}
for path in sys.argv[3:]:
    m = json.load(open(path, encoding="utf-8"))
    check(path, m)
    first = [s["id"] for s in m["settings"][:len(canon_ids)]]
    if first != [s["id"] for s in canon["settings"]]:
        bad(path, "the canonical settings must come first, in order")

if failures:
    print("manifest check FAILED:", file=sys.stderr)
    for f in failures:
        print("  " + f, file=sys.stderr)
    sys.exit(1)
print("manifest check: ok")
PY
