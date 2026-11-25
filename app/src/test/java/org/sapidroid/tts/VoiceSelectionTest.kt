package org.sapidroid.tts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class VoiceSelectionTest {
    private val david = VoiceCatalog.infoFor("David")!!
    private val zira = VoiceCatalog.infoFor("Zira")!!

    @Test fun pickerChangeOverridesStaleClientVoiceOnNextUtterance() {
        assertEquals(zira, VoiceCatalog.resolveForSynthesis(
            listOf(david, zira), zira, david.label, "eng", "USA"
        ))
    }

    @Test fun removedVoiceDoesNotPreventAnotherInstalledVoiceFromSpeaking() {
        assertEquals(david, VoiceCatalog.resolveForSynthesis(
            listOf(david), zira, zira.label, "eng", "USA"
        ))
    }

    @Test fun noInstalledVoiceUsesFallback() {
        assertNull(VoiceCatalog.resolveForSynthesis(emptyList(), null, david.label, "eng", "USA"))
    }
}
