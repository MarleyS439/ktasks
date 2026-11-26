#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds, packages and signs the KTasks Android APK with KDE Craft. Runs inside
# invent-registry.kde.org/sysadmin/ci-images/android-qt611 with the repository
# mounted at /src, a scratch directory at /work and the output at /out.
# Signing uses ANDROID_KEYSTORE_B64 and ANDROID_KEYSTORE_PASSWORD (key alias
# "ktasks") when set; without them an unsigned APK is produced.
set -euo pipefail

SRC=/src
WORK=/work
OUT=/out
TARGET=android-arm64-clang
mkdir -p "$WORK" "$OUT"
cd "$WORK"

[[ -d craftmaster ]] || git clone --depth 1 https://invent.kde.org/packaging/craftmaster.git
touch "$SRC/.craft.ini"

# The checkout belongs to the CI user, and ECM's fastlane metadata step asks
# git for the upstream branch, which a detached CI checkout does not have.
git config --global --add safe.directory '*'
if ! git -C "$SRC" rev-parse --symbolic-full-name '@{u}' >/dev/null 2>&1; then
    git -C "$SRC" checkout -q -B craft-build
    git -C "$SRC" update-ref refs/remotes/origin/main HEAD
    git -C "$SRC" config branch.craft-build.remote origin
    git -C "$SRC" config branch.craft-build.merge refs/heads/main
fi

craftmaster() {
    python3 "$WORK/craftmaster/CraftMaster.py" \
        --config "$SRC/packaging/android/CraftConfig.ini" \
        --config-override "$SRC/.craft.ini" \
        --variables "Root=$WORK/craft" \
        --target "$TARGET" "$@"
}

craftmaster --setup
craftmaster -c -i craft

# KTasks is not in craft-blueprints-kde: add its blueprint next to the others.
bpdir=$(find "$WORK/craft/blueprints" -type d -path '*craft-blueprints-kde/kde/applications' | head -n1)
if [[ -z "$bpdir" ]]; then
    echo "ERROR: craft-blueprints-kde checkout not found" >&2
    exit 1
fi
rm -rf "$bpdir/ktasks"
cp -r "$SRC/packaging/craft/ktasks" "$bpdir/ktasks"

craftmaster -c --install-deps ktasks
craftmaster -c --no-cache --options "ktasks.srcDir=$SRC" ktasks
craftmaster -c --package --options "ktasks.srcDir=$SRC" ktasks

pkgdir=$(craftmaster -c -q --get "packageDestinationDir()" virtual/base | tail -n1)
echo "Craft package directory: $pkgdir"
ls -la "$pkgdir"
apk=$(ls -t "$pkgdir"/*.apk | head -n1)
version=$(sed -n 's/^project(ktasks VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt")
name="ktasks-${version}-android-arm64"

buildtools="$ANDROID_HOME/build-tools/$(ls "$ANDROID_HOME/build-tools" | sort -V | tail -n1)"
"$buildtools/zipalign" -p -f 4 "$apk" "$WORK/aligned.apk"

if [[ -n "${ANDROID_KEYSTORE_B64:-}" ]]; then
    echo "$ANDROID_KEYSTORE_B64" | base64 -d > "$WORK/release.keystore"
    "$buildtools/apksigner" sign \
        --ks "$WORK/release.keystore" \
        --ks-key-alias ktasks \
        --ks-pass env:ANDROID_KEYSTORE_PASSWORD \
        --key-pass env:ANDROID_KEYSTORE_PASSWORD \
        --out "$OUT/${name}.apk" "$WORK/aligned.apk"
    rm -f "$WORK/release.keystore"
    "$buildtools/apksigner" verify --print-certs "$OUT/${name}.apk"
else
    echo "WARNING: no signing key; the APK is unsigned and cannot be installed" >&2
    cp "$WORK/aligned.apk" "$OUT/${name}-unsigned.apk"
fi
ls -la "$OUT"
