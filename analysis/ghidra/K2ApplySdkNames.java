// Kessen II analysis: apply Sony SDK function names (from ps2_analyzer's embedded
// SCE signature database) to the analyzed program.
//
// Input CSV: header "name,address", address as 0x-prefixed hex. A name is applied
// only where the function has no name yet (Ghidra default FUN_*) or does not
// exist; names from the ELF symbol table / STABS are never overwritten.
//
// Usage (postScript): K2ApplySdkNames.java <sdk-names.csv> <report.txt>
//@category Kessen2

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class K2ApplySdkNames extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) {
            throw new IllegalArgumentException("usage: K2ApplySdkNames.java <sdk-names.csv> <report.txt>");
        }
        File csv = new File(args[0]);
        File report = new File(args[1]);

        // address -> name, sorted by address for a deterministic report.
        TreeMap<Long, String> entries = new TreeMap<>();
        List<String> lines = Files.readAllLines(csv.toPath(), StandardCharsets.UTF_8);
        for (int i = 1; i < lines.size(); i++) { // skip header
            String line = lines.get(i).trim();
            if (line.isEmpty()) {
                continue;
            }
            String[] parts = line.split(",");
            if (parts.length != 2 || !parts[0].matches("[A-Za-z_][A-Za-z0-9_]*")
                || !parts[1].matches("0x[0-9A-Fa-f]{1,8}")) {
                throw new IllegalArgumentException(csv + ":" + (i + 1) + ": bad row: " + line);
            }
            entries.put(Long.parseUnsignedLong(parts[1].substring(2), 16), parts[0]);
        }

        int renamed = 0, created = 0, alreadyNamed = 0;
        List<String> conflicts = new ArrayList<>();
        List<String> failures = new ArrayList<>();

        for (var e : entries.entrySet()) {
            Address addr = toAddr(e.getKey());
            String name = e.getValue();
            Function f = getFunctionAt(addr);
            if (f == null) {
                if (getInstructionAt(addr) == null) {
                    disassemble(addr);
                }
                f = createFunction(addr, null); // default name; renamed below as IMPORTED
                if (f == null) {
                    failures.add(String.format("0x%08X %s: could not create function", e.getKey(), name));
                } else {
                    f.setName(name, SourceType.IMPORTED);
                    created++;
                }
                continue;
            }
            if (f.getSymbol().getSource() == SourceType.DEFAULT) {
                f.setName(name, SourceType.IMPORTED);
                renamed++;
            } else if (f.getName().equals(name)) {
                alreadyNamed++;
            } else {
                conflicts.add(String.format("0x%08X sdk=%s existing=%s (%s)",
                    e.getKey(), name, f.getName(), f.getSymbol().getSource()));
            }
        }

        report.getParentFile().mkdirs();
        try (PrintWriter w = new PrintWriter(report, StandardCharsets.UTF_8)) {
            w.println("sdk_names_in = " + entries.size());
            w.println("renamed_default_functions = " + renamed);
            w.println("created_functions = " + created);
            w.println("already_named_same = " + alreadyNamed);
            w.println("conflicts = " + conflicts.size());
            w.println("failures = " + failures.size());
            for (String c : conflicts) {
                w.println("conflict " + c);
            }
            for (String f : failures) {
                w.println("failure " + f);
            }
        }
        println(String.format("K2ApplySdkNames: %d in, %d renamed, %d created, %d same, %d conflicts, %d failures",
            entries.size(), renamed, created, alreadyNamed, conflicts.size(), failures.size()));
    }
}
