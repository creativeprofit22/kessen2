// Kessen II analysis: write deterministic reports about the analyzed boot ELF.
//
// Outputs (all sorted by address unless stated):
//   summary.txt        memory blocks, .mdebug/STABS and .symtab presence, function
//                      counts (named vs unnamed, per SourceType), SDK families
//   sdk-functions.csv  address,name,source for functions carrying an SDK name
//   vu0-sites.csv      address,function,mnemonic,kind for every COP2/VU0 instruction
//   vu0-functions.csv  function_address,function,sites,macro_ops (sites desc)
//
// Usage (postScript): K2Report.java <reports-dir> <sdk-names.csv>
//@category Kessen2

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class K2Report extends GhidraScript {

    private static final Pattern SCE_FAMILY = Pattern.compile("^_*(sce[A-Z][a-z0-9]*)");

    // EE primary opcodes (bits 31..26).
    private static final int OP_COP2 = 0x12;
    private static final int OP_LQC2 = 0x36;
    private static final int OP_SQC2 = 0x3E;

    private static final long SHT_SYMTAB = 2;
    private static final long SHT_MIPS_DEBUG = 0x70000005L; // .mdebug

    private static final class Vu0Func {
        String name;
        int sites;
        int macroOps;
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) {
            throw new IllegalArgumentException("usage: K2Report.java <reports-dir> <sdk-names.csv>");
        }
        File dir = new File(args[0]);
        dir.mkdirs();
        Set<String> sdkNames = readSdkNames(new File(args[1]));

        List<String> summary = new ArrayList<>();
        summary.add("program = " + currentProgram.getName());
        summary.add("language = " + currentProgram.getLanguageID());
        summary.add("compiler_spec = " + currentProgram.getCompilerSpec().getCompilerSpecID());
        summary.add("image_base = " + currentProgram.getImageBase());
        summary.add("");

        // --- Memory blocks ---
        summary.add("[memory_blocks] name start end size perms initialized loaded");
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            String n = b.getName();
            summary.add(String.format("  %-24s %s %s 0x%X %s%s%s %b %b", n, b.getStart(), b.getEnd(),
                b.getSize(), b.isRead() ? "r" : "-", b.isWrite() ? "w" : "-", b.isExecute() ? "x" : "-",
                b.isInitialized(), b.isLoaded()));
        }

        // --- ELF section header table (types, not just names) ---
        summary.add("");
        boolean hasMdebug = false;
        long symtabSize = -1;
        MemoryBlock shdrs = getMemoryBlock("_elfSectionHeaders");
        MemoryBlock shstr = getMemoryBlock(".shstrtab");
        if (shdrs == null) {
            summary.add("elf_section_headers = absent");
        } else {
            byte[] sh = getBytes(shdrs.getStart(), (int) shdrs.getSize());
            byte[] names = shstr == null ? new byte[0] : getBytes(shstr.getStart(), (int) shstr.getSize());
            TreeMap<String, Integer> typeCounts = new TreeMap<>();
            for (int off = 0; off + 40 <= sh.length; off += 40) {
                int nameOff = le32(sh, off);
                long type = le32(sh, off + 4) & 0xFFFFFFFFL;
                long size = le32(sh, off + 20) & 0xFFFFFFFFL;
                String name = cString(names, nameOff);
                typeCounts.merge(String.format("0x%X", type), 1, Integer::sum);
                if (type == SHT_MIPS_DEBUG || name.equals(".mdebug")) {
                    hasMdebug = true;
                }
                if (type == SHT_SYMTAB) {
                    symtabSize = size;
                }
            }
            summary.add("elf_section_count = " + sh.length / 40);
            summary.add("[elf_section_types] sh_type = count");
            for (Map.Entry<String, Integer> e : typeCounts.entrySet()) {
                summary.add("  " + e.getKey() + " = " + e.getValue());
            }
        }
        summary.add("mdebug_present = " + hasMdebug);
        summary.add("symtab_present = " + (symtabSize >= 0));
        summary.add("symtab_entries = " + (symtabSize > 0 ? symtabSize / 16 : 0));

        // --- Functions ---
        TreeMap<String, Integer> bySource = new TreeMap<>();
        TreeMap<String, Integer> sdkFamilies = new TreeMap<>();
        List<String> sdkRows = new ArrayList<>();
        int total = 0, named = 0, sdkNamed = 0, importedNonSdk = 0;
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            total++;
            SourceType src = f.getSymbol().getSource();
            bySource.merge(src.toString(), 1, Integer::sum);
            if (src != SourceType.DEFAULT) {
                named++;
            }
            if (src == SourceType.IMPORTED && !sdkNames.contains(f.getName()) && !f.getName().equals("entry")) {
                importedNonSdk++;
            }
            if (sdkNames.contains(f.getName())) {
                sdkNamed++;
                sdkRows.add(f.getEntryPoint() + "," + f.getName() + "," + src);
                Matcher m = SCE_FAMILY.matcher(f.getName());
                sdkFamilies.merge(m.find() ? m.group(1) : "(non-sce: kernel/libc/crt)", 1, Integer::sum);
            }
        }
        summary.add("");
        summary.add("functions_total = " + total);
        summary.add("functions_named = " + named);
        summary.add("functions_unnamed = " + (total - named));
        for (Map.Entry<String, Integer> e : bySource.entrySet()) {
            summary.add("functions_source_" + e.getKey() + " = " + e.getValue());
        }
        // IMPORTED names minus SDK names (K2ApplySdkNames) and the ELF entry point. When both
        // .mdebug and .symtab exist this also counts .symtab names: K2ApplySdkNames never
        // overwrites non-default names, so the two loader sources cannot be told apart here.
        summary.add("stabs_named_functions = " + (hasMdebug ? importedNonSdk : 0));
        summary.add("sdk_named_functions = " + sdkNamed);
        // Fold one-member prefixes (sceOpen, sceClose, ...) into one bucket.
        TreeMap<String, Integer> families = new TreeMap<>();
        for (Map.Entry<String, Integer> e : sdkFamilies.entrySet()) {
            String key = e.getValue() > 1 || !e.getKey().startsWith("sce") ? e.getKey() : "sce* (single-function prefixes)";
            families.merge(key, e.getValue(), Integer::sum);
        }
        summary.add("[sdk_families]");
        for (Map.Entry<String, Integer> e : families.entrySet()) {
            summary.add("  " + e.getKey() + " = " + e.getValue());
        }

        // --- COP2 / VU0 sites ---
        List<String> vuRows = new ArrayList<>();
        TreeMap<String, Integer> vuKinds = new TreeMap<>();
        TreeMap<Address, Vu0Func> vuFuncs = new TreeMap<>();
        int vuOutside = 0;
        InstructionIterator ii = currentProgram.getListing().getInstructions(true);
        while (ii.hasNext()) {
            Instruction ins = ii.next();
            if (ins.getLength() != 4) {
                continue;
            }
            byte[] b = ins.getBytes();
            int word = le32(b, 0);
            String kind = cop2Kind(word);
            if (kind == null) {
                continue;
            }
            vuKinds.merge(kind, 1, Integer::sum);
            Function f = getFunctionContaining(ins.getAddress());
            String fname = f == null ? "" : f.getName();
            vuRows.add(ins.getAddress() + "," + fname + "," + ins.getMnemonicString() + "," + kind);
            if (f == null) {
                vuOutside++;
                continue;
            }
            Vu0Func vf = vuFuncs.computeIfAbsent(f.getEntryPoint(), a -> new Vu0Func());
            vf.name = f.getName();
            vf.sites++;
            if (kind.equals("macro")) {
                vf.macroOps++;
            }
        }
        summary.add("");
        summary.add("vu0_sites_total = " + vuRows.size());
        for (Map.Entry<String, Integer> e : vuKinds.entrySet()) {
            summary.add("vu0_sites_" + e.getKey() + " = " + e.getValue());
        }
        summary.add("vu0_sites_outside_functions = " + vuOutside);
        summary.add("vu0_functions = " + vuFuncs.size());
        summary.add("vu0_functions_with_macro_ops = "
            + vuFuncs.values().stream().filter(v -> v.macroOps > 0).count());

        List<Map.Entry<Address, Vu0Func>> ranked = new ArrayList<>(vuFuncs.entrySet());
        ranked.sort((x, y) -> y.getValue().sites != x.getValue().sites
            ? Integer.compare(y.getValue().sites, x.getValue().sites)
            : x.getKey().compareTo(y.getKey()));
        List<String> vuFuncRows = new ArrayList<>();
        for (Map.Entry<Address, Vu0Func> e : ranked) {
            Vu0Func v = e.getValue();
            vuFuncRows.add(e.getKey() + "," + v.name + "," + v.sites + "," + v.macroOps);
        }

        write(new File(dir, "summary.txt"), null, summary);
        write(new File(dir, "sdk-functions.csv"), "address,name,source", sdkRows);
        write(new File(dir, "vu0-sites.csv"), "address,function,mnemonic,kind", vuRows);
        write(new File(dir, "vu0-functions.csv"), "function_address,function,sites,macro_ops", vuFuncRows);
        println(String.format("K2Report: %d functions (%d named, %d unnamed), %d SDK-named, %d VU0 sites in %d functions, mdebug=%b",
            total, named, total - named, sdkNamed, vuRows.size(), vuFuncs.size(), hasMdebug));
    }

    /** Classifies an EE instruction word as a COP2 (VU0) use, or returns null. */
    private static String cop2Kind(int word) {
        int op = word >>> 26;
        if (op == OP_LQC2 || op == OP_SQC2) {
            return "load_store";
        }
        if (op != OP_COP2) {
            return null;
        }
        if ((word & (1 << 25)) != 0) {
            return "macro"; // CO bit: VU0 macro-mode operation (incl. vcallms/vcallmsr)
        }
        int rs = (word >>> 21) & 0x1F;
        switch (rs) {
            case 0x01: case 0x02: case 0x05: case 0x06:
                return "transfer"; // qmfc2 / cfc2 / qmtc2 / ctc2
            case 0x08:
                return "branch"; // bc2*
            default:
                return "other";
        }
    }

    private static int le32(byte[] b, int off) {
        return (b[off] & 0xFF) | (b[off + 1] & 0xFF) << 8 | (b[off + 2] & 0xFF) << 16 | (b[off + 3] & 0xFF) << 24;
    }

    private static String cString(byte[] b, int off) {
        if (off < 0 || off >= b.length) {
            return "";
        }
        int end = off;
        while (end < b.length && b[end] != 0) {
            end++;
        }
        return new String(b, off, end - off, StandardCharsets.US_ASCII);
    }

    private static Set<String> readSdkNames(File csv) throws Exception {
        Set<String> names = new HashSet<>();
        List<String> lines = Files.readAllLines(csv.toPath(), StandardCharsets.UTF_8);
        for (int i = 1; i < lines.size(); i++) {
            String line = lines.get(i).trim();
            int comma = line.indexOf(',');
            if (comma > 0) {
                names.add(line.substring(0, comma));
            }
        }
        return names;
    }

    private static void write(File file, String header, List<String> rows) throws Exception {
        try (PrintWriter w = new PrintWriter(file, StandardCharsets.UTF_8)) {
            if (header != null) {
                w.print(header + "\n");
            }
            for (String r : rows) {
                w.print(r + "\n");
            }
        }
    }
}
