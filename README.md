# Universal Data Plotter

A desktop application for quickly plotting data from CSV / TXT files (or from numbers you type or paste in), analysing it, and exporting publication-ready figures, including ready-to-compile **LaTeX / pgfplots** code.

It is built for lab and measurement data: load a file, pick columns, and plot. Then fit curves, integrate areas, read off interpolated values, and export to LaTeX, PDF or SVG.

> Looking for build instructions or code internals? See [UniversalPlotterC++_source\README_BUILD.md](README_BUILD.md).

---

## Contents

1. [Quick start](#quick-start)
2. [The main window](#the-main-window)
3. [Loading data](#loading-data)
4. [Choosing columns](#choosing-columns)
5. [Plot types](#plot-types)
6. [Plot options](#plot-options)
7. [Navigating the plot](#navigating-the-plot)
8. [Axis limits](#axis-limits)
9. [Curve fitting](#curve-fitting)
10. [Integral (area under a curve)](#integral-area-under-a-curve)
11. [Interpolation cursor](#interpolation-cursor)
12. [Exporting](#exporting)
13. [LaTeX / pgfplots export in detail](#latex--pgfplots-export-in-detail)
14. [Custom function syntax](#custom-function-syntax)
15. [Settings that are remembered](#settings-that-are-remembered)
16. [Tips and troubleshooting](#tips-and-troubleshooting)

---

## Quick start

1. Click **Open CSV / TXT...** and choose one or more data files (or click **Enter data manually...** and paste from Excel).
2. In the **Columns** box pick the X column and select one or more Y columns.
3. Click **Plot**.
4. Optionally turn on **Curve fit**, **Integral** or **Interpolation cursor** and click on the plot to place them.
5. Click **Export LaTeX (pgfplots)...** to get a vector figure you can drop into a paper, saved as `.tex`, PDF or SVG.

---

## The main window

| Area | What it holds |
|---|---|
| **Top bar** | Plot type selector (Line / scatter, Bar, Histogram), **Open CSV / TXT...**, **Enter data manually...**, and a label showing which file(s) are loaded. |
| **Left panel** (scrollable) | Parsing options, Columns, Plot options, Curve fit, Integral, Interpolation cursor, Axis limits, Export. |
| **Right side** | The plot, a navigation toolbar above it (Home / Back / Forward / Pan / Zoom), and a bottom bar with **Plot**, **Clear plot** and **Export LaTeX (pgfplots)...**. |

The left panel and the plot are separated by a splitter you can drag. The window starts at 1150 x 700 pixels.

Mouse wheels never change a drop-down or spin box by accident. Scrolling over them just scrolls the panel.

---

## Loading data

### Supported files

CSV, TXT, TSV and DAT files, with one or more files loaded at once. Each file is a *dataset* and gets its own colour in the plot. The default colour cycle is the familiar blue / orange / green / red / purple / brown / pink / grey / olive / cyan palette.

### Automatic detection

The loader works out the file layout for you:

- **Separator**: comma, semicolon, tab, pipe (`|`) or runs of whitespace.
- **Decimal mark**: `.` or `,` (so European-style `1,5` and `1.234,56` are understood).
- **Header row**: detected automatically. If a file has no header, columns are named `Col1`, `Col2`, ...
- **Preamble lines**: instrument headers, comments or other text above the data are skipped automatically.
- **Encoding**: UTF-8 (with or without BOM), falling back to Latin-1 when the file isn't valid UTF-8.
- **Bad lines**: rows with the wrong number of fields are skipped.
- **Numeric vs. text columns**: a column counts as numeric when at least 80% of its cells parse as numbers. Missing or non-numeric cells become gaps. Text columns are kept so they can be used as **category labels** in bar charts.

After loading, a short summary under **Parsing options** shows what was detected (separator, decimal mark, header, encoding, rows, columns, skipped lines).

### Overriding the detection

If auto-detection guesses wrong, choose a **Separator** (Auto, Comma, Semicolon, Tab, Pipe, Whitespace) and/or a **Decimal mark** (Auto, Dot, Comma) in **Parsing options**, then press **Re-parse file(s)**.

### Enter data manually

**Enter data manually...** opens a spreadsheet-like window:

- Type values directly, or click a cell and press **Ctrl+V** to paste from Excel, LibreOffice or any tab-separated source.
- **Ctrl+C**, **Ctrl+X** and **Delete** work on the selected cells.
- Decimal commas are understood.
- **First row contains column names** controls whether row 1 is a header.
- Buttons: **+ 10 rows**, **+ Column**, **Delete selected rows**, **Delete selected columns**, **Clear all**.
- **Plot this data** sends the table to the plotter as a dataset. If you've already loaded manual data, it is updated.
- **Save as CSV...** optionally writes the table to a file, with a choice of separator.

---

## Choosing columns

In the **Columns** box:

- **X axis**: one drop-down per loaded file. Pick a column, or pick **(row index)** to use 1, 2, 3, ... as X.
- **Y axis**: a multi-select list of every column of every file. Select as many as you like with Ctrl/Shift-click. Curves are coloured by their source file.

Click **Plot** to draw the selection. **Clear plot** removes everything.

---

## Plot types

Choose the plot type from the top bar.

### Line / scatter

Curves of Y against X, with three drawing styles: **Line**, **Line + markers** and **Markers only**. This is the only type that supports curve fitting, integrals and the interpolation cursor; those panels are hidden for bar charts and histograms.

### Bar

Grouped bars, one group per X value or category, one bar per selected Y column.

- **Bar width**: the width of a bar group as a fraction of the spacing between X values (0.05 to 1.0).
- **Sideways bars**: horizontal bars, with categories running up the vertical axis.
- **Treat X numbers as text**: puts the bars at equal distances, one per distinct X value, labelled with the number. Without it, bars sit at their true numeric position, so uneven X values leave uneven gaps.
- If the X column is text, its entries become the category labels.

### Histogram

The distribution of the selected column(s), with the series overlaid.

- **Bins**: 2 to 500.
- **Show**: **Count** (values per bin), **Probability** (bars sum to 1) or **Density** (bars integrate to 1).
- **Bin ticks at**: Auto, on every bin edge, or in the middle of every bin.
- **Sideways histogram**: swaps the axes. This setting stays in step with the bar chart's sideways option.

---

## Plot options

| Option | What it does |
|---|---|
| **Style** | Line / Line + markers / Markers only (line plots). |
| **Log X / Log Y** | Logarithmic axes. |
| **Grid** | Show a grid. |
| **Snap draggable lines to nearest data point** | While dragging the fit, integral or cursor lines, they jump to the nearest data X value. |
| **Error bars (± standard deviation)** | Pick, for each plotted curve, which column holds its standard deviation. Symmetric Y error bars are drawn. |
| **Second Y axis** | Put chosen columns on a second, right-hand Y axis, with its own log option and limits. Useful for comparing quantities with different units or magnitudes. Curves on the second axis are tagged `[Y2]`. With sideways bars the second value axis moves to the top. |

---

## Navigating the plot

A navigation toolbar sits above the plot:

| Button | Action |
|---|---|
| **⌂ Home** | Back to the view the plot started at. |
| **◀ Back / Forward ▶** | Step through your view history. Bursts of zooming or panning are grouped into a single history step. |
| **✋ Pan** | Drag to pan. The scroll wheel zooms. |
| **🔍 Zoom** | Drag a rectangle to zoom into it. |

The axis-limit boxes in the left panel update automatically as you navigate.

---

## Axis limits

Type exact **X min / X max / Y min / Y max** values (and **Y2 min / Y2 max** when the second axis is on) and click **Apply limits**. **Autoscale** returns to automatic ranges.

---

## Curve fitting

Enable **Curve fit** and click on the plot to place a fit range. The range is drawn as two draggable vertical lines.

### Fit types

| Type | Model | How it is fitted |
|---|---|---|
| **Linear** | `m·x + b` | Least squares |
| **Polynomial** | Degree 1 to 12 | Least squares |
| **Exponential** | `a·e^(bx) + c` | Levenberg-Marquardt non-linear fit (starting values are chosen automatically) |
| **Logarithmic** | `a·ln(x) + b` | Least squares on ln(x) (needs x > 0) |
| **Power** | `a·x^b` | Levenberg-Marquardt non-linear fit (only points with x > 0 are used) |
| **Custom** | Your own f(x) | Levenberg-Marquardt |

Every result shows the fitted formula, the parameters, **R²** and the number of points used.

### Custom fits

Choose **Custom** and fill in:

- **f(x) =** your expression, e.g. `a*exp(b*x)+c`
- **Params:** the parameter names, comma-separated, e.g. `a,b,c`
- **Initial guess:** one starting value per parameter, e.g. `1,1,1`

Good starting values matter for non-linear fits. If the fit says it did not converge, adjust the guesses. See [Custom function syntax](#custom-function-syntax) for what you can write.

### Placing and adjusting the range

- Click the plot once to start; the range lines appear and the fit is drawn.
- **Drag** either line to change the range. The fit updates live.
- Or type exact bounds in **Range: xmin / xmax** and press **Apply**.
- **Snap** (in Plot options) makes the lines jump to data points.

### Several intervals

Use **+ New interval** to fit another region of the same data, and the **◀ ▶** buttons to move between intervals. Each interval remembers its own range and which columns it covers, so you can, for example, fit the linear part and the saturation part of a curve separately and see both fits at once.

### Several columns

**Columns to fit** lists the plotted curves. Select any number of them and each gets its own fit over the same range, with results listed together.

---

## Integral (area under a curve)

Enable **Integral** and click the plot to set a range, exactly like the curve fit (draggable lines, typed bounds, snapping, multiple intervals with **+ New interval** and **◀ ▶**).

- The area is computed with the **trapezoidal rule** between your two limits. The ends are interpolated linearly, so the limits don't have to sit on data points.
- The region under the curve is shaded in the plot.
- **Integrate (data and/or fit)** lists the raw data of every plotted curve and any fitted curve, so you can integrate the measurement, the fit, or both, and compare.
- Results show the numeric area for each selection.

---

## Interpolation cursor

Enable **Interpolation cursor** and click the plot to drop a vertical line. For every plotted curve the app shows the interpolated Y value at that X, marks it on the plot with a dot, and flags `(extrapolated)` when the cursor lies outside the data range.

Drag the line, or type an exact value in **x =** and press **Apply**.

---

## Exporting

Figures are exported through **Export LaTeX (pgfplots)...** (button at the bottom right), described next. From there you can save the result as a **`.tex`** file, a compiled **PDF**, or a vector **SVG**. The save folder is remembered.

---

## LaTeX / pgfplots export in detail

**Export LaTeX (pgfplots)...** turns what you see into a complete, standalone LaTeX document using `pgfplots`. It works for line plots, bar charts and histograms. Click **Generate LaTeX Code** to produce the code, which opens in a window with these buttons:

| Button | Does |
|---|---|
| **Copy to clipboard** | Copies the code. |
| **Save .tex** | Saves the `.tex` file (and the CSV file(s), if you chose CSV data). |
| **Compile to PDF...** | Runs LaTeX on your machine and lets you save the PDF. |
| **Save as SVG...** | Produces a vector SVG straight from LaTeX. |

If compilation fails, a log window shows the LaTeX output with a **Copy log** button.

*Compiling needs a LaTeX installation on your computer (see [README_BUILD.md](README_BUILD.md#latex-tools-for-compile-and-svg)).*

### Output format

- Without the figure wrapper, the document is a `standalone` class, so the PDF is cropped tightly around the plot, ideal for including as a graphic. The code can also be pasted as a `tikzpicture` into an existing document.
- With the figure wrapper, you get an `article` with a `figure` environment.
- Custom hex colours are defined automatically with `\definecolor`.

### Settings, section by section

**Data source**
- *Embed data points directly in the code* (self-contained), or
- *Load from CSV file(s)* using `\pgfplotstable` (you choose a file name and separator per dataset; the CSV files are saved alongside the `.tex`). Histograms and bars with "numbers as text" are always embedded.

**Labels & axis limits**
- Title, X label, Y label (and Y2 label with a second axis), label rotation.
- X / Y / Y2 limits, log axes.
- **Escape special characters**: when ticked, `$ _ & % # { } ~ ^ \` are escaped. Leave it off to type LaTeX maths such as `$\alpha_1$` directly.

**Ticks & number format**
- Tick step per axis; decimals per axis (or "Default" to let pgfplots choose); zero-fill (`1.20` rather than `1.2`).
- Minor ticks, either the same on all axes or set per axis, with the number of minor ticks per interval.
- Tick direction (inside / outside), tick position, tick-label rotation and anchor per axis, hide all ticks.

**Style**
- Grid (none / major / both), smoothing (none / smooth / sharp plot), plot width and height in cm, and a full four-sided axis box.

**Legend**
- None, a preset corner position, or an exact position in axis coordinates with a chosen anchor.

**Curves**
- One row per curve: legend label, colour, marker, line style and line width.
- Colours come from the 19 built-in LaTeX colour names, a colour picker, or a hex code.
- 26 marker types are offered (circle, square, triangle, diamond, star, and more).
- **Reorder** curves by dragging a row or using the up/down arrows. The order is the plot and legend order.

**Bar chart / histogram** (only for those plot types)
- Bar width in pt, fill opacity, fill type (solid, none, or patterns such as north-east lines, grid, crosshatch, dots, bricks, stars), outline colour, style and width, sideways bars, and where histogram ticks sit.

**Error bars**
- Direction (both / plus / minus), colour (or same as curve), line width and style, and cap shape and size.

**Curve fit**
- Include the fits currently on screen as analytic `\addplot` curves, with their own colour, label, style, width and sample count.
- Fits are translated into pgfplots maths. For custom fits the code carries a note that the translation should be checked.

**Integral / area under curve**
- Include the shaded integral regions, with colour, opacity, and an optional label showing the numeric area.

**Extra function series**
- Add any theoretical curve, such as `2*x+1` or `sin(x)`, over a chosen domain with its own colour, label and sample count. Handy for plotting theory against data.

**Figure wrapper**
- Wrap the plot in a `figure` environment with caption, label and placement (`htbp`, `H`, etc.).

### Remembered settings and reset

The dialog remembers your settings separately for line plots, bar charts and histograms. **Reset settings** restores the defaults for the current plot type only, leaving the others untouched.

---

## Custom function syntax

Used by custom curve fits and by the extra function series in the LaTeX export.

| Item | Supported |
|---|---|
| Operators | `+  -  *  /  ^` (power), unary minus, parentheses |
| Variable | `x` |
| Parameters | Any names you list (they can't clash with `x`, `pi`, `e` or function names) |
| Constants | `pi`, `e` |
| Functions | `sin cos tan asin acos atan sinh cosh tanh exp log log10 log2 sqrt abs` |

Note that `log` is the **natural** logarithm. Expressions are evaluated by a small built-in parser, so nothing else can run through the formula box.

Examples: `a*exp(-b*x)+c`, `a*sin(b*x+c)`, `a/(1+exp(-b*(x-c)))`, `a*x^2+b`

When exported to LaTeX, the expression is translated to pgfmath: `log` becomes `ln`, `log2` becomes `ln(x)/ln(2)`, and the fitted parameter values are substituted in place of their names.

---

## Settings that are remembered

Between sessions the app remembers:

- The last folder you opened data from, and the last folder you saved to (`.tex`, PDF and SVG share one).
- The LaTeX export dialog's window size and its settings, per plot type.

---

## Tips and troubleshooting

- **File loads as one column or with odd values**: set the separator and decimal mark by hand in **Parsing options** and press **Re-parse file(s)**.
- **Fit doesn't converge**: for exponential and custom fits, try better **Initial guess** values or a narrower range.
- **Logarithmic or power fit fails**: both need at least two points with X > 0 inside the range.
- **Curve fit / Integral / Cursor panels are missing**: they only appear for the **Line / scatter** plot type.
- **"Nothing to export"**: plot something first.
- **Compile / SVG buttons report a missing tool**: install a LaTeX distribution (TeX Live or MiKTeX). SVG export also needs `dvisvgm`.
- **Special characters in LaTeX labels look wrong**: toggle **Escape special characters** depending on whether you typed plain text or LaTeX maths.

---

## Credits and licence

Plotting is powered by [QCustomPlot](https://www.qcustomplot.com/) 2.1.1 (GPL v3 or later) and the Qt framework. See [README_BUILD.md](README_BUILD.md) for licensing notes.
