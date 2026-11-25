package org.sapidroid.tts

import android.app.Service
import android.content.Intent
import android.os.IBinder
import android.os.SystemClock
import android.util.Log
import java.io.File
import java.util.ArrayDeque
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import kotlin.math.abs
import kotlin.math.log2
import kotlin.math.roundToInt

/**
 * Holds the engine families and their open voices, in its own process.
 *
 * One request at a time on one thread. Cancellation calls the engine's own
 * thread-safe stop; a call that will not return is cleared by the owner
 * killing this process.
 */
class SapiDroidWorkerService : Service() {

    companion object {
        private const val TAG = "SapiDroidWorker"
        const val BUSY = -2
        const val HANG_PREFIX = "\$HANG"
        private const val MAX_PENDING_BYTES = 512 * 1024
    }

    private val synthesis = Executors.newSingleThreadExecutor { r ->
        Thread(r, "sapidroid-engine").apply { priority = Thread.NORM_PRIORITY + 1 }
    }

    private val state = Object()
    private var currentId = 0
    private var currentInfo: VoiceCatalog.Info? = null
    private var busy = false
    private var finished = true
    private var failed = false
    private var cancelled = false
    @Volatile private var destroyed = false
    // Audio produced so far for the current request, drained by pull().
    private val pending = ArrayDeque<ByteArray>()
    private var pendingOffset = 0
    private var pendingBytes = 0

    private val ziraHandles = ConcurrentHashMap<String, Long>()
    private val annaHandles = ConcurrentHashMap<String, Long>()
    private val s4Handles = ConcurrentHashMap<String, Long>()
    private val s5Handles = ConcurrentHashMap<String, Long>()
    private val s4Limits = HashMap<String, IntArray>()

    @Volatile private var ziraDir: File? = null
    @Volatile private var annaDir: File? = null
    @Volatile private var s4Dir: File? = null
    @Volatile private var s5Dir: File? = null
    @Volatile private var openError: String = ""
    private var s4Initialised = false

    // ------------------------------------------------------------------
    // Opening voices
    // ------------------------------------------------------------------

    @Synchronized private fun openZira(voice: String): Long {
        if (destroyed) return 0
        ziraHandles[voice]?.let { if (it != 0L) return it }
        val dir = ziraDir ?: return 0
        val handle = NativeZira.open(dir.absolutePath, voice)
        if (handle == 0L) {
            openError = "no OneCore voice data for $voice in ${dir.absolutePath}"
            Log.e(TAG, openError)
            return 0
        }
        ziraHandles[voice] = handle
        return handle
    }

    @Synchronized private fun openAnna(voice: String): Long {
        if (destroyed) return 0
        annaHandles[voice]?.let { if (it != 0L) return it }
        val dir = annaDir ?: return 0
        val handle = NativeAnna.open(dir.absolutePath)
        if (handle == 0L) {
            openError = "no Anna voice data in ${dir.absolutePath}"
            Log.e(TAG, openError)
            return 0
        }
        annaHandles[voice] = handle
        return handle
    }

    @Synchronized private fun openSapi4(mode: String): Long {
        if (destroyed) return 0
        s4Handles[mode]?.let { if (it != 0L) return it }
        val dir = s4Dir ?: return 0
        if (!s4Initialised) {
            val dll = File(dir, "msttssyn.dll")
            if (!dll.exists()) {
                openError = "no SAPI 4 data: ${dll.absolutePath} is missing"
                Log.e(TAG, openError)
                return 0
            }
            if (NativeSapi4.init(dll.absolutePath) != 0) {
                openError = "SAPI 4 engine data would not load from ${dll.absolutePath}"
                Log.e(TAG, openError)
                return 0
            }
            s4Initialised = true
        }
        val handle = NativeSapi4.open(dir.absolutePath, mode)
        if (handle == 0L) {
            openError = "SAPI 4 mode $mode did not open"
            Log.e(TAG, openError)
            return 0
        }
        s4Handles[mode] = handle
        return handle
    }

    @Synchronized private fun openSapi5(voice: String): Long {
        if (destroyed) return 0
        s5Handles[voice]?.let { if (it != 0L) return it }
        val dir = s5Dir ?: return 0
        // Internal names carry a 5 so they cannot collide with the SAPI 4 voices.
        val engineVoice = voice.removeSuffix("5")
        val handle = NativeSapi5.open(dir.absolutePath, engineVoice)
        if (handle == 0L) {
            openError = "no SAPI 5 voice data for $engineVoice in ${dir.absolutePath}"
            Log.e(TAG, openError)
            return 0
        }
        s5Handles[voice] = handle
        return handle
    }

