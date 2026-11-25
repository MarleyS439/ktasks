#!/usr/bin/env bash
set -euo pipefail

# Assemble a self-contained portable Windows x86_64 bundle of KTasks from an
# MSYS2 UCRT64 CMake build directory and verify standalone launch with isolated PATH.
SRC_DIR="${1:?Usage: package-windows.sh <source-dir> <build-dir> <out-dir> <version>}"
BUILD_DIR="${2:?}"
OUT_DIR="${3:?}"
PKG_VER="${4:?}"

BUNDLE_NAME="ktasks-${PKG_VER}-windows-x86_64"
PKG_DIR="${OUT_DIR}/${BUNDLE_NAME}"
STAGE_DIR=$(mktemp -d)
trap 'rm -rf "$STAGE_DIR"' EXIT

rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR" "$PKG_DIR/plugins" "$PKG_DIR/qml" "$PKG_DIR/data" "$PKG_DIR/share" "$PKG_DIR/translations"

cmake --install "$BUILD_DIR" --prefix "$STAGE_DIR"

cp -v "$STAGE_DIR/bin/ktasks.exe" "$PKG_DIR/ktasks.exe"
if [[ -d "$STAGE_DIR/share/locale" ]]; then
    cp -a "$STAGE_DIR/share/locale" "$PKG_DIR/share/"
    cp -a "$STAGE_DIR/share/locale" "$PKG_DIR/data/"
fi
if [[ -d "$STAGE_DIR/share/knotifications6" ]]; then
    cp -a "$STAGE_DIR/share/knotifications6" "$PKG_DIR/data/"
fi
cp -v "$SRC_DIR/LICENSE" "$PKG_DIR/LICENSE.txt"

# Copy Breeze compiled icon resource so KIconTheme::initTheme() finds all app/UI icons
if [[ -f /ucrt64/bin/data/icontheme.rcc ]]; then
    cp -v /ucrt64/bin/data/icontheme.rcc "$PKG_DIR/data/icontheme.rcc"
fi
if [[ -f /ucrt64/bin/data/icons/breeze/breeze-icons.rcc ]]; then
    mkdir -p "$PKG_DIR/data/icons/breeze"
    cp -v /ucrt64/bin/data/icons/breeze/breeze-icons.rcc "$PKG_DIR/data/icons/breeze/breeze-icons.rcc"
fi
if [[ -d /ucrt64/share/locale/pt_BR ]]; then
    mkdir -p "$PKG_DIR/share/locale" "$PKG_DIR/data/locale"
    cp -a /ucrt64/share/locale/pt_BR "$PKG_DIR/share/locale/"
    cp -a /ucrt64/share/locale/pt_BR "$PKG_DIR/data/locale/"
fi

# Run windeployqt if present, deploying plugins into plugins/ and QML imports cleanly
WINDEPLOYQT=$(command -v windeployqt6 || command -v windeployqt-qt6 || true)
if [[ -n "$WINDEPLOYQT" ]]; then
    "$WINDEPLOYQT" \
        --release \
        --no-compiler-runtime \
        --plugindir "$(cygpath -w "$PKG_DIR/plugins")" \
        --qmldir "$(cygpath -w "$SRC_DIR/src/app/qml")" \
        "$(cygpath -w "$PKG_DIR/ktasks.exe")" || true
fi

# Move any QML dirs placed at PKG_DIR root by windeployqt into qml/
for d in Qt QtQml QtQuick; do
    if [[ -d "$PKG_DIR/$d" ]]; then
        cp -a "$PKG_DIR/$d" "$PKG_DIR/qml/"
        rm -rf "$PKG_DIR/$d"
    fi
done

# Ensure Qt plugins needed at runtime are present
QT_PLUGINS=$(qtpaths6 --query QT_INSTALL_PLUGINS)
QT_PLUGINS_UNIX=$(cygpath -u "$QT_PLUGINS")
for sub in platforms styles sqldrivers tls networkinformation iconengines imageformats kiconthemes6 kf6; do
    if [[ -d "$QT_PLUGINS_UNIX/$sub" ]]; then
        mkdir -p "$PKG_DIR/plugins/$sub"
        cp -a "$QT_PLUGINS_UNIX/$sub/." "$PKG_DIR/plugins/$sub/"
    fi
done

# Remove debug plugin DLLs if any (*d.dll when non-debug exists) to save space cleanly
find "$PKG_DIR/plugins" -type f -name "*.a" -delete 2>/dev/null || true

# Ensure all Qt6 & KDE QML modules required by KTasks are included
QT_QML=$(qtpaths6 --query QT_INSTALL_QML)
QT_QML_UNIX=$(cygpath -u "$QT_QML")
for mod in QtQml QtQuick; do
    if [[ -d "$QT_QML_UNIX/$mod" ]]; then
        mkdir -p "$PKG_DIR/qml/$mod"
        cp -a "$QT_QML_UNIX/$mod/." "$PKG_DIR/qml/$mod/"
    fi
