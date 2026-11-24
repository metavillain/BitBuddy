## ✨ New in 1.2

- **Game sounds:** one tap makes a new COIN, JUMP, LASER, HIT, BOOM, POWER-UP, BLIP or BOMB -
  **SAVE WAV** for your own game - plus **jingles** (level clear, game over, got item, fanfare) in your key
- **Your voice on the NES:** record yourself (or load a WAV) and play it as the new **VOICE** wave -
  crunchy 1-bit on the NES chip, 4-bit on Game Boy
- **HUM:** hum or sing a tune and BitBuddy plays it with your sound (BitBuddy app)
- **MIDI songs:** open any `.mid` file and hear it chiptune style - melody on your sound,
  triangle bass, pulse chords, drum kit (BitBuddy app)
- **Sound codes:** **SHARE** a sound as a line of text; your friend pastes it and has the same sound
- **New movement + stereo:** trill / random vibrato, sweeps that keep going (falling bombs) or bounce
  (sirens), Game Boy-style **WIDE** stereo and a side-to-side **SWING**
- **Phone tricks:** slide up / down on a key to bend it, shake your phone for a new sound, tilt it for wobble
- **VIEW → Show dice buttons** to hide the dice; 4 new presets (Trill Lead, Stereo Arp, Falling Bomb, Siren)

## New in 1.1

- **Scale Lock** + **Smart chords**: pick a key and scale, and every note (and chord) fits
- **JAM**: a drums + bass + chords band in your key to play along with (BitBuddy app)
- **REC**: record what you play as WAV + MIDI (BitBuddy app)
- **DRAW your own wave** (Game Boy style) and **macros** (FamiTracker style) on the SCREEN
- **C64 filter**, **NES / Game Boy / C64 chip modes**
- **My Presets**, per-section **dice**, **MUTATE**
- **BIT** the dancing mascot, 4 colour themes, and boxes you can hide and move (**VIEW**)
- **Phones:** a new phone layout - the keyboard stays at the bottom, swipe to scroll through the rest

## 🎮 Download BitBuddy

| Your computer | Download |
|---|---|
| **Windows** 10 / 11 (64-bit) | **BitBuddy-…-Windows.zip** (below) |
| **Mac** (Apple Silicon or Intel, macOS 10.13+) | **BitBuddy-…-macOS.zip** (below) |
| **Android** 7.0+ phone or tablet | **BitBuddy-…-Android.apk** (below) |

Unzip it and open **READ ME FIRST.txt**. The short version:

- **Windows:** double-click `BitBuddy.exe` to play right away, or `Install BitBuddy.bat` to use it in
  your music software. If Windows says *"Windows protected your PC"*, click **More info → Run anyway**.
- **Mac:** double-click `Install BitBuddy.command`. If macOS blocks it, open
  **System Settings → Privacy & Security → Open Anyway** (macOS 14 and older: right-click → **Open**).
  Then find BitBuddy in GarageBand / Logic under *AU Instruments → BitBuddy Labs*.
- **Android:** open the `.apk` on your phone. Android asks to allow installing apps from your
  browser or Files app: tap **Settings → Allow from this source**, then **Install**. BitBuddy
  only asks for the microphone if you press REC VOICE or HUM. Turn your phone sideways and tap a
  control to see what it does.
- **iPhone / iPad:** coming later (Apple requires a paid developer account to share apps).

New to making music? The **[Beginner's Guide](https://github.com/metavillain/BitBuddy/blob/main/docs/BEGINNERS-GUIDE.md)**
takes you from zero to your first chiptune song.

## What you get

- The **BitBuddy app** (plays on its own, no music software needed)
- The **plugin** for music software: VST3 (Windows + Mac) and Audio Unit (Mac: GarageBand, Logic)
- 31 presets, a demo song with MIDI parts to remix, and the full source code

## Tested on every build

77 automated audio tests (tuning, arpeggios, Scale Lock, filter, chip modes, macros, Jam, recording,
presets, save/reload, CPU) + a UI test that clicks through the real window ·
[pluginval](https://github.com/Tracktion/pluginval) at its strictest level ·
Apple's `auval` Audio Unit validator on Mac.

BitBuddy is free software (GNU AGPLv3).
