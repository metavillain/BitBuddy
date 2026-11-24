# BitBuddy – Workflow Notes

How this plugin was built: what worked, what slowed things down, the exact test
criteria, and a reusable checklist so the *next* audio-plugin project starts faster.

---

## 1. Timeline / log

| # | Step | Result | Time sink? |
|---|------|--------|-----------|
| 1 | Toolchain check (`Get-Command`, `vswhere`) | git + winget present; no compiler, no CMake, no Python | – |
| 2 | `winget install Kitware.CMake`, `Ninja-build.Ninja` | OK, ~1 min, no admin needed | – |
| 3 | `winget install Microsoft.VisualStudio.2022.BuildTools` (VCTools workload) | **1st attempt failed: exit 1602** = Windows admin (UAC) prompt cancelled / timed out | **Yes**: needed a round-trip with the user |
| 4 | Retry with the user watching for UAC | OK (MSVC 14.44, Win SDK 10.0.22621 + 10.0.26100) | ~10 min download, overlapped with design |
| 5 | Pick JUCE version via `git ls-remote --tags` | JUCE 9.0.6 exists; chose **8.0.15** (last 8.x, most battle-tested) | – |
| 6 | Shallow-clone JUCE into `external/JUCE` | OK | – |
| 7 | Grep JUCE headers for APIs before writing code | Found 8.0.15 API moves (§4) | Saved compile-fix loops |
| 8 | Write all code (engine, params, presets, processor, UI, test host) | ~2,900 lines | – |
| 9 | First full build | **Compiled first try**, 278 s, 1 warning | – |
| 10 | Own test host: 3 runs | 35/43 → 42/44 → **44/44** (§6) | ~15 min of diagnosis |
| 11 | pluginval strictness 10 | **SUCCESS** first try (16 s) | – |
| 12 | UI snapshots → visual review | 1 fix (drum help text overflowed) | – |
| 13 | Demo song + MIDI render | OK | – |
| 14 | Install plugin + REAPER / Audacity / Cakewalk | system-folder copy UAC **cancelled** (overlapping prompts); per-user copy OK; all 3 apps installed | Yes (UAC again) |
| 15 | Mutation testing (3 planted bugs) | **3/3 caught** | – |
| 16 | GitHub: private repo, JUCE as submodule | pushed | – |
| 17 | `install-plugin.ps1` (single, announced UAC prompt) | system-wide install OK, verified by SHA-256 against the build | – |
| 18 | **User bug report:** computer keys only work after clicking the piano | root cause: every button/knob/menu took keyboard focus when clicked; the one-time startup focus grab could fire before the window was active | – |
| 19 | Fix: only the piano may take focus; clicks inside BitBuddy and an active-window timer hand keys back; KEYS ON indicator | new `--focus-test` (simulated mouse clicks on a real window): **9/9**; with the fix disabled (mutant): **1/9** | stale-binary trap, see §4 |
| 20 | pluginval L10 re-run, reinstall (1 announced UAC) | SUCCESS, hash-verified | – |
| 21 | Share package for a friend (`scripts/make-share-package.ps1`) | self-contained zip: exe + VST3 + .bat installer + READ ME FIRST + guide + demo + source + licenses | – |
| 22 | Cleanup + efficiency pass (measured before/after, §9) | VST3 9.10 → 6.73 MB, chords CPU −25%, idle CPU −75%; 46/46 tests, pluginval OK, 0 warnings | caught own idle-path bug via a new test |
| 23 | Folder consolidation | `src/ tests/ scripts/ docs/reports/`; one `scripts/build-and-test.ps1` replaces ~8 manual commands | – |
| 24 | Mac version via GitHub Actions (universal AU + VST3 + app) | **first CI run green**: auval OK, 46/46 on VST3 *and* AU, pluginval OK ×2 | – (no Mac needed) |
| 25 | Repo made public + AGPLv3 LICENSE (user's choice) | free unlimited CI incl. 10×-cost macOS minutes; Release page shareable by link | first `git push` of a workflow file: **HTTP 403** (see §4) |
| 26 | Android: `android/BitBuddy.jucer` (JUCE CMake can't do Android) | Projucer → Gradle APK in CI + Android 14 emulator smoke test; zero permissions | – |
| 27 | iOS: CMake Xcode build, AUv3 + app | CI simulator build + iPad simulator smoke test (device install needs an Apple account) | – |
| 28 | App icon + touch UI (tap = help, fit-to-screen) | one `assets/icon.png` for all platforms | – |
| 29 | CI fixes: Projucer `useAppConfig="0"`, Android launch activity, iOS app path; macOS runner capacity re-runs | **v1.0.1 released** on all 4 platforms (Windows/Mac zips + APK) | GitHub Mac runners failed to start twice (capacity, not code) |
| 30 | 1.1 on a feature branch (release stays as tested) | user: "build all of these in the order you best see fit" | – |
| 31 | Scale Lock + Smart chords (`Scales.h`, key→note map in the engine) | 52/52; static_asserts on the music theory | test expectation wrong once (B→C, not A, in A minor penta) |
| 32 | Piano shows the scale + computer-key letters | snapshot review | – |
| 33 | My Presets, per-section dice, MUTATE | 56/56 | – |
| 34 | C64 filter (TPT SVF + envelope) as a self-contained `Box` | 61/61 | – |
| 35 | DRAW wave, console modes, macros | 64 → 68 → 74/74 | NES-triangle test window too early (test bug) |
| 36 | RECORD (WAV + MIDI) + JAM band | 75 → 77/77 | crash cymbal needs ~4 s to fade (test window) |
| 37 | BIT mascot + 4 colour themes (palette swap + live rebuild) | theme snapshots | – |
| 38 | User asked mid-way: hideable / movable boxes + VIEW menu | every section → `Box`; flow layout; window reshapes; layout saved | a PowerShell edit script carried on after an exception and deleted 141 lines (rebuilt from context; §4) |
| 39 | Cleanup + efficiency pass (§8b) | idle back to 1.0 speed, 3 save/load paths → 1, 0 warnings | benchmark noise from other apps (§4) |
| 40 | Mutation tests on the new code | 2/2 caught (drawing overwritten → 1 FAIL; knob changes ignored → 9 FAIL) | – |
| 41 | Release run for v1.1.0 | held back (correctly): Mac RECORD test dropped audio (writer buffer too small), Android app quit after 1.4 s | – |
| 42 | **User report: on an Android phone the UI is cut off - no scrolling, no resizing** | root cause: desktop window rules (minimum size, fixed shape) applied on a phone screen that is only ~411 pt tall | the most important bug of the release; found by the user, not by CI |
| 43 | Phone layout: keyboard + help pinned, page scrolls by swipe; phones never get a forced window size | UI test at 914 × 411 + phone snapshots; 11/11 | a desktop test window's shape lock and an async re-fit both interfered with the test (§4) |
| 44 | Release run retried: Android launch test hit a still-booting emulator; Windows MUTATE test failed once | launch test waits + retries (passes first try); MUTATE test now finds switches by type | two flaky tests, not app bugs: random + boot timing (§4) |
| 45 | **v1.1.0 released** (tag moved to the green commit 223d199 - no release existed yet) + reinstalled on the PC | 4/4 platforms green, release page = latest, Windows zip / macOS zip / Android APK uploaded; installed copies hash-match the build | – |
| 46 | 1.2 scope: user picked 9 of 12 ideas in a picker widget; defaults stated up front (mic + MIDI songs app-only, tilt off by default, branch `v1.2-features`) | – | one idea ("wobble + sweep") already existed in 1.0: re-scoped to sweep modes + vibrato shapes and said so |
| 47 | Sweep modes, vibrato shapes, Game Boy stereo (`ChannelFx` per side) | 89/89 | an `\n` in a Python-in-bash edit became a real newline (§4) |
| 48 | NotePlayer + GAME SOUNDS (8 effects, SAVE WAV, 4 jingles) | 94/94, focus 14/14 | `getSampleRate()` is 0 when you call `prepareToPlay` yourself (§4) |
| 49 | User asked mid-build: "an option to hide the dice icons?" -> VIEW > SHOW DICE BUTTONS; SHARE + sound codes | 96/96, focus 15/15 | a random test (BOOM echo tail) needed a longer window |
| 50 | VOICE wave, MicInput (app only), HumTracker (YIN) | 101/101 | **stack overflow**: the processor grew to ~100 KB, tests kept several on the stack (§4) |
| 51 | Branch CI #1: all 4 platforms build the mic code; Mac failed the new exact parameter count (AU adds no bypass/program) | test made format-aware | – |
| 52 | MIDI SONGS (arranger + song band), window opens <= 90% of the screen | 104/104 | – |
| 53 | Phone tricks: slide-to-bend, shake, tilt (Android Java listener via JUCE, iOS CoreMotion) | 105/105, focus 16/16; **emulator shake test PASS in CI** | the remembered MIDI pitch bend leaked across restarts - the 1.0 bug again, caught by the same tests |
| 54 | Cleanup + mutation tests (9 bugs planted, 9 caught) + docs | – | restoring a mutated file with its old timestamp made MSBuild keep the mutant (§4) |
| 55 | **User feedback: pasting a sound code gave no feedback** -> SHARE opens a box: live check ("Found: Siren" / typo / newer version), LOAD only when valid, LOADED! + a test note; copy shows the code | focus 17/17 (paste box test), 105/105 | text boxes report changes asynchronously: tests must pump the message loop |
| 56 | **v1.2.0 released**: tested commit pushed to `main` (fast-forward) + tagged, without switching branches (Play work kept uncommitted until then) | release run | a second run on the same branch was cancelled while the first was still cancelling (`concurrency` group); the watcher re-ran it |
| 57 | User: "available on the Google Play store?" -> researched current rules (API 36 since Aug 2026, 12 testers x 14 days for new personal accounts, 16 KB pages by Feb 2027), then built the Play kit on branch `play-store` | – | store screenshots first showed the *plugin* UI and an all-black feature graphic (Direct2D draws when the `Graphics` is destroyed) |

---

## 2. Prework / scope questions to ask *before* starting

These would have removed guesswork or round-trips. Ask them in **one** message, up front:

1. **Which DAW(s) do you use?** Drives the plugin formats:
   VST3 (almost everything), AU (Logic/GarageBand, **macOS only, needs a Mac to build**),
   CLAP (Bitwig, REAPER, FL Studio 2024+), VST2 (LMMS; the SDK is no longer distributable).
2. **Windows only, or Mac too?** A Mac build needs a Mac (or CI with macOS runners).
3. **Personal use, share, or sell?** JUCE is AGPLv3 or commercial (free Starter tier under a
   revenue cap). Decides license file, public/private repo, company/plugin codes.
4. **Authentic vs. easy?** Strict NES limits (mono, 4 channels) vs. a friendly polyphonic synth.
5. **MIDI keyboard?** If not, prioritise on-screen + computer-keyboard input.
6. **Will you be at the PC for admin prompts?** UAC prompts time out (MSI exit 1602).
7. **Disk space for ~8 GB of tools?**
8. **Name / branding?** Shows in every DAW plugin list.
9. **Where should it live?** (GitHub: public/private, account) Asked at the end here; better up front.
10. **Which free DAW to install for testing?** (Here: REAPER + Cakewalk + Audacity.)
11. **Will you share it, and with whom (their OS)?** Asked mid-project here. Knowing up front shapes
    packaging (zip + installer + plain-text readme), licensing (AGPL → ship source) and platform (Mac = AU build).
12. **How will you play it: mouse, computer keys, MIDI keyboard?** Computer-key playing turned out to
    matter a lot, and focus behaviour needed a dedicated fix and test.
13. **Which platforms, eventually: Windows / Mac / iPhone-iPad / Android?** Mac, iOS and Android were asked
    for after the Windows build. Knowing up front means touch-friendly UI, an app icon, and CI from day one.
14. **Public or private repo, and do you have an Apple Developer account ($99/yr) or an Android phone?**
    Decides CI cost (macOS minutes are 10× on private repos), the license, and whether iOS can be shared.
15. **Fixed layout or customisable?** Hideable / movable boxes were asked for after 9 features were built
    with absolute positions; every section had to be turned into a box. Building sections as self-contained
    components from day one makes this nearly free.
17. **Which phone / tablet will you use, and can you try a test build on it early?** An emulator smoke test
    only proves the app *starts*; it can't tell you a phone screen is unusable. The user's phone did.
16. **Which features belong in the app only?** (Jam, Record: a DAW already has tracks and recording.)
    Decide before the UI is laid out, so the plugin view doesn't show dead controls.

---

18. **Feature menus: check every idea against what already exists before showing it.** 1.2's list offered
    "wobble + pitch sweep", which BitBuddy had since 1.0. A 2-minute grep of the parameter list first.
20. **App stores: which ones, and whose developer account?** Google Play needs the owner's account ($25,
    ID check, 12 testers for 14 days), an upload key only they hold, a privacy policy and a public contact
    email. Ask before 1.0 if a store is likely: it changes signing (a stable key from day one) and permissions.
19. **Microphone / sensors: OK to ask for permissions?** Mic features add a permission prompt (phones, Mac);
    state when it appears (only on first use) before building.

## 3. Efficiency ideas (expedite completion)

**Do in parallel**
- Kick off the biggest install (VS Build Tools, ~6 GB) *first*, in the background, then design
  and write code while it downloads.
- Write code while JUCE compiles. The first build is ~5 min; later builds ~20 s.

**Avoid round-trips**
- **Never fire two admin prompts at once.** Sequence them and announce each one. Both UAC failures
  here were "cancelled": once timed out, once overlapping with installer prompts.
- Batch all scope questions (§2) into one message at the start.
- Check the API in the cloned framework's headers (grep) instead of relying on memory.

**Tooling defaults that just work on Windows**
- `cmake -G "Visual Studio 17 2022" -A x64` needs no "Developer Prompt" (Ninja + MSVC needs `vcvars64.bat`).
- Static MSVC runtime (`CMAKE_MSVC_RUNTIME_LIBRARY`): the plugin works on PCs without the VC++ redist.
- Python isn't guaranteed on Windows; PowerShell can do quick numeric checks.
- PowerShell + `gh --jq` with `\(...)` interpolation breaks quoting. Use `ConvertFrom-Json` instead.
- `winget` installs refresh PATH only for new shells. Re-read PATH from the registry in the same session.

**Verification without ears or eyes**
- **Headless test host that loads the real VST3** (JUCE `AudioPluginFormatManager`): renders offline,
  measures pitch/levels/silence. That's the closest thing to a DAW that can run unattended.
- **Render the editor to PNG** (`createComponentSnapshot`) for UI review without screen control.
- **Render a demo song** from MIDI parts: an end-to-end smoke test plus a deliverable.
- **Mutation-test the suite** (plant 2–3 bugs, confirm they're caught) before trusting "all green".

**Debugging heuristics that paid off here**
- Many failures with the same constant offset = one leaked state, not many bugs.
- Validate the measuring instrument (pitch detector) before "fixing" the product.
- Simulate the host's real timing (continuous audio blocks between UI actions).

---

## 4. Gotchas found

- **JUCE 8.0.15**: plugin hosting moved to the new `juce_audio_processors_headless` module;
  `AudioPluginFormatManager::addDefaultFormats()` → free function `juce::addDefaultFormatsToManager(fm)`.
  `JUCE_PLUGINHOST_VST3` defaults to 0 (set to 1 for a test host).
- JUCE 8 writer API: `createWriterFor (std::unique_ptr<OutputStream>&, AudioFormatWriterOptions)`.
- `FontOptions (const Typeface::Ptr&)` exists in JUCE 8. Use it for embedded fonts.
- A `COMPANY_NAME` with spaces → set `BUNDLE_ID` explicitly (CMake warning otherwise).
- VST3 hosts ignore a program change to the already-active program.
- While the transport is "playing", JUCE's VST3 wrapper applies controller-side parameter changes
  only via the next `process()` call (`vst3IsPlaying`).
- A hosted JUCE VST3 shows ~2,080 extra hidden MIDI-CC parameters (needed for mod wheel / pitch bend).
- NES noise LFSR: long mode period 32767, short ("metal") mode period **93** from seed 1, so clocking
  the short mode at 93 × f plays exactly in tune.
- MSI exit code **1602** = user cancelled (usually an unanswered or overlapping UAC prompt).
- REAPER scans the per-user VST3 folder (`%LOCALAPPDATA%\Programs\Common\VST3`); Cakewalk does not.
- LMMS hosts only VST2; Audacity hosts VST3 *effects* but not instruments.
- **JUCE keyboard focus:** `Button`, `ComboBox` etc. take keyboard focus when clicked. For an instrument
  that plays from letter keys, set `setWantsKeyboardFocus(false)` + `setMouseClickGrabsKeyboardFocus(false)`
  on everything except the piano, *after* `setLookAndFeel` (which recreates slider/combo text boxes).
  Only auto-refocus when your own window is active (`getPeer()->isFocused()`) and no menu is open
  (`ModalComponentManager::getNumModalComponents() == 0`), so DAW shortcuts aren't stolen.
- `ComponentPeer::handleMouseEvent` can simulate real clicks for UI tests (needs the window to be active).
- **Stale-binary trap in mutation testing:** restoring a file with `Copy-Item` keeps its *old* timestamp,
  so MSBuild skips the rebuild and you test the mutant again. Restore with `git checkout` (new mtime) or
  touch the file (`(Get-Item f).LastWriteTime = Get-Date`). A "fixed" run that fails identically to the
  mutant is the tell-tale sign.
- The Claude Code harness blocks wildcard `Remove-Item` on variable paths. Delete exact paths (`-LiteralPath`).

**Cross-platform + CI gotchas**
- **`git push` of `.github/workflows/*` fails with HTTP 403** if git's stored credential lacks the `workflow`
  scope, even when `gh` has it. Push with gh's token for that one command:
  `git -c credential.helper= -c "credential.helper=!gh auth git-credential" push`.
- **Private repos: macOS runner minutes count 10×** against the free 2,000/month. Public repos are free and
  unlimited. Decide visibility before turning on Mac/iOS CI.
- `concurrency: cancel-in-progress: true` means **a new push cancels the running CI run**. Wait for a run you
  care about before pushing again.
- **JUCE's CMake API does not support Android.** Use a Projucer `.jucer` that compiles the same `src/`.
  Build the Projucer from `external/JUCE` (`-DJUCE_BUILD_EXTRAS=ON`, target `Projucer`), run `--resave`
  (Linux CI: under `xvfb-run`).
- **A `.jucer` sharing CMake-style sources needs `useAppConfig="0"`.** CMake-style code includes JUCE modules
  directly; with the default AppConfig.h every file fails with *"No global header file was included!"*.
  `useAppConfig="0"` passes the same settings as compiler definitions, exactly like CMake.
- **Don't hard-code the Android launch activity** (`com.rmsl.juce.JuceActivity` doesn't exist in JUCE 8):
  ask `adb shell cmd package resolve-activity --brief -c android.intent.category.LAUNCHER <pkg>`.
- **JUCE CMake artefacts don't follow Xcode's paths:** the iOS app is in
  `build-ios/BitBuddy_artefacts/<config>/Standalone/BitBuddy.app` (no `iphonesimulator` in the path).
- **macOS runners can fail to start** ("not acquired by Runner … even after multiple attempts" = GitHub
  capacity, not your code). Just re-run; check the job's annotations before debugging anything.
- **PowerShell 5.1 mangles double quotes in native-command arguments** (`git commit -m "... \"x\" ..."`
  became stray pathspecs). Write the message to a file and use `git commit -F file`.
- **PowerShell variables are case-insensitive:** a loop variable `$h` silently overwrote `$H` (the home folder).
- **`System.Web.Script.Serialization` dictionaries need `.ContainsKey()`** in PowerShell (`.Contains()` is an
  explicit interface member and fails).
- **A script that edits files must stop on the first error** (`$ErrorActionPreference = 'Stop'` or an explicit
  `throw`): one carried on after a missing input file and replaced 141 lines with nothing. Recovered because
  the lines had just been read; otherwise `git checkout` the file.
- **Benchmarks are noisy when other apps are busy:** compare a change against a reference scenario measured in
  the *same* run (here: idle), and repeat 2-3 times before believing a 10% difference.
- **Test the plugin as the app:** `AudioProcessor::setTypeOfNextNewPlugin (wrapperType_Standalone)` before
  constructing the processor shows app-only UI (JAM / REC) in snapshots and UI tests.
- **Tests must not touch the user's settings:** BitBuddy's settings file (theme, layout) has a test override
  (`bb::useSettingsFile`).
- **Desktop window rules break phones:** `setResizeLimits` minimums and a fixed aspect ratio made the Android
  window taller than a sideways phone screen (~411 pt) - the keyboard was cut off and nothing scrolls. On
  iOS / Android never set a size or limits; fit the screen. For short screens use a scrolling page with the
  keyboard pinned (`Viewport` + `ScrollOnDragMode::nonHover` + `setViewportIgnoreDragFlag` on knobs).
- **Gradle downloads a Java 8 toolchain mid-build** for Projucer Android projects; that download can arrive
  corrupt ("Cannot expand TAR"). Install Java 8 *and* 21 with `actions/setup-java` up front.
- **Maven Central rate-limits GitHub's shared runners** (HTTP 429 during Gradle's dependency download):
  cache Gradle (`setup-java` `cache: gradle`) and retry the Gradle build a couple of times.