    // ------------------------------------------------------------------
    // Binder
    // ------------------------------------------------------------------

    private val binder = object : IZiraWorker.Stub() {

        override fun say(
            voice: String,
            ratePercent: Int,
            pitchPercent: Int,
            text: String,
            waitMs: Int
        ): Int {
            val id: Int
            val info = VoiceCatalog.infoFor(voice)
            synchronized(state) {
                if (destroyed) return BUSY
                val deadline = SystemClock.elapsedRealtime() + waitMs.coerceIn(0, 5_000)
                while (busy) {
                    if (destroyed) return BUSY
                    val left = deadline - SystemClock.elapsedRealtime()
                    if (left <= 0) {
                        Log.w(TAG, "say refused: request $currentId still running")
                        return BUSY
                    }
                    try {
                        (state as java.lang.Object).wait(left)
                    } catch (e: InterruptedException) {
                        Thread.currentThread().interrupt()
                        return BUSY
                    }
                }
                currentId += 1
                id = currentId
                currentInfo = info
                busy = true
                finished = false
                failed = false
                cancelled = false
                pending.clear()
                pendingOffset = 0
                pendingBytes = 0
                synthesis.submit { runRequest(id, voice, info, ratePercent, pitchPercent, text) }
            }
            return id
        }

        override fun pull(requestId: Int, maxBytes: Int, waitMs: Int): ByteArray? {
            val limit = maxBytes.coerceIn(1, 256 * 1024)
            val deadline = SystemClock.elapsedRealtime() + waitMs.coerceIn(0, 5_000)
            synchronized(state) {
                while (true) {
                    if (requestId != currentId) return null
                    if (cancelled) return null
                    val buf = pending.peekFirst()
                    if (buf != null) {
                        val n = minOf(limit, pendingBytes)
                        val out: ByteArray
                        if (n <= buf.size - pendingOffset) {
                            out = if (pendingOffset == 0 && n == buf.size) buf
                                else buf.copyOfRange(pendingOffset, pendingOffset + n)
                            pendingOffset += n
                            if (pendingOffset == buf.size) {
                                pending.removeFirst()
                                pendingOffset = 0
                            }
                        } else {
                            out = ByteArray(n)
                            var copied = 0
                            while (copied < n) {
                                val head = pending.peekFirst()
                                val count = minOf(n - copied, head.size - pendingOffset)
                                head.copyInto(out, copied, pendingOffset, pendingOffset + count)
                                copied += count
                                pendingOffset += count
                                if (pendingOffset == head.size) {
                                    pending.removeFirst()
                                    pendingOffset = 0
                                }
                            }
                        }
                        pendingBytes -= n
                        (state as java.lang.Object).notifyAll()
                        return out
                    }
                    if (finished) return null
                    val left = deadline - SystemClock.elapsedRealtime()
                    if (left <= 0) return null
                    try {
                        (state as java.lang.Object).wait(left)
                    } catch (e: InterruptedException) {
                        Thread.currentThread().interrupt()
                        return null
                    }
                }
            }
        }

        override fun finished(requestId: Int): Boolean = synchronized(state) {
            requestId != currentId || (finished && pendingBytes == 0)
        }

        override fun failed(requestId: Int): Boolean = synchronized(state) {
            requestId != currentId || failed
        }

        override fun cancel(requestId: Int) {
            synchronized(state) {
                if (requestId != currentId) return
                cancelled = true
                pending.clear()
                pendingOffset = 0
                pendingBytes = 0
                val info = currentInfo
                val engine = info?.engine
                val voiceName = info?.name ?: ""
                val handle = when (engine) {
                    VoiceCatalog.Engine.ONECORE -> ziraHandles[voiceName] ?: 0L
                    VoiceCatalog.Engine.ANNA -> annaHandles[voiceName] ?: 0L
                    VoiceCatalog.Engine.SAPI4 -> s4Handles[voiceName] ?: 0L
                    VoiceCatalog.Engine.SAPI5 -> s5Handles[voiceName] ?: 0L
                    null -> 0L
                }
                if (handle != 0L) {
                    try {
                        when (engine) {
                            VoiceCatalog.Engine.ONECORE -> NativeZira.cancel(handle)
                            VoiceCatalog.Engine.ANNA -> NativeAnna.cancel(handle)
                            VoiceCatalog.Engine.SAPI4 -> NativeSapi4.cancel(handle)
                            VoiceCatalog.Engine.SAPI5 -> NativeSapi5.cancel(handle)
                            null -> {}
                        }
                    } catch (t: Throwable) {
                        Log.w(TAG, "engine cancel failed", t)
                    }
                }
                (state as java.lang.Object).notifyAll()
            }
        }

        override fun shutdown() {
            Log.i(TAG, "retiring worker process")
            android.os.Process.killProcess(android.os.Process.myPid())
        }

        override fun retireIfStuck(requestId: Int) {
            synchronized(state) {
                if (currentId == requestId && busy && cancelled) {
                    Log.w(TAG, "retiring worker stuck on request $requestId")
                    android.os.Process.killProcess(android.os.Process.myPid())
                }
            }
        }

        override fun sampleRate(voice: String): Int = synchronized(this@SapiDroidWorkerService) {
            if (destroyed) return@synchronized 0
            val info = VoiceCatalog.infoFor(voice) ?: return 0
            when (info.engine) {
                VoiceCatalog.Engine.ONECORE ->
                    if (openZira(info.name) != 0L) VoiceCatalog.SAMPLE_RATE else 0
                VoiceCatalog.Engine.ANNA ->
                    if (openAnna(info.name) != 0L) VoiceCatalog.SAMPLE_RATE else 0
                VoiceCatalog.Engine.SAPI4 -> {
                    val h = openSapi4(info.name)
                    if (h != 0L) NativeSapi4.sampleRate(h) else 0
                }
                VoiceCatalog.Engine.SAPI5 -> {
                    val h = openSapi5(info.name)
                    if (h != 0L) NativeSapi5.sampleRate(h) else 0
                }
            }
        }

        override fun probe(voice: String): String = synchronized(this@SapiDroidWorkerService) {
            if (destroyed) return@synchronized "worker shutting down"
            val info = VoiceCatalog.infoFor(voice) ?: return "unknown voice $voice"
            val ok = when (info.engine) {
                VoiceCatalog.Engine.ONECORE -> openZira(info.name) != 0L
                VoiceCatalog.Engine.ANNA -> openAnna(info.name) != 0L
                VoiceCatalog.Engine.SAPI4 -> openSapi4(info.name) != 0L
                VoiceCatalog.Engine.SAPI5 -> openSapi5(info.name) != 0L
            }
            if (ok) "" else openError.ifEmpty { "voice data missing" }
        }
    }

