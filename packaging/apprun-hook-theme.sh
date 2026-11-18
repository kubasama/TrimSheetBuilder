# Sourced by the AppImage's AppRun before the app starts.
# Use the host GTK theme (colors, fonts, icons, native file dialogs) via the
# bundled gtk3 platform theme plugin, unless the user picked something else
# or is on KDE (where the bundled Qt can't load Plasma's own plugins anyway).
if [ -z "$QT_QPA_PLATFORMTHEME" ]; then
    case "$XDG_CURRENT_DESKTOP" in
        *KDE*) export QT_QPA_PLATFORMTHEME=xdgdesktopportal ;;
        *)     export QT_QPA_PLATFORMTHEME=gtk3 ;;
    esac
fi