- **Smoke tests only prove "it starts"**: get the app onto a real device early for a usability check.
- **After changing the version, delete `build/BitBuddy_artefacts/JuceLibraryCode/BitBuddy_resources.rc`:** JUCE
  doesn't regenerate the Windows version resource, so the exe's file properties kept saying 1.0.1 (the plugin
  code and VST3 moduleinfo did say 1.1.0). Clean CI builds are fine.
- **A desktop test window's native title bar skews its outer size** under a fixed aspect ratio; check the
  constrainer's ratio instead of the window size.
- **Projucer's Android defaults request INTERNET, READ_MEDIA_*, storage and notification permissions.**
  Turn off `androidInternetNeeded`, `androidReadMedia*Permission`, `androidExternalWriteNeeded`,
  `androidPushNotifications` → zero permissions.
- **Generated `local.properties` contains the local SDK path (with the Windows username).** Never commit
  `android/Builds/` or `android/JuceLibraryCode/`; CI regenerates them.
- Projucer Gradle variants are `<flavor>_<buildType>`: the release APK task is **`assembleRelease_Release`**.
  Signing uses `~/.android/debug.keystore` (create it with `keytool` in CI).
- `ReactiveCircus/android-emulator-runner`'s `script:` runs **line by line**: put multi-step logic in a script file.
- **iOS works with CMake:** `-G Xcode -DCMAKE_SYSTEM_NAME=iOS`. Set `CMAKE_OSX_DEPLOYMENT_TARGET` /
  `CMAKE_OSX_ARCHITECTURES` *before* `project()` depending on `CMAKE_SYSTEM_NAME`; formats `AUv3 Standalone`;
  skip desktop-only targets (`return()`). Simulator builds need no signing (`CODE_SIGNING_ALLOWED=NO`).
