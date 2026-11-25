package org.sapidroid.tts

import android.content.Context
import android.content.ContextWrapper
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class VoiceFastPathTest {
    @Test
    fun selectedVoiceChecksTheSameFilesAsTheFullCatalog() {
        val base = ApplicationProvider.getApplicationContext<Context>().createDeviceProtectedStorageContext()
        val dir = File(base.cacheDir, "voice-fast-path-${System.nanoTime()}")
        check(dir.mkdirs())
        val prefsName = "voice-fast-path-${System.nanoTime()}"
        val context = object : ContextWrapper(base) {
            override fun getFilesDir(): File = dir
            override fun getSharedPreferences(name: String, mode: Int) =
                base.getSharedPreferences(prefsName, mode)
        }
        val labelField = VoiceCatalog::class.java.getDeclaredField("cachedDefaultLabel").apply { isAccessible = true }
        val validField = VoiceCatalog::class.java.getDeclaredField("defaultCacheValid").apply { isAccessible = true }
        val oldLabel = labelField.get(null)
        val oldValid = validField.getBoolean(null)
        try {
            val cases = listOf(
                "David (OneCore)" to listOf("voice/MSTTSLocEnUS.dat", "voice/M1033David.APM", "voice/M1033David.INI"),
                "Anna (Vista)" to listOf("CSD", "IDX", "UNT", "CRT", "APL", "UDT", "TTS", "LTS", "WIH", "KEY", "HAN")
                    .map { "anna/M1033DSK.$it" },
                "Sam (SAPI 4)" to listOf("msttssyn.dll", "sam.vce", "male.vce", "female.vce", "sam.cfg", "male.cfg", "female.cfg")
                    .map { "sapi4/$it" },
                "Sam (SAPI 5)" to listOf("LTTS1033.LXA", "r1033tts.LXA", "Sam.spd", "Sam.sdf")
                    .map { "sapi5/$it" }
            )
            for ((label, paths) in cases) {
                for (path in paths) File(dir, path).apply { parentFile!!.mkdirs(); writeText("x") }
                val info = checkNotNull(VoiceCatalog.infoFor(label))
                VoiceCatalog.saveVoice(context, info)
                assertEquals(info, VoiceCatalog.savedInstalledVoice(context))
                assertEquals(info, VoiceCatalog.installed(context).first { it == info })
                check(File(dir, paths.first()).delete())
                assertNull(VoiceCatalog.savedInstalledVoice(context))
                assertEquals(false, info in VoiceCatalog.installed(context))
            }
        } finally {
            labelField.set(null, oldLabel)
            validField.setBoolean(null, oldValid)
            base.getSharedPreferences(prefsName, Context.MODE_PRIVATE).edit().clear().commit()
            dir.deleteRecursively()
        }
    }
}
