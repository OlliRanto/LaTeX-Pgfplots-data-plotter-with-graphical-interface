# Universal Data Plotter: building and technical notes

For a description of the features see [README.md](README.md).

## 1. Requirements

| Item | Version |
|---|---|
| C++ compiler | C++17 (GCC, Clang, MSVC, MinGW) |
| Qt | **6.x** with the *Widgets* and *PrintSupport* modules (compiled and started with 6.4.2) |
| Build system | CMake >= 3.16 **or** qmake (both project files are kept in sync) |
| QCustomPlot | 2.1.1, **already included** in `third_party/qcustomplot/` |

At run time a LaTeX installation is only needed for the *Compile to PDF* and
*Save as SVG* buttons of the export viewer:

- `lualatex`, `latexmk`, `pdflatex` or `xelatex` (searched in that order), with
  the `pgfplots` package (version 1.18 or newer, the code writes `compat=1.18`),
  `pgfplotstable` for the CSV data source, and the `standalone` class
  (the `geometry`/`float` packages for the figure wrapper).
- `latex` + `dvisvgm` for SVG (fallback: `pdf2svg` or `inkscape`).

## 2. Building

### Debian / Ubuntu
```bash
sudo apt install qt6-base-dev cmake build-essential libgl1-mesa-dev
```

### With CMake
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./UniversalPlotter
```
Use `cmake --build . --parallel` on any platform. If CMake cannot find Qt, point
it to the installation: `cmake .. -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64`.

### With qmake
```bash
mkdir build_qmake && cd build_qmake
qmake6 ../UniversalPlotter.pro        # or: qmake ../UniversalPlotter.pro
make -j$(nproc)
./UniversalPlotter
```

### Qt Creator
Open `CMakeLists.txt` (or `UniversalPlotter.pro`), pick a Qt 6 kit and press
Build/Run. The folder `.qtcreator/` holds a user file with local kit settings;
it can be deleted safely.

### Windows (MinGW)
Both project files add `-Wa,-mbig-obj` for `qcustomplot.cpp`. Without it a
Debug build fails with "too many sections / file too big". The `.pro` file
also sets `RC_ICONS = resources/plot.ico` for the Windows executable icon
(the file in the repository is called `Plot.ico`; this is fine on Windows,
but rename one of them if you build the resource on a case-sensitive system).

### Adding a new source file
Add it to **both** `CMakeLists.txt` (`add_executable`) and
`UniversalPlotter.pro` (`SOURCES` / `HEADERS`). Header-only files such as
`LatexColors.h` and `ColorSchemes.h` only need to appear in the `.pro` file.
Qt's AUTOMOC/AUTORCC/AUTOUIC are switched on in CMake; there are no `.ui` or
`.qrc` files.

### Verifying the build
A clean build should end with `Built target UniversalPlotter`. The compiler
warnings about `__builtin_memset` come from Qt's own headers with some GCC
versions. A headless smoke test:
```bash
QT_QPA_PLATFORM=offscreen timeout 3 ./UniversalPlotter   # exit 124 = it kept running
```

## 3. Source layout

```
CMakeLists.txt, UniversalPlotter.pro   build files (keep both in sync)
resources/Plot.ico                     Windows icon
third_party/qcustomplot/               QCustomPlot 2.1.1 (qcustomplot.cpp/.h, config.h)
src/
  main.cpp                 QApplication, organisation/app name (for QSettings), MainWindow
  Dataset.h                Dataset, ColumnData, CurveKey, LoadInfo, file colours
  DataLoader.*             file parsing: separator / decimal / header / preamble detection
  MainWindow.*             the main window: all UI, plotting, fits, integrals, cursor, functions
  CurveFit.*               least squares (linear / polynomial) + Levenberg-Marquardt, R^2
  ExprEval.*               small recursive-descent expression evaluator (functions, custom fits)
  DraggableVLine.*         draggable vertical lines on a QCustomPlot (fit/integral/cursor)
  DataEntryDialog.*        "Enter data manually" window with paste support
  ColorPickerWidget.*      colour name / hex / swatch picker used by the export dialog
  ColorSchemes.h           the named colour schemes (tokens + optional on-screen hex)
  LatexColors.h            19 xcolor base colours, line styles, fill types, LaTeX escaping
  LatexExport.*            LatexExportOptions + generation of the pgfplots document and CSVs
  LatexExportDialog.*      the export settings dialog, code viewer, compile / SVG tools
  NoWheelComboFilter.h     event filter: combo boxes ignore the wheel unless focused
