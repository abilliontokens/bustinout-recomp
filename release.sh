#!/usr/bin/env bash
# Package the native Linux build for release. The Windows counterpart is release.bat.
#
#   tools: none beyond the build itself; run from the repo root
#   out:   SimsBustinOut-linux-x64.tar.zst
#
# The binary needs a shader compiler at run time: the Vulkan backend translates the
# title's shaders to SPIR-V through glslang (sdk/runtime/gpu/spirv.cpp), so glslang,
# SPIRV and SPIRV-Tools are real runtime dependencies. Arch ships no static
# SPIRV-Tools, so they cannot be linked in; they travel in lib/ beside the binary and
# are found through an $ORIGIN rpath. Everything else the binary needs (SDL3, toml++,
# Dear ImGui) is already static, and the Vulkan loader itself is expected from the
# system like any other Vulkan application.
set -euo pipefail

BUILD="${1:-out/build/linux-release}"
EXE="SimsBustinOut"
STAGE="out/package/linux"
TARBALL="$EXE-linux-x64.tar.zst"

[ -x "$BUILD/$EXE" ] || { echo "no $BUILD/$EXE - build first: cmake --build --preset linux-release"; exit 1; }

# Relink with an rpath that looks beside the binary. Only the link step reruns.
# CMAKE_BUILD_RPATH, not a raw -Wl,-rpath in CMAKE_EXE_LINKER_FLAGS: Ninja treats
# $ as its own escape character, so a literal $ORIGIN in a flag string reaches the
# linker as an empty string and the rpath silently becomes "/lib", which resolves
# against the system libraries and defeats the whole point. CMake's own rpath
# variables are escaped by the generator, so $ORIGIN survives into the ELF.
# CMAKE_EXE_LINKER_FLAGS carries --disable-new-dtags and is otherwise reset. Two
# reasons. A previous configure of this build directory may have left a stale
# -Wl,-rpath in it, and rpath entries concatenate, so it would sit in front of
# $ORIGIN/lib and keep winning. And the default new-style RUNPATH applies ONLY to
# the binary's own direct dependencies, not to theirs: libglslang pulls in
# libSPIRV-Tools, and with RUNPATH that lookup skips $ORIGIN/lib and falls through
# to the system copy, so the package breaks on any machine that lacks it.
# --disable-new-dtags emits the old DT_RPATH, which the whole dependency chain
# inherits.
cmake --preset linux-release -DCMAKE_BUILD_RPATH='$ORIGIN/lib' \
      -DCMAKE_EXE_LINKER_FLAGS=-Wl,--disable-new-dtags >/dev/null
cmake --build --preset linux-release >/dev/null

rm -rf "$STAGE"
mkdir -p "$STAGE/lib" "$STAGE/assets"
cp "$BUILD/$EXE" "$STAGE/"

# every non-system shared object the binary resolves, by what it actually links
ldd "$BUILD/$EXE" | awk '/=> \//{print $3}' |
    grep -E 'glslang|SPIRV' |
    while read -r so; do cp -L "$so" "$STAGE/lib/"; done

cp README.md "$STAGE/"
[ -f sdk/THIRD_PARTY.txt ] && cp sdk/THIRD_PARTY.txt "$STAGE/"
cat > "$STAGE/assets/PUT_YOUR_GAME_HERE.txt" <<'EOF'
Put your own legally acquired copy of The Sims: Bustin' Out (original Xbox, USA)
in this folder: either the extracted disc files (including default.xbe) or the
disc image itself as a .iso. The game ships no data of its own.
EOF

rm -f "$TARBALL"
tar --zstd -C "$STAGE/.." -cf "$TARBALL" linux --transform "s,^linux,$EXE-linux-x64,"
echo "wrote $TARBALL ($(du -h "$TARBALL" | cut -f1))"
echo "contents:"
tar --zstd -tf "$TARBALL" | sed 's/^/  /'
