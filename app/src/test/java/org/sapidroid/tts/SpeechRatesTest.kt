package org.sapidroid.tts

import org.junit.Assert.assertEquals
import org.junit.Test

class SpeechRatesTest {
    @Test fun supportsEachEnginesExtendedFastRates() {
        assertEquals(0, sapiRate(100, 20))
        assertEquals(10, sapiRate(300, 20))
        assertEquals(20, sapiRate(900, 20))
        assertEquals(18, sapiRate(900, 18))
        assertEquals(-10, sapiRate(1, 18))
    }
}
