// Headless Ghidra post-script: decompile every function whose name contains
// one of the given substrings and write each to <outdir>/<sanitized-name>.c.
// Also writes <outdir>/_index.tsv (address, size, name).
//
// usage (analyzeHeadless ... -postScript DecompileMatching.java <outdir> <pat1> [pat2 ...])
// A pattern of "*" matches every function.
//@category MikuFlick

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileMatching extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("usage: DecompileMatching.java <outdir> <pattern>...");
            return;
        }
        File out = new File(args[0]);
        out.mkdirs();

        DecompInterface dec = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        dec.setOptions(opts);
        dec.toggleCCode(true);
        dec.toggleSyntaxTree(true);
        dec.setSimplificationStyle("decompile");
        if (!dec.openProgram(currentProgram)) {
            printerr("decompiler failed to open program: " + dec.getLastMessage());
            return;
        }

        int count = 0;
        try (PrintWriter idx = new PrintWriter(new FileWriter(new File(out, "_index.tsv"), true))) {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                String name = f.getName(true);
                boolean hit = false;
                for (int i = 1; i < args.length; i++) {
                    if (args[i].equals("*") || name.contains(args[i])) { hit = true; break; }
                }
                if (!hit) continue;

                DecompileResults r = dec.decompileFunction(f, 60, monitor);
                String code = (r != null && r.decompileCompleted())
                        ? r.getDecompiledFunction().getC()
                        : "/* decompile failed: " + (r == null ? "null" : r.getErrorMessage()) + " */\n";

                String fn = name.replaceAll("[^A-Za-z0-9_.+-]", "_");
                if (fn.length() > 150) fn = fn.substring(0, 150);
                fn = fn + "_" + f.getEntryPoint().toString();
                try (PrintWriter w = new PrintWriter(new FileWriter(new File(out, fn + ".c")))) {
                    w.println("// " + name + " @ " + f.getEntryPoint() + " size " + f.getBody().getNumAddresses());
                    w.print(code);
                }
                idx.println(f.getEntryPoint() + "\t" + f.getBody().getNumAddresses() + "\t" + name);
                count++;
            }
        }
        dec.dispose();
        println("DecompileMatching: wrote " + count + " functions to " + out);
    }
}