- macOS AU testing in CI: copy the `.component` to `~/Library/Audio/Plug-Ins/Components`,
  `killall -9 AudioComponentRegistrar`, then `auval -v aumu <subtype> <manufacturer>`.
- Share zips: `git archive HEAD -- . ':(exclude)demo/*.wav'` avoids duplicating big assets in the source copy.
- **Random tests need limits that hold for every roll.** MUTATE checked knobs against a hand-kept list of
  switches; two new switches (Filter, Console) were missing, so a rare double hop (2 × 33 %) failed CI. Derive
  such lists from the code (here: every `AudioParameterChoice`) and run a random test several times locally.
- **A freshly booted CI emulator can drop the first app launch** (still busy with BOOT_COMPLETED jobs).
  Wait ~20 s, launch with `am start -W`, check `pidof`, retry up to 3 times.
- **Big objects on the stack crash the tests, not the app.** The voice sample (48 K atomics) made
  `BitBuddyProcessor` ~100 KB; the test function kept six on the stack -> exit code 0xC00000FD (stack
  overflow) before printing anything. Hosts create processors on the heap; tests must too (`make_unique`).
- **`getSampleRate()` is 0 if nobody called `setRateAndBufferSizeDetails`.** DAWs do; tests and private
  copies (SAVE WAV) only call `prepareToPlay`. Keep your own copy of the rate from `prepareToPlay`.
