import java.io.BufferedWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.text.Normalizer;
import java.util.HexFormat;

// Test-only oracle. Run through check_unicode.py with the pinned OpenJDK 25 GA.
public class UnicodeOracle {
	private static String nfcHex(String value) {
		return HexFormat.of().formatHex(
		    Normalizer.normalize(value, Normalizer.Form.NFC).getBytes(StandardCharsets.UTF_8));
	}

	public static void main(String[] args) throws Exception {
		String runtime = System.getProperty("java.runtime.version");
		System.out.println("Oracle: " + System.getProperty("java.runtime.name") + " " + runtime
		    + "; vendor=" + System.getProperty("java.vendor")
		    + "; java.home=" + System.getProperty("java.home"));
		if (!"25+36-3489".equals(runtime)) {
			throw new IllegalStateException("Expected pinned OpenJDK 25 GA runtime 25+36-3489");
		}
		System.out.println("Character/Normalizer Unicode=16.0 (JDK 25 specification)");
		Path directory = Path.of(args[0]);
		// Decimal scalar, simple lower, simple upper, Java whitespace, NFC UTF-8 hex.
		// Surrogates are not Unicode scalar values; unassigned/noncharacters ARE.
		try (BufferedWriter out = Files.newBufferedWriter(directory.resolve("scalars.tsv"))) {
			for (int cp = 0; cp <= Character.MAX_CODE_POINT; cp++) {
				if (cp >= 0xD800 && cp <= 0xDFFF) {
					continue;
				}
				String value = new String(Character.toChars(cp));
				out.write(cp + "\t" + Character.toLowerCase(cp) + "\t"
				    + Character.toUpperCase(cp) + "\t" + Character.isWhitespace(cp)
				    + "\t" + nfcHex(value) + "\n");
			}
		}
		// Whole sequences, not concatenated singleton normalization results.
		try (var in = Files.newBufferedReader(directory.resolve("sequences.hex"));
		     var out = Files.newBufferedWriter(directory.resolve("normalized.hex"))) {
			String line;
			while ((line = in.readLine()) != null) {
				String value = new String(HexFormat.of().parseHex(line), StandardCharsets.UTF_8);
				out.write(nfcHex(value));
				out.newLine();
			}
		}
	}
}
