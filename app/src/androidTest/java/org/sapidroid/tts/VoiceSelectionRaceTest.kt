package org.sapidroid.tts

import android.content.Context
import android.content.ContextWrapper
import android.content.SharedPreferences
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class VoiceSelectionRaceTest {
    @Test
    fun savingDuringInitialReadKeepsTheNewSelection() {
        val base = ApplicationProvider.getApplicationContext<Context>().createDeviceProtectedStorageContext()
        val name = "voice-race-${System.nanoTime()}"
        val prefs = base.getSharedPreferences(name, Context.MODE_PRIVATE)
        val oldVoice = checkNotNull(VoiceCatalog.infoFor("David (OneCore)"))
        val newVoice = checkNotNull(VoiceCatalog.infoFor("Zira (OneCore)"))
        prefs.edit().putString("default_voice", oldVoice.label).commit()
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val saved = CountDownLatch(1)
        val blockingPrefs = object : SharedPreferences by prefs {
            override fun getString(key: String?, default: String?): String? {
                val value = prefs.getString(key, default)
                entered.countDown()
                check(release.await(5, TimeUnit.SECONDS))
                return value
            }
        }
        val context = object : ContextWrapper(base) {
            override fun getSharedPreferences(name: String, mode: Int): SharedPreferences = blockingPrefs
        }
        val label = VoiceCatalog::class.java.getDeclaredField("cachedDefaultLabel").apply { isAccessible = true }
        val valid = VoiceCatalog::class.java.getDeclaredField("defaultCacheValid").apply { isAccessible = true }
        val previousLabel = label.get(null)
        val previousValid = valid.getBoolean(null)
        val reader = Thread { VoiceCatalog.savedVoice(context, VoiceCatalog.all) }
        val writer = Thread {
            VoiceCatalog.saveVoice(context, newVoice)
            saved.countDown()
        }
        try {
            label.set(null, null)
            valid.setBoolean(null, false)
            reader.start()
            assertTrue(entered.await(5, TimeUnit.SECONDS))
            writer.start()
            saved.await(200, TimeUnit.MILLISECONDS)
            release.countDown()
            reader.join(5_000)
            writer.join(5_000)
            assertTrue(saved.await(5, TimeUnit.SECONDS))
            assertEquals(newVoice, VoiceCatalog.savedVoice(context, VoiceCatalog.all))
        } finally {
            release.countDown()
            reader.join(5_000)
            if (writer.isAlive) writer.join(5_000)
            label.set(null, previousLabel)
            valid.setBoolean(null, previousValid)
            prefs.edit().clear().commit()
        }
    }
}
