package org.sapidroid.tts;

final class NativeSapi4 {
    static native String[] listModes(String voiceDir);
}

public final class Sapi4JniListModesTest {
    static {
        System.loadLibrary("sapi4_jni_list_modes_test");
    }

    private static native void configure(int firstCount, int secondCount);
    private static native void failNextMalloc();
    private static native int listCalls();
    private static native int warningLogs();

    public static void main(String[] args) {
        testLargeListPreservesCountAndOrder();
        testAtCapacityNeedsOneCall();
        testShrinkingListUsesSecondCount();
        testGrowingListKeepsAllocatedCapacity();
        testSecondCallFailureReturnsNull();
        testEmptyListReturnsNull();
        testAllocationFailureThrows();
        System.out.println("SAPI4 JNI listModes tests passed");
    }

    private static void testLargeListPreservesCountAndOrder() {
        configure(100, 100);
        String[] modes = NativeSapi4.listModes("mock-voices");
        check(modes != null && modes.length == 100, "large list count");
        check(modes[0].equals("mode-000") && modes[63].equals("mode-063")
                && modes[64].equals("mode-064") && modes[99].equals("mode-099"), "large list order");
        check(listCalls() == 2, "large list uses a size query and a fill call");
    }

    private static void testAtCapacityNeedsOneCall() {
        configure(64, 64);
        String[] modes = NativeSapi4.listModes("mock-voices");
        check(modes != null && modes.length == 64, "64-entry list count");
        check(modes[0].equals("mode-000") && modes[63].equals("mode-063"), "64-entry list order");
        check(listCalls() == 1, "64-entry list avoids an unnecessary allocation");
    }

    private static void testShrinkingListUsesSecondCount() {
        configure(70, 66);
        String[] modes = NativeSapi4.listModes("mock-voices");
        check(modes != null && modes.length == 66, "shrinking list uses actual count");
        check(modes[65].equals("mode-065"), "shrinking list final element");
        check(listCalls() == 2, "shrinking list makes second call");
    }

    private static void testGrowingListKeepsAllocatedCapacity() {
        configure(70, 72);
        String[] modes = NativeSapi4.listModes("mock-voices");
        check(modes != null && modes.length == 70, "growth beyond allocated capacity is bounded");
        check(modes[69].equals("mode-069"), "bounded result remains ordered");
        check(warningLogs() == 1, "growth beyond capacity is logged");
    }

    private static void testSecondCallFailureReturnsNull() {
        configure(70, -1);
        check(NativeSapi4.listModes("mock-voices") == null, "second-call failure returns null");
        check(listCalls() == 2, "second-call failure occurs after retry");
    }

    private static void testEmptyListReturnsNull() {
        configure(0, 0);
        check(NativeSapi4.listModes("mock-voices") == null, "empty list returns null");
        check(listCalls() == 1, "empty list does not retry");
    }

    private static void testAllocationFailureThrows() {
        configure(65, 65);
        failNextMalloc();
        try {
            NativeSapi4.listModes("mock-voices");
            throw new AssertionError("allocation failure should throw OutOfMemoryError");
        } catch (OutOfMemoryError expected) {
            check(listCalls() == 1, "allocation failure occurs before second list call");
        }
    }

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
