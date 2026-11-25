package org.sapidroid.tts

import android.content.Intent
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.util.ArrayDeque

@RunWith(AndroidJUnit4::class)
class WorkerDrainTest {
    @Test
    fun finishedWaitsUntilTheLastAudioHasBeenPulled() {
        val service = SapiDroidWorkerService()
        val queueField = SapiDroidWorkerService::class.java.getDeclaredField("pending")
        queueField.isAccessible = true
        @Suppress("UNCHECKED_CAST")
        val queue = queueField.get(service) as ArrayDeque<ByteArray>
        queue.add(byteArrayOf(1, 2, 3, 4))
        val bytesField = SapiDroidWorkerService::class.java.getDeclaredField("pendingBytes")
        bytesField.isAccessible = true
        bytesField.setInt(service, 4)

        val worker = IZiraWorker.Stub.asInterface(service.onBind(Intent()))
        assertFalse(worker.finished(0))
        assertArrayEquals(byteArrayOf(1, 2), worker.pull(0, 2, 0))
        assertFalse(worker.finished(0))
        assertArrayEquals(byteArrayOf(3, 4), worker.pull(0, 2, 0))
        assertTrue(worker.finished(0))
    }
}
