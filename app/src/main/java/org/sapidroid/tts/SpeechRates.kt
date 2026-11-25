package org.sapidroid.tts

import kotlin.math.abs
import kotlin.math.ln
import kotlin.math.roundToInt

internal fun sapiRate(percent: Int, maximum: Int): Int {
    if (percent <= 0 || abs(percent - 100) <= 3) return 0
    return (10.0 * ln(percent / 100.0) / ln(3.0)).roundToInt().coerceIn(-10, maximum)
}
