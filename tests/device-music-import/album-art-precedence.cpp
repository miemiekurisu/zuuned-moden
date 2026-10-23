// Where an album picture comes from when a folder holds more than one claim on
// it. Compiled against the real service translation unit - the resolution order
// is a file-static function, and the only honest way to gate it is to compile
// the file that owns it, which is also how the September audit reproduced the
// two defects this test retires (docs/audits/evidence/album-art.cpp).
//
// Embedded extraction is a stub, so a run touches no FFmpeg and no device, and
// the org/app names are the test's own, so the cache it writes is never the
// profile a listener uses.
#include "AlbumArtService.cpp"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>

namespace {
int failures = 0;
int extractions = 0;
const char *embeddedName = "embedded.mp3";

void check(bool condition, const char *message) {
    if (condition) {
        std::printf("PASS: %s\n", message);
        return;
    }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

QColor colourAt(const QString &path) {
    const QImage image(path);
    return image.isNull() ? QColor() : image.pixelColor(0, 0);
}

void write(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        std::abort();
    file.close();
}

void picture(const QString &path, const QColor &colour) {
    QImage image(24, 24, QImage::Format_RGB32);
    image.fill(colour);
    if (!image.save(path, "JPEG"))
        std::abort();
}
} // namespace

// The service translation unit calls out to the in-process libav for embedded
// pictures. Only one fixture ever has one, and it is always the same colour.
extern "C" int zuuned_extract_art(const char *source, const char *destination,
                                 int) {
    ++extractions;
    if (!QString::fromUtf8(source).endsWith(QLatin1String(embeddedName)))
        return -1;
    QImage cover(48, 48, QImage::Format_RGB32);
    cover.fill(Qt::green);
    return cover.save(QString::fromUtf8(destination), "JPEG") ? 0 : -1;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ZuunedTests"));
    QCoreApplication::setApplicationName(QStringLiteral("AlbumArtPrecedence"));
    QTemporaryDir root;
    check(root.isValid(), "isolated album-art precedence scratch tree is available");
    if (!root.isValid())
        return 1;
    const QColor red(255, 0, 0), blue(0, 0, 255);
    int step = 0;
    auto out = [&] { return root.filePath(QStringLiteral("case%1.jpg").arg(++step)); };

    // 1. A name chosen on purpose outranks a picture inside the file.
    const QString own = root.filePath(QStringLiteral("own"));
    QDir().mkpath(own);
    write(own + "/embedded.mp3");
    picture(own + "/Cover.JPG", red);
    const QString ownOut = out();
    check(resolveArt(own + "/embedded.mp3", ownOut)
              && colourAt(ownOut).red() > 200 && colourAt(ownOut).blue() < 60,
          "a cover named on purpose wins over an embedded picture");

    // 2. The album's own cover wins before anything one level up is consulted,
    //    even inside a disc folder.
    const QString both = root.filePath(QStringLiteral("both/CD1"));
    QDir().mkpath(both);
    write(both + "/embedded.mp3");
    picture(both + "/folder.jpg", red);
    picture(root.filePath(QStringLiteral("both/cover.jpg")), blue);
    const QString bothOut = out();
    check(resolveArt(both + "/embedded.mp3", bothOut)
              && colourAt(bothOut).red() > 200,
          "a disc folder's own named cover beats the album cover above it");

    // 3. Embedded art beats a picture that goes by no recognised name. This is
    //    the precedence the audit found inverted: a scanned lyric sheet in with
    //    the lossless files used to replace the cover every track carried.
    const QString embedded = root.filePath(QStringLiteral("embedded"));
    QDir().mkpath(embedded);
    write(embedded + "/embedded.mp3");
    picture(embedded + "/back.jpg", red);
    const QString embeddedOut = out();
    const int beforeEmbedded = extractions;
    check(resolveArt(embedded + "/embedded.mp3", embeddedOut)
              && colourAt(embeddedOut).green() > 200 && beforeEmbedded < extractions,
          "an embedded picture is not displaced by an unnamed sidecar image");

    // 4. An unnamed image is still the answer when nothing better exists.
    const QString stray = root.filePath(QStringLiteral("stray"));
    QDir().mkpath(stray);
    write(stray + "/no-art.mp3");
    picture(stray + "/DSC00890.JPG", blue);
    const QString strayOut = out();
    check(resolveArt(stray + "/no-art.mp3", strayOut)
              && colourAt(strayOut).blue() > 200,
          "an unnamed image still supplies art when nothing named or embedded exists");

    // 5. A disc subfolder inherits the album's named cover, in each of the
    //    spellings listeners actually use.
    const QStringList discNames = {QStringLiteral("CD1"), QStringLiteral("cd 2"),
                                   QStringLiteral("Disc_3"), QStringLiteral("DISK 4"),
                                   QStringLiteral("LP5")};
    for (const QString &disc : discNames) {
        const QString album = root.filePath(QStringLiteral("disc/%1").arg(disc));
        QDir().mkpath(album);
        write(album + "/no-art.mp3");
        picture(root.filePath(QStringLiteral("disc/cover.jpg")), blue);
        const QString discOut = out();
        check(resolveArt(album + "/no-art.mp3", discOut)
                  && colourAt(discOut).blue() > 200,
              "a disc subfolder inherits the album cover one level up");
    }

    // 6. Inheriting one level up is reserved for a folder that is nothing but a
    //    disc number, so an album named after a year or a volume never adopts
    //    whatever its own parent directory happens to contain.
    for (const QString &notDisc : {QStringLiteral("Album 2024"),
                                   QStringLiteral("Vol 2"), QStringLiteral("2"),
                                   QStringLiteral("CD1 Live"), QStringLiteral("Bonus tracks")}) {
        const QString album = root.filePath(QStringLiteral("lookalike/%1").arg(notDisc));
        QDir().mkpath(album);
        write(album + "/no-art.mp3");
        picture(root.filePath(QStringLiteral("lookalike/front.jpg")), blue);
        const QString lookOut = out();
        check(!resolveArt(album + "/no-art.mp3", lookOut) && !QFile::exists(lookOut),
              "an album folder that only resembles a disc folder inherits nothing");
    }

    // 7. Nothing anywhere: no cache file is left behind to be mistaken for art.
    const QString bare = root.filePath(QStringLiteral("bare"));
    QDir().mkpath(bare);
    write(bare + "/no-art.mp3");
    const QString bareOut = out();
    check(!resolveArt(bare + "/no-art.mp3", bareOut) && !QFile::exists(bareOut),
          "an album with no picture anywhere reports no cover");

    return failures == 0 ? 0 : 1;
}