- **State that outlives `prepareToPlay` must be reset there.** Moving the MIDI pitch bend from the
  engine into the processor made a +2 st bend leak into every later test (the 1.0 bug, again).
- **Plugin formats add different parameters:** VST3 adds bypass + program (+2), AU adds none.
- **Changing a choice parameter's number of options** (9 -> 10 waves) changes its 0..1 value per option.
  Tests that set choices as fractions must compute them (`index / (count - 1)`); sound codes store
  choices as indexes for the same reason.
- **In Python run through Git Bash heredocs, backslashes are not reliable** (`\n` became a newline,
  `'\\'` broke the script). Write scripts / C++ snippets with the file tool, then run them.
- **Mutation testing: restore with a *new* timestamp.** `shutil.move` of the backup kept the old time,
  so MSBuild skipped the rebuild and the next batch still ran the mutants. Touch restored files.
- **The app's microphone without changing the plugin:** `StandalonePluginHolder::getInstance()` (only in
  the app build, `BB_STANDALONE_HOLDER=1`) -> switch the device's input on, add an extra
  `AudioIODeviceCallback`, restore the old setup afterwards. Include JUCE's processor/utils/gui_extra
  headers before `juce_StandaloneFilterWindow.h`.
- **CI signed every Android build with a new throwaway key**, so installing a newer GitHub APK over an older
  one failed ("App not installed"). A stable upload key (GitHub secret) fixes updates and is what Play needs.
