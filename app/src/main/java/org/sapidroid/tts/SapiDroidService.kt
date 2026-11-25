package org.sapidroid.tts

import android.os.Looper
import android.os.SystemClock
import android.speech.tts.SynthesisCallback
import android.speech.tts.SynthesisRequest
import android.speech.tts.TextToSpeech
import android.speech.tts.TextToSpeechService
import android.speech.tts.Voice
import android.util.Log
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
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
        // Small pulls deliver audio while synthesis is still running.
        private const val PULL_BYTES = 16 * 1024
        private const val PULL_WAIT_MS = 100
        private const val SAY_WAIT_MS = 400
        private const val RETIRE_GRACE_MS = 1_500L
    }

    private val stopped = AtomicBoolean(false)
    private val retirementChecks = Executors.newSingleThreadScheduledExecutor { runnable ->
        Thread(runnable, "sapidroid-retire-check")
    }

    @Volatile private var currentWorker: IZiraWorker? = null
    @Volatile private var currentRequest: Int = -1
    private val fallbackDelegate = lazy { FallbackSpeech(this, stopped) }
    private val fallback by fallbackDelegate
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
                // One-time carry-over of voice data into device-protected
                // storage, so Direct Boot (lock screen) can read it.
                DeviceStorage.migrateIfNeeded(this)
                val worker = SapiDroidWorkers.acquire(this)
                currentWorker = worker
                VoiceCatalog.savedInstalledVoice(this)?.let { voice ->
                    val rate = worker.sampleRate(voice.label)
                    if (rate > 0) rateCache[voice.label] = rate
                }
            } catch (t: Throwable) {
                Log.e(TAG, "engine worker warm-up failed", t)
            }
        }.apply { name = "sapidroid-worker-warmup" }.start()
    }

    override fun onDestroy() {
        synchronized(retirementChecks) { retirementChecks.shutdownNow() }
        val worker = currentWorker
        if (fallbackDelegate.isInitialized()) fallback.close()
        currentWorker = null
        SapiDroidWorkers.retire(worker)
        super.onDestroy()
    }

    override fun onStop() {
        stopped.set(true)
        if (fallbackDelegate.isInitialized()) fallback.stop()
        val worker = currentWorker ?: return
        val id = currentRequest
        if (id <= 0) return
        try {
            worker.cancel(id)
        } catch (t: Throwable) {
            Log.w(TAG, "cancel failed", t)
        }
        synchronized(retirementChecks) {
            if (!retirementChecks.isShutdown) {
                retirementChecks.schedule({
                    try {
                        worker.retireIfStuck(id)
                    } catch (t: Throwable) {
                        Log.w(TAG, "worker retirement check failed", t)
                    }
                }, RETIRE_GRACE_MS, TimeUnit.MILLISECONDS)
            }
        }
    }

    // ------------------------------------------------------------------
    // Voices and languages
    // ------------------------------------------------------------------

    private fun installedVoices(): List<VoiceCatalog.Info> {
        if (Looper.myLooper() != Looper.getMainLooper()) DeviceStorage.migrateIfNeeded(this)
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
        val startedAt = if (Log.isLoggable(TAG, Log.DEBUG)) SystemClock.elapsedRealtimeNanos() else 0L
        val text = request.charSequenceText?.toString()?.trim().orEmpty()
        if (text.isEmpty()) {
            callback.start(VoiceCatalog.SAMPLE_RATE, android.media.AudioFormat.ENCODING_PCM_16BIT, 1)
            callback.done()
            return
        }

        val ratePercent = request.speechRate.takeIf { it > 0 } ?: 100
        val pitchPercent = request.pitch.takeIf { it > 0 } ?: 100
        if (Looper.myLooper() != Looper.getMainLooper()) DeviceStorage.migrateIfNeeded(this)
        val info = VoiceCatalog.savedInstalledVoice(this) ?: run {
            val installed = installedVoices()
            val saved = VoiceCatalog.savedVoice(this, installed)
            VoiceCatalog.resolveForSynthesis(
                installed, saved, request.voiceName, request.language, request.country
            )
        }
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
            if (!fallback.speak(text, ratePercent, pitchPercent, callback) && !stopped.get()) callback.error()
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

        val delivered = AtomicBoolean(false)
        val ok = try {
            speak(worker!!, voice, ratePercent, pitchPercent, text, callback, rateHz, delivered, startedAt)
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
            if (!delivered.get() && fallback.speak(text, ratePercent, pitchPercent, callback)) return
            if (stopped.get()) return
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
        rateHz: Int,
        delivered: AtomicBoolean,
        startedAt: Long
    ): Boolean {
        var worker = startWorker
        var replaced = false
        while (true) {
            try {
                return speakOnce(worker, voice, ratePercent, pitchPercent, text, callback, rateHz, delivered, startedAt)
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
        delivered: AtomicBoolean,
        startedAt: Long
    ): Boolean {
        val id = try {
            worker.say(voice, ratePercent, pitchPercent, text, SAY_WAIT_MS)
        } catch (t: Throwable) {
            SapiDroidWorkers.retire(worker)
            throw WorkerGone("say failed", t)
        }
        val requestId = if (id == BUSY) {
            // Wait on the worker again instead of sleeping: it wakes as soon
            // as the interrupted request has left the native engine.
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
                        if (startedAt != 0L && frames == 0L && off == 0) {
                            val elapsedMs = (SystemClock.elapsedRealtimeNanos() - startedAt) / 1_000_000
                            Log.d(TAG, "first audio callback voice=$voice latency_ms=$elapsedMs")
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
