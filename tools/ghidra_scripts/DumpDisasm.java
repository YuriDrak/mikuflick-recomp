// Headless Ghidra post-script: write annotated disassembly of every function whose
// name contains one of the given substrings to <outdir>/<name>.s.
// Each instruction line shows address, bytes-free text, and for memory references
// the referenced symbol plus the 32-bit value at that address (hex / float / int),
// which resolves literal-pool constants (positions, scales, sprite ids).
//
// usage: analyzeHeadless ... -postScript DumpDisasm.java <outdir> <pat1> [pat2 ...]
//@category MikuFlick

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpDisasm extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { printerr("usage: DumpDisasm.java <outdir> <pattern>..."); return; }
        File out = new File(args[0]);
        out.mkdirs();
        Memory mem = currentProgram.getMemory();
        int count = 0;
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Function f = it.next();
            String name = f.getName(true);
            boolean hit = false;
            for (int i = 1; i < args.length; i++) if (name.contains(args[i])) { hit = true; break; }
            if (!hit) continue;
            String fn = name.replaceAll("[^A-Za-z0-9_.+-]", "_");
            if (fn.length() > 150) fn = fn.substring(0, 150);
            try (PrintWriter w = new PrintWriter(new FileWriter(new File(out, fn + "_" + f.getEntryPoint() + ".s")))) {
                w.println("; " + name + " @ " + f.getEntryPoint());
                InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (ii.hasNext()) {
                    Instruction ins = ii.next();
                    StringBuilder sb = new StringBuilder();
                    sb.append(ins.getAddress()).append("  ").append(ins.toString());
                    for (Reference r : ins.getReferencesFrom()) {
                        Address to = r.getToAddress();
                        if (!to.isMemoryAddress()) continue;
                        Symbol s = getSymbolAt(to);
                        sb.append("   ; -> ").append(to);
                        if (s != null) sb.append(" <").append(s.getName(true)).append(">");
                        try {
                            int v = mem.getInt(to);
                            sb.append(String.format(" [0x%08x f=%g i=%d]", v, Float.intBitsToFloat(v), v));
                            Symbol s2 = getSymbolAt(toAddr(v & 0xfffffffeL));
                            if (s2 != null) sb.append(" *<").append(s2.getName(true)).append(">");
                        } catch (Exception e) { /* unmapped */ }
                    }
                    w.println(sb);
                }
            }
            count++;
        }
        println("DumpDisasm: wrote " + count + " functions to " + out);
    }
}
