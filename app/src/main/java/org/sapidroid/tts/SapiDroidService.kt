package org.sapidroid.tts

import android.speech.tts.SynthesisCallback
import android.speech.tts.SynthesisRequest
import android.speech.tts.TextToSpeech
import android.speech.tts.TextToSpeechService
import android.speech.tts.Voice
import android.util.Log
import java.util.concurrent.atomic.AtomicBoolean

/**
 * SapiDroid: Microsoft's OneCore voices (David, Zira) as an Android TTS engine.
 *
 * The engine runs in a private worker process, so a wedged native call is
 * cleared by replacing that process instead of taking this service down.
 */
class SapiDroidService : TextToSpeechService() {

    companion object {
        private const val TAG = "SapiDroid"
        private const val BUSY = SapiDroidWorkerService.BUSY
        // Small pulls so the first audio ships as soon as the first
        // sentence is ready; later pulls keep draining while synthesis runs.
        private const val PULL_BYTES = 16 * 1024
        private const val PULL_WAIT_MS = 100
        private const val SAY_WAIT_MS = 400
        private const val RETIRE_GRACE_MS = 1_500L
    }

    private val stopped = AtomicBoolean(false)

    @Volatile private var currentWorker: IZiraWorker? = null
    @Volatile private var currentRequest: Int = -1
    private lateinit var fallback: FallbackSpeech
    // A voice's output rate never changes, so remember it after the first
    // lookup and skip a binder round trip on every later utterance.
    private val rateCache = java.util.concurrent.ConcurrentHashMap<String, Int>()

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    override fun onCreate() {
        super.onCreate()
        fallback = FallbackSpeech(this, stopped)
        Thread {
            try {
                // One-time carry-over of voice data into device-protected
                // storage, so Direct Boot (lock screen) can read it.
                DeviceStorage.migrateIfNeeded(this)
                currentWorker = SapiDroidWorkers.acquire(this)
            } catch (t: Throwable) {
                Log.e(TAG, "engine worker warm-up failed", t)
            }
        }.apply { name = "sapidroid-worker-warmup" }.start()
    }

    override fun onDestroy() {
        val worker = currentWorker
        fallback.close()
        currentWorker = null
        SapiDroidWorkers.retire(worker)
        super.onDestroy()
    }

    override fun onStop() {
        stopped.set(true)
        fallback.stop()
        val worker = currentWorker ?: return
        val id = currentRequest
        if (id <= 0) return
        try {
            worker.cancel(id)
        } catch (t: Throwable) {
            Log.w(TAG, "cancel failed", t)
        }
        Thread {
            try {
                Thread.sleep(RETIRE_GRACE_MS)
            } catch (e: InterruptedException) {
                return@Thread
            }
            try {
                worker.retireIfStuck(id)
            } catch (t: Throwable) {
                Log.w(TAG, "worker retirement check failed", t)
            }
        }.apply { name = "sapidroid-retire-check" }.start()
    }

    // ------------------------------------------------------------------
    // Voices and languages
    // ------------------------------------------------------------------

    private fun installedVoices(): List<VoiceCatalog.Info> {
        DeviceStorage.migrateIfNeeded(this)
        return VoiceCatalog.installed(this)
    }

    override fun onGetVoices(): MutableList<Voice> = installedVoices().map { info ->
        Voice(
            info.label,
            info.locale,
            Voice.QUALITY_HIGH,
            Voice.LATENCY_NORMAL,
            false,
            emptySet<String>()
        )
    }.toMutableList().apply {
        if (isEmpty() && fallback.available()) add(fallback.voice)
    }

    override fun onIsValidVoiceName(voiceName: String?): Int {
        if (voiceName == FallbackSpeech.VOICE_NAME) {
            return if (fallback.available()) TextToSpeech.SUCCESS else TextToSpeech.ERROR
        }
        val info = VoiceCatalog.infoFor(voiceName) ?: return TextToSpeech.ERROR
        return if (installedVoices().any { it.label == info.label }) {
            TextToSpeech.SUCCESS
        } else {
            TextToSpeech.ERROR
        }
    }

    override fun onLoadVoice(voiceName: String?): Int {
        val result = onIsValidVoiceName(voiceName)
        if (result == TextToSpeech.SUCCESS && voiceName != null &&
            voiceName != FallbackSpeech.VOICE_NAME && !rateCache.containsKey(voiceName)) {
            // Warm the newly selected voice off this binder thread: pre-open
            // its handle and sample rate on the worker, so the next
            // utterance with it pays no file-load hitch and speaks at once.
            Thread {
                try {
                    var worker = currentWorker
                    if (worker == null || !worker.asBinder().isBinderAlive) {
                        worker = SapiDroidWorkers.acquire(this)
                        currentWorker = worker
                    }
                    val rate = worker?.sampleRate(voiceName) ?: 0
                    if (rate > 0) rateCache[voiceName] = rate
                } catch (t: Throwable) {
                    Log.w(TAG, "voice warm-up failed", t)
                }
            }.apply { name = "sapidroid-voice-warm" }.start()
        }
        return result
    }

