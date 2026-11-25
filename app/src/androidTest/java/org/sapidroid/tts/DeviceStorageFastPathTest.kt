package org.sapidroid.tts

import android.content.Context
import android.content.ContextWrapper
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class DeviceStorageFastPathTest {
    @Test
    fun completedMigrationIsCachedButFailuresAreRetried() {
        val base = ApplicationProvider.getApplicationContext<Context>().createDeviceProtectedStorageContext()
        val prefsName = "migration-fast-path-${System.nanoTime()}"
        val prefs = base.getSharedPreferences(prefsName, Context.MODE_PRIVATE)
        prefs.edit().putBoolean("device_storage_migrated", true).commit()
        var accesses = 0
        val context = object : ContextWrapper(base) {
            override fun getSharedPreferences(name: String, mode: Int) =
                if (++accesses == 1) throw IllegalStateException("temporary preference failure") else prefs
        }
        val field = DeviceStorage::class.java.getDeclaredField("migrationComplete").apply { isAccessible = true }
        val previous = field.getBoolean(null)
        try {
            field.setBoolean(null, false)
            DeviceStorage.migrateIfNeeded(context)
            DeviceStorage.migrateIfNeeded(context)
            DeviceStorage.migrateIfNeeded(context)
            assertEquals(2, accesses)
        } finally {
            field.setBoolean(null, previous)
            prefs.edit().clear().commit()
        }
    }
}
