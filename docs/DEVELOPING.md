# Developing SapiDroid

This guide is for someone opening the repository for the first time. It covers
what is where, how to build it, how to run the tests, and how to poke at a
running engine. For how the pieces fit together, read
[ARCHITECTURE.md](ARCHITECTURE.md) first.

## What you need

- **JDK 17.** Android Gradle Plugin 8.5.2 is built for JDK 17; newer JDKs fail
  the build. The full Windows walk-through, including `JAVA_HOME`, is in the
  README.
- **Android SDK** with platform 34, build-tools 34.0.0, NDK 28.0.13004108 and
  CMake 3.22.1 — the versions `app/build.gradle.kts` pins.
- **No voice data.** Microsoft's voice files are not in this repository and must
  not be added to it. The app downloads them at the user's request, into its own
  private storage; the README explains where each set comes from.

## Build and install

```bash
./gradlew :app:assembleDebug           # app/build/outputs/apk/debug/app-debug.apk
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

On Windows use `.\gradlew.bat`. Debug builds are signed with a per-machine key,
so a copy installed from someone else's build needs `adb uninstall` first —
which also deletes its downloaded voices.

The first build compiles all four native engines and takes several minutes. The
APK contains `arm64-v8a` and `armeabi-v7a`; `x86_64` is added only when you pass
`-PemulatorTests`.

## Repository map

```
app/src/main/java/org/sapidroid/tts/   Kotlin: TTS service, worker, catalogue, downloads
app/src/main/aidl/…/IZiraWorker.aidl   the service-to-worker binder contract
app/src/main/cpp/                      JNI wrappers, Anna and SAPI 5 engines, vendored SAPI 4
engine/                                OneCore engine (David, Zira, Mark, …)
app/src/test/java/                     JVM unit tests
app/src/test/native/                   native host tests and one small JVM/native harness
app/src/androidTest/java/              instrumentation tests (run on an emulator)
engine/tests/                          native host tests for the OneCore engine
.github/workflows/android.yml          CI: build, host tests, emulator test
```

A rough reading order for the Kotlin side: `SapiDroidService.kt` (what Android
sees) → `IZiraWorker.aidl` (the contract) → `SapiDroidWorkerService.kt` (the
engine process) → `VoiceCatalog.kt` (which voices exist) → `DeviceStorage.kt`.

## Tests

There are three kinds, and CI runs all of them.

**JVM unit tests** — fast, no device needed:

```bash
./gradlew :app:testDebugUnitTest
```

They cover text splitting, speech-rate mapping, catalog/voice selection, worker
retry rules, and the cabinet extractor (`StreamingTextTest`, `SpeechRatesTest`,
`VoiceSelectionTest`, `WorkerRetryTest`, `CabExtractorTest`).

**Native host tests** — the engines compiled on the host with small synthetic
harnesses. The canonical commands are the `Test native engine fast paths` and
`Test JNI PCM streaming` steps in `.github/workflows/android.yml`; copy the
`cc` line for the test you are interested in from there. They are ordinary C,
for example:

```bash
cc -std=c99 -O2 -Iapp/src/main/cpp/sapi4/src -Iapp/src/main/cpp/sapi4/port \
  -ffunction-sections -fdata-sections app/src/test/native/sapi4_queue_test.c \
  -Wl,--gc-sections -lm -pthread -o /tmp/sapi4_queue_test
/tmp/sapi4_queue_test
```

Several of them need a specific compile mode to be meaningful: the PCM test is
built twice (native-endian and with `__BYTE_ORDER__` undefined, to exercise the
portable path), and the cancellation test is built with
`-ffp-contract=off -fno-fast-math` because that is how the engine ships.

**Instrumentation tests** — Android TTS plumbing, worker drain, fallback and
storage, run on an emulator:

```bash
./gradlew :app:connectedDebugAndroidTest -PemulatorTests
```

`TestSpeechService.java` is a stub engine the fallback tests point at, so they do
not need a real second engine installed.

## CI

`.github/workflows/android.yml` has two jobs:

- **build** — compiles and runs the native host tests, then
  `:app:testDebugUnitTest :app:assembleDebug`, and uploads the debug APK as an
  artifact. It runs on every push and pull request and does not need an
  emulator.
- **fallback-on-emulator** — boots an API 34 x86_64 AVD and runs
  `connectedDebugAndroidTest -PemulatorTests`.

A green build job means the project compiles everywhere; it does not mean the
speech sounds right. Nobody on CI has Microsoft's voice data, so real-voice
output is verified by hand on a device.

## Poking at a running engine

Logcat tags, all under `adb logcat`:

| Tag | What it shows |
| --- | --- |
| `SapiDroid` | synthesis requests, default-voice resolution, worker failures |
| `SapiDroidWorker` | voice opening, request handling, cancellation, retirement |
| `SapiDroidWorkers` | binder lifecycle between service and worker process |
| `SapiDroidStorage` | Direct Boot migration |
| `SapiDroidFallback` | borrowing another TTS engine |
| `SapiDroidData` | voice-data downloads and extraction |
| `SapiDroidUi` | settings screen and debug-intent handling |

Debug builds accept intents that skip the UI:

```bash
# Speak a line on launch, optionally with a specific voice
adb shell am start -n org.sapidroid.tts/.MainActivity \
  --es autospeak "Testing one two three" --es voice "Zira (OneCore)"

