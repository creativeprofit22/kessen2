// Kessen II analysis: split far-away body chunks out of functions before export.
//
// Ghidra may fold a tail-jump target into the caller's body (e.g. a shared `jr $ra`
// return stub) even when other functions lie in between. ExportPS2Functions exports
// each function as [entry, max body address], so such a function swallows everything
// between the two chunks (seen: FUN_005d10e0, 60 bytes of body exported as 160 KB),
// which ps2_recomp then emits as one huge, near-uncompilable C++ function.
//
// A body range is "far" when another function's entry point lies between it and the
// entry range. Far ranges are removed from the body and become functions of their own,
// so the jump is a plain tail call. Contiguous and interleaved chunks are left alone.
//
// Usage (postScript, before ExportPS2Functions): K2SplitFarChunks.java <report.txt>
//@category Kessen2

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;

public class K2SplitFarChunks extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("usage: K2SplitFarChunks.java <report.txt>");
        }
        File report = new File(args[0]);
        FunctionManager fm = currentProgram.getFunctionManager();

        // Snapshot first: bodies change while we split.
        List<Function> functions = new ArrayList<>();
        for (FunctionIterator it = fm.getFunctions(true); it.hasNext();) {
            functions.add(it.next());
        }

        List<String> lines = new ArrayList<>();
        int split = 0, created = 0, failed = 0;
        for (Function f : functions) {
            if (monitor.isCancelled()) {
                break;
            }
            AddressSetView body = f.getBody();
            if (body.getNumAddressRanges() < 2) {
                continue;
            }
            Address entry = f.getEntryPoint();
            AddressRange entryRange = body.getRangeContaining(entry);
            if (entryRange == null) {
                continue;
            }
            List<AddressRange> far = new ArrayList<>();
            for (AddressRange r : body.getAddressRanges()) {
                if (r.equals(entryRange) || !hasOtherEntryBetween(fm, f, entryRange, r)) {
                    continue;
                }
                far.add(r);
            }
            if (far.isEmpty()) {
                continue;
            }
            AddressSet newBody = new AddressSet(body);
            for (AddressRange r : far) {
                newBody.delete(r);
            }
            long before = body.getMaxAddress().subtract(entry) + 1;
            try {
                f.setBody(newBody);
            } catch (Exception e) { // OverlappingFunctionException etc.
                failed++;
                lines.add(String.format("keep   %s @ 0x%s: setBody rejected (%s)", f.getName(), entry,
                    e.getClass().getSimpleName()));
                continue;
            }
            split++;
            long after = newBody.getMaxAddress().subtract(entry) + 1;
            lines.add(String.format("split  %s @ 0x%s: exported span 0x%x -> 0x%x, %d far chunk(s)",
                f.getName(), entry, before, after, far.size()));
            for (AddressRange r : far) {
                Address start = r.getMinAddress();
                if (getFunctionAt(start) != null) {
                    lines.add(String.format("  chunk 0x%s-0x%s: already a function", start, r.getMaxAddress()));
                    continue;
                }
                Function nf = createFunction(start, null);
                if (nf == null) {
                    failed++;
                    lines.add(String.format("  chunk 0x%s-0x%s: createFunction failed", start, r.getMaxAddress()));
                } else {
                    created++;
                    lines.add(String.format("  chunk 0x%s-0x%s: new function %s", start, r.getMaxAddress(),
                        nf.getName()));
                }
            }
        }

        try (PrintWriter w = new PrintWriter(report, StandardCharsets.UTF_8)) {
            w.printf("functions_split = %d%nchunk_functions_created = %d%nsplit_failures = %d%n", split,
                created, failed);
            lines.forEach(w::println);
        }
        println(String.format("K2SplitFarChunks: split %d function(s), created %d, failures %d", split,
            created, failed));
    }

    /** True if a function other than {@code self} has its entry between {@code a} and {@code b}. */
    private static boolean hasOtherEntryBetween(FunctionManager fm, Function self, AddressRange a,
        AddressRange b) {
        Address lo, hi;
        if (a.getMaxAddress().compareTo(b.getMinAddress()) < 0) {
            lo = a.getMaxAddress();
            hi = b.getMinAddress();
        } else {
            lo = b.getMaxAddress();
            hi = a.getMinAddress();
        }
        for (FunctionIterator it = fm.getFunctions(new AddressSet(lo, hi), true); it.hasNext();) {
            Function other = it.next();
            if (!other.equals(self)) {
                return true;
            }
        }
        return false;
    }
}
