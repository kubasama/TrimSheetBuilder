#!/usr/bin/env bash
# Builds Trim_Sheet_Builder-x86_64.AppImage.
#
#   ./packaging/build-appimage.sh
#
# Needs only what a normal build needs (cmake, compiler, Qt 6 dev packages,
# libzip) plus curl. The packaging tools (linuxdeploy, its Qt plugin and
# appimagetool) are downloaded once into ~/.cache/trimsheet-appimage-tools;
# nothing is added to the repo. Set TOOLS=/some/dir to use your own copies.
#
# The AppImage bundles Qt, libzip and the other libraries the app uses, so it
# runs on systems without them. glibc is not bundled: build on the oldest
# distro you want to support.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLS="${TOOLS:-${XDG_CACHE_HOME:-$HOME/.cache}/trimsheet-appimage-tools}"
BUILD="$ROOT/build-appimage"
APPDIR="$BUILD/AppDir"
OUT="$ROOT/Trim_Sheet_Builder-x86_64.AppImage"

# AppImages need FUSE to run; extracting instead works everywhere (containers too).
export APPIMAGE_EXTRACT_AND_RUN=1

# ---------------------------------------------------------------- tools
fetch() { # name url
    local dest="$TOOLS/$1"
    if [ ! -x "$dest" ]; then
        echo ">> downloading $1"
        mkdir -p "$TOOLS"
        curl -fL --retry 3 -o "$dest.part" "$2"
        mv "$dest.part" "$dest"
        chmod +x "$dest"
    fi
}
fetch linuxdeploy-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
fetch appimagetool-x86_64.AppImage \
    https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
export PATH="$TOOLS:$PATH" # linuxdeploy finds its plugins on PATH

# ---------------------------------------------------------------- Qt
find_qmake() {
    for q in "${QMAKE:-}" qmake6 qmake-qt6 /usr/lib64/qt6/bin/qmake /usr/lib/qt6/bin/qmake \
             /usr/lib/x86_64-linux-gnu/qt6/bin/qmake qmake; do
        [ -z "$q" ] && continue
        if command -v "$q" >/dev/null 2>&1 && "$q" -query QT_VERSION 2>/dev/null | grep -q '^6\.'; then
            command -v "$q"
            return
        fi
    done
    echo "error: no Qt 6 qmake found; set QMAKE=/path/to/qmake" >&2
    exit 1
}
QMAKE="$(find_qmake)"
export QMAKE
QT_PLUGINS="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
echo ">> using $QMAKE (Qt $("$QMAKE" -query QT_VERSION))"

# ---------------------------------------------------------------- build
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DTRIMSHEET_BUILD_TESTS=OFF
cmake --build "$BUILD" -j"$(nproc)"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD"

# Wayland support, if the Qt Wayland plugins are installed (X11 is always included)
extra=()
for p in libqwayland-egl.so libqwayland-generic.so; do
    [ -f "$QT_PLUGINS/platforms/$p" ] && extra+=("$p")
done
if [ ${#extra[@]} -gt 0 ]; then
    EXTRA_PLATFORM_PLUGINS="$(IFS=';'; echo "${extra[*]}")"
    export EXTRA_PLATFORM_PLUGINS
else
    echo "warning: Qt Wayland plugins not found (install your distro's Qt 6 Wayland package);" >&2
    echo "         the AppImage will run through XWayland" >&2
fi

# 1) bundle Qt and the libraries the app links to.
#    NO_STRIP: linuxdeploy's bundled strip is too old for newer distros
#    (fails on .relr.dyn sections); stripping only saves a bit.
export NO_STRIP=1
linuxdeploy-x86_64.AppImage --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/trimsheet.desktop" \
    --icon-file "$ROOT/packaging/trimsheet.svg" \
    --plugin qt

# 1b) drop plugins the app never uses but that drag in big dependency chains
#     (the virtual keyboard pulls in Qt Quick / QML / Network / curl / krb5,
#     the PDF image plugin pulls in Qt Pdf)
rm -f "$APPDIR/usr/plugins/platforminputcontexts/libqtvirtualkeyboardplugin.so" \
      "$APPDIR/usr/plugins/imageformats/libqpdf.so"
for lib in libQt6VirtualKeyboard libQt6Quick libQt6QmlWorkerScript libQt6QmlModels libQt6QmlMeta \
           libQt6Qml libQt6Network libQt6Pdf libcurl libgssapi_krb5 libkrb5support libkrb5 \
           libk5crypto libproxy libpxbackend libduktape libnghttp2 libnghttp3 \
           libngtcp2_crypto_ossl libngtcp2 libidn2 libssh2 libssl libunistring libopenjp2; do
    rm -f "$APPDIR/usr/lib/$lib".so*
done
# make sure nothing left behind still needs what was removed
missing=""
while IFS= read -r -d '' f; do
    out="$(LD_LIBRARY_PATH="$APPDIR/usr/lib" ldd "$f" 2>/dev/null | grep 'not found' || true)"
    [ -n "$out" ] && missing+="$f:"$'\n'"$out"$'\n'
done < <(find "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/plugins" -type f \( -name '*.so*' -o -perm -u+x \) -print0)
if [ -n "$missing" ]; then
    echo "error: unresolved libraries after pruning:" >&2
    echo "$missing" >&2
    exit 1
fi

# 2) the gtk3 platform theme plugin, added *after* linuxdeploy so GTK itself is
#    not bundled: the plugin uses the system's GTK, which is how it follows the
#    desktop theme (fonts, icons, native file dialogs). If the system has no
#    GTK 3 the plugin just doesn't load and the app still works.
if [ -f "$QT_PLUGINS/platformthemes/libqgtk3.so" ]; then
    mkdir -p "$APPDIR/usr/plugins/platformthemes"
    cp "$QT_PLUGINS/platformthemes/libqgtk3.so" "$APPDIR/usr/plugins/platformthemes/"
else
    echo "warning: libqgtk3.so not found; native file dialogs won't be used" >&2
fi

# 3) our own AppRun: linuxdeploy skips its hook wrapper on Qt 6, so the theme
#    setup would never run. Set the environment, then start the app.
rm -f "$APPDIR/AppRun"
{
    echo '#!/usr/bin/env bash'
    echo 'HERE="$(dirname "$(readlink -f "$0")")"'
    grep -v '^#' "$ROOT/packaging/apprun-hook-theme.sh"
    echo 'exec "$HERE/usr/bin/trimsheet" "$@"'
} > "$APPDIR/AppRun"
chmod 755 "$APPDIR/AppRun"

# icon for file managers (.DirIcon) and readable permissions for everyone
cp "$ROOT/packaging/trimsheet.png" "$APPDIR/trimsheet.png"
cp "$ROOT/packaging/trimsheet.png" "$APPDIR/.DirIcon"
chmod -R a+rX "$APPDIR"

# 4) pack
rm -f "$OUT"
ARCH=x86_64 appimagetool-x86_64.AppImage --no-appstream "$APPDIR" "$OUT"
echo
echo ">> built $OUT"
