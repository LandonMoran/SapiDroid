package org.sapidroid.tts

import android.media.AudioFormat
import android.speech.tts.SynthesisCallback
import android.speech.tts.SynthesisRequest
import android.speech.tts.TextToSpeech
import android.speech.tts.TextToSpeechService
import android.speech.tts.Voice
import java.util.Locale

class TestSpeechService : TextToSpeechService() {
    override fun onGetLanguage() = arrayOf("eng", "USA", "")
    override fun onIsLanguageAvailable(lang: String?, country: String?, variant: String?) =
        TextToSpeech.LANG_COUNTRY_AVAILABLE
    override fun onLoadLanguage(lang: String?, country: String?, variant: String?) =
        TextToSpeech.LANG_COUNTRY_AVAILABLE
    override fun onGetDefaultVoiceNameFor(lang: String?, country: String?, variant: String?) = "test voice"
    override fun onIsValidVoiceName(voiceName: String?) =
        if (voiceName == "test voice") TextToSpeech.SUCCESS else TextToSpeech.ERROR
    override fun onLoadVoice(voiceName: String?) = onIsValidVoiceName(voiceName)
    override fun onGetVoices() = mutableListOf(
        Voice("test voice", Locale.US, Voice.QUALITY_NORMAL, Voice.LATENCY_NORMAL, false, emptySet())
    )
    override fun onStop() {}

    override fun onSynthesizeText(request: SynthesisRequest?, callback: SynthesisCallback?) {
        if (callback == null) return
        val audio = ByteArray(3200) { if (it % 2 == 0) 42 else 0 }
        if (callback.start(16000, AudioFormat.ENCODING_PCM_16BIT, 1) != TextToSpeech.SUCCESS) return
        if (callback.audioAvailable(audio, 0, audio.size) != TextToSpeech.SUCCESS) return
        callback.done()
    }
}
