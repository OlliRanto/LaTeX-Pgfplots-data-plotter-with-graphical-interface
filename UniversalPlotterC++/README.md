# Universal Data Plotter (C++ / Qt6 / QCustomPlot)

A C++/Qt6 port of the Python "universal plotter" tool: load one or more
CSV/TXT/TSV/DAT files with auto-detected separator, decimal mark, and
header; pick X/Y columns; plot; fit curves; integrate; interpolate; and
export to LaTeX (pgfplots).

## Building

Requires Qt 6 (Widgets + PrintSupport) and either CMake >= 3.16 or qmake
(both project files are included). QCustomPlot 2.1.1 is vendored in
`third_party/qcustomplot/` (no separate install needed).

```bash
sudo apt install qt6-base-dev cmake build-essential   # Debian/Ubuntu
```

**With CMake:**
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./UniversalPlotter
```

**With qmake:**
```bash
mkdir build_qmake && cd build_qmake
qmake6 ../UniversalPlotter.pro      # or: qmake ../UniversalPlotter.pro
make -j$(nproc)
./UniversalPlotter
```

Or open `UniversalPlotter.pro` directly in Qt Creator and build/run from there.

## Features

- **File loading**: multi-file open, auto-detected separator (`,` `;` tab
  `|` or whitespace), auto-detected decimal mark (`.` or `,`), auto-detected
  header row and preamble lines to skip, with manual overrides + a
  re-parse button. Bad/short rows are skipped rather than aborting the load.
- **Column picking**: one X column (or row index) per loaded file; multi-select
  Y columns, color-coded by source file.
- **Plotting**: line / line+markers / markers-only styles, log X/Y, grid,
  legend, draggable pan + scroll-wheel zoom, axis-limit entry boxes +
  autoscale.
- **Curve fitting**: linear, polynomial (degree 1-12), exponential
  (`a*e^(bx)+c`), logarithmic (`a*ln(x)+b`), power (`a*x^b`), and a custom
  fit where you type your own `f(x)` expression and parameter names (fit via
  Levenberg-Marquardt with a numeric Jacobian). Drag two vertical lines on
  the plot (or type exact bounds) to set the fit range; place multiple
  fit intervals and step through them; per-curve R² is reported.
- **Integral**: drag two vertical lines to select a range and compute the
  area under any plotted curve *or* any fitted curve in that range
  (trapezoidal rule, boundary-interpolated), with a shaded fill drawn on
  the plot; multiple integral intervals, steppable.
- **Interpolation cursor**: drag one vertical line to read off the
  interpolated Y value of every plotted curve at that X.
- **Snapping**: optional "snap to nearest data point" for all draggable
  lines (fit range / integral range / interpolation cursor).
- **Export**: PNG/JPEG/PDF snapshot of the plot, and a full-featured
  "Export to LaTeX (pgfplots)" dialog matching the Python app section for
  section:
  - **Data source**: embed points directly, or reference external CSV(s)
    (one per source file, with its own filename/column separator) via
    `\pgfplotstable`/`table=` -- the CSV(s) are written alongside the
    `.tex` file when you save.
  - **Labels & axis limits**: title, X/Y labels (+ Y-label rotation),
    X/Y min/max, log X/Y.
  - **Ticks & number format**: tick step, decimal precision, minor ticks,
    tick alignment/position, zero-fill, hide-ticks.
  - **Style**: grid mode, smoothing, figure width/height, boxed axis.
  - **Legend**: none / preset corner / exact axis-coordinate position.
  - **Per-curve styling**: legend label, color (19 base LaTeX colors or a
    custom hex via a color picker), marker, line style, line width.
  - **Curve fit**: include the currently-shown fit(s) as analytic
    `\addplot ... {expression}` (domain-sampled), with per-curve/interval
    color and legend label; custom fits get their parameter names
    substituted with fitted values and common function names translated
    to pgfmath syntax (`log`→`ln`, `arcsin`→`asin`, etc.), with a
    reminder comment to double-check the translation.
  - **Integral**: include the currently-shown shaded region(s), with
    opacity and per-region color, optionally labeled with the numeric
    area on the plot.
  - **Extra function series**: an optional additional analytic curve
    (e.g. a theoretical prediction) in pgfmath syntax, independent of any
    loaded data.
  - **Figure wrapper**: optionally wrap the plot in
    `\begin{figure}...\end{figure}` with a caption, label, and placement
    specifier.

  The generated code opens in a read-only viewer with "Copy to clipboard",
  "Save .tex (+ CSV)", and "Compile to PDF..." actions. Compiling
  auto-detects `lualatex`/`latexmk`/`pdflatex`/`xelatex` on your system
  PATH (same preference order as the Python app, with `latexmk` forced
  onto the `lualatex` engine), runs it in a temp directory alongside any
  needed CSVs, and either prompts you to save the resulting PDF (with an
  option to open it) or shows the full compiler log if it fails.

## Known simplifications vs. the Python original

- The Python app could also embed the *exact* pandas objects for a live
  Python-console debug view; that's obviously not applicable here.
- Custom fit functions support `+ - * / ^`, parentheses, and
  `sin cos tan asin acos atan sinh cosh tanh exp log log10 log2 sqrt abs`
  (`log` is natural log, matching the Python app's `numpy` namespace) --
  no arbitrary Python expressions.

## Project layout

```
CMakeLists.txt
UniversalPlotter.pro   qmake project file (alternative to CMakeLists.txt)
src/
  main.cpp            entry point
  MainWindow.*         all UI + feature wiring
  Dataset.h            shared data model
  DataLoader.*          file parsing / separator+decimal+header detection
  CurveFit.*            linear/poly least squares + Levenberg-Marquardt
  ExprEval.*             tiny expression evaluator for custom fits
  DraggableVLine.*       draggable vertical-line widget for QCustomPlot
  LatexColors.h           LaTeX/pgfplots color palette + text-escaping helpers
  DataEntryDialog.*       "Enter data manually" window: Excel-like paste-able table, CSV save
  ColorPickerWidget.*     reusable color-name/hex/swatch picker widget
  LatexExport.*           pgfplots document + CSV generation
  LatexExportDialog.*     the full "Export to LaTeX" settings dialog
third_party/qcustomplot/ vendored QCustomPlot 2.1.1
```
