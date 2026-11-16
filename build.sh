#!/bin/bash
# Builds taskbar-player.wh.cpp from src/ and compiles it with Windhawk's compiler.
#   ./build.sh            -> concatenate + compile the mod DLL (same flags as Windhawk)
#   ./build.sh syntax     -> concatenate + syntax check only
#   ./build.sh harness    -> build the standalone test harness (harness/harness.exe)
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
COMPILER="/c/Program Files/Windhawk/Compiler"
CXX="$COMPILER/bin/clang++.exe"
OUT="$ROOT/build"
mkdir -p "$OUT"

cat "$ROOT"/src/*.cpp > "$ROOT/taskbar-player.wh.cpp"
OPTS=$(grep -m1 '@compilerOptions' "$ROOT/taskbar-player.wh.cpp" | sed 's/.*@compilerOptions//' | tr -d '\r')
DEFS=(-DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -D_WIN32_IE=0x0A00
      -DNTDDI_VERSION=0x0A000008 -D__USE_MINGW_ANSI_STDIO=0)
MODE=${1:-mod}

cd "$COMPILER"
case "$MODE" in
  syntax)
    "$CXX" -std=c++23 -fsyntax-only "${DEFS[@]}" -DWH_MOD '-DWH_MOD_ID=L"taskbar-player"' \
      '-DWH_MOD_VERSION=L"1.1.0"' -x c++ "$ROOT/taskbar-player.wh.cpp" -include windhawk_api.h \
      -target x86_64-w64-mingw32
    ;;
  mod)
    "$CXX" -std=c++23 -O2 -shared "${DEFS[@]}" -DWH_MOD '-DWH_MOD_ID=L"taskbar-player"' \
      '-DWH_MOD_VERSION=L"1.1.0"' "C:/Program Files/Windhawk/Engine/1.7.3/64/windhawk.lib" \
      -x c++ "$ROOT/taskbar-player.wh.cpp" -include windhawk_api.h -target x86_64-w64-mingw32 \
      -Wl,--export-all-symbols -o "$OUT/taskbar-player.dll" $OPTS
    echo "Built $OUT/taskbar-player.dll"
    ;;
  harness)
    "$CXX" -std=c++23 -O1 -g "${DEFS[@]}" -DTP_HARNESS -I"$ROOT/harness" \
      -x c++ "$ROOT/harness/harness.cpp" -target x86_64-w64-mingw32 -static \
      -municode -o "$OUT/harness.exe" $OPTS -lshcore
    echo "Built $OUT/harness.exe"
    ;;
esac
