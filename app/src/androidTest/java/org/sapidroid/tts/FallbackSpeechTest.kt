package org.sapidroid.tts

import android.os.Bundle
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class FallbackSpeechTest {
    @Test fun noInstalledVoiceStreamsOneUtteranceThroughAnotherEngine() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val ready = CountDownLatch(1)
        val init = AtomicInteger(TextToSpeech.ERROR)
        val tts = TextToSpeech(context, {
            init.set(it)
            ready.countDown()
        }, context.packageName)
        val file = File.createTempFile("fallback-test-", ".wav", context.cacheDir)
        try {
            assertTrue("engine initialization timed out", ready.await(30, TimeUnit.SECONDS))
            assertEquals(TextToSpeech.SUCCESS, init.get())
            assertTrue("client did not bind SapiDroid", tts.voices.any { it.name == "System fallback" })
            val done = CountDownLatch(1)
            val errors = AtomicInteger()
            val bytes = AtomicInteger()
            tts.setOnUtteranceProgressListener(object : UtteranceProgressListener() {
                override fun onStart(utteranceId: String?) {}
                override fun onAudioAvailable(utteranceId: String?, audio: ByteArray) {
                    bytes.addAndGet(audio.size)
                }
                override fun onDone(utteranceId: String?) { done.countDown() }
                override fun onError(utteranceId: String?) {
                    errors.incrementAndGet()
                    done.countDown()
                }
            })
            assertEquals(TextToSpeech.SUCCESS, tts.synthesizeToFile("fallback test", Bundle(), file, "test"))
            assertTrue("fallback synthesis timed out", done.await(30, TimeUnit.SECONDS))
            assertEquals(0, errors.get())
            assertEquals(3200, bytes.get())
        } finally {
            tts.shutdown()
            file.delete()
        }
    }
}
