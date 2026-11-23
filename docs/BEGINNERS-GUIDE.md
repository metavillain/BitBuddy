# Making 8-bit music with BitBuddy – a beginner's guide

No experience needed. By the end you'll know what chiptune is, how to load
BitBuddy in a DAW, and you'll have a full song playing that you can remix.

---

## 1. What is chiptune?

Old game consoles (NES, Game Boy, Commodore 64) couldn't play recorded music.
Instead, each had a tiny **sound chip** that generated a few simple waveforms
in real time. The NES chip, for example, had exactly **5 "channels"**:

| Channel | What it sounds like | Used for |
|---|---|---|
| Pulse 1 | bright, buzzy square wave | melody (lead) |
| Pulse 2 | same | harmony / chords / echo |
| Triangle | soft, round, a bit "steppy" | bass lines (and kick drums) |
| Noise | hiss, static | drums, explosions, wind |
| DPCM | tiny low-quality samples | (BitBuddy skips this one) |

Composers had to be clever to make music with so little. Their tricks
*are* the chiptune sound, and BitBuddy has each one built in:

- **Duty cycles.** Pulse waves came in a few fixed widths: 50% (Square),
  25% and 12.5%. Thinner = more nasal and buzzy.
  → *BitBuddy section 1: PICK A SOUND*
- **Fake chords (arpeggios).** One channel can play only one note, so composers
  flipped between a chord's notes *really* fast. Your ear blends them into a
  shimmering chord.
  → *Section 3: MAGIC CHORDS → CHORD, then hold ONE key*
- **Delayed vibrato.** Long notes start clean, then start to "sing".
  → *Section 4: VIBRATO + VIB WAIT*
- **Pitch sweeps.** Sliding pitch makes lasers, jumps, coins and even kick drums.
  → *Section 4: SWEEP*
- **Echo.** A quieter copy of the melody on a spare channel, a moment later.
  → *Section 5: ECHO*
- **Lo-fi grit.** Low resolution (bits) and low sample rates.
  → *Section 5: BITS / RATE*

---

## 2. The two ways to use BitBuddy

### A. On its own (quickest way to play)

