#pragma once
#include <QString>
#include <QStringList>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <algorithm>

// Helpers for "Save as PNG" of the LaTeX export windows: the figure is compiled to PDF, rasterised at the chosen DPI
// by whichever tool is installed, and then re-saved by Qt with the chosen PNG compression and the DPI stored in it.
namespace PngExport {

struct Raster { QString program; QStringList args; QString name; };

// Finds a program: first on the PATH, then in the places where installers put it without adding it to the PATH
// (Windows: Program Files / Program Files (x86) / %LOCALAPPDATA%\\Programs; macOS: /Applications and Homebrew).
// Returns the full path (or the bare name when it is on the PATH), or an empty string.
inline QString findTool(const QString &name) {
    if (!QStandardPaths::findExecutable(name).isEmpty()) return name;
    QStringList dirs;
#ifdef Q_OS_WIN
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList roots;
    for (const QString &v : { QStringLiteral("ProgramFiles"), QStringLiteral("ProgramFiles(x86)"), QStringLiteral("ProgramW6432") })
        if (!env.value(v).isEmpty()) roots << env.value(v);
    if (!env.value("LOCALAPPDATA").isEmpty()) roots << env.value("LOCALAPPDATA") + "/Programs";
    roots << "C:/Program Files" << "C:/Program Files (x86)";
    const QString base = QFileInfo(name).baseName().toLower();
    const QString vendor = base.startsWith("gs") ? "gs" : base == "inkscape" ? "Inkscape" : base == "mutool" ? "MuPDF"
                         : base == "pdftoppm" ? "poppler" : QString();
    if (!vendor.isEmpty())
        for (const QString &r : roots) {
            QDir top(r);
            // e.g. C:/Program Files/Inkscape/bin, C:/Program Files/gs/gs10.03.1/bin, C:/Program Files/poppler-24.02.0/Library/bin
            const QStringList subs = top.entryList({ vendor + "*" }, QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &sub : subs) {
                const QString d = top.filePath(sub);
                dirs << d << d + "/bin" << d + "/Library/bin";
                QDir inner(d);
                for (const QString &v : inner.entryList({ "*" }, QDir::Dirs | QDir::NoDotAndDotDot))
                    dirs << inner.filePath(v) + "/bin" << inner.filePath(v) + "/Library/bin";
            }
        }
#else
    dirs << "/Applications/Inkscape.app/Contents/MacOS" << "/opt/homebrew/bin" << "/usr/local/bin" << "/opt/local/bin"
         << "/snap/bin" << "/var/lib/flatpak/exports/bin";
#endif
    const QString found = QStandardPaths::findExecutable(name, dirs);
    return found;
}

inline QString findGhostscript() {
    for (const QString &n : { QStringLiteral("gs"), QStringLiteral("gswin64c"), QStringLiteral("gswin32c") }) {
        const QString f = findTool(n);
        if (!f.isEmpty()) return f;
    }
    return QString();
}

inline QString findInkscape() { return findTool("inkscape"); }

// PDF "figure.pdf" -> "figure.png" in the working directory. Returns false when no rasteriser is installed.
// A transparent background needs Ghostscript or Inkscape; without them the figure gets a white background.
inline bool rasterizer(int dpi, bool transparent, Raster &out, bool *transparentOk = nullptr) {
    const QString d = QString::number(dpi);
    const QString gs = findGhostscript();
    const QString inkscapePath = findInkscape();
    const bool inkscape = !inkscapePath.isEmpty();
    if (transparentOk) *transparentOk = true;
    auto useGs = [&](const QString &device) {
        out = { gs, { "-dSAFER", "-dBATCH", "-dNOPAUSE", "-sDEVICE=" + device, "-r" + d, "-dTextAlphaBits=4",
                      "-dGraphicsAlphaBits=4", "-sOutputFile=figure.png", "figure.pdf" }, "Ghostscript" };
    };
    auto useInkscape = [&]() {
        out = { inkscapePath, { "figure.pdf", "--export-type=png", "--export-dpi=" + d, "--export-filename=figure.png" }, "Inkscape" };
    };
    if (transparent) {
        if (!gs.isEmpty()) { useGs("pngalpha"); return true; }
        if (inkscape) { useInkscape(); return true; }
        if (transparentOk) *transparentOk = false;      // fall through: white background
    }
    const QString pdftoppm = findTool("pdftoppm"), mutool = findTool("mutool");
    if (!pdftoppm.isEmpty()) {
        out = { pdftoppm, { "-png", "-r", d, "-singlefile", "figure.pdf", "figure" }, "pdftoppm" };
        return true;
    }
    if (!gs.isEmpty()) { useGs("png16m"); return true; }
    if (!mutool.isEmpty()) {
        out = { mutool, { "draw", "-r", d, "-o", "figure.png", "figure.pdf" }, "mutool" };
        return true;
    }
    if (inkscape) { useInkscape(); return true; }
    return false;
}

inline QString missingToolText() {
    return "PNG export needs a program that turns a PDF into an image: pdftoppm (Poppler), Ghostscript (gs), MuPDF (mutool) "
           "or Inkscape, on your system PATH. Ghostscript is included with TeX Live and MiKTeX installations on most "
           "systems; Poppler utilities are available for Windows, macOS (brew install poppler) and Linux.\n\n"
           "Alternatively use \"Compile to PDF\" and convert it elsewhere.";
}

// Re-saves the PNG with the DPI recorded in it and zlib compression `level` (0 = none ... 9 = smallest file, slowest).
inline bool finalize(const QString &path, int dpi, int level, QString *error = nullptr) {
    QImageReader reader(path);
    reader.setAllocationLimit(0);                     // large figures at a high DPI exceed Qt's default 256 MB limit
    QImage img = reader.read();
    if (img.isNull()) { if (error) *error = reader.errorString(); return false; }
    const int dpm = int(dpi / 0.0254 + 0.5);
    img.setDotsPerMeterX(dpm);
    img.setDotsPerMeterY(dpm);
    QImageWriter writer(path, "png");
    writer.setQuality(100 - std::clamp(level, 0, 9) * 100 / 9);     // Qt: quality 0 = smallest file, 100 = no compression
    if (!writer.write(img)) { if (error) *error = writer.errorString(); return false; }
    return true;
}

}  // namespace PngExport
