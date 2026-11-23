# BitBuddy – Workflow Notes (living log)

A running log of how this plugin was built: what worked, what slowed things
down, the exact test criteria, and what to do differently next time.
Written so the *next* audio-plugin project can start faster.

---

## 1. Timeline / log

| # | Step | Result | Time sink? |
|---|------|--------|-----------|
| 1 | Toolchain check (`Get-Command`, `vswhere`) | git + winget present; no compiler, no CMake, no Python | – |
| 2 | `winget install Kitware.CMake`, `Ninja-build.Ninja` | OK, ~1 min, no admin needed | – |
| 3 | `winget install Microsoft.VisualStudio.2022.BuildTools` (VCTools workload) | **1st attempt failed: exit 1602** = Windows admin (UAC) prompt cancelled or timed out | **Yes** – needed a round-trip with the user |
| 4 | Retry with the user watching for the UAC prompt | OK (MSVC 14.44, Win SDK 10.0.22621 + 10.0.26100) | ~10 min download |
| 5 | Pick JUCE version via `git ls-remote --tags` | JUCE 9.0.6 exists; chose **8.0.15** (last of the 8.x line, most battle-tested) | – |
| 6 | Shallow clone JUCE into `external/JUCE` | OK (~4,400 files) | – |
| 7 | Grep JUCE headers for APIs before writing code | Found JUCE 8.0.15 API moves (see §4) | Saved compile-fix loops |

---

## 2. Prework / scope questions to ask *before* starting

These would have removed guesswork or round-trips:

1. **Which DAW(s) do you use?** Drives the plugin formats:
   VST3 (most DAWs on Windows/Mac), AU (Logic/GarageBand, **macOS only, needs a Mac to build**),
   CLAP (Bitwig, Reaper, FL Studio 2024+), VST2 (LMMS – SDK no longer distributable).
2. **Windows only, or Mac too?** A Mac build needs a Mac (or CI with macOS runners).
3. **Personal use or will you share/sell it?** JUCE is AGPLv3 or commercial
   (free "Starter" tier under a revenue cap). Affects the license file and the company/plugin codes.
4. **Authentic vs. easy?** Strict NES limits (mono, 4 channels) vs. a friendly polyphonic synth.
5. **Do you have a MIDI keyboard?** If not, prioritize the on-screen and computer-keyboard input.
6. **Will you be at the PC during installs?** Admin prompts (UAC) time out. Tell the
   user up front to watch for "Do you want to allow this app to make changes?".
