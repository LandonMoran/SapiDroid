package org.sapidroid.tts

import android.content.Intent
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.util.ArrayDeque
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

@RunWith(AndroidJUnit4::class)
class WorkerDrainTest {
    @Test
    fun pullCoalescesAvailableBlocksAndPreservesPartialOffsets() {
        val service = SapiDroidWorkerService()
        val enqueue = SapiDroidWorkerService::class.java.getDeclaredMethod(
            "enqueueAudio", Int::class.javaPrimitiveType, ByteArray::class.java
        ).apply { isAccessible = true }
        assertTrue(enqueue.invoke(service, 0, byteArrayOf(1, 2, 3, 4)) as Boolean)
        assertTrue(enqueue.invoke(service, 0, byteArrayOf(5, 6)) as Boolean)
        assertTrue(enqueue.invoke(service, 0, byteArrayOf(7, 8, 9)) as Boolean)
        val worker = IZiraWorker.Stub.asInterface(service.onBind(Intent()))
        assertArrayEquals(byteArrayOf(1, 2, 3), worker.pull(0, 3, 0))
        assertArrayEquals(byteArrayOf(4, 5, 6, 7, 8), worker.pull(0, 5, 0))
        assertFalse(worker.finished(0))
        assertArrayEquals(byteArrayOf(9), worker.pull(0, 5, 0))
        assertTrue(worker.finished(0))
    }

    @Test
    fun finishedWaitsUntilTheLastAudioHasBeenPulled() {
        val service = SapiDroidWorkerService()
        val queueField = SapiDroidWorkerService::class.java.getDeclaredField("pending")
        queueField.isAccessible = true
        @Suppress("UNCHECKED_CAST")
        val queue = queueField.get(service) as ArrayDeque<ByteArray>
        queue.add(byteArrayOf(1, 2, 3, 4))
        queue.add(byteArrayOf(5, 6))
        val bytesField = SapiDroidWorkerService::class.java.getDeclaredField("pendingBytes")
        bytesField.isAccessible = true
        bytesField.setInt(service, 6)

        val worker = IZiraWorker.Stub.asInterface(service.onBind(Intent()))
        assertFalse(worker.finished(0))
        assertArrayEquals(byteArrayOf(1, 2), worker.pull(0, 2, 0))
        assertFalse(worker.finished(0))
        assertArrayEquals(byteArrayOf(3, 4), worker.pull(0, 2, 0))
        assertFalse(worker.finished(0))
        assertArrayEquals(byteArrayOf(5, 6), worker.pull(0, 2, 0))
        assertTrue(worker.finished(0))
    }

    @Test
    fun cancellationWakesAProducerWaitingForQueueSpace() {
        val service = SapiDroidWorkerService()
        val enqueue = SapiDroidWorkerService::class.java.getDeclaredMethod(
            "enqueueAudio", Int::class.javaPrimitiveType, ByteArray::class.java
        ).apply { isAccessible = true }
        val block = ByteArray(4096)
        repeat(128) { assertTrue(enqueue.invoke(service, 0, block) as Boolean) }
        val entered = CountDownLatch(1)
        val done = CountDownLatch(1)
        val accepted = AtomicBoolean(true)
        val producer = Thread {
            entered.countDown()
            accepted.set(enqueue.invoke(service, 0, block) as Boolean)
            done.countDown()
        }
        producer.start()
        val worker = IZiraWorker.Stub.asInterface(service.onBind(Intent()))
        try {
            assertTrue(entered.await(2, TimeUnit.SECONDS))
            assertFalse(done.await(100, TimeUnit.MILLISECONDS))
            worker.cancel(0)
            assertTrue(done.await(2, TimeUnit.SECONDS))
            assertFalse(accepted.get())
            assertNull(worker.pull(0, 4096, 0))
        } finally {
            worker.cancel(0)
            producer.join(2_000)
        }
    }
}
