package org.sapidroid.tts

import android.content.Context
import java.io.File
import java.util.Locale

/**
 * The voices this build can speak with, across both engine families.
 *
 * OneCore voices load from Microsoft's M1033 files; SAPI 4 modes load from
 * msttssyn.dll and its .vce files. Both sets of data are the user's own.
 */
object VoiceCatalog {

    /** OneCore output rate; SAPI 4 reports its own per mode. */
    const val SAMPLE_RATE = 16000

    /** The eight files Microsoft's Speech SDK 5.1 puts in `files/sapi5`. */
    val sapi5Files = listOf(
        "Sam.spd", "Sam.sdf", "Mike.spd", "Mike.sdf",
        "Mary.spd", "Mary.sdf", "LTTS1033.LXA", "r1033tts.LXA"
    )

    enum class Engine { ONECORE, ANNA, SAPI4, SAPI5 }

    data class Info(
        val engine: Engine,
        val name: String,
        val label: String,
        val locale: Locale = Locale.US
    )

    private val oneCore = listOf(
        Info(Engine.ONECORE, "David", "David (OneCore)"),
        Info(Engine.ONECORE, "Zira", "Zira (OneCore)"),
        Info(Engine.ONECORE, "Mark", "Mark (OneCore)"),
        // Microsoft Speech Platform 11 packs, which this engine also reads.
        Info(Engine.ONECORE, "Hazel", "Hazel (OneCore)", Locale("en", "GB")),
        Info(Engine.ONECORE, "Hayley", "Hayley (OneCore)", Locale("en", "AU")),
        Info(Engine.ONECORE, "Heather", "Heather (OneCore)", Locale("en", "CA")),
        Info(Engine.ONECORE, "Heera", "Heera (OneCore)", Locale("en", "IN")),
        Info(Engine.ONECORE, "Helen", "Helen (OneCore)", Locale("en", "US")),
        Info(Engine.ONECORE, "ZiraPro", "Zira Pro (OneCore)", Locale("en", "US")),
        // OneCore voices from Windows 10/11 language packs.
        Info(Engine.ONECORE, "George", "George (OneCore)", Locale("en", "GB")),
        Info(Engine.ONECORE, "Susan", "Susan (OneCore)", Locale("en", "GB")),
        Info(Engine.ONECORE, "Sarah", "Sarah (OneCore)", Locale("en", "GB")),
        Info(Engine.ONECORE, "James", "James (OneCore)", Locale("en", "AU")),
        Info(Engine.ONECORE, "Matilda", "Matilda (OneCore)", Locale("en", "AU")),
        Info(Engine.ONECORE, "Catherine", "Catherine (OneCore)", Locale("en", "AU")),
        Info(Engine.ONECORE, "Richard", "Richard (OneCore)", Locale("en", "IN")),
        Info(Engine.ONECORE, "Linda", "Linda (OneCore)", Locale("en", "IN")),
    )

    /** Microsoft Anna, the Windows Vista and 7 voice: her own engine. */
    private val anna = listOf(
        Info(Engine.ANNA, "Anna", "Anna (Vista)"),
    )

    private val sapi4 = listOf(
        "Sam", "Mike", "Mary",
        "Sam Whisper", "Male Whisper", "Female Whisper",
        "Sam in Space", "Mike in Space", "Mary in Space",
        "Sam in Hall", "Mike in Hall", "Mary in Hall",
        "Sam in Stadium", "Mike in Stadium", "Mary in Stadium",
        "RoboSoft One", "RoboSoft Two", "RoboSoft Three",
        "RoboSoft Four", "RoboSoft Five", "RoboSoft Six",
    ).map { Info(Engine.SAPI4, it, "$it (SAPI 4)") }

    /**
     * SAPI 5's Sam, Mike and Mary. Different engine from the SAPI 4 voices of
     * the same names, so their internal names carry the 5.
     */
    private val sapi5 = listOf(
        Info(Engine.SAPI5, "Sam5", "Sam (SAPI 5)"),
        Info(Engine.SAPI5, "Mike5", "Mike (SAPI 5)"),
        Info(Engine.SAPI5, "Mary5", "Mary (SAPI 5)"),
    )

    val all: List<Info> = sapi5 + sapi4 + oneCore + anna

    private val exactLabels = all.associate { info ->
        info.label to all.first { it.label.equals(info.label, ignoreCase = true) }
    }

    val defaultVoice: String = "David"

    private const val PREFS = "sapidroid"
    private const val KEY_DEFAULT_VOICE = "default_voice"

    /**
     * The voice chosen in the app's picker. Screen readers and other apps do
     * not choose a voice themselves: they set a language and take the engine's
     * default, which is this voice.
     */
    @Volatile private var cachedDefaultLabel: String? = null
    @Volatile private var defaultCacheValid = false

