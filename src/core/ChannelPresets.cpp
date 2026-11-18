#include "core/ChannelPresets.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace {
QString tr(const char *s) { return QCoreApplication::translate("ChannelPresets", s); }

QJsonArray colorToJson(const QColor &c) { return {c.redF(), c.greenF(), c.blueF(), c.alphaF()}; }
QColor colorFromJson(const QJsonValue &v, const QColor &fallback)
{
    const QJsonArray a = v.toArray();
    if (a.size() < 3)
        return fallback;
    QColor c;
    c.setRgbF(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()),
              float(a.size() > 3 ? a[3].toDouble() : 1.0));
    return c;
}
} // namespace

QColor ChannelPresets::defaultColorFor(ChannelKind kind)
{
    switch (kind) {
    case ChannelKind::Normal:    return QColor(128, 128, 255);
    case ChannelKind::Grayscale: return QColor(128, 128, 128);
    case ChannelKind::Data:      return QColor(255, 128, 0);   // ORM: AO 1, rough .5, metal 0
    case ChannelKind::Color:     return QColor(128, 128, 128);
    }
    return QColor(128, 128, 128);
}

QString ChannelPresets::uniqueId(const QString &name, const QVector<Channel> &existing)
{
    static const QRegularExpression nonWord(QStringLiteral("[^a-z0-9]+"));
    QString base = name.toLower().replace(nonWord, QStringLiteral("_"));
    while (base.startsWith(u'_')) base.remove(0, 1);
    while (base.endsWith(u'_')) base.chop(1);
    if (base.isEmpty())
        base = QStringLiteral("channel");
    auto taken = [&](const QString &id) {
        for (const Channel &c : existing)
            if (c.id == id)
                return true;
        return false;
    };
    QString id = base;
    for (int n = 2; taken(id); ++n)
        id = QStringLiteral("%1_%2").arg(base).arg(n);
    return id;
}

Channel ChannelPresets::make(const QString &name, ChannelKind kind, const QString &suffix,
                             const QVector<Channel> &existing)
{
    Channel c;
    c.id = uniqueId(name, existing);
    c.name = name;
    c.kind = kind;
    c.suffix = suffix;
    c.defaultColor = defaultColorFor(kind);
    return c;
}

QVector<Channel> ChannelPresets::defaultChannels()
{
    return builtIns()[2].channels;
}

const QVector<ChannelPresets::Preset> &ChannelPresets::builtIns()
{
    static const QVector<Preset> presets = [] {
        auto build = [](const QString &name,
                        std::initializer_list<std::tuple<const char *, ChannelKind, const char *>> list) {
            Preset p;
            p.name = name;
            for (const auto &[n, k, sfx] : list) {
                Channel c = make(tr(n), k, QString::fromLatin1(sfx), p.channels);
                // ids stay stable and English regardless of translation
                c.id = uniqueId(QString::fromLatin1(n), p.channels);
                p.channels.append(c);
            }
            return p;
        };
        QVector<Preset> out;
        out.append(build(tr("Base Color only"),
                         {{"Base Color", ChannelKind::Color, "_BC"}}));
        out.append(build(tr("Base Color + Normal"),
                         {{"Base Color", ChannelKind::Color, "_BC"},
                          {"Normal", ChannelKind::Normal, "_N"}}));
        out.append(build(tr("Base Color + Normal + ORM"),
                         {{"Base Color", ChannelKind::Color, "_BC"},
                          {"Normal", ChannelKind::Normal, "_N"},
                          {"ORM", ChannelKind::Data, "_ORM"}}));
        out.append(build(tr("PBR separate maps"),
                         {{"Base Color", ChannelKind::Color, "_BaseColor"},
                          {"Normal", ChannelKind::Normal, "_Normal"},
                          {"Roughness", ChannelKind::Grayscale, "_Roughness"},
                          {"Metallic", ChannelKind::Grayscale, "_Metallic"},
                          {"AO", ChannelKind::Grayscale, "_AO"}}));
        out.append(build(tr("PBR + Height"),
                         {{"Base Color", ChannelKind::Color, "_BaseColor"},
                          {"Normal", ChannelKind::Normal, "_Normal"},
                          {"Roughness", ChannelKind::Grayscale, "_Roughness"},
                          {"Metallic", ChannelKind::Grayscale, "_Metallic"},
                          {"AO", ChannelKind::Grayscale, "_AO"},
                          {"Height", ChannelKind::Grayscale, "_Height"}}));
        // sensible defaults that differ from the per-kind ones
        for (Preset &p : out)
            for (Channel &c : p.channels) {
                if (c.id == u"metallic") c.defaultColor = QColor(0, 0, 0);
                if (c.id == u"ao")       c.defaultColor = QColor(255, 255, 255);
                if (c.id == u"roughness") c.defaultColor = QColor(128, 128, 128);
                if (c.id == u"base_color") c.alpha = true; // RGBA by default
            }
        return out;
    }();
    return presets;
}

QJsonObject ChannelPresets::toJson(const Channel &c)
{
    QJsonObject o{
        {"id", c.id},
        {"name", c.name},
        {"kind", toString(c.kind)},
        {"suffix", c.suffix},
        {"export", c.exportEnabled},
        {"alpha", c.alpha},
        {"default", colorToJson(c.defaultColor)},
    };
    if (c.isNormal())
        o["convention"] = toString(c.convention);
    return o;
}

Channel ChannelPresets::fromJson(const QJsonObject &o)
{
    Channel c;
    c.id = o["id"].toString();
    c.name = o["name"].toString(c.id);
    if (o.contains("kind")) {
        c.kind = channelKindFromString(o["kind"].toString());
    }
    // format v1 had no "kind" for color/data: derive it from the color space
    if (!o.contains("kind") || o["kind"].toString().isEmpty())
        c.kind = o["colorspace"].toString() == u"srgb" ? ChannelKind::Color : ChannelKind::Data;
    c.suffix = o["suffix"].toString();
    c.exportEnabled = o["export"].toBool(true);
    c.alpha = o["alpha"].toBool(false);
    c.convention = conventionFromString(o["convention"].toString());
    c.defaultColor = colorFromJson(o["default"], defaultColorFor(c.kind));
    return c;
}

QJsonArray ChannelPresets::toJson(const QVector<Channel> &channels)
{
    QJsonArray a;
    for (const Channel &c : channels)
        a.append(toJson(c));
    return a;
}

QVector<Channel> ChannelPresets::fromJson(const QJsonArray &a)
{
    QVector<Channel> out;
    for (const QJsonValue &v : a) {
        Channel c = fromJson(v.toObject());
        if (!c.id.isEmpty())
            out.append(c);
    }
    return out;
}
