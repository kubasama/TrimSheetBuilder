#pragma once
// Plain data types for a trim sheet project. No logic beyond small helpers.

#include <QColor>
#include <QHash>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QVector>

enum class FillMode { Stretch, Tile, Fit, Crop };
enum class NormalConvention { DirectX, OpenGL };
enum class TextureSource { Linked, Embedded };

inline QString toString(FillMode m)
{
    switch (m) {
    case FillMode::Stretch: return QStringLiteral("stretch");
    case FillMode::Tile:    return QStringLiteral("tile");
    case FillMode::Fit:     return QStringLiteral("fit");
    case FillMode::Crop:    return QStringLiteral("crop");
    }
    return QStringLiteral("stretch");
}
inline FillMode fillModeFromString(const QString &s)
{
    if (s == u"tile") return FillMode::Tile;
    if (s == u"fit")  return FillMode::Fit;
    if (s == u"crop") return FillMode::Crop;
    return FillMode::Stretch;
}
inline QString toString(NormalConvention c)
{
    return c == NormalConvention::OpenGL ? QStringLiteral("opengl") : QStringLiteral("directx");
}
inline NormalConvention conventionFromString(const QString &s)
{
    return s == u"opengl" ? NormalConvention::OpenGL : NormalConvention::DirectX;
}

enum class ChannelKind {
    Color,      // sRGB color (base color, emissive)
    Data,       // linear RGB data (packed ORM, masks)
    Normal,     // tangent-space normal map
    Grayscale,  // single channel (roughness, height, AO), exported as 8-bit gray
};

inline QString toString(ChannelKind k)
{
    switch (k) {
    case ChannelKind::Color:     return QStringLiteral("color");
    case ChannelKind::Data:      return QStringLiteral("data");
    case ChannelKind::Normal:    return QStringLiteral("normal");
    case ChannelKind::Grayscale: return QStringLiteral("grayscale");
    }
    return QStringLiteral("color");
}
inline ChannelKind channelKindFromString(const QString &s)
{
    if (s == u"data")      return ChannelKind::Data;
    if (s == u"normal")    return ChannelKind::Normal;
    if (s == u"grayscale") return ChannelKind::Grayscale;
    return ChannelKind::Color;
}

// One output map ("layer") of the sheet. Each one becomes one exported file.
struct Channel {
    QString id;                 // stable key; materials refer to channels by id
    QString name;               // display name
    ChannelKind kind = ChannelKind::Color;
    QString suffix;             // file name suffix, e.g. "_BC" -> MySheet_BC.png
    bool exportEnabled = true;
    bool alpha = false;         // Color / Data: keep an alpha channel (RGBA export)
    NormalConvention convention = NormalConvention::DirectX; // output convention (normal only)
    QColor defaultColor{128, 128, 128, 255};                 // fills empty zones / background

    bool isNormal() const { return kind == ChannelKind::Normal; }
    bool isGrayscale() const { return kind == ChannelKind::Grayscale; }
    bool srgb() const { return kind == ChannelKind::Color; }
    QString fileSuffix() const { return suffix.isEmpty() ? QStringLiteral("_") + id : suffix; }

    bool operator==(const Channel &o) const
    {
        return id == o.id && name == o.name && kind == o.kind && suffix == o.suffix
            && exportEnabled == o.exportEnabled && alpha == o.alpha && convention == o.convention
            && defaultColor == o.defaultColor;
    }
};

struct FillSettings {
    FillMode mode = FillMode::Stretch;
    double rotation = 0;        // degrees, clockwise; any angle
    bool flipX = false;
    bool flipY = false;
    QPointF scale{1.0, 1.0};    // Tile: texel scale (1 = source resolution)
    QPointF offset{0.0, 0.0};   // Tile/Crop: offset in fractions of the zone size

    bool operator==(const FillSettings &o) const
    {
        return mode == o.mode && rotation == o.rotation && flipX == o.flipX && flipY == o.flipY
            && scale == o.scale && offset == o.offset;
    }
};

struct Zone {
    QString id;
    QString name;
    QRect rect;                 // integer sheet pixels
    QString materialId;         // empty = channel defaults
    FillSettings fill;
    bool locked = false;        // layout (move / resize / delete) is blocked

    bool operator==(const Zone &o) const
    {
        return id == o.id && name == o.name && rect == o.rect && materialId == o.materialId
            && fill == o.fill && locked == o.locked;
    }
};

// A texture assigned to one channel of a material.
// One of R, G, B, A of a channel filled from its own (grayscale) texture.
// Used for packing, e.g. ORM from separate AO / roughness / metallic maps.
struct ComponentRef {
    QString textureId;          // empty = not overridden
    int source = 4;             // which part of that texture: 0..3 = R/G/B/A, 4 = gray
    bool operator==(const ComponentRef &o) const
    {
        return textureId == o.textureId && source == o.source;
    }
};

inline QString componentName(int c)
{
    static const char *names[] = {"r", "g", "b", "a", "gray"};
    return QString::fromLatin1(names[qBound(0, c, 4)]);
}
inline int componentFromName(const QString &s)
{
    for (int i = 0; i < 5; ++i)
        if (componentName(i) == s)
            return i;
    return 4;
}

// What fills one channel of a material: a texture, and optionally separate
// textures for single components that override it.
struct MapRef {
    QString textureId;          // may be empty when only components are used
    NormalConvention convention = NormalConvention::DirectX; // convention of the *source* image
    ComponentRef components[4]; // R, G, B, A
    bool hasComponents() const
    {
        for (const ComponentRef &c : components)
            if (!c.textureId.isEmpty())
                return true;
        return false;
    }
    bool isEmpty() const { return textureId.isEmpty() && !hasComponents(); }
    bool operator==(const MapRef &o) const
    {
        if (textureId != o.textureId || convention != o.convention)
            return false;
        for (int i = 0; i < 4; ++i)
            if (!(components[i] == o.components[i]))
                return false;
        return true;
    }
};

// An image set: one texture per channel.
struct Material {
    QString id;
    QString name;
    QHash<QString, MapRef> maps; // channelId -> texture
    bool operator==(const Material &o) const
    {
        return id == o.id && name == o.name && maps == o.maps;
    }
};

struct TextureEntry {
    QString id;
    TextureSource source = TextureSource::Embedded;
    QString path;               // Linked: absolute path at runtime (relative in the file)
    QString origin;             // "file", "clipboard", ...
    QImage image;               // current pixels (from disk, or cache/embedded data)
    bool missing = false;       // Linked file could not be read; image is the cached copy
};

struct ExportSettings {
    QString baseName = QStringLiteral("trimsheet");
    QString directory;          // empty = next to the project file
    QString format = QStringLiteral("png");
    bool writeZoneMetadata = true;
};