    override fun onGetLanguage(): Array<String> = arrayOf("eng", "USA", "")

    override fun onIsLanguageAvailable(lang: String?, country: String?, variant: String?): Int =
        when {
            lang == null -> TextToSpeech.LANG_NOT_SUPPORTED
            !lang.startsWith("en", ignoreCase = true) -> TextToSpeech.LANG_NOT_SUPPORTED
            installedVoices().isEmpty() && !fallback.available() -> TextToSpeech.LANG_MISSING_DATA
            country.equals("US", ignoreCase = true) || country.equals("USA", ignoreCase = true) ->
                TextToSpeech.LANG_COUNTRY_AVAILABLE
            country.isNullOrEmpty() -> TextToSpeech.LANG_AVAILABLE
            else -> TextToSpeech.LANG_AVAILABLE
        }

    override fun onLoadLanguage(lang: String?, country: String?, variant: String?): Int =
        onIsLanguageAvailable(lang, country, variant)

    override fun onGetDefaultVoiceNameFor(lang: String?, country: String?, variant: String?): String {
        val installed = installedVoices()
        // An empty name is the platform's "no default"; it must not name a
        // voice that onIsValidVoiceName would then refuse.
        if (installed.isEmpty()) return if (fallback.available()) FallbackSpeech.VOICE_NAME else ""
        // The app's picker wins over the locale: every voice this engine has
        // shares the en-US frontend table, and a screen reader only sets a
        // language, so otherwise David would always speak.
        val name = VoiceCatalog.savedVoice(this, installed)?.label
            ?: VoiceCatalog.forLocale(lang, country, installed).label
        Log.i(TAG, "default voice for $lang-$country: $name")
        return name
    }

    // ------------------------------------------------------------------
    // Synthesis
    // ------------------------------------------------------------------

    override fun onSynthesizeText(request: SynthesisRequest?, callback: SynthesisCallback?) {
        if (request == null || callback == null) return
        if (request.params.getBoolean(FallbackSpeech.PARAM_DELEGATED, false)) {
            Log.e(TAG, "refusing recursive fallback synthesis")
            callback.error()
            return
        }
        stopped.set(false)
        try {
            synthesize(request, callback)
        } catch (t: Throwable) {
            Log.e(TAG, "synthesis failed", t)
            try {
                callback.error()
            } catch (t2: Throwable) {
                Log.e(TAG, "callback.error failed", t2)
            }
        }
    }

    private fun synthesize(request: SynthesisRequest, callback: SynthesisCallback) {
        val text = request.charSequenceText?.toString()?.trim().orEmpty()
        if (text.isEmpty()) {
            callback.start(VoiceCatalog.SAMPLE_RATE, android.media.AudioFormat.ENCODING_PCM_16BIT, 1)
            callback.done()
            return
        }

        val ratePercent = request.speechRate.takeIf { it > 0 } ?: 100
        val pitchPercent = request.pitch.takeIf { it > 0 } ?: 100
        val installed = installedVoices()
        val saved = VoiceCatalog.savedVoice(this, installed)
        val info = VoiceCatalog.resolveForSynthesis(
            installed, saved, request.voiceName, request.language, request.country
        )
        if (info == null) {
            if (!fallback.speak(text, ratePercent, pitchPercent, callback) && !stopped.get()) callback.error()
            return
        }
        val voice = info.label

        Log.i(TAG, "synth start voice=$voice requested=${request.voiceName} " +
            "engine=${info.engine} chars=${text.length} rate=$ratePercent pitch=$pitchPercent")

        var worker = currentWorker
        try {
            if (worker == null || !worker.asBinder().isBinderAlive) {
                worker = SapiDroidWorkers.acquire(this)
                currentWorker = worker
            }
        } catch (t: Throwable) {
            Log.e(TAG, "worker synthesis failed", t)
            callback.error()
            return
        }
        // Sample rate first: the worker caches the open handle, so this is
        // one binder round trip (cached after the first utterance) and lets
        // callback.start() run before any audio is synthesized instead of
        // after all of it.
        val rateHz = rateCache[voice] ?: try {
            worker?.sampleRate(voice) ?: 0
        } catch (t: Throwable) {
            Log.w(TAG, "voice sample-rate lookup failed", t)
            0
        }.takeIf { it > 0 }?.also { rateCache[voice] = it }
        if (rateHz == null) {
            Log.e(TAG, "voice $voice could not be opened; trying fallback")
            if (!fallback.speak(text, ratePercent, pitchPercent, callback) && !stopped.get()) callback.error()
            return
        }

        val ok = try {
            speak(worker!!, voice, ratePercent, pitchPercent, text, callback, rateHz)
        } catch (t: Throwable) {
            Log.e(TAG, "worker synthesis failed", t)
            false
        }
        if (stopped.get()) {
            try {
                callback.done()
            } catch (t: Throwable) {
                Log.w(TAG, "callback.done failed", t)
            }
            return
        }
        if (!ok) {
            try {
                callback.error()
            } catch (t: Throwable) {
                Log.w(TAG, "callback.error failed", t)
            }
        }
    }

