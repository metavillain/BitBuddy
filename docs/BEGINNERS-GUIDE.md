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
  The bottom-right corner says **KEYS ON** (green) when your letter keys play BitBuddy,
  and shows the current octave. Clicking knobs, buttons or menus never takes the keys away.
- Click **SURPRISE ME!** for random sounds, or step through presets with `<` `>`.

### B. Inside a DAW (to make real songs)

A **DAW** (Digital Audio Workstation) is the app you make songs in. **VST3** is
the plugin format that lets BitBuddy run inside almost any DAW.

**Step 1: make sure the DAW can find BitBuddy.**
Run `scripts\install-plugin.ps1` (right-click → *Run with PowerShell*, click **Yes** on
the admin prompt). That copies BitBuddy into the standard folder every DAW scans:
`C:\Program Files\Common Files\VST3`.

> Already done on this PC: BitBuddy is installed in both the system folder and your
> *personal* VST3 folder (`%LOCALAPPDATA%\Programs\Common\VST3`), so REAPER, Cakewalk
> and other DAWs will all find it. After you rebuild BitBuddy, run the script again to
> update both copies.

**Step 2: load it.**

| DAW | How to add BitBuddy |
|---|---|
| **GarageBand / Logic Pro** (Mac) | New *Software Instrument* track → click the instrument slot → *AU Instruments → BitBuddy Labs → BitBuddy*. |
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
   **Easier:** pick the *Smart Chords* preset, set **KEY = A** and **SCALE LOCK = Minor**, and just press
   A, F, C, G: BitBuddy picks minor or major for you.
5. **Melody** (*Hero Lead*): short notes, then a long note at the end of each phrase so the
   delayed vibrato can bloom.
6. **Mix:** lead loudest, drums and bass next, chords quieter (lower BitBuddy's VOLUME).
7. **Sound effects:** sprinkle in *Coin Pickup*, *Power Up* or *Laser Zap* for fun.

---

## 5. Every control, in one line

| Control | What it does |
|---|---|
| **Wave** | The raw sound: Square, Pulse 25%, Pulse 12.5%, Triangle, Saw, Noise, Metal Noise, Drum Kit, **Draw** (your own) |
| **Chip** | Modern (no limits), or behave like a real **NES**, **Game Boy** or **C64** |
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
| **C64 Filter: Off / Low / Band / High, Cutoff, Reso, Env** | The squelchy Commodore 64 filter; Env makes it "wow" on every note |
| **Key / Scale Lock** | Every key you press snaps into the scale: no wrong notes |
| **Jam / Style / Tempo** (app) | A backing band (drums, bass, chords) in your key |
| **Rec** (app) | Records a WAV (sound) + MIDI (notes) of what you play |
| **Screen: Scope / Wave / Macros** | Watch the sound, draw your own wave, or draw macros |
| **Save / Mutate / dice** | Keep your sound as a preset; nudge it; re-roll one section |
| **View** | Show / hide boxes (drag a box's title to move it) |
| **Mod wheel / Pitch bend / Sustain pedal** | All supported from a MIDI keyboard |

Hover over any control in BitBuddy and the help bar at the bottom explains it.
Double-click a knob to reset it.

---

## 6. New in 1.1: the fun stuff

**No wrong notes (Scale Lock).** In the PLAY ALONG box (bottom right), pick a **KEY** (the song's home
note; C is fine) and a **SCALE LOCK** (Major = happy, Minor = sad, Penta = can't miss, Dorian = adventure,
Harm. Minor = spooky). Now every key you press snaps to a note that fits. The piano dims the notes that
aren't in the scale and marks the home note pink.

**Smart chords.** Set MAGIC CHORDS to **CHORD** and the chord type to **SMART**: every key plays the chord
that belongs to it in your scale (in C major: C → C major, D → D minor, B → B diminished).

**Jam with a band (BitBuddy app).** Press **JAM**: drums, bass and chords start playing in your key.
Pick a style (Adventure, Dungeon, Racing, Chill) and a tempo, turn on Scale Lock, and make up a melody.
Press **REC** to record it: you get a WAV file (send it to a friend!) and a MIDI file of your notes in
*Music → BitBuddy Recordings* (press **SHOW** to open the folder; **SHARE** on phones).

**Draw your own wave.** Pick the **DRAW** wave: the SCREEN turns into a drawing board. Click and drag to
draw one cycle of the sound - 32 steps, 16 levels, exactly like the Game Boy's "wave channel". Smooth
shapes sound soft, jagged shapes buzzy. SINE / TRI / SAW / SQR / RND give you a starting point.

**Macros (like the music tracker FamiTracker).** Click **MACROS** on the SCREEN. A macro is a little
pattern that plays from the start of every note, 60 steps a second:
- **VOL**: the volume of each step: fade-outs, stutters, fake echoes.
- **ARP**: notes added at each step: 0, 4, 7 looping is the classic NES chord.
- **PITCH**: tiny bends: scoops into notes, warbles.
- **DUTY**: the pulse width changing as the note plays.

Turn a lane **ON**, draw bars, set **LEN** (how many steps), and click the strip at the bottom to make it
loop from that step. Try the *Macro Chord Blip* and *Macro Stutter Lead* presets to see examples.

**C64 filter.** Box 6 adds the Commodore 64's resonant filter. **LOW** + low **CUTOFF** + some **RESO**
+ **ENV** = rubbery "wow" basses (try the *C64 Squelch Bass* preset).

**Chip modes.** The **CHIP** menu (top of box 1) makes BitBuddy behave like real hardware: the **NES** and
**Game Boy** only play 2 notes at once, have 16 volume steps and their own slightly-off tuning; the NES
triangle can't fade. It's how the old music really sounded (try *Real NES Lead*).

**Your own presets, dice and MUTATE.** Press **SAVE** to keep a sound (it shows under MY PRESETS in the
preset menu). The little **dice** on each box re-rolls just that part. **MUTATE** nudges every knob a
little - press it a few times to let a sound evolve.

**On a phone.** Hold it sideways. The keyboard and the help bar stay at the bottom of the screen;
swipe up and down on the rest to reach every box (dragging a knob still turns it). Tap any control to
see what it does. Hide the boxes you don't use with **VIEW** to make the page shorter.

**Make it yours.** Click **BIT** (the little chip next to VIEW - it dances when you play) to change the
colours: Sweetie 16, Game Boy, Amber CRT or NES. Use **VIEW** to hide boxes you don't use, and drag a
box by its title bar to move it. Hidden boxes keep working; VIEW tells you if one is still changing
the sound.
