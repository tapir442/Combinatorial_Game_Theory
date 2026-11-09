import org.cgsuite.core.impartial.TakeAndBreak;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Prototype of a fast path for plain octal / hexadecimal codes in CGSuite's
 * NimValueSequence, compared with CGSuite's generic engine on the same JVM.
 *
 * Build and run with CGSuite's bundled JRE (Java 17):
 *
 *   CP=~/CGSuite/CGSuite/modules/ext/cgsuite-core.jar
 *   javac --release 17 -cp $CP FastOctalBench.java
 *   ~/CGSuite/jre/bin/java -cp $CP:. FastOctalBench bench 0.007,0.6,0.77 2000,20000,100000
 *   ~/CGSuite/jre/bin/java -cp $CP:. FastOctalBench check all2 2000
 *
 * Output of "bench": code,n,cgsuite_seconds,fastpath_seconds,identical
 */
public class FastOctalBench {

    /**
     * Fast path.  d[k] = code digit k (bit j: removing k tokens may leave exactly
     * j nonempty heaps, j = 0..3); d[0] may only use bits 2 and 3.
     * Returns G(0) .. G(n-1).
     */
    static int[] fastOctal(int[] d, int n) {
        int t = d.length - 1;
        int[] g = new int[n];
        int[] seen = new int[1024];          // stamp table: seen[v] == h  <=>  v is an option value of h
        Arrays.fill(seen, -1);
        int maxG = 0;
        for (int h = 1; h < n; h++) {
            for (int k = 0; k <= Math.min(t, h); k++) {
                int dk = d[k];
                if (dk == 0) continue;
                int m = h - k;
                if ((dk & 1) != 0 && m == 0) seen[0] = h;
                if ((dk & 2) != 0 && k > 0 && m >= 1) seen[g[m]] = h;
                if ((dk & 4) != 0)
                    for (int a = 1, b = m - 1; a <= b; a++, b--) seen[g[a] ^ g[b]] = h;
                if ((dk & 8) != 0)
                    for (int a = 1; 3 * a <= m; a++)
                        for (int b = a; a + 2 * b <= m; b++) seen[g[a] ^ g[b] ^ g[m - a - b]] = h;
            }
            int v = 0;
            while (seen[v] == h) v++;
            g[h] = v;
            if (v > maxG) {                  // keep room for every XOR of values seen so far
                maxG = v;
                int need = 2 * Integer.highestOneBit(maxG) + 2;
                if (need > seen.length) {
                    int old = seen.length;
                    seen = Arrays.copyOf(seen, Math.max(need, 2 * old));
                    Arrays.fill(seen, old, seen.length, -1);
                }
            }
        }
        return g;
    }

    /** Digits of a plain code "d0.d1d2..." (hexadecimal digits). */
    static int[] digits(String code) {
        int dot = code.indexOf('.');
        String s = code.substring(dot + 1);
        int[] d = new int[s.length() + 1];
        d[0] = dot > 0 ? Character.digit(code.charAt(dot - 1), 16) & ~3 : 0;
        for (int i = 0; i < s.length(); i++) d[i + 1] = Character.digit(s.charAt(i), 16);
        return d;
    }

    static List<String> codes(String spec) {
        List<String> out = new ArrayList<>();
        if (spec.equals("all2")) {           // all 1- and 2-digit octal codes, plus d0 and hex samples
            for (int a = 1; a < 8; a++) out.add("0." + a);
            for (int a = 0; a < 8; a++) for (int b = 1; b < 8; b++) out.add("0." + a + b);
            out.addAll(List.of("4.0", "4.07", "8.0", "C.0", "0.F", "0.F6", "0.9", "0.28", "0.9C", "0.137", "0.007", "0.106"));
        } else {
            out.addAll(Arrays.asList(spec.split(",")));
        }
        return out;
    }

    public static void main(String[] args) {
        String mode = args[0];
        List<String> codes = codes(args[1]);
        if (mode.equals("check")) {
            int n = Integer.parseInt(args[2]);
            int bad = 0;
            for (String c : codes) {
                boolean hex = Arrays.stream(digits(c)).anyMatch(x -> (x & 8) != 0);
                int nn = hex ? Math.min(n, 400) : n;  // three-heap moves are O(n^2) per heap size
                int[] cg = new TakeAndBreak(c).sequence().nimValues(nn);
                int[] fa = fastOctal(digits(c), nn + 1);
                if (!Arrays.equals(cg, Arrays.copyOfRange(fa, 1, nn + 1))) {
                    System.out.println("DIFFERENT: " + c);
                    bad++;
                }
            }
            System.out.printf("checked %d codes against CGSuite: %d different%n", codes.size(), bad);
            System.exit(bad == 0 ? 0 : 1);
        }
        int[] sizes = Arrays.stream(args[2].split(",")).mapToInt(Integer::parseInt).toArray();
        for (String c : codes) { new TakeAndBreak(c).sequence().nimValues(3000); fastOctal(digits(c), 3001); }  // JIT warm-up
        System.out.println("code,n,cgsuite_s,fastpath_s,identical");
        for (String c : codes) for (int n : sizes) {
            long t0 = System.nanoTime();
            int[] cg = new TakeAndBreak(c).sequence().nimValues(n);      // G(1..n)
            long t1 = System.nanoTime();
            int[] fa = fastOctal(digits(c), n + 1);                      // G(0..n)
            long t2 = System.nanoTime();
            boolean same = Arrays.equals(cg, Arrays.copyOfRange(fa, 1, n + 1));
            System.out.printf("%s,%d,%.4f,%.4f,%s%n", c, n, (t1 - t0) / 1e9, (t2 - t1) / 1e9, same);
        }
    }
}
