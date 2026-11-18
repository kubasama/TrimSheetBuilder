// Core tests: save/load round trip, compositing, normal map rotation, export.
#include "core/Commands.h"
#include "core/Compositor.h"
#include "core/Exporter.h"
#include "core/Project.h"
#include "core/Serializer.h"
#include "io/TextureWatcher.h"
#include "core/LayoutPresets.h"
#include "core/ChannelPresets.h"
#include "gui/Snapper.h"

#include <QFile>

#include <filesystem>
#include <system_error>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <cstdio>

class TestCore : public QObject
{
    Q_OBJECT

    static QImage solid(QColor c, QSize s = {8, 8})
    {
        QImage img(s, QImage::Format_ARGB32);
        img.fill(c);
        return img;
    }

    // Normal pointing "right" (+X) is the same in both conventions.
    static QImage normalRight() { return solid(QColor(255, 128, 128)); }
    // Normal pointing "up the image" in OpenGL convention (G = +Y up).
    static QImage normalUpGL() { return solid(QColor(128, 255, 128)); }

private slots:
    void undoRedoZone()
    {
        Project *p = Project::createDefault({1024, 1024});
        Zone z;
        z.id = "z1";
        z.rect = {0, 0, 1024, 64};
        p->undoStack()->push(new AddZoneCommand(p, z));
        QCOMPARE(p->zones().size(), 1);
        Zone moved = z;
        moved.rect.moveTop(128);
        p->undoStack()->push(new SetZoneCommand(p, z, moved, "move"));
        QCOMPARE(p->zone("z1")->rect.top(), 128);
        p->undoStack()->undo();
        QCOMPARE(p->zone("z1")->rect.top(), 0);
        p->undoStack()->undo();
        QCOMPARE(p->zones().size(), 0);
        delete p;
    }

    void stretchFillAndPadding()
    {
        Project *p = Project::createDefault({64, 64});
        p->setPadding(4);
        Zone z;
        z.id = "z1";
        z.rect = {16, 16, 32, 8};
        p->insertZone(0, z);
        const QString tex = p->addTextureFromImage(solid(Qt::red), "test");
        Edit::assignTexture(p, "z1", "base_color", tex);

        const QImage out = Compositor::renderChannel(*p, "base_color", true);
        QCOMPARE(QColor(out.pixel(20, 20)), QColor(Qt::red));     // inside
        QCOMPARE(QColor(out.pixel(20, 13)), QColor(Qt::red));     // top bleed
        QCOMPARE(QColor(out.pixel(13, 13)), QColor(Qt::red));     // corner bleed
        QCOMPARE(QColor(out.pixel(20, 2)), QColor(128, 128, 128)); // beyond padding
        delete p;
    }

    void normalRotation()
    {
        Channel ch;
        ch.id = "normal";
        ch.kind = ChannelKind::Normal;
        ch.convention = NormalConvention::OpenGL;

        FillSettings f;
        f.rotation = 90; // clockwise: +X (right) should become down = -Y in GL
        QImage out = Compositor::prepareSource(normalRight(), f, ch, NormalConvention::OpenGL);
        QColor c(out.pixel(0, 0));
        QCOMPARE(c.red(), 128);
        QCOMPARE(c.green(), 1); // -Y

        // GL up rotated 90° CW -> right
        out = Compositor::prepareSource(normalUpGL(), f, ch, NormalConvention::OpenGL);
        c = QColor(out.pixel(0, 0));
        QCOMPARE(c.red(), 255);
        QCOMPARE(c.green(), 128);

        // DX source, DX output, no transform: unchanged
        ch.convention = NormalConvention::DirectX;
        f = {};
        out = Compositor::prepareSource(normalUpGL(), f, ch, NormalConvention::DirectX);
        QCOMPARE(QColor(out.pixel(0, 0)).green(), 255);

        // GL source into DX output: green flips
        out = Compositor::prepareSource(normalUpGL(), f, ch, NormalConvention::OpenGL);
        QCOMPARE(QColor(out.pixel(0, 0)).green(), 1);

        // flat normal stays flat through rotation
        f.rotation = 90;
        out = Compositor::prepareSource(solid(QColor(128, 128, 255)), f, ch, NormalConvention::OpenGL);
        QCOMPARE(QColor(out.pixel(0, 0)), QColor(128, 128, 255));
        f.rotation = 0;

        // free angle: 45° on a GL "right" normal -> down-right
        f.rotation = 45;
        ch.convention = NormalConvention::OpenGL;
        out = Compositor::prepareSource(normalRight(), f, ch, NormalConvention::OpenGL);
        c = QColor(out.pixel(0, 0));
        QVERIFY(qAbs(c.red() - 218) <= 1);   // 128 + 127*cos45
        QVERIFY(qAbs(c.green() - 38) <= 1);  // 128 - 127*sin45
        // flat normal stays flat at any angle
        out = Compositor::prepareSource(solid(QColor(128, 128, 255)), f, ch, NormalConvention::OpenGL);
        QCOMPARE(QColor(out.pixel(0, 0)), QColor(128, 128, 255));
        f.rotation = 0;
        ch.convention = NormalConvention::DirectX;

        // flip X negates red
        f.flipX = true;
        out = Compositor::prepareSource(normalRight(), f, ch, NormalConvention::DirectX);
        QCOMPARE(QColor(out.pixel(0, 0)).red(), 1);
    }

