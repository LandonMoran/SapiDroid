package org.sapidroid.tts

/** JNI entry points into the profiler table (see app/src/main/cpp/prof.c). */
internal object NativeProfile {
    init {
        System.loadLibrary("sapidroid")
    }

    @JvmStatic external fun addNs(name: String, ns: Long)

    @JvmStatic external fun dump(tag: String?)

    @JvmStatic external fun reset()
}

/**
 * Opt-in timing of the synthesis path. Compiled out unless the build used `-Pprofile`
 * ([BuildConfig.PROFILE]): then [enabled] is a constant false, the branches fold away and the
 * native instrumentation is absent too.
 *
 * Samples land in the same native table the JNI bridges fill, so one dump ranks native and Kotlin
 * costs together, most expensive first.
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
}
