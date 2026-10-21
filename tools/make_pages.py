#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the DRIFT site for GitHub Pages (from FoMni-1's):

  index.html                    what FoMni-1 is, how it plays, and the ways in: try, install, download, source
  img/                          screenshots (docs/img)
  emu/                          FoMni-1 in the browser (build/emu, from web/emu/build.sh), with fm1-reader's
                                words.js and voice in emu/reader/ (the screen in words)
  words/                        the screen reader's words files (web/words, from firmware/words.tsv)
  install/index.html            the web installer (web/omni_installer.html, with fm1pkg.js, fm1ota.js and
                                the package's metadata inlined; Chrome or Edge, Web MIDI)
  firmware/omni-VERSION.fwsc    the package the installer writes; also the download

  tools/make_pages.py build/omni-0.1-beta.fwsc 0.1-beta OUT_DIR

The package must be a release build (./build.sh --release X.Y): its identity, FM-1_7XXYYZZ, is what
the installer checks the download against and what the FM-1 reports after the install."""
import html
import json
import re
import shutil
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "web"))
from make_site import product_of, strip_module  # noqa: E402  (Felucca's: the package format)

import os
REPO = "https://github.com/" + os.environ.get("GITHUB_REPOSITORY", "charlesvestal/fm1-fomni")

LANDING = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>DRIFT · an ambient instrument for the FM-1</title>
<meta name="description" content="An ambient firmware for the M-VAVE FM-1: a chord-following drone, Euclidean rhythms and an Omnichord-style strum plate.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow:wght@400;600&family=Barlow+Semi+Condensed:wght@600;700&display=swap">
<style>
/* light: --muted, --accent-ink and --teal darkened just enough for 4.5:1 on the cream; --accent (the bright coral)
   only for the big title */
:root { --ground: #faf4e8; --ink: #48392f; --muted: #6e5c4b; --rule: #e6dccb; --accent: #e0683a; --accent-ink: #b84a1e; --teal: #1b776e;
        --card: #fffaf1; --focus: #1f6fd1; color-scheme: light; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --ground: #1c1814; --ink: #f2e8d8; --muted: #b3a28e; --rule: #3a322a; --accent: #f28a5a; --accent-ink: #f28a5a; --teal: #4cc3b6; --card: #26201b; --focus: #79adff; color-scheme: dark; }
}
:root[data-theme="dark"] { --ground: #1c1814; --ink: #f2e8d8; --muted: #b3a28e; --rule: #3a322a; --accent: #f28a5a; --accent-ink: #f28a5a; --teal: #4cc3b6; --card: #26201b; --focus: #79adff; color-scheme: dark; }
:focus-visible { outline: 3px solid var(--focus); outline-offset: 2px; }
body { background: var(--ground); color: var(--ink); margin: 0; padding-inline: 16px; padding-block: 48px 64px;
       font: 400 17px/1.6 "Barlow", "Helvetica Neue", Arial, sans-serif; }
main { max-width: 42rem; margin: 0 auto; }
h1 { font: 700 3rem/1 "Barlow Semi Condensed", "Barlow", sans-serif; margin: 0 0 .5rem; color: var(--accent); }
h2 { font: 700 1.4rem "Barlow Semi Condensed", "Barlow", sans-serif; margin: 2.2rem 0 .6rem; }
.lede { font-size: 1.15rem; margin: 0 0 1.6rem; max-width: 36rem; }
.shots { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; margin: 0 0 1.8rem; }
.shots img { width: 100%; height: auto; border-radius: 14px; image-rendering: pixelated; box-shadow: 0 1px 0 var(--rule), 0 6px 18px rgba(72, 57, 47, .12); }
.status { border: 1px solid var(--rule); background: var(--card); border-radius: 14px; padding: 12px 16px; margin: 0 0 1.6rem; }
.ways { display: grid; gap: 12px; margin: 0 0 1rem; }
.ways a { display: block; text-decoration: none; color: var(--ink); background: var(--card); border: 1px solid var(--rule);
          border-radius: 14px; padding: 14px 18px; }
.ways a:hover, .ways a:focus-visible { border-color: var(--accent-ink); }
.ways strong { font: 600 1.25rem "Barlow Semi Condensed", "Barlow", sans-serif; color: var(--accent-ink); display: block; }
.ways span { color: var(--muted); font-size: .95rem; }
table { border-collapse: collapse; width: 100%; font-size: .95rem; }
th, td { border-top: 1px solid var(--rule); padding: 8px 10px 8px 0; vertical-align: top; text-align: left; }
th { font-weight: 600; white-space: nowrap; color: var(--teal); }
p.small { color: var(--muted); font-size: .9rem; }
a { color: var(--accent-ink); }
footer { max-width: 42rem; margin: 0 auto; }
code { font-size: .9em; }
ul.pages { padding-left: 1.2rem; margin: 0 0 1rem; }
ul.pages li { margin: 0 0 .4rem; }
ul.pages b { color: var(--teal); }
.video { position: relative; aspect-ratio: 16 / 9; max-width: 100%; margin: 0 0 1.4rem; border-radius: 14px; overflow: hidden; background: #000; }
.video iframe { position: absolute; inset: 0; width: 100%; height: 100%; border: 0; }
</style>
</head>
<body>
<main>
<h1>DRIFT</h1>
<p class="lede">An ambient instrument for the M-VAVE FM-1. A drone that follows your chords (or holds one key), four
Euclidean rhythm lanes that drift against each other, and an Omnichord-style strum plate to play over the top.
Built on FoMni-1 by Charles Vestal and Felucca by Hügelton Instruments.</p>
<div class="shots">
  <img src="img/drift-drone.png" width="240" height="240" alt="The Drone page: Follow, the drone's notes C2 G2 C3 E3 and a breathing wave, then Mode, Key, Level, Tone">
  <img src="img/drift-pulse.png" width="240" height="240" alt="The Pulse page: four lanes of steps (LOW, MALLET, SHAKER, SWELL) with their hits, then Rate, Tempo, Perc, Vary">
  <img src="img/drift-play.png" width="240" height="240" alt="The Play page: the chord, its strings and the chord buttons, then Voice 1, Voice 2, Sustain, Chord">
</div>
<div class="status"><strong>Version __VERSION__.</strong> It installs and uninstalls the way Felucca, X0X and FoMni do,
and the installer can put M-VAVE's own firmware back. Installing is at your own risk.</div>
<nav class="ways" aria-label="Get DRIFT">
  <a href="emu/"><strong>Try it in the browser</strong><span>The same code the FM-1 runs, with sound. No FM-1 needed.</span></a>
  <a href="install/"><strong>Install</strong><span>From Chrome or Edge, with the FM-1 connected by USB. Nothing to install on the computer.</span></a>
  <a href="firmware/__PKG__"><strong>Download __PKG__</strong><span>For the command-line installer: <code>python3 tools/fm1_install.py __PKG__</code></span></a>
  <a href="__REPO__"><strong>Source</strong><span>GitHub, GPL-3.0.</span></a>
</nav>
<h2 id="playing">Playing</h2>
<table aria-labelledby="playing">
<tr><th scope="row">Black keys</th><td>The chord buttons. The drone follows them (Mode Follow). Hold ENV for the minor, LFO for the 7th.</td></tr>
<tr><th scope="row">White keys</th><td>The strum plate: each key plucks a string of the chord.</td></tr>
<tr><th scope="row">PLAY</th><td>The pulse on and off. The drone fades in with it, or whenever a chord is held.</td></tr>
<tr><th scope="row">FX</th><td>Drone, Texture, Sound pages (press again for the next).</td></tr>
<tr><th scope="row">SEQ</th><td>Pulse, Low, Mallet, Shaker, Swell, Rhythm pages (press again for the next).</td></tr>
<tr><th scope="row">HOME · SEL · GLO</th><td>Play, Chords, Setup pages. EDIT steps through every page.</td></tr>
<tr><th scope="row">ARP · REC</th><td>Chord hold, sync start.</td></tr>
<tr><th scope="row">SELECT · ALGORITHM · PRESETS</th><td>Tempo, OM rhythm, transpose.</td></tr>
</table>
<h2>The pages</h2>
<ul class="pages">
  <li><b>Drone.</b> Mode (Off, Follow, Fixed), Key (for Fixed), Level, Tone.</li>
  <li><b>Texture.</b> Glide, Spread (detune and width), Breathe (slow motion), Color (the chord's third).</li>
  <li><b>Pulse.</b> Rate (1/16 to 1/2), Tempo, Perc (all percussion), Vary (humanize).</li>
  <li><b>Low, Mallet, Shaker, Swell.</b> Hits, Steps, Rotate, Level. Swell makes no sound: its hits open and lift the drone.</li>
</ul>
</main>
<footer>
<p class="small">To go back to the official firmware, use the installer's "Back to the stock firmware", or M-VAVE's
updater, M-UPGRADE, from <a href="https://www.m-vave.com/download">m-vave.com/download</a>. Omnichord is a trademark of
Suzuki; M-VAVE and FM-1 are trademarks of their owners. DRIFT is not affiliated with any of them.</p>
</footer>
</body>
</html>
"""


def rebrand(page):
    """FoMni-1's pages, as DRIFT's: its name and its source (the credits to FoMni and Felucca stay)"""
    page = page.replace("https://github.com/charlesvestal/fm1-fomni/blob/main/BUILDING.md", REPO + "/blob/main/BUILDING.md")
    page = page.replace("https://github.com/charlesvestal/fm1-fomni", REPO).replace("github.com/charlesvestal/fm1-fomni", REPO[8:])
    page = page.replace("FoMni-1 is built on Felucca", "DRIFT is built on FOMNI_CREDIT by Charles Vestal and on Felucca")
    page = page.replace("FoMni-1", "DRIFT").replace("chord harp firmware", "ambient firmware")
    return page.replace("FOMNI_CREDIT", "FoMni-1")


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_7\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (./build.sh --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    name = f"drift-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for d in ("install", "firmware"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    page = (SRC / "web" / "omni_installer.html").read_text(encoding="utf-8")
    lib = strip_module((SRC / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((SRC / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"omni_installer.html must contain {mark} once")
    (out / "install" / "index.html").write_text(rebrand(page.replace("/*LIB*/", lib).replace("/*META*/", meta)), encoding="utf-8")
    shutil.copytree(SRC / "docs" / "img", out / "img")
    (out / "index.html").write_text(LANDING.replace("__VERSION__", html.escape(version)).replace("__PKG__", name)
                                    .replace("__REPO__", REPO), encoding="utf-8")
    emu = SRC / "build" / "emu"
    if (emu / "omni.wasm").exists():
        if not (emu / "reader" / "words.js").exists():
            print("warning: no build/emu/reader/: the emulator's screen is read as plain text, without its voice")
        shutil.copytree(emu, out / "emu")
        (out / "emu" / "index.html").write_text(rebrand((out / "emu" / "index.html").read_text(encoding="utf-8")),
                                                encoding="utf-8")
    else:                                         # DRIFT: the site without the browser version
        print("warning: no build/emu/omni.wasm (web/emu/build.sh needs Emscripten): the site has no emu/")
        landing = (out / "index.html").read_text(encoding="utf-8")
        landing = re.sub(r'  <a href="emu/">.*?</a>\n', "", landing)
        (out / "index.html").write_text(landing, encoding="utf-8")
    # the screen reader's words files (fm1-reader): every one ever made, for the devices that point at them
    shutil.copytree(SRC / "web" / "words", out / "words")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, install/ ({product}), emu/, words/, firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