    override fun onCreate() {
        super.onCreate()
        // Device-protected storage: the engine's files stay readable during
        // Direct Boot, so the lock screen keeps speaking before first unlock.
        val store = DeviceStorage.context(this)
        ziraDir = File(store.filesDir, "voice").also { it.mkdirs() }
        annaDir = File(store.filesDir, "anna").also { it.mkdirs() }
        s4Dir = File(store.filesDir, "sapi4").also { it.mkdirs() }
        s5Dir = File(store.filesDir, "sapi5").also { it.mkdirs() }
    }

    // ------------------------------------------------------------------
    // Speaking
    // ------------------------------------------------------------------

    private fun isCancelled(id: Int): Boolean = synchronized(state) {
        id != currentId || cancelled
    }

    private fun synthChunk(
        id: Int,
        info: VoiceCatalog.Info,
        ratePercent: Int,
        pitchPercent: Int,
        piece: String,
        sink: PcmSink
    ): Int {
        return when (info.engine) {
            VoiceCatalog.Engine.ONECORE -> {
                val h = openZira(info.name)
                if (h == 0L || isCancelled(id)) -1 else {
                    NativeZira.setRate(h, sapiRate(ratePercent, 20))
                    NativeZira.setPitch(h, sapi5Pitch(pitchPercent))
                    NativeZira.speak(h, piece, sink)
                }
            }
            VoiceCatalog.Engine.ANNA -> {
                val h = openAnna(info.name)
                if (h == 0L || isCancelled(id)) -1 else {
                    NativeAnna.setRate(h, sapiRate(ratePercent, 18))
                    NativeAnna.setPitch(h, sapi5Pitch(pitchPercent))
                    NativeAnna.speak(h, piece, sink)
                }
            }
            VoiceCatalog.Engine.SAPI5 -> {
                val h = openSapi5(info.name)
                if (h == 0L || isCancelled(id)) -1 else {
                    NativeSapi5.setRate(h, sapiRate(ratePercent, 18))
                    NativeSapi5.setPitch(h, sapi5Pitch(pitchPercent))
                    NativeSapi5.speak(h, piece, sink)
                }
            }
            VoiceCatalog.Engine.SAPI4 -> {
                val h = openSapi4(info.name)
                if (h == 0L || isCancelled(id)) -1 else {
                    val lim = s4Limits[info.name] ?: NativeSapi4.limits(h)?.also {
                        if (it.size >= 6) s4Limits[info.name] = it
                    }
                    if (lim != null && lim.size >= 6) {
                        NativeSapi4.setSpeed(
                            h,
                            scale(lim[3], ratePercent, lim[4], lim[5])
                        )
                        NativeSapi4.setPitch(
                            h,
                            scale(lim[0], pitchPercent, lim[1], lim[2])
                        )
                    }
                    NativeSapi4.speak(h, piece.toByteArray(charset("windows-1252")), sink)
                }
            }
        }
    }