done
for kmod in kirigami kirigamiaddons desktop qqc2desktopstyle kquickcontrolsaddons breeze sonnet config coreaddons i18n; do
    if [[ -d "$QT_QML_UNIX/org/kde/$kmod" ]]; then
        mkdir -p "$PKG_DIR/qml/org/kde/$kmod"
        cp -a "$QT_QML_UNIX/org/kde/$kmod/." "$PKG_DIR/qml/org/kde/$kmod/"
    fi
done
# Strip static libs / pdb / debug files from QML modules
find "$PKG_DIR/qml" -type f \( -name "*.a" -o -name "*.prl" -o -name "*.pdb" \) -delete 2>/dev/null || true

# Write qt.conf so Qt locates bundled plugins, QML modules, and translations relative to ktasks.exe
cat > "$PKG_DIR/qt.conf" << 'EOF'
[Paths]
Prefix = .
Plugins = plugins
QmlImports = qml
Qml2Imports = qml
Translations = translations
EOF

# Recursively resolve every MinGW/UCRT64 PE DLL dependency using objdump
PKG_DIR_WIN="$(cygpath -w "$PKG_DIR")" UCRT_BIN_WIN="$(cygpath -w /ucrt64/bin)" python3 - << 'PYEOF'
import os, re, shutil, subprocess
from collections import deque

pkg_dir = os.environ["PKG_DIR_WIN"]
ucrt_bin = os.environ["UCRT_BIN_WIN"]
ucrt_map = {f.lower(): f for f in os.listdir(ucrt_bin) if f.lower().endswith(".dll")}

queue = deque()
for root, _, files in os.walk(pkg_dir):
    for fn in files:
        if fn.lower().endswith((".exe", ".dll")):
            queue.append(os.path.join(root, fn))

seen_bins = set()
copied = 0
dll_re = re.compile(r"DLL Name:\s+(\S+)")

while queue:
    path = queue.popleft()
    if path in seen_bins:
        continue
    seen_bins.add(path)
    out = subprocess.check_output(["objdump", "-p", path], text=True, errors="ignore")
    for m in dll_re.finditer(out):
        dep_low = m.group(1).lower()
        if dep_low in ucrt_map:
            real_name = ucrt_map[dep_low]
            dst = os.path.join(pkg_dir, real_name)
            if not os.path.exists(dst):
                shutil.copy2(os.path.join(ucrt_bin, real_name), dst)
                copied += 1
                queue.append(dst)

print(f"Resolved and copied {copied} UCRT64 runtime DLLs")
PYEOF

# Smoke-test the portable bundle with MSYS2 removed from PATH to prove standalone execution
SMOKE_DATA=$(mktemp -d)
SMOKE_IMG="${SMOKE_DATA}/smoke.png"
echo "Running isolated Windows portable bundle smoke test..."
SMOKE_DATA_WIN=$(cygpath -w "$SMOKE_DATA")
SMOKE_IMG_WIN=$(cygpath -w "$SMOKE_IMG")

env -i \
    SYSTEMROOT="${SYSTEMROOT:-C:\\Windows}" \
    WINDIR="${WINDIR:-C:\\Windows}" \
    TEMP="$SMOKE_DATA_WIN" \
    TMP="$SMOKE_DATA_WIN" \
    LOCALAPPDATA="$SMOKE_DATA_WIN" \
    APPDATA="$SMOKE_DATA_WIN" \
    PATH="/c/Windows/System32:/c/Windows" \
    QT_QPA_PLATFORM="offscreen" \
    KTASKS_DATA_DIR="$SMOKE_DATA_WIN" \
    KTASKS_SCREENSHOT="$SMOKE_IMG_WIN" \
    KTASKS_SCREENSHOT_DELAY="1500" \
    "$PKG_DIR/ktasks.exe"

test -s "$SMOKE_IMG"
echo "Smoke screenshot verified: $(ls -lh "$SMOKE_IMG")"
rm -rf "$SMOKE_DATA"

# Pack final portable zip
(cd "$OUT_DIR" && if command -v 7z >/dev/null 2>&1; then
    7z a -tzip -mx=9 "${BUNDLE_NAME}.zip" "${BUNDLE_NAME}" >/dev/null
else
    python3 -c "
import zipfile, os
zip_path = '${BUNDLE_NAME}.zip'
base = '${BUNDLE_NAME}'
with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
    for r, _, fs in os.walk(base):
        for f in fs:
            p = os.path.join(r, f)
            zf.write(p, p)
"
fi)
ls -lh "${OUT_DIR}/${BUNDLE_NAME}.zip"
