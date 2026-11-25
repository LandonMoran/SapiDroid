package org.sapidroid.tts

internal fun mayRetryWorker(replaced: Boolean, stopped: Boolean, callbackStarted: Boolean): Boolean =
    !replaced && !stopped && !callbackStarted
