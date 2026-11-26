package org.sapidroid.tts

/**
 * The OneCore voices that can be downloaded as plain files, from Sapple's
 * bundle: Microsoft's English OneCore voices.
 *
 * `prefix` is the name the bundle gives the voice's files ("M2057George.APM");
 * the installer renames them to "M1033George.APM", which is what the engine
 * looks for. David, Zira and Mark already arrive under the M1033 names.
 *
 * Eva, Sarah and Matilda are the neural voices: their APM carries durations
 * only, and their sound comes from the `.nnm`/`.tdat` files next to it, which
 * the engine's neural back end reads. The installer copies every file the
 * bundle has for the voice, so those three arrive complete.
 */
object OneCorePacks {

    data class Pack(
        val voice: String,
        val prefix: String,
        val language: String,
        val country: String,
        /** True for the neural voices, which also need their network (`.nnm`) and its data (`.tdat`). */
        val neural: Boolean = false
    ) {
        val label: String get() = "$language — $voice"
    }

    val all: List<Pack> = listOf(
        Pack("David", "M1033David", "English (United States)", "en-US"),
        Pack("Zira", "M1033Zira", "English (United States)", "en-US"),
        Pack("Mark", "M1033Mark", "English (United States)", "en-US"),
        Pack("George", "M2057George", "English (United Kingdom)", "en-GB"),
        Pack("Susan", "M2057Susan", "English (United Kingdom)", "en-GB"),
        Pack("Hazel", "M2057Hazel", "English (United Kingdom)", "en-GB"),
        Pack("James", "M3081James", "English (Australia)", "en-AU"),
        Pack("Catherine", "M3081Catherine", "English (Australia)", "en-AU"),
        Pack("Richard", "M4105Richard", "English (India)", "en-IN"),
        Pack("Linda", "M4105Linda", "English (India)", "en-IN"),
        // The neural voices (Cortana's): durations from the APM, acoustics from .nnm/.tdat.
        Pack("Sarah", "M2057Sarah", "English (United Kingdom)", "en-GB", neural = true),
        Pack("Eva", "M1033Eva", "English (United States)", "en-US", neural = true),
        Pack("Matilda", "M3081Matilda", "English (Australia)", "en-AU", neural = true),
    )

    /** What the menu says about everything it cannot offer. */
    const val NOTE =
        "Everything here comes from one download, Sapple's 200 MB app bundle, " +
            "which carries Microsoft's voice files for personal use. It is kept " +
            "while the app is open, so picking several voices downloads it once. " +
            "Every download also installs " + SappleVoices.SHARED_DATA + ", the " +
            "shared table the engine reads whichever voice it speaks with. Sarah, " +
            "Eva and Matilda are neural voices: their sound comes from the .nnm " +
            "and .tdat files in the same bundle, which the engine now reads, and " +
            "every one of their files is installed with them."

    fun byVoice(voice: String): Pack? = all.firstOrNull { it.voice == voice }
}
