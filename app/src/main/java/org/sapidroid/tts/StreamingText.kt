package org.sapidroid.tts

internal object StreamingText {
    fun split(text: String): List<String> {
        val trimmed = text.trim()
        if (trimmed.length <= 350) return listOf(trimmed)
        val out = mutableListOf<String>()
        var start = 0
        var i = 0
        while (i < trimmed.length) {
            val c = trimmed[i]
            val endOfSentence = (c == '.' || c == '!' || c == '?') &&
                (i + 1 >= trimmed.length || trimmed[i + 1].isWhitespace())
            if ((endOfSentence || c == '\n') && i + 1 - start >= 20) {
                var end = i + 1
                while (end < trimmed.length && trimmed[end].isWhitespace()) end++
                out.add(trimmed.substring(start, end).trim())
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
                out.add(trimmed.substring(start, end).trim())
                while (end < trimmed.length && trimmed[end].isWhitespace()) end++
                start = end
                i = end
                continue
            }
            i++
        }
        if (start < trimmed.length) out.add(trimmed.substring(start).trim())
        return out.filter { it.isNotEmpty() }.ifEmpty { listOf(trimmed) }
    }
}