    private fun enqueueAudio(id: Int, bytes: ByteArray): Boolean = synchronized(state) {
        while (id == currentId && !cancelled && pendingBytes + bytes.size > MAX_PENDING_BYTES) {
            try {
                (state as java.lang.Object).wait()
            } catch (e: InterruptedException) {
                Thread.currentThread().interrupt()
                cancelled = true
                break
            }
        }
        if (id != currentId || cancelled) return@synchronized false
        pending.addLast(bytes)
        pendingBytes += bytes.size
        (state as java.lang.Object).notifyAll()
        true
    }

    private fun runRequest(
        id: Int,
        voice: String,
        info: VoiceCatalog.Info?,
        ratePercent: Int,
        pitchPercent: Int,
        text: String
    ) {
        try {
            if (BuildConfig.DEBUG && text.contains(HANG_PREFIX)) {
                Log.w(TAG, "debug hang for request $id")
                try {
                    Thread.sleep(120_000)
                } catch (e: InterruptedException) {
                    Thread.currentThread().interrupt()
                }
                return
            }
            if (info == null) {
                synchronized(state) { failed = true }
                return
            }
            val sink = object : PcmSink {
                override fun onPcm(bytes: ByteArray) = enqueueAudio(id, bytes)
                override fun isCancelled() = this@SapiDroidWorkerService.isCancelled(id)
            }
            StreamingText.forEachChunk(text) { piece ->
                if (isCancelled(id)) return@forEachChunk false
                val result = synthChunk(id, info, ratePercent, pitchPercent, piece, sink)
                if (result != 0) {
                    synchronized(state) { if (!cancelled) failed = true }
                    if (!isCancelled(id)) Log.e(TAG, "synthesis failed for $voice (status $result)")
                    return@forEachChunk false
                }
                true
            }
        } catch (t: Throwable) {
            Log.e(TAG, "synthesis failed", t)
            synchronized(state) {
                if (id == currentId && !cancelled) failed = true
            }
        } finally {
            synchronized(state) {
                busy = false
                finished = true
                (state as java.lang.Object).notifyAll()
            }
        }
    }

    /** Android's percent applied to one of the engine's own limits. */
    private fun scale(default: Int, percent: Int, min: Int, max: Int): Int {
        if (percent <= 0) return default
        val wanted = (default * (percent / 100.0)).roundToInt()
        return wanted.coerceIn(min, max)
    }

    /** Android pitch ratio -> -10..10, one unit = half a semitone. */
    private fun sapi5Pitch(percent: Int): Int {
        if (percent <= 0 || abs(percent - 100) <= 2) return 0
        return (24.0 * log2(percent / 100.0)).roundToInt().coerceIn(-10, 10)
    }

    override fun onBind(intent: Intent): IBinder = binder

    override fun onDestroy() {
        synchronized(state) {
            destroyed = true
            binder.cancel(currentId)
            synthesis.execute { closeHandles() }
            synthesis.shutdown()
        }
        super.onDestroy()
    }

    @Synchronized private fun closeHandles() {
        synchronized(state) {
            for (h in ziraHandles.values) {
                try {
                    NativeZira.close(h)
                } catch (t: Throwable) {
                    Log.w(TAG, "zira close failed", t)
                }
            }
            ziraHandles.clear()
            for (h in annaHandles.values) {
                try {
                    NativeAnna.close(h)
                } catch (t: Throwable) {
                    Log.w(TAG, "anna close failed", t)
                }
            }
            annaHandles.clear()
            for (h in s4Handles.values) {
                try {
                    NativeSapi4.close(h)
                } catch (t: Throwable) {
                    Log.w(TAG, "sapi4 close failed", t)
                }
            }
            s4Handles.clear()
            s4Limits.clear()
            for (h in s5Handles.values) {
                try {
                    NativeSapi5.close(h)
                } catch (t: Throwable) {
                    Log.w(TAG, "sapi5 close failed", t)
                }
            }
            s5Handles.clear()
        }
    }
}
