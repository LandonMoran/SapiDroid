package org.sapidroid.tts

/** JNI entry points into the profiler table (see app/src/main/cpp/prof.c). */
internal object NativeProfile {
    init {
        System.loadLibrary("sapidroid")
    }

    @JvmStatic external fun addNs(name: String, ns: Long)

    @JvmStatic external fun dump(tag: String?)

    @JvmStatic external fun reset()

    @JvmStatic external fun take(): String?

    @JvmStatic external fun merge(name: String, totalNs: Long, count: Long, maxNs: Long, bytes: Long)
}

/**
 * Opt-in timing of the synthesis path. Compiled out unless the build used `-Pprofile`
 * ([BuildConfig.PROFILE]): then [enabled] is a constant false, the branches fold away and the
 * native instrumentation is absent too.
 *
 * One table per request: [reset] before the request, [mergeWorker] plus [dump] after it, so the
 * counts and totals describe that request only.  The engine runs in another process, so its rows
 * arrive through [IZiraWorker.profileTake]; they are folded into this process's table and the two
 * halves are ranked together, native and Kotlin, most expensive first.
 */
internal object Prof {
    val enabled: Boolean = BuildConfig.PROFILE

    inline fun <T> timed(name: String, block: () -> T): T {
        if (!enabled) return block()
        val t0 = System.nanoTime()
        try {
            return block()
        } finally {
            NativeProfile.addNs(name, System.nanoTime() - t0)
        }
    }

    fun dump(tag: String) {
        if (enabled) NativeProfile.dump(tag)
    }

    fun reset() {
        if (enabled) NativeProfile.reset()
    }

    /**
     * Drops rows the engine process collected outside a request (voice prewarm, probes), so they
     * are not attributed to the request that happens to run next.
     */
    fun discardWorker(worker: IZiraWorker?) {
        if (!enabled || worker == null) return
        try {
            worker.profileTake()
        } catch (t: Throwable) {
            // worker died before this request; nothing to drop
        }
    }

    /**
     * Folds the engine process's rows for the request that just finished into this table, and
     * clears them there so the next request starts from zero.
     */
    fun mergeWorker(worker: IZiraWorker?) {
        if (!enabled || worker == null) return
        val blob = try {
            worker.profileTake()
        } catch (t: Throwable) {
            null // worker died mid-request; its rows die with it
        }
        if (blob.isNullOrEmpty()) return
        for (row in blob.split(';')) {
            val f = row.split(',')
            if (f.size != 5) continue
            val total = f[1].toLongOrNull() ?: continue
            NativeProfile.merge(
                f[0],
                total,
                f[2].toLongOrNull() ?: 0L,
                f[3].toLongOrNull() ?: 0L,
                f[4].toLongOrNull() ?: 0L
            )
        }
    }
}