- **Images drawn with `Graphics` on Windows (Direct2D) are only finished when the `Graphics` is destroyed:**
  save them after its scope ends, or you save a black picture.
- **Android sensors without Java files:** `JUCE_CORE_INCLUDE_JNI_HELPERS 1` (before the first JUCE
  include) + `AndroidInterfaceImplementer` + `CreateJavaInterface ("android/hardware/SensorEventListener")`.
  The emulator fakes motion: `adb emu sensor set acceleration x:y:z`.

---

## 5. Testing parameters (acceptance criteria)

Four layers. All automated and re-runnable (commands in README → Testing).

**Run everything:** `scripts\build-and-test.ps1` (build + all layers below, reports in `docs/reports/`).

**Layer 1: own test host** (`tests/TestHost.cpp --test`): loads the *built VST3* through
JUCE's VST3 hosting, exactly like a DAW, renders offline and measures the audio.

| Area | Criterion |
|---|---|
| Identity | name = BitBuddy, accepts MIDI, stereo out, 24–26 automatable params, 21 programs |
| Every preset (21) | output finite (no NaN/Inf), peak > −46 dB (makes sound), peak ≤ 0 dBFS, **tail < −60 dB** 8.5 s in (no stuck notes / runaway echo) |
| Tuning | YIN pitch detector, **±8 cents** for Square, Pulse 25/12.5, Triangle, Saw, Metal Noise |
| Sample rates / blocks | A4 correct at 48 k/64, 96 k/1024, 22.05 k/**37** (odd block size on purpose) |
| Controllers | pitch bend +2 st within ±10 cents; all-sound-off → < −60 dB |
| Chord arp | one key → exact semitone sequence 0,4,7,0,4,7 (±0.3 st) |
| Notes arp | C-E-G held, Up&Down, 2 oct → 60 64 67 72 76 79 76 72 67 64 |
| Mono legato | A2 → E3 lands on the right pitch |
| Drum kit | all 12 drums sound (> −34 dB) and decay below −60 dB within 4.5 s |
| Limiter | 8-note chord, vel 127, volume +6 dB → peak < 1.0 |
| Project save/restore | tweak a preset, save, load another, restore → tweaks + program name survive |
| CPU | 8-voice chords for 60 s → must be > 20× real time (measured ~85–93×, ≈1.1% of a core) |

**Layer 2: pluginval** (Tracktion, open source) at `--strictness-level 10`: 25 groups (open cold/warm,
programs, editor, editor-while-processing, processing at many rates/blocks, state restoration,
automation, parameter thread safety, buses, fuzzing). The Steinberg `vst3 validator` group is skipped
unless `--vst3validator <path>` is given (needs the full VST3 SDK; optional follow-up).

**Layer 3: visual review.** `--snapshot` renders 4 UI states (Hero Lead playing, Chord Arp, Drum Kit,
Help overlay) to PNG.

**Layer 4: mutation testing** (manual, ~2 min): plant a bug, rebuild, confirm the suite fails, `git checkout` to restore.

| Mutant | Caught by |
|---|---|
| Metal-noise clock 93× → 92× (detuned) | Tuning: Metal Noise |
| Pitch bend not reset on `prepare()` | CHORD arp, NOTES arp, MONO legato |
| Major chord built as minor | CHORD arp |

**Layer 5: interaction test** (`--focus-test`, added after a user bug report). Opens the editor as a
real window and simulates mouse clicks (wave button, knob, MONO, next preset, SURPRISE ME!, drop-down
menu open/close). Criteria: only the piano can take keyboard focus; keys work as soon as the window
opens; keys still work after each click; keys come back after a menu closes; the simulated SAW click
really selected Saw (proves clicks land). Prints SKIP if the window can't become active.

| Focus mutant | Result |
|---|---|
| Fix disabled (old behaviour) | 1/9 pass: all 8 focus checks fail, i.e. the user's bug is reproduced |
| Fix enabled | 9/9 pass |

**Not automated (needs a human):** listening for taste and balance, real-DAW UI behaviour
(resizing and key routing in each host), MIDI-keyboard feel.
*Lesson from the user's report:* automated tests checked sound and state but not "click then type".
For instruments, add an interaction test for the main play loop from day one.

---

## 6. Test results

| Run | Result | Notes |
|---|---|---|
| Build #1 | compiled first try (278 s, 1 warning) | design-first + API grep paid off |
| Tests run 1 | 35 / 43 | see root causes below |
| Tests run 2 | 42 / 44 | |
| Tests run 3+ | **44 / 44** | |
| pluginval L10 | **SUCCESS** (16 s) | |
| After cleanup pass | **46 / 46**, focus 9/9, pluginval OK | `docs/reports/` |
| Mutation | **3 / 3 caught** | |
| 1.1 (all features + cleanup) | **77 / 77**, focus + layout 10/10, 0 warnings | `docs/reports/` |
| 1.1 mutation | **2 / 2 caught** | |
| 1.2 (all features + cleanup) | **105 / 105**, focus + layout + phone 16/16, 0 warnings, CI 4/4 incl. emulator shake | `docs/reports/` |
| 1.2 mutation | **9 / 9 caught** | TRILL, WIDE, code checksum, HUM timing, NES voice, stuck notes, songs + Scale Lock, BOMB, voice trim |

Root causes of the run-1 failures:

1. **Real bug: controller state leaked across playback restarts.** Pitch bend from one test shifted every
   later test by +2 st. Fix: reset pitch bend + mod wheel in `prepare()`.
2. **Test-instrument bug: naive autocorrelation pitch detection** had octave errors on the smooth stepped
   triangle and the jittery metal noise. Fix: YIN.
3. **Test-method bug: re-selecting the active program is a no-op** in VST3 hosts. Fix: switch away first.
4. **Test-method bug: knob changes while "playing" are applied with the next audio block**, so saving
   with no block in between captured stale values. Fix: "pump" audio between steps like a DAW.
5. **Real improvement: big chords hit 0 dBFS.** Added a soft limiter (transparent below −2 dB, ceiling −0.18 dBFS).
6. Defensive: ignore a host program change to the already-active program (JUCE's wrapper already does,
   but some hosts resend the program number on project load).

---

## 7. Reusable checklist: next audio-plugin project

**Phase 0: prework (5 min, user present)**
- [ ] Ask the §2 questions in one message (formats/DAWs, OS, license/visibility, authenticity, input device, name, repo).
- [ ] Tell the user how many admin prompts to expect and when.

**Phase 1: toolchain (background)**
- [ ] `Get-Command git,cmake,cl,gh,python` + `vswhere -all`.
- [ ] Start the VS Build Tools install in the background (VCTools + Win11 SDK). CMake/Ninja via winget.
- [ ] Clone the framework (pin a tag) and grep it for APIs you'll use.

**Phase 2: design + code (while installing)**
- [ ] Engine as plain C++ (no framework types), so it's testable.
- [ ] Parameters with readable text, sensible skews, versioned IDs.
- [ ] Presets that each demonstrate one feature.
- [ ] UI: numbered workflow sections, hover-help bar, live visual feedback.
- [ ] Test host target in the same CMake project, from day one.

**Phase 3: build + verify**
- [ ] Full build → fix warnings.
- [ ] Own test host (`--test`) → triage failures: product bug vs. instrument bug vs. method bug.
- [ ] pluginval `--strictness-level 10`.
- [ ] UI snapshots → review.
- [ ] Interaction test of the main play loop (click controls, then play from the computer keyboard).
- [ ] Mutation-test 2–3 planted bugs (restore with `git checkout` or touch the file: stale-binary trap).
- [ ] Demo render (audio + MIDI parts).

**Phase 4: deliver**
- [ ] Install: per-user VST3 folder (no admin) + system folder (one admin prompt, alone).
- [ ] Install the DAW(s) the user chose; note which folders each scans.
- [ ] README + beginner guide + workflow notes; scan for personal info; `.gitignore` build output.
- [ ] Git: framework as a submodule, noreply commit identity, private repo unless told otherwise.
- [ ] Share package: zip with exe + plugin + double-click installer + plain-text readme + source (AGPL) + licenses.

**Rough timings this run:** tools ~15 min (mostly download) · code ~1 h · first build 5 min ·
test/fix loop ~15 min · pluginval 16 s · docs + install + GitHub ~15 min.

---

## 8. Cleanup + efficiency pass (measure → change → re-measure)

Benchmark = `TestHost --bench` (60 s of audio per scenario, best of 3, 44.1 kHz, block 512).

| Metric | Before | After | How |
|---|---|---|---|
| VST3 binary | 9.10 MB | **6.73 MB** | dropped unused JUCE codecs (FLAC, Ogg, WMA) + link-time optimisation |
| Standalone exe | 8.56 MB | **6.41 MB** | same |
| 8-voice chords | 1.088 % core | **0.816 %** | pitch maths at control rate (every 16 samples) + frequency cache |
| Lead + echo | 0.200 % | 0.194 % | (already cheap) |
| Drum pattern | 0.243 % | 0.224 % | |
| Idle, no notes | 0.117 % | **0.029 %** | engine early-out; skip all effects once tails are silent |
| Code | 2 near-identical button-group classes, repeated test boilerplate | 1 `ParamButtons` class; shared `renderEvents` / `findComponent` helpers | |
| Folders | scripts at root + `tools/`, 4 test-report locations, stray logs | `src/ tests/ scripts/ docs/reports/`, 1 screenshot, logs in `build/logs/` | |

**Caught by writing the test first:** the first idle-skip version treated the silent gap *before* a
dotted-eighth echo arrives as "finished" and cut the echo off. New test "Echo survives the silent gap";
mutant (naive idle) → echo −120 dB = FAIL; fix (stay awake for the echo's full delay length) → −24 dB = PASS.
*Lesson: every optimisation that skips work needs a test for the case where skipping is wrong.*

Not done (on purpose): dropping the static C++ runtime (+~0.3 MB, but needed so it runs on any PC);
disabling JUCE's hidden MIDI-CC parameters (needed for mod wheel / pitch bend in VST3).

### 8b. 1.1 cleanup + efficiency pass

| Metric | Before (1.1 features done) | After | How |
|---|---|---|---|
| Idle, no notes | 2,829× real time (0.035 %) | **~3,800×** (0.026 %, = 1.0.1) | engine settings re-read only when a knob / drawing / macro / tempo changes (parameter listener + dirty flag) instead of every block |
| 8-voice chords | ~108× (relative to idle) | ~15 % faster | one chord-shape lookup, a plain fast path (no chip mode, no macros), macro stepping moved out of line |
| Save / load of drawings + macros | 3 hand-written copies (project, your presets, factory presets) | **1** `writeExtras` / `readExtras` pair | |
| Small duplicates | `addFloat` + `addFloatV2`, 3 "choice index" formulas, 2 step-fill loops, BIT copying the audio a 2nd time, unused `JamBand::getStep` | 1 each / removed | |
| Test code | repeated preset lookups, PNG writing, a 2nd direct-render loop | `programIndex`, `writePng`, `renderDirect` | |
| Binary size | VST3 6.68 MB with all 1.1 features | same (1.0.1: 6.73 MB) | JUCE dominates; the features cost ~nothing |

*Lesson again: the "skip work when nothing changed" optimisation got its own mutation test (ignore knob
changes → 9 tests fail).*

### 8c. 1.2 cleanup + efficiency pass

| Change | Why |
|---|---|
| `ChannelFx`: DC blocker + filter + console tone + crunch as one unit, one per side | WIDE stereo needed a second chain; mono path unchanged |
| `NotePlayer`: one sample-accurate note list for sound FX, jingles and songs | three features, one player; no allocation on the audio thread |
| `bandBassSound()` shared by the JAM band and the song band | same sound, one definition |
| Tests: one `runOwn()` helper instead of five copies | shorter tests, one place to fix |
| Default voice saved as `"default"`, not 25 KB of base64 | projects stay ~2.7 KB |
| Idle cost re-measured: ~3600x real time (unchanged within noise) | new per-block checks are all cheap |

## 9. Fun-factor ideas: all built in 1.1

All nine are in 1.1, in this order (each enables the next): Scale Lock + Smart chords → piano scale
display + key letters → My Presets / dice / Mutate → C64 filter → DRAW wave → console modes → macros →
RECORD → JAM → BIT + themes, then (user request) hideable / movable boxes + VIEW.

## 10. Possible next steps

- ~~Run `install-plugin.ps1`~~: done; the retry alone, announced, succeeded. Lesson confirmed.
  The script now verifies the copy by hash (elevated processes don't report failure back).
- Steinberg VST3 validator (build from the full VST3 SDK, pass to pluginval `--vst3validator`).
- CLAP build via `clap-juce-extensions` (native in Bitwig/REAPER/FL Studio).
- ~~GitHub Actions CI~~, ~~GitHub Release~~, ~~user presets~~, ~~macro (tracker-style) editor~~: done.
- Share recordings from the desktop app (email / drag out), a recording "count-in".
- Jam: more styles, a song-structure mode (verse / chorus), MIDI export of the band.
- iOS: share via TestFlight (needs an Apple Developer account).
- 1.2 ideas not picked yet: **Song maker** (4-track step grid, export WAV/MIDI - reuse the JAM sequencer
  + NotePlayer), **Follow BIT** (falling-notes play-along game), **stickers + BIT outfits**.
- Try REC VOICE / HUM / shake / tilt on a real phone (CI covers the emulator shake and the pitch tracker
  with synthetic singing, not a real microphone).