    /** One utterance through the worker, streamed to the callback as it arrives. */
    private fun speak(
        startWorker: IZiraWorker,
        voice: String,
        ratePercent: Int,
        pitchPercent: Int,
        text: String,
        callback: SynthesisCallback,
        rateHz: Int
    ): Boolean {
        var worker = startWorker
        var replaced = false
        val delivered = AtomicBoolean(false)
        while (true) {
            try {
                return speakOnce(worker, voice, ratePercent, pitchPercent, text, callback, rateHz, delivered)
            } catch (e: WorkerGone) {
                if (!mayRetryWorker(replaced, stopped.get(), delivered.get())) return false
                replaced = true
                Log.w(TAG, "replacing engine worker: ${e.message}")
                worker = SapiDroidWorkers.replace(this, worker)
                currentWorker = worker
            }
        }
    }

    private class WorkerGone(message: String, cause: Throwable? = null) : Exception(message, cause)

    private fun speakOnce(
        worker: IZiraWorker,
        voice: String,
        ratePercent: Int,
        pitchPercent: Int,
        text: String,
        callback: SynthesisCallback,
        rateHz: Int,
        delivered: AtomicBoolean
    ): Boolean {
        val id = try {
            worker.say(voice, ratePercent, pitchPercent, text, SAY_WAIT_MS)
        } catch (t: Throwable) {
            SapiDroidWorkers.retire(worker)
            throw WorkerGone("say failed", t)
        }
        val requestId = if (id == BUSY) {
            // A transient overlap (e.g. flush racing the previous tail)
            // should not kill the worker process: one quick retry on the
            // same worker covers it; only a stuck engine retires it.
            try {
                Thread.sleep(120)
            } catch (e: InterruptedException) {
                Thread.currentThread().interrupt()
                throw WorkerGone("interrupted", e)
            }
            val retry = try {
                worker.say(voice, ratePercent, pitchPercent, text, SAY_WAIT_MS)
            } catch (t: Throwable) {
                SapiDroidWorkers.retire(worker)
                throw WorkerGone("say retry failed", t)
            }
            if (retry == BUSY) {
                SapiDroidWorkers.retire(worker)
                throw WorkerGone("engine stayed busy")
            }
            if (retry < 0) throw WorkerGone("say refused")
            retry
        } else {
            if (id < 0) throw WorkerGone("say refused")
            id
        }
        currentRequest = requestId
        var started = false
        var frames = 0L
        try {
            val maxBuf = callback.maxBufferSize
            if (maxBuf <= 0) {
                worker.cancel(requestId)
                return false
            }
            while (true) {
                if (stopped.get()) {
                    try {
                        worker.cancel(requestId)
                    } catch (t: Throwable) {
                        Log.w(TAG, "cancel failed", t)
                    }
                    return false
                }
                val block = try {
                    worker.pull(requestId, PULL_BYTES, PULL_WAIT_MS)
                } catch (t: Throwable) {
                    SapiDroidWorkers.retire(worker)
                    throw WorkerGone("pull failed", t)
                }
                if (block != null && block.isNotEmpty()) {
                    if (!started) {
                        if (callback.start(rateHz, android.media.AudioFormat.ENCODING_PCM_16BIT, 1) != TextToSpeech.SUCCESS) {
                            worker.cancel(requestId)
                            return false
                        }
                        delivered.set(true)
                        started = true
                    }
                    var off = 0
                    while (off < block.size) {
                        if (stopped.get()) {
                            try {
                                worker.cancel(requestId)
                            } catch (t: Throwable) {
                                Log.w(TAG, "cancel failed", t)
                            }
                            return false
                        }
                        val n = minOf(maxBuf, block.size - off)
                        if (callback.audioAvailable(block, off, n) != TextToSpeech.SUCCESS) {
                            worker.cancel(requestId)
                            return false
                        }
                        off += n
                    }
                    frames += block.size / 2L
                    continue
                }
                try {
                    if (worker.finished(requestId)) {
                        if (worker.failed(requestId)) return false
                        break
                    }
                } catch (t: Throwable) {
                    SapiDroidWorkers.retire(worker)
                    throw WorkerGone("finished failed", t)
                }
            }
        } finally {
            currentRequest = -1
        }
        if (!started) return false
        try {
            callback.done()
            Log.i(TAG, "request done voice=$voice frames=$frames sr=$rateHz")
        } catch (t: Throwable) {
            Log.e(TAG, "callback.done failed", t)
        }
        return true
    }
}
