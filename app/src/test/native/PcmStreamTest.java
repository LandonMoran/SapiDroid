import java.io.ByteArrayOutputStream;
import java.util.Arrays;

public class PcmStreamTest {
    public interface Sink {
        boolean onPcm(byte[] bytes);
    }

    private static native int emit(Sink sink);

    public static void main(String[] args) {
        System.load(args[0]);
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        int[] calls = {0};
        boolean[] returned = {false};
        int result = emit(bytes -> {
            if (returned[0] || bytes.length > 4096 || bytes.length % 2 != 0)
                throw new AssertionError("invalid or late PCM block");
            calls[0]++;
            output.write(bytes, 0, bytes.length);
            return true;
        });
        returned[0] = true;
        byte[] expected = new byte[5003 * 2];
        for (int i = 0; i < 5003; i++) {
            int sample = (i * 193 + 32768) % 65536 - 32768;
            expected[2 * i] = (byte) sample;
            expected[2 * i + 1] = (byte) (sample >> 8);
        }
        if (result != 0 || calls[0] != 3 || !Arrays.equals(expected, output.toByteArray()))
            throw new AssertionError("incorrect PCM output");

        calls[0] = 0;
        result = emit(bytes -> ++calls[0] < 2);
        if (result != 1 || calls[0] != 2)
            throw new AssertionError("sink cancellation was ignored");

        try {
            emit(bytes -> { throw new IllegalStateException("sink error"); });
            throw new AssertionError("sink exception was swallowed");
        } catch (IllegalStateException expectedError) {
            if (!"sink error".equals(expectedError.getMessage())) throw expectedError;
        }
    }
}
