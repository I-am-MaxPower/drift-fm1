# DRIFT for the M-VAVE FM-1

DRIFT turns the M-VAVE FM-1 into an ambient instrument. A drone sits underneath and follows your chords.
Four Euclidean rhythm lanes play soft percussion and make the drone breathe, each lane with its own
length so they drift against each other. On top, the white keys are an Omnichord-style strum plate.

It is a fork of [FoMni-1](https://github.com/charlesvestal/fm1-fomni) by Charles Vestal, which is built on
[Felucca](https://github.com/hugelton/Felucca) by Hügelton Instruments. Everything FoMni-1 does still works.

Installing custom firmware is at your own risk. The web installer can always put M-VAVE's firmware back.

## Playing

| Control | What it does |
|---|---|
| Black keys | Chord buttons. The drone follows them. Hold **ENV** for minor, **LFO** for 7th, both for m7. |
| White keys | The strum plate. Each key plucks one string of the chord. |
| PLAY | Starts and stops the pulse. The drone fades in with it, or whenever a chord is held. |
| ARP | Chord hold: the chord (and the drone) keep going after you let go. |
| REC | Sync start: PLAY waits for your first chord. |
| FX | Drone page, then Texture, then Sound (press again for the next). |
| SEQ | Pulse page, then Low, Mallet, Shaker, Swell, Rhythm (press again for the next). |
| HOME, SEL, GLO | Play, Chords, Setup pages. EDIT steps through every page. |
| SELECT | Tempo. |
| ALGORITHM | The OM-84 rhythm (silent by default, see Rhythm below). |
| PRESETS | Transpose. |
| KNOB 1 to 4 | The four values on the screen. |
| SAVE | Saves. DRIFT also saves by itself a few seconds after a change. |

### The drone (FX)

**Drone page:** Mode, Key, Level, Tone.

- **Mode.** *Follow*: the drone takes its root from the chord button you press and glides to each new one.
  *Fixed*: the drone stays on **Key** (a pedal tone) while its color voice still follows your chords.
  *Off*: no drone.
- **Tone** opens the drone's filter, from dark to bright.

**Texture page:** Glide, Spread, Breathe, Color.

- **Glide** is how long the drone takes to reach a new chord: a quick slide at 0, a slow drift at 100.
- **Spread** detunes each voice's two oscillators and spreads them left and right.
- **Breathe** is a very slow swell in the filter and level, independent of the tempo.
- **Color** brings in a fourth voice on the chord's third (major or minor) above the root, fifth and octave.

The drone uses the Sound page's **Chord rev** as its reverb send. Turn it up for a wash.

### The pulse (SEQ)

**Pulse page:** Rate (1/16, 1/8, 1/4, 1/2), Tempo, Perc (all percussion), Vary.

**Vary** humanizes: softer and uneven hits, a few left out, and the mallet straying to other chord tones.

**Lane pages (Low, Mallet, Shaker, Swell):** Hits, Steps, Rotate, Level.

A Euclidean rhythm spreads **Hits** as evenly as possible across **Steps**. Three hits over eight steps
is the tresillo (x..x..x.). **Rotate** shifts where the pattern starts. Each lane has its own length, so
a 3-over-8 against a 5-over-16 lines up only now and then.

- **Low**: a soft thump tuned to the chord's root.
- **Mallet**: bell-like pings on the chord's tones, walking up through them. They ring into the reverb.
- **Shaker**: short filtered noise.
- **Swell**: no sound of its own. Each hit opens the drone's filter and lifts its level, then lets go.
  Its Level knob is **Depth**.

### FoMni-1's pages, still here

- **Play (HOME):** Voice 1, Voice 2, Sustain, Chord level.
- **Rhythm (SEQ, last):** the OM-84's ten rhythms. DRIFT sets **Drums** to 0 and **Auto bass** off by
  default so the pulse leads. Turn them up to layer the two.
- **Sound (FX, third):** Reverb, Space, Width, Chord rev.
- **Chords (SEL):** change any chord button: press it, then turn Root and Type.
- **Setup (GLO):** Tune, MIDI out, Lights.

### MIDI out

The strings on channel 1, the chord on 2, the bass on 3, the mallet on 4. Low (note 36) and Shaker
(note 70) on channel 10, along with the OM drums when they are up. Clock goes out while the pulse runs.

## Getting it onto your FM-1

The firmware has to be compiled with JieLi's toolchain, which only runs on Linux. This repository builds
it for you on GitHub.

1. Put this folder on GitHub as a new repository (public, so GitHub Pages is free).
2. In the repository, open **Settings > Pages** and set **Source** to **GitHub Actions**.
3. Push (or open **Actions > Build DRIFT > Run workflow**). The build takes a few minutes.
4. When it finishes, open `https://YOUR-NAME.github.io/REPO-NAME/install/` in Chrome or Edge, plug in the
   FM-1 by USB and press **Install**. The page also has **Back to the stock firmware**.

The `.fwsc` file is also attached to every run (Actions > the run > Artifacts) if you prefer the
command-line installer: `python3 tools/fm1_install.py drift-X.Y.fwsc`.

Keep M-VAVE's V15 firmware (`FM-1.fwsc`, from [m-vave.com/download](https://www.m-vave.com/download))
on your computer as a way back. If an install is interrupted and the FM-1 stays black, see
"If the FM-1 does not start" in [Felucca's README](https://github.com/hugelton/Felucca#if-the-fm-1-does-not-start).

## Working on the code

- `firmware/src/dsp/drift.c` is DRIFT's sound: the drone, the four lanes and the swell.
- `firmware/src/dsp/omni.c` is FoMni-1's instrument. It includes `drift.c` and calls its hooks.
- `firmware/src/app/ui.c` is the panel and the screen, including the Drone and Pulse pages.

Everything except the final package can be built and tested on any computer with a C compiler:

```
tests/run_tests.sh            # unit tests, the drift test, the whole app in a simulator
```

`build/host/omni_host SCRIPT OUTDIR` runs the firmware from a script (`tests/scenarios/*.omni`) and writes
audio and screenshots. `tests/scenarios/drift.omni` walks through the new pages.
`build/scenarios/drift-walk.wav` is a minute of DRIFT to listen to after the tests run.

## Credits

DRIFT's drone and pulse are new. The strum plate, chords, rhythms, reverb, UI and screen-reader support are
[FoMni-1](https://github.com/charlesvestal/fm1-fomni)'s, by Charles Vestal. Its waves, drum sounds and
rhythms come from [Chordian](https://github.com/Jan125/pb.chordian), Jan125's OM-84 emulator (CC0). The
platform (hardware layer, USB, update loader, storage, installer) is
[Felucca](https://github.com/hugelton/Felucca)'s by Leo Kuroshita (Hügelton Instruments), by way of
[X0X](https://github.com/charlesvestal/fm1-x0x). GPL-3.0-only; see [LICENSING.md](LICENSING.md).

Omnichord is a trademark of Suzuki. M-VAVE and FM-1 are trademarks of their owners. DRIFT isn't affiliated
with or endorsed by Suzuki, M-VAVE, Hügelton Instruments or the FoMni-1 project.
