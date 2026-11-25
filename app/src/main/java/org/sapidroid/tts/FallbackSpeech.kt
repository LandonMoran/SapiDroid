package org.sapidroid.tts

import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.speech.tts.SynthesisCallback
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import android.speech.tts.Voice
import android.util.Log
import java.io.File
import java.util.Locale
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/** Uses another installed engine, never its playback API, when our voice data is unavailable. */
internal class FallbackSpeech(private val context: Context, private val stopped: AtomicBoolean) {
    companion object {
        const val VOICE_NAME = "System fallback"
        const val PARAM_DELEGATED = "org.sapidroid.tts.delegated"
        private const val TAG = "SapiDroidFallback"
    }

    val voice = Voice(VOICE_NAME, Locale.US, Voice.QUALITY_NORMAL, Voice.LATENCY_NORMAL, false, emptySet())
    @Volatile private var engine: TextToSpeech? = null

    private fun candidates(): List<String> {
        val intent = Intent(TextToSpeech.Engine.INTENT_ACTION_TTS_SERVICE)
        return context.packageManager.queryIntentServices(intent, PackageManager.MATCH_DEFAULT_ONLY)
            .mapNotNull { it.serviceInfo }
            .filter { it.exported && it.enabled && it.packageName != context.packageName }
            .map { it.packageName }.distinct()
    }

    fun available(): Boolean = candidates().isNotEmpty()

    private fun acquire(): TextToSpeech? {
        engine?.let { return it }
        for (packageName in candidates()) {
            if (stopped.get()) return null
            val ready = CountDownLatch(1)
            var status = TextToSpeech.ERROR
            val candidate = TextToSpeech(context, {
                status = it
                ready.countDown()
            }, packageName)
            val initialized = ready.await(5, TimeUnit.SECONDS)
            if (initialized && status == TextToSpeech.SUCCESS && isExternal(candidate)) {
                engine = candidate
                return candidate
            }
            Log.w(TAG, "fallback engine $packageName could not initialize")
            candidate.shutdown()
        }
        return null
    }

    private fun isExternal(candidate: TextToSpeech): Boolean {
        val voices = candidate.voices ?: return false
        if (voices.isEmpty()) return false
        return voices.none { voice ->
            voice.name == VOICE_NAME || VoiceCatalog.all.any { it.label == voice.name }
        }
    }

    fun speak(text: String, rate: Int, pitch: Int, callback: SynthesisCallback): Boolean {
        var tts = acquire() ?: return false
        if (!isExternal(tts)) {
            tts.shutdown()
            engine = null
            tts = acquire() ?: return false
        }
        if (stopped.get()) return false
        val file = File.createTempFile("fallback-", ".wav", DeviceStorage.context(context).cacheDir)
        val done = CountDownLatch(1)
        val failed = AtomicBoolean(false)
        val started = AtomicBoolean(false)
        val delivered = AtomicBoolean(false)
        val active = AtomicBoolean(true)
        val id = "fallback-${System.nanoTime()}"
        val maxBuffer = callback.maxBufferSize
        try {
            if (maxBuffer <= 0) return false
            tts.setOnUtteranceProgressListener(object : UtteranceProgressListener() {
                override fun onStart(utteranceId: String?) {}
                override fun onBeginSynthesis(utteranceId: String?, sampleRateInHz: Int, audioFormat: Int, channelCount: Int) {
                    if (utteranceId != id || stopped.get() || !active.get()) return
                    if (callback.start(sampleRateInHz, audioFormat, channelCount) != TextToSpeech.SUCCESS) {
                        failed.set(true)
                    } else {
                        started.set(true)
                    }
                }
                override fun onAudioAvailable(utteranceId: String?, audio: ByteArray) {
                    if (utteranceId != id || stopped.get() || failed.get() || !active.get()) return
                    if (!started.get()) {
                        failed.set(true)
                        return
                    }
                    var offset = 0
                    while (offset < audio.size && !stopped.get()) {
                        val size = minOf(maxBuffer, audio.size - offset)
                        if (callback.audioAvailable(audio, offset, size) != TextToSpeech.SUCCESS) {
                            failed.set(true)
                            break
                        }
                        delivered.set(true)
                        offset += size
                    }
                }
                override fun onDone(utteranceId: String?) {
                    if (utteranceId == id) done.countDown()
                }
                override fun onError(utteranceId: String?) {
                    if (utteranceId != id) return
                    failed.set(true)
                    done.countDown()
                }
                override fun onStop(utteranceId: String?, interrupted: Boolean) {
                    if (utteranceId == id) done.countDown()
                }
            })
            tts.setSpeechRate(rate / 100f)
            tts.setPitch(pitch / 100f)
            val params = Bundle().apply { putBoolean(PARAM_DELEGATED, true) }
            if (tts.synthesizeToFile(text, params, file, id) != TextToSpeech.SUCCESS) return false
            val deadline = android.os.SystemClock.elapsedRealtime() + 120_000
            val startDeadline = android.os.SystemClock.elapsedRealtime() + 10_000
            while (!done.await(100, TimeUnit.MILLISECONDS)) {
                val now = android.os.SystemClock.elapsedRealtime()
                if (stopped.get() || failed.get() || now >= deadline || (!started.get() && now >= startDeadline)) {
                    if (!stopped.get()) Log.e(TAG, "fallback synthesis failed or timed out")
                    tts.stop()
                    return false
                }
            }
            if (stopped.get() || failed.get() || !started.get() || !delivered.get()) return false
            callback.done()
            return true
        } finally {
            active.set(false)
            if (!file.delete()) Log.w(TAG, "could not delete fallback audio file")
        }
    }

    fun stop() {
        engine?.stop()
    }

    fun close() {
        engine?.shutdown()
        engine = null
    }
}
