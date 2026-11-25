package org.sapidroid.tts

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class WorkerRetryTest {
    @Test fun retriesOnlyBeforeTheCallbackStarts() {
        assertTrue(mayRetryWorker(false, false, false))
        assertFalse(mayRetryWorker(false, false, true))
        assertFalse(mayRetryWorker(true, false, false))
        assertFalse(mayRetryWorker(false, true, false))
    }
}
