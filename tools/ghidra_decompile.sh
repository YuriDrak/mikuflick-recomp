#!/bin/sh
# Headless Ghidra import + auto-analysis + decompile of selected subsystems.
# Run from project root:  sh tools/ghidra_decompile.sh [pattern ...]
# All Ghidra state (settings, cache, temp, project) stays inside the project.
set -eu
ROOT="$(pwd)"
GH="${GHIDRA_DIR:-$ROOT/tools/third_party/ghidra_11.2.1_PUBLIC}"
if [ ! -x "$GH/support/analyzeHeadless" ]; then
    echo "Ghidra not found at: $GH" >&2
    echo "Set GHIDRA_DIR=/path/to/ghidra_11.2.1_PUBLIC (it is not vendored)." >&2
    exit 1
fi
BIN="$ROOT/extracted/ipa/Payload/MikuFlick2.app/MikuFlick2"
OUT="${DECOMP_OUT:-$ROOT/work/decomp}"
PROJ="$ROOT/work/ghidra_proj"
SANDBOX="$ROOT/work/ghidra_home"
mkdir -p "$OUT" "$PROJ" "$SANDBOX/tmp" "$SANDBOX/config" "$SANDBOX/cache"

export HOME="$SANDBOX"
export XDG_CONFIG_HOME="$SANDBOX/config"
export XDG_CACHE_HOME="$SANDBOX/cache"
# The project path contains a space, which JAVA_TOOL_OPTIONS cannot carry and
# Ghidra requires an absolute tmpdir. launch.properties VMARGS lines are passed
# as single argv entries, so pin the sandbox dirs there (idempotent).
LP="$GH/support/launch.properties"
if ! grep -q '^# miku-flick sandbox' "$LP"; then
    {
        echo '# miku-flick sandbox'
        echo "VMARGS=-Djava.io.tmpdir=$SANDBOX/tmp"
        echo "VMARGS=-Duser.home=$SANDBOX"
    } >> "$LP"
fi

if [ $# -eq 0 ]; then
    set -- CuePoint NoteManager NoteNormal NoteInterlude NoteObjBase SceneManager SceneGame \
           StageManager TouchFlickBoard TouchInputTiming MikuFlickCriManager CriManager LyricsData \
           StatusData EffectFlickResult SoundManager SoundBase MusicData SceneResult SceneBoot \
           ES2Renderer EAGLView SystemTexture ShaderManager MikuFlick2AppDelegate TexTextManager \
           TouchManager WindowGameWindow ReplayManager
fi

if [ -d "$PROJ/MikuFlick2.rep" ]; then
    # Project already analysed: just run the decompile script on it.
    "$GH/support/analyzeHeadless" "$PROJ" MikuFlick2 -process MikuFlick2 -noanalysis \
        -scriptPath "$ROOT/tools/ghidra_scripts" -postScript DecompileMatching.java "$OUT" "$@"
else
    "$GH/support/analyzeHeadless" "$PROJ" MikuFlick2 -import "$BIN" \
        -analysisTimeoutPerFile 3600 \
        -scriptPath "$ROOT/tools/ghidra_scripts" -postScript DecompileMatching.java "$OUT" "$@"
fi