Double-click **`BitBuddy.exe`** (in `build\BitBuddy_artefacts\Release\Standalone\`).

- Click **Options** (top-left) → choose your audio output (and MIDI keyboard if you have one).
- Play the on-screen keyboard with the mouse, or your computer keys:
  `A W S E D F T G Y H U J K` = one octave, `Z` / `X` = octave down / up.
- Click **SURPRISE ME!** for random sounds, or step through presets with `<` `>`.

### B. Inside a DAW (to make real songs)

A **DAW** (Digital Audio Workstation) is the app you make songs in. **VST3** is
the plugin format that lets BitBuddy run inside almost any DAW.

**Step 1: make sure the DAW can find BitBuddy.**
Run `install-plugin.ps1` (right-click → *Run with PowerShell*, click **Yes** on
the admin prompt). That copies BitBuddy into the standard folder every DAW scans:
`C:\Program Files\Common Files\VST3`.

> Already done on this PC: BitBuddy is installed in both the system folder and your
> *personal* VST3 folder (`%LOCALAPPDATA%\Programs\Common\VST3`), so REAPER, Cakewalk
> and other DAWs will all find it. After you rebuild BitBuddy, run the script again to
> update both copies.

**Step 2: load it.**

| DAW | How to add BitBuddy |
|---|---|
| **REAPER** | *Track → Insert virtual instrument on new track* → type `BitBuddy` → OK. If it's missing: *Options → Preferences → Plug-ins → VST → Re-scan*. |
| **Cakewalk** | *Insert → Soft Synth → BitBuddy* (or drag it from the *Browser → Plug-ins → Instruments* tab). If it's missing: *Edit → Preferences → File → VST Settings → Scan*. |
| **FL Studio** | *Plugin database → Installed → Generators → VST3 → BitBuddy* (or *Options → Manage plugins → Find plugins*). |
| **Ableton Live** | *Preferences → Plug-ins → turn on "Use VST3 Plug-in System Folders" → Rescan*, then find it under *Plug-ins*. |
| **Bitwig / Studio One** | Rescan plugins, then search for `BitBuddy`. |
| **Audacity** | Audacity doesn't play instrument plugins. Use it to trim, edit and export your finished songs. |

**Step 3: play it.**
- With a MIDI keyboard: select the track and play.
- Without one: in REAPER, press **Alt+B** for the *Virtual MIDI Keyboard*.
  Most DAWs have a similar feature. BitBuddy's own on-screen keyboard works too.
- Or **draw notes** in the piano roll. In REAPER: *Insert → New MIDI item*,
  then double-click it.

> **Tip:** inside a DAW, your computer keyboard usually controls the DAW
> (e.g. Space = Play), so BitBuddy's `A W S E D…` keys work best in the
> standalone app.

---

## 3. Your first song in 10 minutes (using the demo)

The `demo` folder has an original tune, **"Pixel Quest"**, as audio
(`BitBuddy-Demo-PixelQuest.wav`) and as MIDI files: the notes, ready to load into
your DAW.

1. Open REAPER (or Cakewalk) and set the tempo to **140 BPM**.
2. Make **4 tracks**, each with **BitBuddy** on it, and pick these presets:

   | Track | BitBuddy preset | Drag in this MIDI file |
   |---|---|---|
   | Lead | **Hero Lead** | `demo/midi/1-Lead (Hero Lead).mid` |
   | Chords | **Note Arpeggiator** | `demo/midi/2-Chords (Note Arpeggiator).mid` |
   | Bass | **Triangle Bass** | `demo/midi/3-Bass (Triangle Bass).mid` |
   | Drums | **Drum Kit** | `demo/midi/4-Drums (Drum Kit).mid` |

3. Press Play. That's a complete chiptune track, 4 channels just like the NES.
4. Now make it yours:
   - Change the **Lead** preset to *Thin Lead* or *Lo-Fi Saw Buzz*.
   - Turn up **ECHO** on the lead.
   - Switch the chords to **Major Chord Arp**, delete all but the lowest note of each chord, and listen to the "magic chord" trick.
   - Move some lead notes around. Stick to the notes **A B C D E F G**
     (white keys) and it'll always sound right in this song.

---

## 4. Writing from scratch: a simple recipe

1. **Tempo:** 120–160 BPM feels "video game".
2. **Drums** (*Drum Kit*): kick on beats 1 and 3 (any **C**), snare on 2 and 4 (**D**),
   hi-hats on every 8th note (**F#**). Every octave has the same kit.
3. **Bass** (*Triangle Bass*): one note per beat or per 8th note, following the chord root.
   Jumping up an octave and back is a classic NES bass move.
4. **Chords** (*Major/Minor Chord Arp*): hold ONE key per chord. A good first progression:
   **A minor → F → C → G** (use *Minor Chord Arp* for the A, *Major Chord Arp* for the others).
5. **Melody** (*Hero Lead*): short notes, then a long note at the end of each phrase so the
   delayed vibrato can bloom.
6. **Mix:** lead loudest, drums and bass next, chords quieter (lower BitBuddy's VOLUME).
7. **Sound effects:** sprinkle in *Coin Pickup*, *Power Up* or *Laser Zap* for fun.

---

## 5. Every control, in one line

| Control | What it does |
|---|---|
| **Wave** | The raw sound: Square, Pulse 25%, Pulse 12.5%, Triangle, Saw, Noise, Metal Noise, Drum Kit |
| **Attack / Decay / Sustain / Release** | Volume shape of each note: fade-in, drop, hold level, fade-out |
| **Arp: Off / Chord / Notes** | Chord = one key plays a fast chord. Notes = held keys play as a pattern |
| **Chord / Pattern / Speed / Octaves** | Which chord, what order, how fast (tempo-synced or 15/30/60 Hz), how wide |
| **Vibrato / Vib Rate / Vib Wait** | Pitch wobble: amount, speed, delay before it starts |
| **Sweep / Swp Time** | Notes slide in from above (+) or below (−) |
| **PWM** | Animates the pulse width: lush and chorus-like (Square/Pulse waves) |
| **Glide / Poly / Mono** | Slide between notes; Mono = one note at a time like a real chip |
| **Bits / Rate** | Lo-fi crunch: fewer bits, lower sample rate |
| **Echo / Time / Repeats** | Ping-pong echo, tempo-synced |
| **Volume** | Output level. A gentle limiter stops loud chords from crackling |
| **Mod wheel / Pitch bend / Sustain pedal** | All supported from a MIDI keyboard |

Hover over any control in BitBuddy and the help bar at the bottom explains it.
Double-click a knob to reset it.
