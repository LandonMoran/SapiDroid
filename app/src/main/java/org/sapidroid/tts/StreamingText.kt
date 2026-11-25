package org.sapidroid.tts

internal object StreamingText {
    inline fun forEachChunk(text: String, consume: (String) -> Boolean) {
        val trimmed = text.trim()
        if (trimmed.length <= 350) {
            consume(trimmed)
            return
        }
        var emitted = false
        var start = 0
        var i = 0
        while (i < trimmed.length) {
            val c = trimmed[i]
            val endOfSentence = (c == '.' || c == '!' || c == '?') &&
                (i + 1 >= trimmed.length || trimmed[i + 1].isWhitespace())
            if ((endOfSentence || c == '\n') && i + 1 - start >= 20) {
                var end = i + 1
                val chunk = trimmed.substring(start, end).trim()
                if (chunk.isNotEmpty()) {
                    emitted = true
                    if (!consume(chunk)) return
                }
                while (end < trimmed.length && trimmed[end].isWhitespace()) end++
                start = end
                i = end
                continue
            }
            if (i + 1 - start >= 450) {
                var end = i
                while (end > start && !trimmed[end].isWhitespace()) end--
                if (end <= start) {
                    end = i + 1
                    if (end < trimmed.length && Character.isHighSurrogate(trimmed[end - 1]) &&
                        Character.isLowSurrogate(trimmed[end])) end++
                }
                val chunk = trimmed.substring(start, end).trim()
                if (chunk.isNotEmpty()) {
                    emitted = true
                    if (!consume(chunk)) return
                }
                while (end < trimmed.length && trimmed[end].isWhitespace()) end++
                start = end
                i = end
                continue
            }
            i++
        }
        if (start < trimmed.length) {
            val chunk = trimmed.substring(start).trim()
            if (chunk.isNotEmpty()) {
                emitted = true
                if (!consume(chunk)) return
            }
        }
        if (!emitted) consume(trimmed)
    }
}
