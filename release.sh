#!/usr/bin/env bash
# The Sims: Bustin' Out - Linux release packager (Vulkan + SDL3, no Wine).
# Mirrors release.bat: build if needed, then pack the executable with the
# README, the SDK's THIRD_PARTY.txt and an empty assets folder.
set -euo pipefail

PRESET="${1:-linux-release}"
cd "$(dirname "$0")"

GAME_EXE="SimsBustinOut"
OUT="out/build/${PRESET}"
ARCHIVE="${GAME_EXE}-linux.tar.gz"

echo
echo "  The Sims: Bustin' Out - release (${PRESET})"
echo "  ================================================"
echo

if [ ! -f "${OUT}/${GAME_EXE}" ]; then
    ./build.sh "${PRESET}"
fi
# the configure step writes where the SDK is
if [ ! -f "${OUT}/sdk_dir.txt" ]; then
    cmake --preset "${PRESET}" > /dev/null
fi
if [ ! -f "${OUT}/sdk_dir.txt" ]; then
    echo "${OUT}/sdk_dir.txt was not written, update the SDK." >&2
    exit 1
fi
SDK="$(cat "${OUT}/sdk_dir.txt")"

# Refuse to package game data or build output: only the executable may come
# from assets/, generated/ or out/. The assets folder in the archive is empty.
for forbidden in assets generated; do
    if [ -e "${forbidden}" ] && [ -n "$(find "${forbidden}" -mindepth 1 -print -quit 2>/dev/null)" ]; then
        : # present in the worktree is fine; it is simply never added below
    fi
done
# Build the member list explicitly and check it before archiving.
STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT
mkdir -p "${STAGE}/release/assets"
cp "${OUT}/${GAME_EXE}" "${STAGE}/release/${GAME_EXE}"
cp README.md "${STAGE}/release/README.md"
cp "${SDK}/THIRD_PARTY.txt" "${STAGE}/release/THIRD_PARTY.txt"
# an empty assets folder beside the exe, where the game (files or .iso) goes
touch "${STAGE}/release/assets/.keep"

MEMBERS="$(cd "${STAGE}/release" && find . -type f -printf '%P\n' | sort)"
EXPECTED="$(printf '%s\n' "${GAME_EXE}" "README.md" "THIRD_PARTY.txt" "assets/.keep" | sort)"
if [ "${MEMBERS}" != "${EXPECTED}" ]; then
    echo "refusing to package unexpected files:" >&2
    diff <(printf '%s\n' "${EXPECTED}") <(printf '%s\n' "${MEMBERS}") >&2 || true
    exit 1
fi
case "${MEMBERS}" in
    *assets/*[!/.keep]*|*generated/*|*out/*)
        echo "refusing to package anything from assets/, generated/ or out/ other than the executable" >&2
        exit 1
        ;;
esac

rm -f "${ARCHIVE}"
tar -czf "${ARCHIVE}" -C "${STAGE}/release" .

echo
echo "  The Sims: Bustin' Out packaged."
echo "  ${ARCHIVE}: ${GAME_EXE}, README.md, THIRD_PARTY.txt, assets/ (empty)"
echo
