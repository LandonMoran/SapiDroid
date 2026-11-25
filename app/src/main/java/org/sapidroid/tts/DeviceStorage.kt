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
    @Synchronized fun migrateIfNeeded(appContext: Context) {
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
                dst.parentFile?.mkdirs()
                if (!dstEmpty) {
                    copyMissing(src, dst)
                } else if (!src.renameTo(dst)) {
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

    private fun copyMissing(src: File, dst: File) {
        if (src.isDirectory) {
            check(dst.isDirectory || dst.mkdirs()) { "cannot create $dst" }
            for (child in src.listFiles() ?: throw IllegalStateException("cannot list $src")) {
                copyMissing(child, File(dst, child.name))
            }
        } else if (!dst.exists()) {
            copyRecursively(src, dst)
        }
    }

    private fun copyRecursively(src: File, dst: File) {
        if (src.isDirectory) {
            check(dst.isDirectory || dst.mkdirs()) { "cannot create $dst" }
            for (child in src.listFiles() ?: throw IllegalStateException("cannot list $src")) {
                copyRecursively(child, File(dst, child.name))
            }
        } else {
            val parent = dst.parentFile ?: throw IllegalStateException("no parent for $dst")
            check(parent.isDirectory || parent.mkdirs()) { "cannot create $parent" }
            val part = File.createTempFile("migration-", ".part", parent)
            try {
                FileInputStream(src).use { input ->
                    FileOutputStream(part).use { output -> input.copyTo(output, 1 shl 16) }
                }
                check(part.renameTo(dst)) { "cannot move $part to $dst" }
            } finally {
                if (part.exists() && !part.delete()) Log.w(TAG, "could not delete $part")
            }
        }
    }
}
