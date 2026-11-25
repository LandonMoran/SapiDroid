package org.sapidroid.tts

import android.content.Context
import android.os.UserManager
import android.util.Log
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream

/**
 * Device-protected storage, so the engine stays usable during Direct Boot:
 * on the lock screen after a reboot, before the first unlock.
 *
 * Credential-protected storage (plain `filesDir`) is sealed until the user
 * unlocks, so voice data and the default-voice setting live here instead.
 * Device-protected storage reads fine after unlock too, so one location
 * serves both cases. A one-time migration carries existing installs over.
 */
object DeviceStorage {

    private const val TAG = "SapiDroidStorage"
    private const val PREFS = "sapidroid"
    private const val KEY_MIGRATED = "device_storage_migrated"
    private const val KEY_DEFAULT_VOICE = "default_voice"

    private val VOICE_DIRS = arrayOf("voice", "anna", "sapi4", "sapi5")

    /**
     * A context whose filesDir, cacheDir and shared preferences live in
     * device-protected storage: readable before first unlock and after.
     */
    fun context(context: Context): Context =
        if (context.isDeviceProtectedStorage) context
        else context.createDeviceProtectedStorageContext()

    /**
     * Moves voice data and the default-voice setting from credential-protected
     * to device-protected storage, once. Skipped while locked (credential
     * storage is sealed, so there is nothing readable to move) and after the
     * marker is set. Never throws; a deferred move just retries next launch.
     */
    fun migrateIfNeeded(appContext: Context) {
        try {
            val device = context(appContext)
            val prefs = device.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            if (prefs.getBoolean(KEY_MIGRATED, false)) return
            val um = appContext.getSystemService(UserManager::class.java)
            if (um != null && !um.isUserUnlocked) return
            for (name in VOICE_DIRS) {
                val src = File(appContext.filesDir, name)
                if (!src.exists()) continue
                val dst = File(device.filesDir, name)
                val dstEmpty = !dst.exists() ||
                    (dst.isDirectory && (dst.list()?.isEmpty() ?: true))
                if (!dstEmpty) continue
                dst.parentFile?.mkdirs()
                if (!src.renameTo(dst)) {
                    copyRecursively(src, dst)
                    src.deleteRecursively()
                }
                Log.i(TAG, "moved $name to device-protected storage")
            }
            if (!prefs.contains(KEY_DEFAULT_VOICE)) {
                val old = appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                    .getString(KEY_DEFAULT_VOICE, null)
                if (old != null) prefs.edit().putString(KEY_DEFAULT_VOICE, old).apply()
            }
            prefs.edit().putBoolean(KEY_MIGRATED, true).apply()
        } catch (t: Throwable) {
            Log.w(TAG, "storage migration deferred", t)
        }
    }

    private fun copyRecursively(src: File, dst: File) {
        if (src.isDirectory) {
            dst.mkdirs()
            for (child in src.listFiles() ?: emptyArray()) {
                copyRecursively(child, File(dst, child.name))
            }
        } else {
            dst.parentFile?.mkdirs()
            FileInputStream(src).use { input ->
                FileOutputStream(dst).use { output -> input.copyTo(output, 1 shl 16) }
            }
        }
    }
}