    /** Device-protected storage, so the default voice survives Direct Boot. */
    private fun storage(context: Context): Context = DeviceStorage.context(context)

    @Synchronized fun savedVoice(context: Context, voices: List<Info> = installed(context)): Info? {
        // In-memory cache first: the picker's choice is visible to the next
        // utterance in this process without waiting on disk I/O.
        if (!defaultCacheValid || cachedDefaultLabel == null) {
            cachedDefaultLabel = storage(context).getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getString(KEY_DEFAULT_VOICE, null)
            defaultCacheValid = true
        }
        val label = cachedDefaultLabel ?: return null
        val info = infoFor(label) ?: return null
        return if (voices === all || voices.any { it.label == info.label }) info else null
    }

    /**
     * Remembers the picker's choice. The in-memory copy applies at once; the
     * disk write must target device-protected storage too, or a reboot
     * before unlock would forget the voice on the lock screen.
     */
    @Synchronized fun saveVoice(context: Context, info: Info) {
        cachedDefaultLabel = info.label
        defaultCacheValid = true
        storage(context).getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit().putString(KEY_DEFAULT_VOICE, info.label).apply()
    }

    internal fun savedInstalledVoice(context: Context): Info? =
        savedVoice(context, all)?.takeIf { InstalledData(storage(context).filesDir).has(it) }

    private class InstalledData(files: File) {
        private val oneCore = File(files, "voice")
        private val anna = File(files, "anna")
        private val sapi4 = File(files, "sapi4")
        private val sapi5 = File(files, "sapi5")

        private val shared by lazy { File(oneCore, SappleVoices.SHARED_DATA).let { it.isFile && it.length() > 0 } }
        private val sapi4Ready by lazy {
            listOf("msttssyn.dll", "sam.vce", "male.vce", "female.vce",
                "sam.cfg", "male.cfg", "female.cfg").all { File(sapi4, it).isFile }
        }
        private val sapi5Ready by lazy {
            listOf("LTTS1033.LXA", "r1033tts.LXA").all { File(sapi5, it).isFile }
        }
        private val annaReady by lazy {
            listOf("CSD", "IDX", "UNT", "CRT", "APL", "UDT", "TTS", "LTS", "WIH", "KEY", "HAN")
                .all { File(anna, "M1033DSK.$it").isFile }
        }

        fun has(info: Info): Boolean = when (info.engine) {
            Engine.ONECORE -> shared && listOf("APM", "INI").all {
                File(oneCore, "M1033${info.name}.$it").isFile
            }
            Engine.ANNA -> annaReady
            Engine.SAPI4 -> sapi4Ready
            Engine.SAPI5 -> sapi5Ready && listOf("spd", "sdf").all {
                File(sapi5, "${info.name.removeSuffix("5")}.$it").isFile
            }
        }
    }

    /**
     * The voices whose data is actually on the device, so the picker and the
     * system's voice list never offer a voice that could not load. OneCore needs
     * the voice's own APM and the shared frontend table beside it; the other
     * engines keep one folder each.
     */
    fun installed(context: android.content.Context): List<Info> {
        val data = InstalledData(storage(context).filesDir)
        return all.filter { data.has(it) }
    }

    fun infoFor(name: String?): Info? {
        if (name == null) return null
        exactLabels[name]?.let { return it }
        all.firstOrNull { it.label.equals(name, ignoreCase = true) }?.let { return it }
        all.firstOrNull { it.name.equals(name, ignoreCase = true) }?.let { return it }
        val bare = name.removeSuffix(" (SAPI 5)")
            .removeSuffix(" (SAPI 4)")
            .removeSuffix(" (OneCore)")
        return all.firstOrNull { it.name.equals(bare, ignoreCase = true) }
    }

    internal fun resolveForSynthesis(
        voices: List<Info>, saved: Info?, requested: String?, lang: String?, country: String?
    ): Info? {
        if (voices.isEmpty()) return null
        saved?.takeIf { it in voices }?.let { return it }
        if (requested == FallbackSpeech.VOICE_NAME) return null
        infoFor(requested)?.takeIf { it in voices }?.let { return it }
        return forLocale(lang, country, voices)
    }

    /**
     * The first voice in [voices] for the language, else David if he is there,
     * else the first voice.
     */
    fun forLocale(lang: String?, country: String?, voices: List<Info> = all): Info =
        voices.firstOrNull {
            it.locale.language.equals(lang, ignoreCase = true) &&
                (country.isNullOrEmpty() || it.locale.country.equals(country, ignoreCase = true))
        } ?: voices.firstOrNull { it.name == defaultVoice }
            ?: voices.firstOrNull()
            ?: all.first { it.name == defaultVoice }
}
