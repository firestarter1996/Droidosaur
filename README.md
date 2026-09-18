# Droidosaur 🦖📱

**Pangea Software's Nanosaur, running on Android.** The app installs as "Droidosaur" (package `io.jor.nanosaur`); *Nanosaur* is Pangea's name for the original game, used here only to credit it.

This is a build-and-release wrapper around the Android port of [jorio/Nanosaur](https://github.com/jorio/Nanosaur)
that lives in [LachlanBWWright/Nanosaur-android](https://github.com/LachlanBWWright/Nanosaur-android)
(branch `copilot/update-android-port`: GLES3 bridge, touch controls, game data packed into the APK). That branch
never published an APK, so this repo builds it, signs it with a stable key and publishes it as a GitHub Release.

## Install / updates

- Grab the latest `Droidosaur-<version>-arm64.apk` from [Releases](../../releases) (arm64-v8a only).
- **Obtainium:** add this repo's URL as an app; it tracks the releases here.
- Every release is signed with the same key (cert SHA-256 `5e5de121b0c6d0088cc3ff71c9fc9577aae60f0b3d125216771486c651f882dd`),
  so updates install over each other. Package id: `io.jor.nanosaur`.

## How it's built

`.github/workflows/droidosaur-release.yml`: JDK 17, SDL 3.2.8, NDK r27, `gradlew assembleRelease` (arm64 only),
zipalign + apksigner, release. `sync-upstream.yml` merges the upstream port branch weekly and re-releases.

## Credits & license

Nanosaur © Pangea Software (Brian Greenstone); modern port by Iliyas Jorio; Android port by Lachlan Wright's
Copilot branch. See [LICENSE.md](LICENSE.md). This repo only adds packaging.
