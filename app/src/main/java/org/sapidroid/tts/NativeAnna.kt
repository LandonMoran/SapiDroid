package org.sapidroid.tts

/** JNI entry points into libsapidroid.so (the Microsoft Anna engine). */
object NativeAnna {
    init {
        System.loadLibrary("sapidroid")
    }

    @JvmStatic external fun open(dir: String): Long

    @JvmStatic external fun close(handle: Long)

    @JvmStatic external fun setRate(handle: Long, rate: Int)

    @JvmStatic external fun setPitch(handle: Long, pitch: Int)

    @JvmStatic external fun setVolume(handle: Long, volume: Int)

    /** Blocking; [cancel] requests a stop from another thread. */
    @JvmStatic external fun speak(handle: Long, text: String, sink: PcmSink): Int

    @JvmStatic external fun cancel(handle: Long)
}