    void saveLoadRoundTrip()
    {
        QTemporaryDir dir;
        const QString linkedPath = dir.filePath("bricks.png");
        QVERIFY(solid(Qt::blue, {16, 16}).save(linkedPath));

        Project *p = Project::createDefault({512, 256});
        Zone z;
        z.id = "z1";
        z.name = "strip";
        z.rect = {0, 0, 512, 32};
        z.fill.mode = FillMode::Tile;
        z.fill.rotation = 270;
        p->insertZone(0, z);
        const QString linked = p->addTextureFromFile(linkedPath);
        const QString pasted = p->addTextureFromImage(solid(Qt::green), "clipboard");
        QVERIFY(!linked.isEmpty() && !pasted.isEmpty());
        Edit::assignTexture(p, "z1", "base_color", linked);
        Edit::assignTexture(p, "z1", "orm", pasted);

        const QString file = dir.filePath("test.trimsheet");
        QString err;
        QVERIFY2(Serializer::save(*p, file, &err), qPrintable(err));

        Project *q = Serializer::load(file, &err);
        QVERIFY2(q, qPrintable(err));
        QCOMPARE(q->sheetSize(), QSize(512, 256));
        QCOMPARE(q->zones().size(), 1);
        QVERIFY(q->zones()[0].fill.mode == FillMode::Tile);
        QCOMPARE(q->zones()[0].fill.rotation, 270.0);
        QVERIFY(q->texture(linked)->source == TextureSource::Linked);
        QVERIFY(q->texture(pasted)->source == TextureSource::Embedded);
        QCOMPARE(QColor(q->texture(pasted)->image.pixel(0, 0)), QColor(Qt::green));
        delete q;

        // linked file gone -> cached copy is used
        QFile::remove(linkedPath);
        q = Serializer::load(file, &err);
        QVERIFY(q);
        QVERIFY(q->texture(linked)->missing);
        QCOMPARE(QColor(q->texture(linked)->image.pixel(0, 0)), QColor(Qt::blue));
        delete q;
        delete p;
    }

    void watcherReloadsAtomicSave()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("tex.png");
        QVERIFY(solid(Qt::red).save(path));
        Project *p = Project::createDefault({64, 64});
        const QString id = p->addTextureFromFile(path);
        TextureWatcher w;
        w.setProject(p);

