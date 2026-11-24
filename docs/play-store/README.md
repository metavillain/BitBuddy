# Putting BitBuddy on Google Play

Everything the build can do is done: CI makes a Google Play bundle (`BitBuddy-<version>-GooglePlay.aab`,
on every Release page next to the APK), BitBuddy targets Android 16 (API 36, Play's minimum since
31 Aug 2026), its native code is built for 16 KB memory pages, and the store pictures and texts are in this
folder. What's left are the steps only you can do: your Google account, your signing key, and the Play
Console forms. Do them in this order.

## 1. Make your upload key (once, ~5 minutes)

Google Play only accepts apps signed with *your* key. Until you add one, CI signs with a throwaway key.

1. Install Java (it includes the `keytool` program). In PowerShell:
   ```powershell
   winget install EclipseAdoptium.Temurin.21.JDK
   ```
   Then open a **new** PowerShell window.
2. Make the key. It asks you for a password: choose a strong one and **save it in your password manager**.
   ```powershell
   keytool -genkeypair -v -keystore "$HOME\bitbuddy-upload.keystore" -storetype PKCS12 -alias bitbuddy-upload -keyalg RSA -keysize 4096 -validity 10000 -dname "CN=BitBuddy Labs"
   ```
3. **Back up** `bitbuddy-upload.keystore` (from your user folder) somewhere safe, e.g. a USB stick or your
   cloud drive. If you ever lose it, Google can reset it, but that takes days.
4. Give it to GitHub's build machines as two secret settings (only the build can read them). The first
   command uploads the key file, the second asks you to type the password:
   ```powershell
   [Convert]::ToBase64String([IO.File]::ReadAllBytes("$HOME\bitbuddy-upload.keystore")) | gh secret set ANDROID_UPLOAD_KEYSTORE_BASE64 --repo metavillain/BitBuddy
   ```
   ```powershell
   gh secret set ANDROID_UPLOAD_KEYSTORE_PASSWORD --repo metavillain/BitBuddy
   ```

From the next release on, the APK and the Play bundle are both signed with your key. (Bonus: GitHub APKs
then update each other; until now each release had its own throwaway key, so Android made you uninstall
first. The Play Store version is signed by Google, so a phone has *either* the Play version *or* the GitHub
one.)

## 2. Your Google Play developer account (once)

- Sign up at <https://play.google.com/console/signup> as a **Personal** account: $25 once, plus an identity
  check (ID + a phone with Play Store).
- New personal accounts must run a **closed test with at least 12 testers for 14 days in a row** before
  the app can go public. Start collecting testers now: friends with an Android phone and a Google account.

## 3. Create the app in Play Console

**Create app** → name **BitBuddy: 8-bit Music Maker** (or your choice, max 30 characters) → *App* →
*Free*. Free can never be changed to paid later.

**Store listing** (Grow → Store presence → Main store listing):

| Field | What to use |
|---|---|
| Short description (80) | `Make 8-bit chiptune music, game sound effects and NES-style voices in seconds.` |
| Full description | the text in [listing.md](listing.md) |
| App icon (512 × 512) | [icon-512.png](icon-512.png) |
| Feature graphic (1024 × 500) | [feature-graphic.png](feature-graphic.png) |
| Phone screenshots | [phone-1-play.png](phone-1-play.png), [phone-2-chords.png](phone-2-chords.png), [phone-3-game-sounds.png](phone-3-game-sounds.png), [phone-4-gameboy-theme.png](phone-4-gameboy-theme.png) |
| Tablet screenshots | [tablet-1.png](tablet-1.png) (use it for 7" and 10") |
| Category | Music & Audio |
| Contact email | an address you're happy to show publicly (it's on the store page) |

(`check-tablet-portrait.png` is not for the store: it checks the layout on an upright tablet.)

**App content** (Policy → App content), suggested answers:

| Form | Answer |
|---|---|
| Privacy policy | `https://github.com/metavillain/BitBuddy/blob/main/docs/PRIVACY.md` |
| App access | All functionality is available without special access |
| Ads | No ads |
| Content rating | Questionnaire: category *Utility, Productivity, Communication or Other*; answer **No** to everything (no violence, no user chat, no sharing of location, no purchases) |
| Target audience | **13 and over** (younger ages bring the stricter Families rules) |
| Data safety | **No data collected or shared.** (Microphone audio is processed on the device and never leaves it, which Google doesn't count as "collected".) No encryption question applies, no account deletion needed |
| Government / financial / health / news app | No |

## 4. Closed test (14 days)

1. Testing → **Closed testing** → create a track → **Testers**: add your 12+ testers' Google account
   emails (or a Google Group).
2. **Create release** → upload `BitBuddy-<version>-GooglePlay.aab` from the newest
   [GitHub release](https://github.com/metavillain/BitBuddy/releases/latest) (made *after* step 1, so it's
   signed with your key). When asked, let **Google manage the app signing key** (Play App Signing).
3. Release notes: copy the "New in ..." part of the release page.
4. Send testers the **opt-in link** (shown on the track). They tap *Become a tester*, then install BitBuddy
   from the Play Store, and must **stay opted in for 14 days**.
5. After 14 days: Dashboard → **Apply for production** (a few questions about your test).

## 5. Every update after that

1. Raise the version: `VERSION` in `CMakeLists.txt`, `version` and `androidVersionCode` in
   `android/BitBuddy.jucer` (the version code must always go up: 1.3.0 → 130).
2. Push a tag (`v1.3.0`): CI tests everything and puts the signed `.aab` on the release page.
3. Play Console → your track → **Create new release** → upload the `.aab`.

## Checklist: what BitBuddy already does for Play

- [x] Android App Bundle (`.aab`) built by CI on every release
- [x] Targets API 36 (Android 16); minimum Android 7.0 (API 24)
- [x] 64-bit (arm64-v8a, x86_64) native code, 16 KB page aligned (NDK r28)
- [x] Permissions: microphone only (asked when you first press REC VOICE or HUM); no internet
- [x] Privacy policy ([PRIVACY.md](../PRIVACY.md))
- [x] Store pictures in this folder (made by `BitBuddyTestHost --play-assets docs/play-store assets/icon.png`)
- [ ] Upload key in GitHub secrets (step 1, you)
- [ ] Play developer account, closed test, forms (steps 2-4, you)
