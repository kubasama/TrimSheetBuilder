#pragma once
// App look: dark and flat, with the accent color taken
// from the desktop.

#include <QColor>
#include <QString>

namespace Theme {

// Accent lookup, in order: xdg-desktop-portal accent-color, the active GTK
// theme's selected/accent color, the platform palette, a built-in blue.
QColor detectSystemAccent(QString *source = nullptr);

// Sets style, palette and stylesheet on the running QApplication.
void apply(const QColor &accent);

QColor accent();
QColor accentText();     // readable text on top of the accent

// Fixed palette
inline QColor canvas()   { return QColor(0x16, 0x16, 0x16); }
inline QColor window()   { return QColor(0x1f, 0x1f, 0x1f); }
inline QColor card()     { return QColor(0x28, 0x28, 0x28); }
inline QColor field()    { return QColor(0x1c, 0x1c, 0x1c); }
inline QColor border()   { return QColor(0x3a, 0x3a, 0x3a); }
inline QColor text()     { return QColor(0xdc, 0xdc, 0xdc); }
inline QColor textDim()  { return QColor(0x8c, 0x8c, 0x8c); }

} // namespace Theme
