// Kessen II analysis: pin the auto-analysis options before analyzeHeadless runs
// analysis, then write every effective analyzer option to a report file.
//
// Usage (preScript): K2SetAnalysisOptions.java <report.txt>
//@category Kessen2

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.TreeMap;

import ghidra.app.script.GhidraScript;

public class K2SetAnalysisOptions extends GhidraScript {

    // Explicit choices; everything else stays at Ghidra's default.
    private static final Map<String, String> OVERRIDES = new LinkedHashMap<>();
    static {
        // EE plugin: parse .mdebug STABS if present (no-op otherwise).
        OVERRIDES.put("STABS", "true");
        // EE plugin: R5900 constant propagation / reference creation.
        OVERRIDES.put("MIPS-R5900 Constant Reference Analyzer", "true");
        // EE plugin README: disable if decompilation fails; also cuts run time.
        OVERRIDES.put("Decompiler Parameter ID", "false");
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("usage: K2SetAnalysisOptions.java <report.txt>");
        }
        File report = new File(args[0]);

        Map<String, String> before = getCurrentAnalysisOptionsAndValues(currentProgram);
        for (Map.Entry<String, String> e : OVERRIDES.entrySet()) {
            if (!before.containsKey(e.getKey())) {
                throw new IllegalStateException("analysis option not found: " + e.getKey()
                    + " (is the ghidra-emotionengine-reloaded extension installed?)");
            }
            setAnalysisOption(currentProgram, e.getKey(), e.getValue());
        }

        Map<String, String> after = new TreeMap<>(getCurrentAnalysisOptionsAndValues(currentProgram));
        report.getParentFile().mkdirs();
        try (PrintWriter w = new PrintWriter(report, StandardCharsets.UTF_8)) {
            w.println("# Effective auto-analysis options (sorted). '*' = set by K2SetAnalysisOptions.");
            w.println("# language = " + currentProgram.getLanguageID()
                + ", compiler spec = " + currentProgram.getCompilerSpec().getCompilerSpecID());
            for (Map.Entry<String, String> e : after.entrySet()) {
                String mark = OVERRIDES.containsKey(e.getKey()) ? "* " : "  ";
                w.println(mark + e.getKey() + " = " + e.getValue());
            }
        }
        println("K2SetAnalysisOptions: " + OVERRIDES.size() + " overrides, "
            + after.size() + " options written to " + report);
    }
}