        // editor-style save: write temp file, rename over the original (twice,
        // to check the watch is re-armed)
        for (QColor c : {QColor(Qt::green), QColor(Qt::blue)}) {
            QTest::qWait(1100); // mtime resolution on some filesystems
            const QString tmp = dir.filePath("tex.png.tmp");
            QVERIFY(solid(c).save(tmp, "PNG"));
            std::error_code err; // replaces the target on Windows too (unlike ::rename)
            std::filesystem::rename(tmp.toStdWString(), path.toStdWString(), err);
            QVERIFY2(!err, err.message().c_str());
            QTRY_COMPARE_WITH_TIMEOUT(QColor(p->texture(id)->image.pixel(0, 0)), c, 3000);
        }
        delete p;
    }

    void presetsTileTheSheet()
    {
        for (const auto &pr : LayoutPresets::all()) {
            const auto zones = LayoutPresets::generate(pr.id, {2048, 2048}, pr.defaultCount,
                                                       pr.defaultCount2, 16);
            for (int i = 0; i < zones.size(); ++i) {
                QVERIFY(QRect(0, 0, 2048, 2048).contains(zones[i].rect));
                for (int j = i + 1; j < zones.size(); ++j)
                    QVERIFY2(!zones[i].rect.intersects(zones[j].rect), qPrintable(pr.id));
            }
        }
        // cascade: biggest square top-left, smallest bottom-right
        const auto cas = LayoutPresets::generate("cascade", {1024, 1024}, 2, 0, 0);
        QCOMPARE(cas.size(), 22);
        QCOMPARE(cas[0].rect, QRect(0, 0, 512, 512));
        QCOMPARE(cas.last().rect, QRect(896, 896, 128, 128));
        // gap stays off the sheet border
        const auto strips = LayoutPresets::generate("strips", {1024, 1024}, 4, 0, 16);
        QCOMPARE(strips[0].rect, QRect(0, 0, 1024, 248));
        QCOMPARE(strips[1].rect, QRect(0, 264, 1024, 240));
    }

    void snapperOverlapAndEdges()
    {
        Project *p = Project::createDefault({1024, 1024});
        p->setGridStep(16);
        Zone a; a.id = "a"; a.rect = {0, 0, 200, 100};
        Zone b; b.id = "b"; b.rect = {500, 0, 200, 100};
        p->insertZone(0, a);
        p->insertZone(1, b);

        SnapSettings s;
        // resizing a's right edge into b stops at b's left edge
        Snapper sn(*p, s, 1.0, false);
        QRect r = sn.resize("a", a.rect, SnapEdge::Right, {600, 0}, a.rect);
        QCOMPARE(r, QRect(0, 0, 500, 100));
        // with overlap allowed it goes through (grid-snapped)
        s.allowOverlap = true;
        Snapper sn2(*p, s, 1.0, false);
        r = sn2.resize("a", a.rect, SnapEdge::Right, {600, 0}, a.rect);
        QCOMPARE(r.width(), 800);
        // moving close to b's left edge snaps to it (edge magnet beats grid)
        s.allowOverlap = false;
        Snapper sn3(*p, s, 1.0, false);
        r = sn3.move("a", a.rect, {295, 0}, a.rect);
        QCOMPARE(r.x() + r.width(), 500);
        QVERIFY(!sn3.guides().isEmpty());
        // snapping off: exact pixel
        Snapper sn4(*p, s, 1.0, true);
        r = sn4.move("a", a.rect, {37, 3}, a.rect);
        QCOMPARE(r.topLeft(), QPoint(37, 3));
        delete p;
    }

    void customChannelsExport()
    {
        QTemporaryDir dir;
        QVector<Channel> chans = ChannelPresets::builtIns()[3].channels; // PBR separate
        QCOMPARE(chans.size(), 5);
        chans[4].exportEnabled = false; // AO off
        Project *p = Project::createDefault({64, 64}, nullptr, chans);
        QString err;
        const QStringList files = Exporter::exportSheet(*p, dir.path(), "T", &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(files.size(), 5); // 4 maps + zones.json
        QImage rough(dir.filePath("T_Roughness.png"));
        QVERIFY(!rough.isNull());
        QVERIFY(rough.isGrayscale());
        QVERIFY(!QFile::exists(dir.filePath("T_AO.png")));

        // round trip keeps kind / suffix / export flag
        const QString file = dir.filePath("c.trimsheet");
        QVERIFY(Serializer::save(*p, file, &err));
        Project *q = Serializer::load(file, &err);
        QVERIFY(q);
        QVERIFY(q->channels() == p->channels());
        delete q;
        delete p;
    }

    void componentPacking()
    {
        // ORM from three separate grayscale textures, plus alpha
        Project *p = Project::createDefault({16, 16});
        QVector<Channel> ch = p->channels();
        ch[2].alpha = true;
        p->setChannels(ch);
        Zone z;
        z.id = "z";
        z.rect = {0, 0, 16, 16};
        p->insertZone(0, z);
        const QString ao = p->addTextureFromImage(solid(QColor(200, 200, 200)), "t");
        const QString rough = p->addTextureFromImage(solid(QColor(10, 90, 10)), "t"); // G = 90
        const QString metal = p->addTextureFromImage(solid(QColor(30, 30, 30)), "t");
        QImage maskImg(8, 8, QImage::Format_ARGB32);
        maskImg.fill(qRgba(0, 0, 0, 77));
        const QString mask = p->addTextureFromImage(maskImg, "t");
        Edit::assignComponent(p, "z", "orm", 0, ao);
        Edit::assignComponent(p, "z", "orm", 1, rough);
        Edit::setComponentSource(p, "z", "orm", 1, 1); // take G
        Edit::assignComponent(p, "z", "orm", 2, metal);
        Edit::assignComponent(p, "z", "orm", 3, mask);
        Edit::setComponentSource(p, "z", "orm", 3, 3); // take A

        const QImage out = Compositor::renderChannel(*p, "orm", false);
        const QRgb px = out.pixel(5, 5);
        QCOMPARE(qRed(px), 200);
        QCOMPARE(qGreen(px), 90);
        QCOMPARE(qBlue(px), 30);
        QCOMPARE(qAlpha(px), 77);

        // round trip keeps components
        QTemporaryDir dir;
        QString err;
        QVERIFY(Serializer::save(*p, dir.filePath("p.trimsheet"), &err));
        Project *q = Serializer::load(dir.filePath("p.trimsheet"), &err);
        QVERIFY(q);
        const MapRef r = q->material(q->zone("z")->materialId)->maps.value("orm");
        QCOMPARE(r.components[1].source, 1);
        QCOMPARE(r.components[3].textureId, mask);
        delete q;
        delete p;
    }

    void exportFiles()
    {
        QTemporaryDir dir;
        Project *p = Project::createDefault({128, 128});
        Zone z;
        z.id = "z1";
        z.name = "top";
        z.rect = {0, 0, 128, 32};
        p->insertZone(0, z);
        QString err;
        const QStringList files = Exporter::exportSheet(*p, dir.path(), "sheet", &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(files.size(), 4); // 3 channels + zones.json
        QVERIFY(QFile::exists(dir.filePath("sheet_BC.png")));
        QFile f(dir.filePath("sheet_zones.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto uv = QJsonDocument::fromJson(f.readAll())
                            .object()["zones"].toArray()[0].toObject()["uv"].toArray();
        QCOMPARE(uv[3].toDouble(), 0.25);
        delete p;
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
