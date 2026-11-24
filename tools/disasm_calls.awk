# disasm_calls.awk - summarize draw calls in a DumpDisasm.java listing (armv7, iOS softfp).
#
# Constant propagation over:
#   core regs : mov/movs/mov.w #imm, movw/movt, add/sub #imm, mov rX,rY
#   VFP regs  : s/d aliasing (dN = s2N,s2N+1), vmov.f32 #imm / simdExpand,
#               vmov.i32 simdExpand(0x0,0x6,imm) (imm<<24), vldr literal (f=),
#               vadd/vsub/vmul/vneg .f32, vcvt from known ints, vmov s<->r, vmov dN,rA,rB
#   stack     : str/str.w/vstr to [sp,#k] (outgoing args)
# Calls returning GetBottomOfs give 0 (640x960 screen); everything else unknown.
# At each bl/blx that looks like a draw call the selector (from the r1 load
# annotation), r2 (frame id), r3 and stack slots are printed.
#
# usage: LC_ALL=C awk -f tools/disasm_calls.awk work/disasm/<func>.s
function hex2dec(h,   i, c, v) {
    h = tolower(h); sub(/^#/, "", h); neg = 0
    if (h ~ /^-/) { neg = 1; h = substr(h, 2) }
    if (h !~ /^0x/) return (neg ? -1 : 1) * (h + 0)
    sub(/^0x/, "", h); v = 0
    for (i = 1; i <= length(h); i++) { c = index("0123456789abcdef", substr(h, i, 1)) - 1; if (c < 0) return 0; v = v * 16 + c }
    return neg ? -v : v
}
function vfpimm(imm8,   a, b, cd, efgh, e) {
    a = int(imm8 / 128) % 2; b = int(imm8 / 64) % 2; cd = int(imm8 / 16) % 4; efgh = imm8 % 16
    e = b ? cd - 3 : cd + 1
    return (a ? -1 : 1) * (1 + efgh / 16) * 2 ^ e
}
function bits2float(u,   s, e, m) {
    if (u < 0) u += 4294967296
    s = int(u / 2147483648); e = int(u / 8388608) % 256; m = u % 8388608
    if (e == 0) return (m == 0) ? 0 : "?"
    if (e == 255) return "?"
    return (s ? -1 : 1) * (1 + m / 8388608) * 2 ^ (e - 127)
}
# register name helpers: d -> two s
function sidx(r) { return substr(r, 2) + 0 }
function setF(r, v,   n) {
    if (r ~ /^s[0-9]+$/) { if (v == "") delete F[r]; else F[r] = v; return }
    if (r ~ /^d[0-9]+$/) { n = sidx(r) * 2; if (v == "") { delete F["s" n]; delete F["s" (n + 1)] } else { F["s" n] = v; F["s" (n + 1)] = v } }
}
function getF(r,   n) {
    if (r ~ /^s[0-9]+$/) return (r in F) ? F[r] : ""
    if (r ~ /^d[0-9]+$/) { n = "s" (sidx(r) * 2); return (n in F) ? F[n] : "" }
    return ""
}
function fmtnum(v) { if (v == "") return "?"; if (v == int(v)) return sprintf("%d", v); return sprintf("%.4g", v) }
# core reg value -> display (ints that look like floats are shown as floats)
function fmtR(r,   v, f) {
    if (!(r in R)) return "?"
    v = R[r]
    if (v ~ /^f/) return fmtnum(substr(v, 2) + 0) "f"
    if (v > 65535 || v < -65535) { f = bits2float(v); if (f != "?" && f > -100000 && f < 100000) return fmtnum(f) "f" }
    return fmtnum(v)
}
function fmtS(k,   v, f) {
    if (!(k in S)) return "?"
    v = S[k]
    if (v ~ /^f/) return fmtnum(substr(v, 2) + 0) "f"
    if (v > 65535 || v < -65535) { f = bits2float(v); if (f != "?" && f > -100000 && f < 100000) return fmtnum(f) "f" }
    return fmtnum(v)
}
function asfloat(rv,   f) { if (rv ~ /^f/) return substr(rv, 2) + 0; f = bits2float(rv); return f }
{
    line = $0; addr = $1; ins = $2; ops = ""
    for (i = 3; i <= NF && $i != ";"; i++) ops = ops (ops == "" ? "" : " ") $i
    gsub(/,/, " ", ops); n = split(ops, o, " ")
    if (match(line, /<s_[A-Za-z0-9_:]+_[0-9a-f]+>/)) {
        s = substr(line, RSTART + 3, RLENGTH - 4); sub(/_[0-9a-f]+$/, "", s); SEL[o[1]] = s
    }
    if (ins ~ /^(mov|movs|mov\.w)$/ && n == 2) {
        if (o[2] ~ /^#/) R[o[1]] = hex2dec(o[2])
        else { if (o[2] in R) R[o[1]] = R[o[2]]; else delete R[o[1]]; if (o[2] in SEL) SEL[o[1]] = SEL[o[2]] }
    } else if (ins == "movw") {
        R[o[1]] = hex2dec(o[2])
    } else if (ins == "movt") {
        if (o[1] in R && R[o[1]] !~ /^f/) R[o[1]] = (R[o[1]] % 65536) + hex2dec(o[2]) * 65536
        else R[o[1]] = hex2dec(o[2]) * 65536
    } else if (ins ~ /^(add|adds|add\.w|sub|subs|sub\.w)$/ && o[1] ~ /^r/ && o[n] ~ /^#/) {
        src = (n == 3) ? o[2] : o[1]
        if (src in R && R[src] !~ /^f/) R[o[1]] = R[src] + (ins ~ /^sub/ ? -1 : 1) * hex2dec(o[n]); else delete R[o[1]]
    } else if (ins == "vmov.f32" && line ~ /simdExpand/) {
        m = line; sub(/.*simdExpand\(0x0,0xf,/, "", m); sub(/\).*/, "", m); setF(o[1], vfpimm(hex2dec(m)))
    } else if (ins == "vmov.f32" && o[2] ~ /^#/) {
        setF(o[1], o[2] + 0)
    } else if (ins == "vmov.i32" && line ~ /simdExpand\(0x0,0x6,/) {
        m = line; sub(/.*simdExpand\(0x0,0x6,/, "", m); sub(/\).*/, "", m); setF(o[1], bits2float(hex2dec(m) * 16777216))
    } else if (ins == "vmov.i32" && o[2] == "#0") {
        setF(o[1], 0)
    } else if (ins ~ /^vldr/ && line ~ /\[pc/ && match(line, /f=[-0-9.e+]+/)) {
        setF(o[1], substr(line, RSTART + 2, RLENGTH - 2) + 0)
    } else if (ins ~ /^vldr/) {
        setF(o[1], "")
    } else if (ins ~ /^v(add|sub|mul)\.f32$/ && n == 3) {
        a = getF(o[2]); b = getF(o[3])
        if (a != "" && b != "") setF(o[1], ins ~ /add/ ? a + b : ins ~ /sub/ ? a - b : a * b); else setF(o[1], "")
    } else if (ins == "vneg.f32") {
        a = getF(o[2]); setF(o[1], a == "" ? "" : -a)
    } else if (ins ~ /^vcvt\.f32\.[su]32$/) {
        setF(o[1], "")
    } else if (ins == "vmov" && n == 3 && o[1] ~ /^d/) {           # vmov dN,rA,rB
        if (o[2] in R) setF(o[1], asfloat(R[o[2]])); else setF(o[1], "")
    } else if (ins == "vmov" && n == 2 && o[1] ~ /^r/ && o[2] ~ /^s/) {
        v = getF(o[2]); if (v != "") R[o[1]] = "f" v; else delete R[o[1]]
    } else if (ins == "vmov" && n == 2 && o[1] ~ /^s/ && o[2] ~ /^r/) {
        if (o[2] in R) setF(o[1], asfloat(R[o[2]])); else setF(o[1], "")
    } else if (ins == "vmov" && n == 2 && o[1] ~ /^d/ && o[2] ~ /^d/) {
        setF(o[1], getF(o[2]))
    } else if (ins ~ /^(str|str\.w)$/ && ops ~ /\[sp( #0x[0-9a-f]+)?\]/) {
        m = ops; k = 0; if (m ~ /#0x/) { sub(/.*\[sp #/, "", m); sub(/\].*/, "", m); k = hex2dec(m) }
        if (k < 64) { if (o[1] in R) S[k] = R[o[1]]; else delete S[k] }
    } else if (ins ~ /^vstr/ && ops ~ /\[sp( #0x[0-9a-f]+)?\]/) {
        m = ops; k = 0; if (m ~ /#0x/) { sub(/.*\[sp #/, "", m); sub(/\].*/, "", m); k = hex2dec(m) }
        v = getF(o[1]); if (k < 64) { if (v != "") S[k] = "f" v; else delete S[k] }
    } else if (ins ~ /^(bl|blx)$/) {
        tgt = ""; if (match(line, /<[^>]+>/)) tgt = substr(line, RSTART + 1, RLENGTH - 2)
        sel = ("r1" in SEL) ? SEL["r1"] : ""
        if (tgt ~ /RenderTex|RenderRect|renderObj/ || sel ~ /Render|render|Draw/) {
            printf "%s %-24s %-40s r2=%s r3=%s sp:[%s %s %s %s %s %s %s %s]\n", addr, tgt, sel, fmtR("r2"), fmtR("r3"),
                fmtS(0), fmtS(4), fmtS(8), fmtS(12), fmtS(16), fmtS(20), fmtS(24), fmtS(28)
        }
        for (k in R) if (k ~ /^r([0-3]|12)$/) delete R[k]
        delete SEL["r0"]; delete SEL["r1"]
        for (k in S) delete S[k]
        if (sel ~ /^GetBottomOfs$/) R["r0"] = 0
        if (sel ~ /^GetUIAlpha$|^GetInterludeUIAlpha$|^GetFullScreenHeight$/) delete R["r0"]
        if (sel ~ /^GetFullScreenHeight$/) R["r0"] = "f960"
    } else if (o[1] ~ /^r([0-9]|1[0-2]|lr)$/ && ins !~ /^(str|cmp|cmn|tst|b|vstr|push|pop|it)/) {
        delete R[o[1]]
    }
}
