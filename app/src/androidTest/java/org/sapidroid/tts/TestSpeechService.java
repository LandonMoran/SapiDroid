package org.sapidroid.tts;

import android.media.AudioFormat;
import android.speech.tts.SynthesisCallback;
import android.speech.tts.SynthesisRequest;
import android.speech.tts.TextToSpeech;
import android.speech.tts.TextToSpeechService;
import android.speech.tts.Voice;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

public class TestSpeechService extends TextToSpeechService {
    @Override public String[] onGetLanguage() { return new String[] {"eng", "USA", ""}; }
    @Override public int onIsLanguageAvailable(String lang, String country, String variant) {
        return TextToSpeech.LANG_COUNTRY_AVAILABLE;
    }
    @Override public int onLoadLanguage(String lang, String country, String variant) {
        return TextToSpeech.LANG_COUNTRY_AVAILABLE;
    }
    @Override public String onGetDefaultVoiceNameFor(String lang, String country, String variant) {
        return "test voice";
    }
    @Override public int onIsValidVoiceName(String voiceName) {
        return "test voice".equals(voiceName) ? TextToSpeech.SUCCESS : TextToSpeech.ERROR;
    }
    @Override public int onLoadVoice(String voiceName) { return onIsValidVoiceName(voiceName); }
    @Override public List<Voice> onGetVoices() {
        return new ArrayList<>(Collections.singletonList(
            new Voice("test voice", Locale.US, Voice.QUALITY_NORMAL, Voice.LATENCY_NORMAL,
                false, Collections.emptySet())));
    }
    @Override public void onStop() {}

    @Override public void onSynthesizeText(SynthesisRequest request, SynthesisCallback callback) {
        byte[] audio = new byte[3200];
        for (int i = 0; i < audio.length; i += 2) audio[i] = 42;
        if (callback.start(16000, AudioFormat.ENCODING_PCM_16BIT, 1) != TextToSpeech.SUCCESS) return;
        if (callback.audioAvailable(audio, 0, audio.length) != TextToSpeech.SUCCESS) return;
        callback.done();
    }
}