7. **Disk space OK for ~8 GB of tools?** (VS Build Tools ~6 GB + JUCE + build output).
8. **Name / branding preferences?** (The plugin name shows up in every DAW's plugin list.)

---

## 3. Efficiency ideas (expedite completion)

- **Kick off the biggest install first, in the background**, then design and write code while it downloads.
  (Done here; the compiler took ~10 min, which overlapped with design work.)
- **Warn about UAC before launching admin installers**, so the 1602 retry never happens.
- **Check the API in the cloned framework's headers** (grep) instead of relying on memory.
  Frameworks move APIs between versions (see §4).
- **Python isn't guaranteed on Windows.** PowerShell can do quick numeric simulations
  (used for the LFSR period check).
- Use the **Visual Studio CMake generator** (`-G "Visual Studio 17 2022"`) to avoid needing
  a "Developer Command Prompt" environment. Ninja + MSVC needs `vcvars64.bat`.
- **Build a headless test host early.** It loads the *real* VST3 binary, renders audio
  offline and measures it. That makes tests runnable without anyone listening, and without GUI automation.
- **Render the editor to a PNG** from the test host so the UI can be reviewed without
  screen-control tools.

---

## 4. Gotchas found

- **JUCE 8.0.15**: plugin hosting lives in the new `juce_audio_processors_headless` module;
  `AudioPluginFormatManager::addDefaultFormats()` → replaced by free function
  `juce::addDefaultFormatsToManager(manager)`. Also `JUCE_PLUGINHOST_VST3` defaults to 0 (set it to 1 for the test host).
- `FontOptions (const Typeface::Ptr&)` exists in JUCE 8. Use it for embedded fonts.
- MSI exit code **1602** = user cancelled (usually an unanswered UAC prompt).
- NES noise LFSR: long mode period 32767, short ("metal") mode period **93** from seed 1.
  So clocking the short mode at 93 × note frequency plays exactly in tune.

---

## 5. Testing parameters (acceptance criteria)

Three layers. All are automated and re-runnable (commands in README → Testing).

**Layer 1: own test host** (`tools/TestHost.cpp --test`). Loads the *built VST3* through
JUCE's VST3 hosting, exactly like a DAW, renders offline and measures the audio.

| Area | Criterion |
|---|---|
| Identity | name = BitBuddy, accepts MIDI, stereo out, 24–26 automatable params, 21 programs |
| Every preset (21) | output finite (no NaN/Inf), peak > −46 dB (makes sound), peak ≤ 0 dBFS, **tail < −60 dB** 8.5 s after note-off (no stuck notes / runaway echo) |
| Tuning | YIN pitch detector, **±8 cents** for Square, Pulse 25/12.5, Triangle, Saw, Metal Noise |
| Sample rates / blocks | A4 correct at 48 k/64, 96 k/1024, 22.05 k/**37** (odd block size on purpose) |
| Controllers | pitch bend +2 st within ±10 cents; all-sound-off → < −60 dB |
| Chord arp | one key → exact semitone sequence 0,4,7,0,4,7 (±0.3 st) |
| Notes arp | C-E-G held, Up&Down, 2 oct → 60 64 67 72 76 79 76 72 67 64 |
| Mono legato | A2 → E3 lands on the right pitch |
| Drum kit | all 12 drums sound (> −34 dB) and decay below −60 dB within 4.5 s |
| Limiter | 8-note chord, vel 127, volume +6 dB → peak < 1.0 |
| Project save/restore | tweak a preset, save, load another, restore → tweaks + program name survive |
| CPU | 8-voice chords for 60 s → must be > 20× real time |

**Layer 2: pluginval** (Tracktion, open source) at `--strictness-level 10`: 25 test groups
(open cold/warm, programs, editor, editor-while-processing, audio processing at many rates
and block sizes, state restoration, automation, parameter thread safety, bus layouts, fuzzing).
The Steinberg `vst3 validator` group is skipped unless `--vst3validator <path>` is given
(needs the full VST3 SDK; optional follow-up).

**Layer 3: visual review.** `--snapshot` renders the editor to PNG for 4 states
(Hero Lead playing, Chord Arp, Drum Kit, Help overlay) so the layout can be checked without screen control.

---

## 6. Test results

| Run | Result | Notes |
|---|---|---|
| Build #1 | compiled first try (278 s full build, 1 warning) | design-first + API grep paid off |
| Tests run 1 | 35 / 43 pass | see root causes below |
| Tests run 2 | 42 / 44 pass | |
| Tests run 3 | **44 / 44 pass** | |
| pluginval L10 | **SUCCESS** (16 s) | vst3 validator group skipped (no SDK validator) |
| Incremental rebuild | ~20 s | |

Root causes of the run-1 failures (useful patterns for next time):

1. **Real bug: controller state leaked across playback restarts.** Pitch bend from one test shifted every
   later test by +2 st. Fix: reset pitch bend + mod wheel in `prepare()`.
   *Lesson: a constant offset across many failures points to shared/leaked state, not many bugs.*
2. **Test-instrument bug: naive autocorrelation pitch detection** had octave errors on the smooth
   stepped triangle and the jittery metal noise. Fix: switched to YIN. *Lesson: validate the
   measuring tool before trusting a "synth bug".*
3. **Test-method bug: re-selecting the active program is a no-op** in VST3 hosts, so a test inherited
   the previous test's wave. Fix: the helper switches away first.
4. **Test-method bug: knob changes while "playing" are delivered with the next audio block** (JUCE
   `vst3IsPlaying`), so saving state with no block in between captured stale values. Fix: "pump"
   audio between steps like a real DAW does. *Lesson: simulate the host's real timing.*
5. **Real improvement: big chords hit 0 dBFS.** Added a soft limiter (transparent below −2 dB, ceiling −0.18 dBFS).
6. Defensive: ignore a host program change to the already-active program (JUCE's wrapper already
   does this, but some hosts resend the program number on project load).
7. The hosted VST3 shows **2,106 params**: 26 real + 2,080 hidden, non-automatable MIDI-CC slots that
   JUCE adds so VST3 can receive mod wheel / pitch bend. Count `isAutomatable()` params in tests.

---

## 7. Install / distribution log

| Step | Result |
|---|---|
| Copy to `C:\Program Files\Common Files\VST3` (elevated) | **UAC prompt cancelled**, probably overlapping with installer prompts. → wrote `install-plugin.ps1` for later |
| Copy to per-user `%LOCALAPPDATA%\Programs\Common\VST3` | OK, no admin. Part of the VST3 spec, but not every DAW scans it by default |
| winget: REAPER, Audacity, Cakewalk | see §1 |
| GitHub | private repo; JUCE as a submodule (keeps the repo small); commit identity = GitHub noreply address |

*Lesson: never fire two admin prompts at once. Sequence them and tell the user before each one.*
