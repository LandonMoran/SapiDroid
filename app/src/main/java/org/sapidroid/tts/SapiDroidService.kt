package org.sapidroid.tts

import android.speech.tts.SynthesisCallback
import android.speech.tts.SynthesisRequest
import android.speech.tts.TextToSpeech
import android.speech.tts.TextToSpeechService
import android.speech.tts.Voice
import android.util.Log
import java.util.Locale
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
    // A voice's output rate never changes, so remember it after the first
    // lookup and skip a binder round trip on every later utterance.
    private val rateCache = java.util.concurrent.ConcurrentHashMap<String, Int>()

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    override fun onCreate() {
        super.onCreate()
        Thread {
            try {
                currentWorker = SapiDroidWorkers.acquire(this)
            } catch (t: Throwable) {
                Log.e(TAG, "engine worker warm-up failed", t)
            }
        }.apply { name = "sapidroid-worker-warmup" }.start()
    }

    override fun onDestroy() {
        val worker = currentWorker
        currentWorker = null
        SapiDroidWorkers.retire(worker)
        super.onDestroy()
    }

    override fun onStop() {
        stopped.set(true)
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
            val done = try {
                worker.finished(id)
            } catch (t: Throwable) {
                false
            }
            if (!done) {
                Log.w(TAG, "engine still busy after stop: retiring worker")
                SapiDroidWorkers.retire(worker)
            }
        }.apply { name = "sapidroid-retire-check" }.start()
    }

    // ------------------------------------------------------------------
    // Voices and languages
    // ------------------------------------------------------------------

    override fun onGetVoices(): MutableList<Voice> = VoiceCatalog.installed(this).map { info ->
        Voice(
            info.label,
            info.locale,
            Voice.QUALITY_HIGH,
            Voice.LATENCY_NORMAL,
            false,
            emptySet<String>()
        )
    }.toMutableList()

    override fun onIsValidVoiceName(voiceName: String?): Int {
        val info = VoiceCatalog.infoFor(voiceName) ?: return TextToSpeech.ERROR
        return if (VoiceCatalog.installed(this).any { it.label == info.label }) {
            TextToSpeech.SUCCESS
        } else {
            TextToSpeech.ERROR
        }
    }

    override fun onLoadVoice(voiceName: String?): Int {
        val result = onIsValidVoiceName(voiceName)
        if (result == TextToSpeech.SUCCESS && voiceName != null && !rateCache.containsKey(voiceName)) {
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
            country.equals("US", ignoreCase = true) || country.equals("USA", ignoreCase = true) ->
                TextToSpeech.LANG_COUNTRY_AVAILABLE
            country.isNullOrEmpty() -> TextToSpeech.LANG_AVAILABLE
            else -> TextToSpeech.LANG_AVAILABLE
        }

    override fun onLoadLanguage(lang: String?, country: String?, variant: String?): Int =
        onIsLanguageAvailable(lang, country, variant)

    override fun onGetDefaultVoiceNameFor(lang: String?, country: String?, variant: String?): String {
        val installed = VoiceCatalog.installed(this)
        // An empty name is the platform's "no default"; it must not name a
        // voice that onIsValidVoiceName would then refuse.
        if (installed.isEmpty()) return ""
        // The app's picker wins over the locale: every voice this engine has
        // shares the en-US frontend table, and a screen reader only sets a
        // language, so otherwise David would always speak.
        val name = VoiceCatalog.savedVoice(this)?.label
            ?: VoiceCatalog.forLocale(lang, country, installed).label
        Log.i(TAG, "default voice for $lang-$country: $name")
        return name
    }

    // ------------------------------------------------------------------
    // Synthesis
    // ------------------------------------------------------------------

    override fun onSynthesizeText(request: SynthesisRequest?, callback: SynthesisCallback?) {
        if (request == null || callback == null) return
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
            callback.done()
            return
        }

        val info = VoiceCatalog.infoFor(request.voiceName)
            ?: VoiceCatalog.savedVoice(this)
            ?: VoiceCatalog.forLocale(request.language, request.country, VoiceCatalog.installed(this))
        val voice = info.label
        val ratePercent = request.speechRate.takeIf { it > 0 } ?: 100
        val pitchPercent = request.pitch.takeIf { it > 0 } ?: 100

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
            0
        }.takeIf { it > 0 }?.also { rateCache[voice] = it }
            ?: VoiceCatalog.SAMPLE_RATE

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
        while (true) {
            try {
                return speakOnce(worker, voice, ratePercent, pitchPercent, text, callback, rateHz)
            } catch (e: WorkerGone) {
                if (replaced || stopped.get()) return false
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
        rateHz: Int
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
            val maxBuf = callback.maxBufferSize.coerceAtLeast(1024)
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
                        callback.start(rateHz, android.media.AudioFormat.ENCODING_PCM_16BIT, 1)
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
                        val n = minOf(maxBuf, block.size - off).coerceAtLeast(2)
                        if (callback.audioAvailable(block, off, n) != TextToSpeech.SUCCESS) break
                        off += n
                    }
                    frames += block.size / 2L
                    continue
                }
                try {
                    if (worker.finished(requestId)) break
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