```

## 4. Architecture

### Data model (`Dataset.h`)
- A `Dataset` is one loaded file, the manual table, **or a plotted function**.
  It holds `QVector<ColumnData>` (numeric values with NaN for missing; text
  columns keep their raw `labels`), the chosen `xColumn` (a name or
  `indexLabel()`), a colour and `LoadInfo`.
- `Dataset::path` identifies the origin: the file path, `manualDataPath()`
  (`"(manual entry)"`) or `functionDataPath()` (`"(function)"`).
- A plotted curve is a `CurveKey {dsIdx, col, y2}`: dataset index, column
  name, and whether it uses the second Y axis. Everything downstream (fits,
  integrals, export rows, error-bar choices in `m_errColForCurve`) is keyed by it.
  **Dataset indices shift when datasets are added or removed**, so code that
  stores keys must clear or remap them (see `rebuildColumnsKeepSelection`).
- `MainWindow::m_datasets` order: loaded files, then the manual table, then
  functions (functions are always last).

### Plotting (`MainWindow`)
- `plot()` reads the selection, builds `allSel = ySel + y2Sel`, sets the state
  that must stay consistent between screen and export (`m_plottedType`,
  `m_flipBars`, `m_numAsTextBars`, the colour scheme) and then draws either
  curves/graphs (line) or calls `buildBarSeries()` (bar and histogram).
- Bars and histograms use `QCPBars`; `keyAxis` is `xAxis`, or `yAxis` when
  sideways. The second value axis becomes the top axis when sideways.
- X values are read through `curveXArray()`. For bar charts with
  "numbers as text" it maps every distinct X to an integer slot
  (`m_textPosMap`) and `categoryLabels()` returns the number labels; the export
  uses the same two functions, so screen and LaTeX agree.
  `curveXArrayRaw()` is the unmapped version.
- Large curves are drawn with fast polylines and thinned markers; the
  navigation history is debounced (250 ms) from the axes' `rangeChanged` signals.

### Plotted functions
`makeFunctionDataset()` (in `MainWindow.cpp`) samples `f(x)` with `ExprEval` on
`n` evenly spaced points and returns a `Dataset` with columns `x` and `f(x)`,
`isFunction = true` and the expression, domain and sample count
(`funcExpr`, `funcMin`, `funcMax`, `funcSamples`) so the UI can show and edit
them again. Non-finite results become NaN (a gap). Consequences:

- The Y list shows only the `f(x)` column of a function dataset
  (as `f(x) = <expr>`); the legend uses the expression (`curveLabelPlain`).
- `loadFiles()` carries the function datasets over to the new set;
  `reparseFiles()` skips them; `addManualDataset()` inserts the manual table
  before the first function.
- In the export a function is **analytic**: `collectOpts()` sets
  `LatexCurveOpts::isFunction` plus `funcExpr/funcMin/funcMax/funcSamples` and
  adds no points; the dataset is left out of the CSV section. `emitCurves()`
  writes `\addplot[<style>, domain=a:b, samples=n, trig format plots=rad]
  {expr};`. `functionToPgf()` translates the expression (`log` -> `ln`,
  `log2(a)` -> `(ln(a)/ln(2))`; the other supported names exist in pgfmath).
  When the axis has limits, `restrict y to domain=lo:hi, unbounded coords=jump`
  (limits +/- 10 spans) is added so huge values (poles) neither overflow TeX nor
  draw vertical connecting lines.
- Add keeps the current plot; Update/Remove call `clearPlot()`.

### Fits, integrals, cursor
- `CurveFit.*`: closed-form least squares for linear/polynomial; the other
  types (and Custom) use Levenberg-Marquardt with a numeric Jacobian.
  `FitResult` carries the callable, a formula string and R^2.
- `DraggableVLineGroup` (`DraggableVLine.*`) provides the draggable lines and
  optional snapping to data points (`snapValuesForCurrentPlot()`).
- Integrals use the trapezoidal rule with boundary interpolation on data or on
  sampled fit curves.
- These tools are line-plot only; `onPlotTypeChanged()` hides their boxes and
  unchecks their enable boxes for bar/histogram.

### Expression evaluator (`ExprEval`)
Whitelisted grammar: `+ - * / ^`, unary +/-, parentheses, numbers, variables
from the supplied map, constants `pi` and `e`, and
`sin cos tan asin acos atan sinh cosh tanh exp log log10 log2 sqrt abs`.
It re-parses on every `evaluate()` call (fine for the sample counts used).
Syntax errors / unknown names throw `std::runtime_error`; domain errors give
NaN/inf.

## 5. LaTeX export

Flow: `LatexExportDialog` (widgets) -> `collectOpts()` -> `LatexExportOptions`
(`LatexExport.h`, plain data) -> `generateLatex()` / `buildCsvExportText()`
(`LatexExport.cpp`, no Qt widgets) -> code viewer.

- **Options struct**: global settings plus a `QVector<LatexCurveOpts>` (one per
  curve: points, colour, marker/line style or, for bars/histograms,
  `fillType`, `outlineColor`, `outlineStyle`, `fillOpacity`, `linewidthPt`),
  fit curves, integral regions and error bars.
- **Per-axis settings**: decimals, minor tick on/off + count
  (`xMinorTicks`, `yMinorTicks`, `y2MinorTicks` and the `*Num`s), tick label
  rotation/anchor (`x/y/y2TickLabelRotate`, `...Anchor`).
- **Axis colors**: `LatexAxisColors` holds one `LatexAxisColorSet`
  (`line`, `tick`, `tickLabel`, `label`, `grid`) each for `x`, `y` and `y2`, plus a
  single `title` and `hasY2`. They are written as `x/y axis line style`,
  `every x/y tick`, `every x/y tick label`, `every axis x/y label` and
  `every x/y grid` (`axisColorLines()` in `LatexExport.cpp`). The second axis
  writes only its value axis (`y`, or `x` when flipped), from `y2` if `hasY2`,
  otherwise from `y`, and no grid. In the dialog the pickers are
  `m_axPicker[axis][what]` (axis 0..2 = X/Y/Y2; what 0..4 = line, tick,
  tick label, label, grid). Saved as `axis<X|Y|Y2><Line|Tick|TickLabel|Label|Grid>`;
  the old shared keys (`axisLine`, ...) are still read into X and Y.
- **Bar value labels**: `LatexBarLabelOpts` (`opts.barLabels`, bar charts only)
  becomes `nodes near coords` + `nodes near coords style={anchor, text, fill,
  fill opacity, draw, font, rotate, inner sep, xshift, yshift, number format}`
  (`barLabelLines()`), emitted on the primary axis and on the second-axis block.
  Its colors go through the normal `\definecolor` collection. Saved as `barLbl*`
  keys in the bar group.
- **Orientation**: `flip = (bar || hist) && opts.flipBars`. When flipped,
  "x" data are the categories and the value axis is horizontal. The second axis
  becomes a separate overlaid `axis` block (top axis when flipped). Error-bar
  caps use `rotate=90` in both orientations (the default cap mark is
  horizontal-looking in pgfplots; this was verified by rendering).
- **Histogram** is exported as `const plot` + `\closedcycle`; sideways, as an
  explicit closed polygon (`-- cycle`) because `const plot` only steps along x.
- **Colours**: tokens are base xcolor names or 6-digit hex; hex colours are
  defined once with `\definecolor` (all colour sources are collected first).
- **Patterns**: `\usetikzlibrary{patterns}` is added only when a pattern fill
  is used; patterns take the series colour and ignore fill opacity.
- **CSV mode**: `buildCsvExportText()` writes one CSV per used dataset
  (X column or row index + selected Y/error columns) and the document uses
  `\pgfplotstable`. Histograms and "numbers as text" bars force embedding
  because their positions are not columns of the source data.
- **Compile**: `QProcess` runs the first available engine in a temp directory.
  `latexmk` is forced onto `-lualatex`. SVG uses `latex` ->
  `dvisvgm --no-fonts --exact-bbox` (with `\pgfsysdriver` set to the dvisvgm
  driver), with PDF -> SVG fallbacks.

### Saved settings (`QSettings`)
Organisation `UniversalDataPlotter`, application `Universal Data Plotter`
(native storage: registry on Windows, `~/.config` on Linux, plist on macOS).

| Key | Content |
|---|---|
| `dirs/lastOpenDir`, `dirs/lastSaveDir` | last used folders |
| `plot/colorScheme` | selected colour scheme |
| `latexExport/...` | export dialog settings of the **line** plot |
| `latexExport/bar/...`, `latexExport/hist/...` | settings of the bar / histogram dialogs |
| `latexExport/dialogSize` | dialog window size |

**Reset settings** removes only the direct keys of the current plot type's
group (`settingsGroup()`), sets `m_resetRequested`, closes the dialog without
saving, and `MainWindow::exportLatex()` reopens a fresh dialog in a loop.
(The line plot's group is the parent of the other two, so removing the whole
group would delete theirs; hence only `childKeys()` are removed.)

Per-curve rows of the export dialog (labels, per-curve colours, fill type, ...)
are **not** persisted; they are rebuilt from the plot every time.

## 6. Conventions and pitfalls

- Plot state that the export needs (`m_plottedType`, `m_flipBars`,
  `m_numAsTextBars`, the colour scheme) is captured **at plot time**, not read
  from the widgets later, so the export always matches what is on screen.
- All persistent state lives in `QSettings` (see above); there are no config files.
- Changing a dataset list means: rebuild X rows, Y/Y2 lists and error rows
  (`populateColumnWidgets()` resets error-bar choices; the function code uses
  `rebuildColumnsKeepSelection()` to keep selections), then `clearPlot()` if the
  indices moved.
- Keep `.pro` and `CMakeLists.txt` in sync when adding files.
- Custom-fit expressions are translated to pgfmath for the export with a simple
  name mapping (`log` -> `ln`, `arcsin` -> `asin`, ...); complicated formulas
  should be checked by compiling.

## 7. Status of testing

The project compiles and links cleanly with Qt 6.4.2 / GCC on Linux and starts
headless. Features are tested interactively by the author; the generated
pgfplots code for the sideways error-bar caps, the pattern fills and the analytic
function curves was checked by compiling small test documents with TeX Live
(for the functions, a small program that calls `buildLatexDocument()` directly).

## 8. Possible extensions

- Log-spaced sampling for functions on log axes.
- Persist the per-curve rows and the function list between sessions.
- Replot automatically after updating/removing a function.