# Run a voice-data download directly
adb shell am start -n org.sapidroid.tts/.MainActivity --es getdata sapi5
adb shell am start -n org.sapidroid.tts/.MainActivity --es getdata onecore --es voice George
```

Text beginning with `$HANG` makes a debug worker sleep for two minutes, which is
the easiest way to see a wedged worker get retired and replaced. Synthesis logs
(`synth start`, `request done voice=… frames=… sr=…`) are written at debug level,
so `adb shell setprop log.tag.SapiDroid DEBUG` turns them on.

## Common tasks

**Add a voice to an existing engine.** Add a `VoiceCatalog.Info` entry with the
engine, the engine's internal name, a label and a locale. If it comes from
Sapple's bundle, add a `OneCorePacks.Pack`; that is also where the bundle's
prefix (`M1033…`, `M2057…`, `M3081…`, `M4105…`) is mapped. Check that the
`installed()` file list still matches what the engine needs.

**Add an engine family.** You need: the C sources in `app/src/main/cpp/`, a JNI
wrapper, the sources and include paths in `CMakeLists.txt`, a Kotlin `external`
object like `NativeZira.kt`, a `VoiceCatalog.Engine` value with an `Info` list,
a files-on-disk check in `VoiceCatalog.InstalledData`, and cases in
`SapiDroidWorkerService` for opening, synthesizing, cancelling and reporting the
sample rate.

**Change how text is split.** `StreamingText.kt` — only sentence boundaries,
newlines, a 20-character minimum and a 450-character hard limit are used today.
`StreamingTextTest.kt` pins the behaviour, including surrogate pairs.

**Touch the native code.** Keep the output format (mono, 16-bit, little-endian,
one of the engine's own sample rates) and keep the cancellation checks between
units. Do not add fast-math or floating-point contraction flags to the OneCore
build; bit-exactness depends on them being off.

**Cut a release.** The release is built and published by
`.github/workflows/release.yml`. Bump `versionCode` and `versionName` in
`app/build.gradle.kts` (a local build uses them; the workflow overrides both
from the tag), make sure CI is green, then push a tag:

```bash
git tag v0.11.0
git push origin v0.11.0
```

The workflow decodes the release keystore from repository secrets, builds
`:app:assembleRelease` with `-PabiSplits` (one APK per ABI plus a universal
APK), sets `versionName` to the tag without its `v` and `versionCode` to the
commit count, checks each APK's signing certificate, and attaches the APKs to
the GitHub release for that tag. `workflow_dispatch` rebuilds an existing tag.

It needs these repository secrets (Settings → Secrets and variables → Actions):

| Secret | What it is |
| --- | --- |
| `KEYSTORE_BASE64` | the release keystore, base64-encoded |
| `KEYSTORE_PASSWORD` | the keystore password |
| `KEY_ALIAS` | the key alias inside it |
| `KEY_PASSWORD` | the key password |
| `EXPECTED_CERT_SHA256` | optional: the certificate's SHA-256, so a wrong keystore fails the build |

Every release must use the same key as the previous one: a build signed with a
different key has to be installed after an uninstall, and uninstalling deletes
the user's voice data. That is what `EXPECTED_CERT_SHA256` guards; set it once
to the certificate of the first release and the workflow refuses anything else.
The ABI splits keep each download to the single architecture the phone uses,
with a universal APK for anyone who is unsure.

## Benchmarking the engines

`sh bench/run.sh` clones the original `devinprater/SapiDroid` into `/tmp` and builds three variants
with the same compiler: the original at the `-O2` it shipped with, the original at this repository's
flags (`-O3 -fno-math-errno`), and this repository. It measures 41 kernels across all four engines
(every DSP leaf, the LPC/LSF stages, both post-filters, the FFTs, the transcendental emulations, the
voice effects, Sonics and the vocoder) and prints ns per sample or per call with an FNV digest of
each output, so the report shows whether the three builds agree bit for bit. Nothing is written
inside the repository.

Read the numbers with these caveats, all of which have bitten this work:

- **Only a paired A/B proves a change.** The clock on a shared machine moves between states about
  10% apart and code placement shifts timings by a similar amount, so the same source can read
  376 ns in one build and 393 ns in another. The runner interleaves the variants and the report uses
the median, but a single-kernel change still needs a dedicated before/after build run alternately
  (15 rounds is enough to settle a 5% effect).
- **A compiler flag change is not a code change.** Several of the largest ratios in the report are
  `-O2` versus `-O3` on unchanged source (Anna's FIR, OneCore's Sonic, SAPI 4's scaling loops).
  Keep the two columns apart when deciding what to fix.
- **Kernel time is not speech time.** The per-sample vocoders cost roughly 0.5 ms per second of
  audio, so a 10% kernel win is invisible next to frontend parsing, voice-data reads and the JNI
  round trip. Measure those before claiming an end-to-end improvement.
- The host numbers come from x86-64 GCC; the app is Clang for ARM. They compare code fairly, they
  are not device timings.

## Gotchas

- **Voice data is not source.** Never commit `.vce`, `.spd`, `M1033*` files or
  Microsoft installers, and do not add them to an APK.
- **Do not bind the worker from the main thread.** `SapiDroidWorkers.acquire`
  checks for it deliberately; the bind also times out after 20 s.
- **Direct Boot.** Anything the engine reads during synthesis must live in
  device-protected storage (`DeviceStorage.context()`), or it disappears on the
  lock screen.
- **Cancellation is cooperative.** `cancel()` asks the engine to stop; only
  `shutdown()` (which kills the worker process) stops a call that will not
  return. Do not add `Thread.interrupt()`-style fixes inside native code.
- **One request at a time.** The worker has one synthesis thread and a 512 KiB
  audio queue. Anything that streams audio must drain the queue or the
  synthesizer blocks.
