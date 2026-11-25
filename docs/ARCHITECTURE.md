# SapiDroid architecture

This document is a map of how SapiDroid works. It assumes you have read the
README once and want to know where things live and why they are shaped the way
they are.

SapiDroid is an Android system TTS engine. Android's own `TextToSpeech` API
calls into it; it synthesizes speech with vendored C reconstructions of four
Microsoft speech engines and streams the resulting PCM back to whoever asked
(TalkBack, a reading app, the test line in SapiDroid's own settings screen).

There are three layers:

| Layer | Code | Runs in |
| --- | --- | --- |
| Android TTS service | `app/src/main/java/org/sapidroid/tts/SapiDroidService.kt` | the app's main process |
| Engine worker | `SapiDroidWorkerService.kt`, `SapiDroidWorkers.kt`, `IZiraWorker.aidl` | the private `:engine` process |
| Native engines | `app/src/main/cpp/`, `engine/` | `libsapidroid.so` inside `:engine` |

Everything below is about how those three talk to each other.

## Why a second process

The Microsoft engines are large C programs that block inside `speak()` until
they are done or asked to stop. A bug or a deadlock there would take the TTS
service — and Android's connection to it — down with it.

So synthesis lives in `SapiDroidWorkerService`, declared with
`android:process=":engine"` and never exported. If the worker stops answering,
the main process kills it (`IZiraWorker.shutdown()`) and binds a fresh one, and
the next utterance starts clean. That is the central design rule: **native code
is never interrupted from the inside; a stuck worker is replaced from the
outside.**

`SapiDroidWorkers` owns the binding: it refuses to bind on the main thread,
waits up to 20 s for a connection, watches with `linkToDeath`, and tears the
old connection down before creating a new one.

## Request lifecycle

1. Android calls `onSynthesizeText(request, callback)` on a binder thread.
2. The service picks a voice (see *Voice selection*), then asks the worker for
   that voice's sample rate. This is one binder call and is cached per voice,
   so `callback.start()` can run before synthesis begins rather than after the
   whole utterance is ready.
3. `IZiraWorker.say(...)` hands the text to the worker, which submits it to a
   single-thread executor (`sapidroid-engine`). The worker allows one request
   at a time; if the previous one is still finishing, `say` waits up to 400 ms
   and then answers `BUSY`.
4. For long text, `StreamingText.forEachChunk` splits the utterance into
   sentence-sized pieces and synthesizes them one after another. Each chunk's
   PCM is pushed from native code through a `PcmSink` into a queue in the
   worker (`pending`, capped at 512 KiB).
5. The service pulls audio with `pull(requestId, 16 KiB, 100 ms)` and forwards
   it to Android with `audioAvailable()`. Pulling with a short wait keeps audio
   moving while synthesis is still running, instead of waiting for the whole
   utterance.
6. `finished(requestId)` is true once the worker is done *and* the queue has
   drained; `failed(requestId)` reports a synthesis failure. The service then
   calls `callback.done()`.

All audio is mono, 16-bit, little-endian PCM. Sample rates come from the
engine: OneCore and Anna are 16 kHz, SAPI 4 and SAPI 5 report their own rate
per voice.

## Cancellation and recovery

Stopping is deliberately layered:

- `onStop()` marks a flag, calls `IZiraWorker.cancel(requestId)`, and schedules
  a check 1.5 s later.
- `cancel` clears queued audio and calls the engine's own thread-safe stop for
  the currently open voice. The native synthesizer checks that flag between
  units, so ordinary interruption is quick.
- If the request is *still* running after the grace period,
  `retireIfStuck(requestId)` kills the worker process.
- The pull loop checks the cancel flag between blocks and between writes, so a
  long piece stops even when the engine is between callbacks.

A failed worker is replaced only when it is safe: `mayRetryWorker` allows a
retry only when no worker has been replaced yet in this utterance, nothing was
stopped, and no audio has reached Android. That last condition is what prevents
duplicate speech after playback has started.

## Voice selection

`VoiceCatalog` is the single list of voices this build knows how to speak with,
across all four engines. Each entry carries an engine, an internal name (what
the native code expects), a user-facing label such as `David (OneCore)`, and a
locale.

`installed()` filters that list by what is actually on disk; the picker, the
system voice list and synthesis all use it, so a voice can never be offered
without its files. The checks are per engine:

- **OneCore** — the shared `MSTTSLocEnUS.dat` plus the voice's `.APM` and
  `.INI`.
- **Anna** — all eleven `M1033DSK.*` files, including the audio key and pitch
  table.
- **SAPI 4** — `msttssyn.dll` plus the three `.vce` and three `.cfg` files.
- **SAPI 5** — both lexicons plus the voice's `.spd` and `.sdf`.

The voice chosen in the app is the engine's *default* voice, and it is what
screen readers get: TalkBack sets a language and takes whatever default the
engine returns. `onGetDefaultVoiceNameFor` therefore prefers the saved picker
choice over the requested locale. The choice is cached in memory (so the next
utterance sees it immediately) and written to device-protected storage (so it
survives a reboot before unlock).

`onLoadVoice` also pre-warms the newly selected voice on a background thread,
opening its handle and caching the sample rate so the first utterance with it
does not pay a file-load hitch.

## Text handling

`StreamingText` splits at `.`, `!` or `?` followed by whitespace, or at a
newline, with a 20-character minimum; text with no boundary is hard-split at
450 characters on a word boundary, and surrogate pairs are kept intact. An
utterance of 350 characters or fewer is passed through whole.

SAPI 4 is the one engine that does not take UTF-8: the worker encodes each
chunk as windows-1252 before calling it.

## Rate and pitch

Android sends percentages, the engines expect their own scales.

- `sapiRate(percent, max)` maps 100 % to 0 and uses
  `10 * ln(percent/100) / ln(3)`, clamped to `-10..max`. OneCore allows up to
  20, Anna and SAPI 5 up to 18, which is what makes the extended fast rates
  possible.
- `sapi5Pitch(percent)` maps a ratio to `-10..10`, one unit per half semitone.
- SAPI 4 is different: each mode reports its own default, minimum and maximum
  (`NativeSapi4.limits`), and the worker scales within those.

## Storage and Direct Boot

The engine is `directBootAware`, so it can speak on the lock screen after a
reboot, before the user unlocks. Credential-protected storage is sealed until
that unlock, so all voice data and the default-voice preference live in
**device-protected storage** (`DeviceStorage.context()`), in `files/voice`,
`files/anna`, `files/sapi4` and `files/sapi5`.

Existing installs are migrated once: `DeviceStorage.migrateIfNeeded` moves the
four voice folders and the saved voice from the old `filesDir`, writes a marker
in preferences, and never throws. If the device is still locked the migration
is deferred and retried on the next launch.

## Fallback and voice data downloads

If no installed voice can be opened, or synthesis fails before any audio is
delivered, `FallbackSpeech` borrows another installed Android TTS engine. It
enumerates services that answer `INTENT_ACTION_TTS_SERVICE`, initializes each
candidate, checks that it is not SapiDroid itself, and synthesizes to a
temporary WAV, forwarding `onAudioAvailable` to our own callback. A
`delegated` parameter guards against recursion.

The user brings the Microsoft files themselves. `VoiceDataInstaller` fetches
Microsoft's Speech SDK 5.1 from the Internet Archive, walks the self-extractor's
local ZIP headers for `Data.Cab`, and extracts the eight SAPI 5 files with
`CabExtractor`. It also downloads the seven SAPI 4 files from the unpacked
GitHub copy and verifies each against its SHA-256 and size. `SappleVoices`
downloads Sapple's 200 MB bundle once per app session for Anna and the English
OneCore voices, and `OneCorePacks` maps each offered voice to the name its files
carry in that bundle.

These downloads are opt-in, announced before they start, and never bundled with
the app.

## Native engines at a glance

All four are compiled into one shared library, `libsapidroid.so`, with a thin
JNI wrapper per engine (`sapidroid_jni.c` for OneCore, `sapidroid_anna_jni.c`,
`sapidroid_sapi4_jni.c`, `sapidroid_sapi5_jni.c`).

| Engine | Sources | Reads |
| --- | --- | --- |
| OneCore (David, Zira, Mark and more) | `engine/` (`zf1_*` frontend, `zf2_*` features/prosody, `zb_*` synthesizer/vocoder) | `M1033<Voice>.{APM,INI}` + `MSTTSLocEnUS.dat` |
| Anna (Vista/7) | `app/src/main/cpp/anna/` | `M1033DSK.*`, including the WMA Voice 9 decoder's tables |
| SAPI 4 (Sam, Mike, Mary + 18 modes) | `app/src/main/cpp/sapi4/` | `msttssyn.dll`, `.vce` and `.cfg` |
| SAPI 5 (Sam, Mike, Mary) | `app/src/main/cpp/sapi5/` | `*.spd`, `*.sdf`, two lexicons |

The OneCore build is compiled with `-ffp-contract=off` and without fast math on
purpose: the output is bit-exact against Microsoft's engine only when the
compiler is not allowed to fuse floating-point operations. Do not relax those
flags to chase speed.

## Where to change what

- **A new voice on an existing engine** — add it to `VoiceCatalog` (and to
  `OneCorePacks` if it can be downloaded), then make sure `installed()` knows
  which files it needs.
- **A new engine family** — add a JNI wrapper and CMake sources, an entry to
  `VoiceCatalog.Engine`, a case in `synthChunk`/`sampleRate`/`cancel` in the
  worker, and a handle map for it.
- **Text splitting** — `StreamingText.kt`, with tests in `StreamingTextTest.kt`.
- **Rate or pitch mapping** — `SpeechRates.kt`, or the SAPI 4 `limits` path in
  `SapiDroidWorkerService.synthChunk`.
- **What Android sees** — `SapiDroidService` (`onGetVoices`,
  `onGetDefaultVoiceNameFor`, `onSynthesizeText`).

See [DEVELOPING.md](DEVELOPING.md) for building, testing and debugging.
