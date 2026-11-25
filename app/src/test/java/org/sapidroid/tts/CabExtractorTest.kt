package org.sapidroid.tts

import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.file.Files
import java.util.zip.Deflater
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class CabExtractorTest {
    private fun cabinet(data: ByteArray, declaredSize: Int): ByteArray {
        val deflater = Deflater(Deflater.DEFAULT_COMPRESSION, true)
        val compressed = ByteArray(1024)
        val count = try {
            deflater.setInput(data)
            deflater.finish()
            deflater.deflate(compressed)
        } finally {
            deflater.end()
        }
        val out = ByteArrayOutputStream()
        fun u16(n: Int) { out.write(n and 255); out.write((n ushr 8) and 255) }
        fun u32(n: Int) { u16(n and 65535); u16(n ushr 16) }
        out.write("MSCF".toByteArray())
        u32(0); u32(0); u32(0); u32(44); u32(0)
        out.write(3); out.write(1); u16(1); u16(1); u16(0); u16(0); u16(0)
        u32(62); u16(1); u16(1)
        u32(declaredSize); u32(0); u16(0); u16(0); u16(0); u16(0)
        out.write('x'.code); out.write(0)
        u32(0); u16(count + 2); u16(declaredSize)
        out.write('C'.code); out.write('K'.code); out.write(compressed, 0, count)
        return out.toByteArray()
    }

    @Test
    fun extractsValidBlockAndRejectsOversizedBlock() {
        val dir = Files.createTempDirectory("cab-test").toFile()
        try {
            val data = "spoken voice data".toByteArray()
            val cab = File(dir, "test.cab")
            cab.writeBytes(cabinet(data, data.size))
            val files = CabExtractor.extract(cab, { "voice" }, dir)
            assertArrayEquals(data, files.single().readBytes())
            cab.writeBytes(cabinet(data, data.size - 1))
            assertThrows(java.io.IOException::class.java) {
                CabExtractor.extract(cab, { "voice" }, dir)
            }
        } finally {
            dir.deleteRecursively()
        }
    }
}
