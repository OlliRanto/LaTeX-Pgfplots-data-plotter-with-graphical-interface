# Universal Data Plotter: Build & Technical Guide

How to compile Universal Data Plotter and how the code is organised.
For what the program does and how to use it, see [README.md](README.md).

> The build steps below are taken from the project's `CMakeLists.txt` and `UniversalPlotter.pro`. They have not been test-built in the environment this document was written in (no Qt installed there), so treat versions and package names as a starting point.

---

## Contents

1. [Requirements](#requirements)
2. [Project layout](#project-layout)
3. [Building with CMake](#building-with-cmake)
4. [Building with qmake](#building-with-qmake)
5. [Building in Qt Creator](#building-in-qt-creator)
6. [Platform notes](#platform-notes)
7. [LaTeX tools for Compile and SVG](#latex-tools-for-compile-and-svg)
8. [Architecture overview](#architecture-overview)
9. [Source file reference](#source-file-reference)
10. [Algorithms and design notes](#algorithms-and-design-notes)
11. [Persisted settings](#persisted-settings)
12. [Third-party code and licensing](#third-party-code-and-licensing)
13. [Known issues and pitfalls](#known-issues-and-pitfalls)
14. [Extending the app](#extending-the-app)

---

## Requirements

| Requirement | Version |
|---|---|
| C++ compiler | C++17 (GCC, Clang, MSVC or MinGW-w64) |
| Qt | **Qt 6** with the **Widgets** and **PrintSupport** modules |
| CMake | 3.16 or newer (for the CMake build) |
| qmake | Qt's own, only for the `.pro` build |

The project uses `qt_standard_project_setup()` (a Qt 6 CMake command) and `find_package(Qt6 ...)`, so the CMake build needs Qt 6. QCustomPlot 2.1.1 is bundled in `third_party/`, so there is nothing else to install.

**Runtime-only extras (optional):** a LaTeX distribution for the in-app *Compile to PDF* and *Save as SVG* buttons. See [LaTeX tools](#latex-tools-for-compile-and-svg). The app runs fine without it.

---

## Project layout

```
UniversalPlotterC++_source/
├── CMakeLists.txt              CMake build (Qt 6)
├── UniversalPlotter.pro        qmake build
├── resources/
│   └── Plot.ico                Windows application icon
├── src/                        Application code (about 8,000 lines)
│   ├── main.cpp
│   ├── MainWindow.{h,cpp}
│   ├── Dataset.h
│   ├── DataLoader.{h,cpp}
│   ├── DataEntryDialog.{h,cpp}
│   ├── CurveFit.{h,cpp}
│   ├── ExprEval.{h,cpp}
│   ├── DraggableVLine.{h,cpp}
│   ├── LatexExport.{h,cpp}
│   ├── LatexExportDialog.{h,cpp}
│   ├── LatexColors.h
│   ├── ColorPickerWidget.{h,cpp}
│   └── NoWheelComboFilter.h
├── third_party/qcustomplot/    QCustomPlot 2.1.1 (qcustomplot.{h,cpp}, config.h)
└── .qtcreator/                 Qt Creator per-user project file
```

---

## Building with CMake

### Linux

Install the toolchain and Qt 6 development packages. On Debian / Ubuntu:

```bash
sudo apt install build-essential cmake qt6-base-dev
```

On Fedora:

```bash
sudo dnf install gcc-c++ cmake qt6-qtbase-devel
```

Then configure, build and run from the project root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/UniversalPlotter
```

### macOS

```bash
brew install cmake qt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j
./build/UniversalPlotter
```

### Windows (MinGW, as shipped with Qt)

Open the *Qt 6.x (MinGW) command prompt* from the Start menu, or make sure Qt's `bin` and the MinGW `bin` folders are on `PATH`:

```bat
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\mingw_64
cmake --build build -j
build\UniversalPlotter.exe
```

### Windows (MSVC)

From an *x64 Native Tools Command Prompt*:

```bat
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2019_64
cmake --build build
```

Use the Qt directory that matches your compiler. If CMake can't find Qt, `CMAKE_PREFIX_PATH` is the thing to set.

### Build types

Use **Release** for normal use. A Debug build of `qcustomplot.cpp` is very large (see the MinGW note below) and slow to compile.

### Deploying on Windows

The exe needs the Qt DLLs next to it. Collect them with Qt's tool:

```bat
windeployqt build\UniversalPlotter.exe
```

On macOS use `macdeployqt`; on Linux, ship via your package format or `linuxdeployqt` / AppImage tooling.

---

## Building with qmake

```bash
mkdir build && cd build
qmake6 ../UniversalPlotter.pro CONFIG+=release     # may be called just `qmake` with Qt 6
make -j            # or mingw32-make / nmake / jom on Windows
```

The `.pro` file requests `QT += core gui widgets printsupport`, `CONFIG += c++17`, and lists every source and header explicitly.

---

## Building in Qt Creator

1. **File → Open File or Project…** and choose `CMakeLists.txt` (or `UniversalPlotter.pro`).
2. Select a **Qt 6** kit.
3. Build and run (Ctrl+R).

Qt Creator keeps your personal settings in `.qtcreator/UniversalPlotter.pro.user`. That file is machine-specific. If Creator complains about a mismatched kit, delete it and reopen the project, and don't commit it to version control.

---

## Platform notes

### MinGW "too many sections" / "file too big"

`qcustomplot.cpp` is template-heavy. In a Debug build with MinGW, it produces more object-file sections than the classic COFF format allows (about 32k) and the assembler fails. Both build files already handle this by passing `-Wa,-mbig-obj`:

- **CMake**: applied only to `qcustomplot.cpp`, and only `if (MINGW)`.
- **qmake**: applied to the whole target under `win32-g++`.

You shouldn't need to do anything, but if you see that error with another toolchain, add the same flag.

### Windows icon

`UniversalPlotter.pro` sets `RC_ICONS = resources/plot.ico` (lowercase `p`) on Windows. The file in the repository is named `Plot.ico`. Windows file systems ignore case so it works there; if you build on a case-sensitive file system for Windows (cross-compiling), rename one or the other to match. The CMake build does not attach the icon at all (see [Known issues](#known-issues-and-pitfalls)).

### Wayland / high-DPI

Standard Qt 6 behaviour applies. Nothing in the app overrides platform plugins or scaling.

---

## LaTeX tools for Compile and SVG

These are **runtime** features, not build dependencies. The app looks for the tools on `PATH` when you press the buttons.

| Feature | Tools used |
|---|---|
| **Compile to PDF...** | The first one found of `lualatex`, `latexmk`, `pdflatex`, `xelatex` (in that order). `latexmk` is run with `-lualatex`. Runs with `-interaction=nonstopmode -halt-on-error` in a temp directory. |
| **Save as SVG...** (preferred route) | `latex` then `dvisvgm` (`--no-fonts --exact-bbox`), using pgf's `pgfsys-dvisvgm.def` driver. |
| **Save as SVG...** (fallback) | PDF produced as above, converted with `pdf2svg` or `inkscape`. |

TeX Live and MiKTeX both cover this. In MiKTeX, install the `dvisvgm` package if prompted. The generated document loads `pgfplots` with `\pgfplotsset{compat=1.18}` (and the TikZ `patterns` library when pattern fills are used), so a reasonably recent TeX distribution is required.

---

## Architecture overview

The application is a single-window Qt Widgets program. There is no separate model/view layer; `MainWindow` owns the state and the UI.

```
              ┌────────────────────────────────────────────┐
              │                 MainWindow                 │
 files ─────► │  datasets, curve selection, plot, overlays │
 DataLoader   │  fit / integral / interpolation state      │
              └──────┬─────────────┬──────────────┬────────┘
   DataEntryDialog ──┘             │              └── LatexExportDialog
   (manual table)        QCustomPlot widget            │ reads MainWindow's
                         + DraggableVLineGroup         │ public accessors
                                                       ▼
                                      LatexExport (pure functions)
                                      buildLatexDocument / buildCsvExportText
```

Data flow:

1. `loadDataset()` parses a file into a `Dataset` (columns of `double`, with `NaN` for gaps).
2. `MainWindow` keeps a `QVector<Dataset>` and builds X-column selectors and the Y-column list from it.
3. **Plot** turns the selected `CurveKey`s into QCustomPlot graphs (line), `QCPBars` (bar) or binned bars (histogram).
4. Fit / integral / cursor features read the *plotted* X and Y arrays, draw overlays, and write their results into `m_fitResults` / `m_integralResults`.
5. `LatexExportDialog` reads everything it needs back through `MainWindow`'s public read-only accessors (curves, colours, fit results, integral regions, axis ranges, histogram bins, category labels …), collects it into a `LatexExportOptions` struct, and hands that to `buildLatexDocument()`.

---

## Source file reference

| File | Responsibility |
|---|---|
| `main.cpp` | Creates the `QApplication`, sets organisation and application name (used for `QSettings`), installs the wheel filter, shows `MainWindow` at 1150×700. |
| `MainWindow.{h,cpp}` (~2,800 lines) | The main window: UI construction, loading, plotting for all three plot types, navigation history, fit / integral / interpolation features, axis limits. Exposes accessor methods for the LaTeX dialog. |
| `Dataset.h` | Core data types: `ColumnData`, `LoadInfo`, `Dataset`, `CurveKey` (dataset index + column name + second-axis flag, with `qHash`/ordering), and the default colour cycle. |
| `DataLoader.{h,cpp}` | `loadDataset()` plus the `Detect` helpers (`detectSeparator`, `detectDecimal`, `tokenizeLine`, `isNumericToken`). |
| `DataEntryDialog.{h,cpp}` | The "Enter data manually" window. A `QTableWidget` subclass with Excel-style copy / cut / paste, a header-row toggle, and conversion of the grid into a `Dataset`. |
| `CurveFit.{h,cpp}` | `performFit()`: linear, polynomial, exponential, logarithmic, power and custom fits, with a self-contained Gaussian-elimination solver and Levenberg-Marquardt optimiser. |
| `ExprEval.{h,cpp}` | Small recursive-descent expression evaluator used for custom fits. |
| `DraggableVLine.{h,cpp}` | `DraggableVLineGroup`: one or more draggable vertical lines (as `QCPItemStraightLine`) with an event filter and optional snapping. Used for fit range, integral range and the interpolation cursor. |
| `LatexExport.{h,cpp}` | Pure (UI-free) code generation: `LatexExportOptions` and friends, `buildLatexDocument()`, `buildCsvExportText()`, `translateCustomExpr()`. |
| `LatexExportDialog.{h,cpp}` (~2,500 lines) | The export dialog: one `QGroupBox` section per option group, validation (`collectOpts()`), settings persistence, the code viewer and the compile / SVG pipeline (`QProcess`). |
| `LatexColors.h` | The 19 built-in LaTeX colour names, tab10 to LaTeX colour mapping, marker / line-style / fill-type lists, `texEscape()` and hex-colour helpers. |
| `ColorPickerWidget.{h,cpp}` | Colour-name drop-down, clickable swatch and hex field in one widget. `colorToken()` returns either a LaTeX colour name or a bare 6-digit hex string. |
| `NoWheelComboFilter.h` | Application-wide event filter that swallows mouse-wheel events on combo boxes and spin boxes. |

---

## Algorithms and design notes

### File parsing (`DataLoader.cpp`)

- Reads up to the first 300 non-blank lines as a *sample*. Tries UTF-8 (BOM stripped), counting replacement characters to decide when to fall back to Latin-1.
- `detectSeparator()` scores candidate separators (`, ; \t |` and whitespace) over the first ~40 lines of the sample and accepts the best one if it appears consistently (at least `max(2, 30% of the sampled lines)`).
- `detectDecimal()` decides between `.` and `,`. For comma decimals, thousands dots are removed and the comma swapped for a dot before parsing.
- `analyzeStructure()` finds the block of consistently-shaped lines, decides if its first line is a header, and marks everything above it as skipped preamble.
- The whole file is then re-read; rows whose field count differs from the expected one are dropped.
- A column is numeric if at least **80%** of its cells parse. Non-numeric columns keep their raw text in `ColumnData::labels` for use as bar categories.
- `loadDataset()` throws `std::runtime_error` on failure; `MainWindow` shows the message.

### Curve fitting (`CurveFit.cpp`)

- **Linear / polynomial**: normal equations solved by Gaussian elimination with partial pivoting. Coefficients are highest-degree first (highest power first). Polynomial degree is limited to 1–12 by the UI, and the data must have more points than the degree.
- **Logarithmic**: linear fit of `y` against `ln(x)` on the points with `x > 0`.
- **Exponential, Power, Custom**: Levenberg-Marquardt with a central-difference numerical Jacobian and adaptive damping. Exponential seeds its starting parameters from the data (amplitude from the Y span and trend, offset from the minimum, rate 0.01) and clamps the exponent to ±700 to avoid overflow. Power seeds `a` from the first Y value and `b = 1`. Custom uses the user's guesses.
- Custom expressions and parameter names are validated up front (names must not clash with `x`, constants, functions or each other). The returned `FitResult::func` captures everything **by value** and is written never to throw, because callers invoke it outside any `try`/`catch`.
- Every `FitResult` carries the formula text, parameters, R², number of points and the x-domain to draw.

### Expression evaluator (`ExprEval.cpp`)

A recursive-descent parser evaluated in a single pass:

```
expr  := term (('+' | '-') term)*
term  := unary (('*' | '/') unary)*
unary := '-' unary | power
power := atom ('^' unary)?          (right-associative)
atom  := number | ident '(' expr (',' expr)* ')' | ident | '(' expr ')'
```

Only `x`, user parameters and a fixed list of maths functions and constants are reachable, so no code can be injected through a formula. (Note that, as written, `-x^2` parses as `-(x^2)`.)

### Integration (`MainWindow.cpp`)

`integrateData()` drops NaN points, sorts by X, clamps the requested range to the data, linearly interpolates the Y values at both ends, and applies the **trapezoidal rule**. It throws if no data lies in the range. Fits can be integrated too (the "Integrate" list offers fitted curves alongside the raw data). The shaded region is a `QCPCurve` polygon closed at y = 0.

### Interpolation

Piecewise-linear interpolation of the sorted, NaN-free curve; values outside the data range are clamped to the end point and flagged as extrapolated in the results text.

### Draggable lines (`DraggableVLine.cpp`)

QCustomPlot's own axis-rect dragging would grab any mouse press, so `DraggableVLineGroup` installs an event filter on the plot that intercepts presses near a line (within a small pixel distance), moves the line while the mouse is held, optionally snaps it to the nearest value from a caller-supplied sorted array, and calls the `onChange` callback continuously during the drag and once on release.

### View history

Range changes (from pan, wheel zoom, zoom-rectangle, limit entry) are recorded as `ViewState{x, y, y2}` entries, coalesced by a debounce timer so a single drag produces one history step. `m_restoringView` guards against re-recording while Back / Forward / Home restore a view.

### LaTeX generation (`LatexExport.cpp`)

- `LatexExportDialog::collectOpts()` validates the UI and fills a `LatexExportOptions`; it throws `std::runtime_error` with a user-readable message on bad input.
- `buildLatexDocument()` is UI-free and deterministic. That makes it easy to unit-test: build an options struct by hand and compare the output string.
- Colours: names from the 19-colour base set are used as-is; any 6-digit hex becomes `\definecolor{cXXXXXX}{HTML}{XXXXXX}`.
- Second Y axis is emitted as a second, overlaid pgfplots `axis` on the right.
- Fits are written as analytic `\addplot` expressions via `translateCustomExpr()`: parameter names are replaced by their fitted values (longest names first, whole words only), `log`→`ln`, `arcsin/arccos/arctan`→`asin/acos/atan`, `log2`→`ln(x)/ln(2)`.
- With **CSV** data source, `buildCsvExportText()` writes one CSV per source dataset, and curves reference columns by name or, for header-less files and row-index X, by position.
- Dialog size and per-plot-type settings are stored through `QSettings`.

### Misc

- `NoWheelComboFilter` is installed on the `QApplication`, so it applies to every dialog without per-widget wiring.
- Dialogs for the LaTeX export are created on the stack in a loop in `MainWindow::exportLatex()`. **Reset settings** closes the dialog and the loop opens a fresh one that starts from the defaults.

---

## Persisted settings

Stored with `QSettings` under organisation **UniversalDataPlotter**, application **Universal Data Plotter** (registry on Windows, `~/.config/UniversalDataPlotter/` on Linux, plist on macOS).

| Key | Meaning |
|---|---|
| `dirs/lastOpenDir` | Last folder used when opening data files |
| `dirs/lastSaveDir` | Last folder used for saving `.tex`, PDF and SVG |
| `latexExport/dialogSize` | Size of the LaTeX export dialog |
| LaTeX export groups (one per plot type) | The dialog's option values; **Reset settings** removes the current type's keys only |

To wipe everything, delete those entries or the configuration file.

---

## Third-party code and licensing

- **QCustomPlot 2.1.1** (2022-11-06), by Emanuel Eichhammer, is bundled unmodified in `third_party/qcustomplot/` and is licensed under the **GNU GPL v3 or later**. Distributing a binary that links it means the combined program must also comply with the GPL (offering source, etc.). A commercial QCustomPlot licence is available from its author if that is a problem.
- **Qt 6** is available under LGPLv3 / GPLv3 / commercial terms. Check your obligations, especially for static linking.

---

## Known issues and pitfalls

- **CMake build has no Windows icon.** Only the `.pro` file sets `RC_ICONS`. To add it in CMake, create a `.rc` file containing `IDI_ICON1 ICON "resources/Plot.ico"` and append it to the source list under `if (WIN32)`.
- **Icon file name case.** `.pro` says `plot.ico`, the file is `Plot.ico` (see [Platform notes](#platform-notes)).
- **Redundant entries in CMake.** `DataEntryDialog.h` and `NoWheelComboFilter.h` are listed as sources. Harmless; AUTOMOC uses them.
- **Qt 5 is not supported** by the CMake file (`find_package(Qt6 ...)`, `qt_standard_project_setup()`). The `.pro` file itself doesn't pin a Qt version, but the project is set up and documented for Qt 6; a Qt 5 build is untested and may need small changes.
- **Large sources.** `MainWindow.cpp` and `LatexExportDialog.cpp` are each over 2,000 lines. Compile time for `qcustomplot.cpp` dominates a clean build; unity builds or precompiled headers would help.
- **Per-user file in the archive.** `.qtcreator/UniversalPlotter.pro.user` should usually be git-ignored.
- **No automated tests.** `buildLatexDocument()`, `performFit()`, `ExprEval` and `loadDataset()` have no UI dependencies and are the natural first targets.

---

## Extending the app

**Add a new fit type**
1. Add a value to `FitKind` and a label to `fitTypeNames()` in `CurveFit.h`.
2. Implement the branch in `performFit()` (`CurveFit.cpp`), filling `FitResult` (`func`, `formula`, `params`, `xsUsed`, `ysUsed`, `plotXmin/Xmax`).
3. Show or hide any extra option widgets in `MainWindow::onFitTypeChanged()`.
4. Teach the LaTeX export how to write it as a pgfmath expression (look at how the existing fit kinds are handled in `LatexExportDialog.cpp` and `LatexExport.cpp`).

**Add a function to custom fits**
Add it to the function table in `ExprEval.cpp`, then add the pgfmath equivalent to `translateCustomExpr()` if the names differ.

**Add a data format**
Add a loader returning a `Dataset` (see `Dataset.h`), and call it from `MainWindow::loadFiles()`. Update the file-dialog filter in `MainWindow::openFiles()`.

**Add a LaTeX option**
1. Add a field to `LatexExportOptions` (`LatexExport.h`).
2. Add the widget in the right `build…Section()` of `LatexExportDialog.cpp`, read it in `collectOpts()`, and save / load it with the other settings.
3. Emit it in `buildLatexDocument()`.
