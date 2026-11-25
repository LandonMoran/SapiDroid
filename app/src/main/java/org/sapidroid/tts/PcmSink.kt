package org.sapidroid.tts

fun interface PcmSink {
    fun onPcm(bytes: ByteArray): Boolean
    fun isCancelled(): Boolean = false
}
