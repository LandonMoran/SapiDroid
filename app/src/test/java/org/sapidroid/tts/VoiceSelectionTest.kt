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

    @Test fun exactAndCaseInsensitiveNamesPreserveCatalogPriority() {
        for (voice in VoiceCatalog.all) {
            val first = VoiceCatalog.all.first { it.label.equals(voice.label, ignoreCase = true) }
            assertEquals(first, VoiceCatalog.infoFor(voice.label))
            assertEquals(first, VoiceCatalog.infoFor(voice.label.uppercase(java.util.Locale.ROOT)))
        }
        assertEquals(VoiceCatalog.infoFor("Sam (SAPI 4)"), VoiceCatalog.infoFor("sam"))
        assertNull(VoiceCatalog.infoFor("not an installed voice"))
    }
}
