package org.sapidroid.tts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class StreamingTextTest {
    @Test fun streamsTheFirstSentenceOfLongText() {
        val first = "This is the first sentence."
        val chunks = StreamingText.split(first + " " + "word ".repeat(100))
        assertEquals(first, chunks.first())
    }

    @Test fun doesNotDropCharactersInLongUnbrokenText() {
        val text = "a".repeat(449) + "\uD83D\uDE00" + "b".repeat(500)
        val chunks = StreamingText.split(text)
        assertEquals(text, chunks.joinToString(""))
        assertFalse(chunks.any { Character.isHighSurrogate(it.last()) || Character.isLowSurrogate(it.first()) })
    }

    @Test fun keepsShortUtterancesTogether() {
        val text = "Short sentence. Another short sentence."
        assertEquals(listOf(text), StreamingText.split(text))
    }
}
