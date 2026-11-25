#!/usr/bin/env bash
set -euo pipefail

# Build and install KF6CalendarCore into MSYS2 UCRT64 prefix if not already installed.
if [[ -f /ucrt64/lib/cmake/KF6CalendarCore/KF6CalendarCoreConfig.cmake ]]; then
    echo "KF6CalendarCore already present in /ucrt64"
    exit 0
fi

kf_ver=$(pacman -Q mingw-w64-ucrt-x86_64-kcoreaddons | awk '{print $2}' | cut -d- -f1)
kf_mm="${kf_ver%.*}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "Building KF6CalendarCore ${kf_ver} for MSYS2 UCRT64..."
if curl -fsSL "https://download.kde.org/stable/frameworks/${kf_mm}/kcalendarcore-${kf_ver}.tar.xz" -o "$work/kcalendarcore.tar.xz"; then
    tar -xf "$work/kcalendarcore.tar.xz" -C "$work"
    src="$work/kcalendarcore-${kf_ver}"
else
    git clone --depth 1 --branch "v${kf_ver}" https://invent.kde.org/frameworks/kcalendarcore.git "$work/src"
    src="$work/src"
fi

cmake -S "$src" -B "$work/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$(cygpath -m /ucrt64)" \
    -DUSE_QML=OFF \
    -DBUILD_TESTING=OFF
cmake --build "$work/build" -j"$(nproc)"
cmake --install "$work/build"
