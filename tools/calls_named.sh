#!/bin/sh
# usage: sh tools/calls_named.sh work/disasm/<func>.s   -> draw calls with frame names
LC_ALL=C awk -f tools/disasm_calls.awk "$1" | LC_ALL=C awk 'NR==FNR { name[$1]=$2; next }
  { if (match($0, /r2=[0-9]+/)) { id = substr($0, RSTART+3, RLENGTH-3); if (id in name) sub(/r2=[0-9]+/, "r2=" id "(" name[id] ")") } print }' work/tpframes.txt -
