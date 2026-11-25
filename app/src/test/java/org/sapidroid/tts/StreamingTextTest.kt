package org.sapidroid.tts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class StreamingTextTest {
    private fun chunks(text: String): List<String> = mutableListOf<String>().apply {
        StreamingText.forEachChunk(text) { add(it); true }
    }

    @Test fun streamsTheFirstSentenceOfLongText() {
        val first = "This is the first sentence."
        val chunks = chunks(first + " " + "word ".repeat(100))
        assertEquals(first, chunks.first())
    }

    @Test fun doesNotDropCharactersInLongUnbrokenText() {
        val text = "a".repeat(449) + "\uD83D\uDE00" + "b".repeat(500)
        val chunks = chunks(text)
        assertEquals(text, chunks.joinToString(""))
        assertFalse(chunks.any { Character.isHighSurrogate(it.last()) || Character.isLowSurrogate(it.first()) })
    }

    @Test fun keepsShortUtterancesTogether() {
        val text = "Short sentence. Another short sentence."
        assertEquals(listOf(text), chunks(text))
    }

    @Test fun stopsProducingChunksWhenConsumerStops() {
        val first = "This is the first sentence."
        val seen = mutableListOf<String>()
        StreamingText.forEachChunk(first + " " + "word ".repeat(1000)) {
            seen.add(it)
            false
        }
        assertEquals(listOf(first), seen)
    }

    @Test fun ignoresEmptyChunksAndKeepsShortBlankText() {
        val text = "Some initial text here.\n" + "\n".repeat(500) + "Last sentence."
        assertEquals(listOf("Some initial text here.", "Last sentence."), chunks(text))
        assertEquals(listOf(""), chunks("   "))
    }
}
