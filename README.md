# Universal Data Plotter

A desktop program for quickly plotting data from text files, typed-in tables or
mathematical functions, analysing it (curve fits, integrals, interpolation) and
exporting publication-quality figures as **LaTeX / pgfplots** code.

It is written in C++ with Qt 6 and QCustomPlot. Build instructions and
technical notes are in [UniversalPlotterC++_source\README_TECHNICAL.md](README_TECHNICAL.md).

---

## Contents

1. [Getting data in](#1-getting-data-in)
2. [Plotting functions](#2-plotting-functions)
3. [Choosing columns and plot types](#3-choosing-columns-and-plot-types)
4. [Plot options](#4-plot-options)
5. [Navigating the plot](#5-navigating-the-plot)
6. [Curve fitting](#6-curve-fitting)
7. [Integral (area under a curve)](#7-integral-area-under-a-curve)
8. [Interpolation cursor](#8-interpolation-cursor)
9. [Axis limits](#9-axis-limits)
10. [Saving the plot as an image](#10-saving-the-plot-as-an-image)
11. [Export to LaTeX (pgfplots)](#11-export-to-latex-pgfplots)
12. [Tips and good to know](#12-tips-and-good-to-know)

---

## 1. Getting data in

### Open CSV / TXT...
Opens one or more files at once (`.csv`, `.txt`, `.tsv`, `.dat`, ...). Everything
is detected automatically:

- **Separator**: comma, semicolon, tab, pipe or whitespace.
- **Decimal mark**: `.` or `,`.
- **Header row** and any text lines before the data (preamble) are recognised and skipped.
- Rows that are too short or contain garbage are skipped instead of aborting the load.

Under **Parsing options** you can override the separator and the decimal mark and
press **Re-parse file(s)** if the automatic guess was wrong. A short summary
(rows, columns, separator, decimal mark, skipped lines) is shown for every file.

Columns that contain text instead of numbers are kept as **category labels**
(useful for bar charts).

### Enter data manually...
Opens a spreadsheet-like window.

- Type values, or click a cell and press **Ctrl+V** to paste straight from
  Excel / LibreOffice / any tab-separated source.
- Tick *First row contains column names* if your first row is a header.
- Buttons to add rows and columns and to delete the selected ones, or clear everything.
- **Plot this data** hands the table to the plotter; **Save as CSV...** writes it to a file.

Manually entered data shows up next to any loaded files. Entering new data
replaces the previous manual table.

---

## 2. Plotting functions

For **line / scatter** plots you can plot mathematical functions instead of (or
together with) data. The **Functions** box sits under the **Columns** box.

| Field | Meaning |
|---|---|
| `f(x) =` | The expression in `x`, e.g. `sin(x)/x` or `exp(-x^2)`. No other free parameters: use numbers |
| Domain min / max | Start and end of the x range. A number or a constant expression such as `2*pi` |
| Samples | How many evenly spaced points the function is evaluated at (2 ... 200000) |

Buttons:

- **Add**: adds the function to the list.
- **Update**: applies the fields to the function currently selected in the list.
- **Remove**: deletes the function selected in the list.

**Click a function in the list** to see its expression, domain min, max and
number of samples in the fields above (and edit them with *Update*).

Every function behaves like a loaded file:

- It appears in the X-axis rows and in the Y list (as `f(x) = <expression>`),
  coloured like a file, so you select it and press **Plot**.
- It can be plotted together with data, on the second Y axis, fitted,
  integrated and read with the interpolation cursor.
- In the **LaTeX export** it is *not* written as data points: it becomes an
  analytic `\addplot[domain=..., samples=...] {expression}` with your domain and
  number of samples, so the curve stays exact and the code stays short. It has a
  row in the export's *Curves* table for colour, line style, width and legend
  label like any other curve. Trigonometric functions use radians in the export too
  (`trig format plots=rad`), and values far outside the y-axis limits are cut so
  poles such as those of `tan(x)` show as clean gaps.
- In the legend it is shown by its formula.
- Points where the function is undefined (e.g. `log(x)` for `x <= 0`, `1/0`) are
  left out and show up as gaps.

Supported syntax: numbers, `+ - * / ^`, parentheses, unary minus, the variable
`x`, the constants `pi` and `e`, and the functions
`sin cos tan asin acos atan sinh cosh tanh exp log log10 log2 sqrt abs`
(`log` is the natural logarithm).

When you *Update* or *Remove* a function the plot is cleared (what was drawn is
out of date); press **Plot** again. Adding a function does not touch the plot.
Functions are kept when you open other files.

---

## 3. Choosing columns and plot types

**Plot type** (top bar): *Line / scatter*, *Bar*, *Histogram*.

**Columns box**

- **X axis**: one drop-down per loaded file (and per function). Pick a column or
  the *(row index)*.
- **Y axis**: select one or more columns (Ctrl/Shift-click). Curve colour
  follows the colour scheme (see below).

### Line / scatter
Style: *Line*, *Line + markers* or *Markers only*.

### Bar chart
Bars are grouped per X value, one bar per selected Y column.

- **Bar width**: width of a bar group as a fraction of the spacing between X values.
- **Sideways bars**: horizontal bars (X and Y swapped).
- **Treat X numbers as text (even bar spacing)**: with numeric X values such as
  1, 2, 5, 10 the bars normally sit at their numeric positions with uneven gaps.
  This option puts one bar slot per distinct X value at equal distances and labels
  it with the number. (Press Plot again after changing it.)
- Text columns used as X are shown as category labels automatically.

### Histogram
Bins the selected Y columns; several columns are overlaid. The X column is not used.

- **Bins**: number of bins (2 ... 500).
- **Show**: *Count*, *Probability* (bars sum to 1) or *Density* (area is 1).
- **Bin ticks at**: automatic, on bin edges, or at bin centres.
- **Sideways histogram**: horizontal bins (this one setting is shared with the bar chart).

### Colour scheme
Under **Plot options**, *Color scheme* picks the colours of curves, bars and
histograms (applied when you press Plot). About fifty schemes are included:
the LaTeX/pgfplots defaults, matplotlib tab10, Tableau, Seaborn, ColorBrewer,
colour-blind-safe sets (Okabe-Ito, Paul Tol, IBM), Viridis/Plasma/Inferno/Magma,
journal palettes (Nature, Lancet, JAMA) and more. The choice is remembered
between sessions, and the LaTeX export starts from the same scheme so the
figure matches the screen.

---

## 4. Plot options

- **Log X / Log Y / Log Y2** (line plots).
- **Grid**.
- **Error bars (± standard deviation)**: for each plotted curve, choose which
  column holds its standard deviation (or *(none)*). Works for line and bar plots.
- **Second Y axis**: tick the box and select the columns that should be drawn
  against a separate right-hand axis (with its own limits and log option). Both
  Y lists can be used at the same time.
- **Snap draggable lines to nearest data point** (line plots): see sections 6-8.

---

## 5. Navigating the plot

Toolbar above the plot:

- **Pan** (drag) and **Zoom** (drag a rectangle) modes.
- **Mouse wheel** zooms; dragging pans.
- **Home / Back / Forward**: view history, like a browser. The view is also
  recorded when you pan, zoom, autoscale or apply limits.

Bottom bar: **Plot** draws the selection, **Clear plot** removes everything.
With a second axis, panning and zooming move both Y axes together.

---

## 6. Curve fitting

Line plots only. Tick **Enable curve fit** and click the plot to place a fit range
(two vertical lines you can drag), or type exact bounds and press **Apply**.

**Types**

| Type | Model |
|---|---|
| Linear | `a*x + b` |
| Polynomial | degree 1 ... 12 |
| Exponential | `a*e^(bx) + c` |
| Logarithmic | `a*ln(x) + b` |
| Power | `a*x^b` |
| Custom | your own `f(x)`, parameter names and starting values |

The custom fit uses Levenberg-Marquardt. Write the expression with the same
syntax as the functions above, e.g. `f(x) =  a*exp(-x/tau) + c`, parameters
`a, tau, c`, initial guesses `1, 2, 0`.

- Select **which columns to fit** in the list.
- The fitted formula and **R²** are shown for each curve.
- **+ New interval** adds another fit range; the arrows step through intervals,
  so you can fit different parts of the data separately and keep all the fits
  on screen.

---

## 7. Integral (area under a curve)

Line plots only. Tick **Enable integral**, set the range with the two draggable
lines (or type it), and choose which curves to integrate: **data** and/or
**fitted curves**. The area (trapezoidal rule with interpolated boundaries) is
reported and the region is shaded on the plot. Several intervals can be placed
and stepped through, like fits.

---

## 8. Interpolation cursor

Line plots only. **Enable cursor** puts one draggable vertical line on the plot
(or type `x =`); the interpolated Y value of every plotted curve at that X is listed.

**Snap to nearest data point** applies to all three draggable-line tools.

---

## 9. Axis limits

**X min / X max / Y min / Y max** (and **Y2** when the second axis is on):
type values and press **Apply limits**, or press **Autoscale**. The boxes follow
the view when you pan or zoom.

---

## 10. Saving the plot as an image

**Save plot image...** writes the plot as **PNG**, **PDF** or **JPEG**.

---

## 11. Export to LaTeX (pgfplots)

**Export LaTeX (pgfplots)...** opens a settings dialog for what is currently
plotted, then generates a complete document. The dialog remembers its settings
**per plot type** (line / bar / histogram) and its window size.

### Buttons
- **Generate LaTeX Code**: builds the code and opens it in a viewer.
- **Cancel**.
- **Reset settings**: sets every setting of the *current plot type* back to its
  default (after asking). Settings of the other plot types are left alone.

### Sections

**Data source**
- *Embed data points directly in the code*, or
- *Load from CSV file(s)* (`\pgfplotstable`, `table=`): one CSV per source file,
  with its own file name and separator; the CSVs are saved next to the `.tex`.
- Histograms and bar charts with "numbers as text" are always embedded.

**Labels & axis limits**: title, X / Y (and Y2) labels, Y-label rotation,
min/max for each axis, log axes, and an option to escape LaTeX special
characters in text.

**Ticks & number format** (one row per axis X, Y and Y2)
- Tick step and tick positions, number of **decimals** (or *Default*), zero-fill (`1.20`).
- **Minor ticks** per axis, each with its own count; or tick
  *Same minor ticks on all axes* to use one setting (the per-axis options are
  then greyed out).
- **Tick label rotation and anchor** per axis, for all plot types.
- Tick alignment and position, and *Hide all ticks*.

**Style**: grid mode, smoothing, figure width and height, boxed axis.

**Axis colors** (optional, switched on with *Customize the colors of the axes*)
- A grid with one column each for the **X**, **Y** and **second Y** axis, side by
  side. Per axis: axis line, tick marks, tick labels (the numbers), axis label and
  grid lines (the second axis has no grid of its own).
- The second-Y column has an *own colors* checkbox; when it is off, the second
  axis copies the Y colors.
- The plot title has its own single color.

**Legend**: none, a preset corner, or an exact position in axis coordinates.

**Curves** (one row per plotted curve; the rows can be re-ordered)
- *Line plots*: legend label, colour (19 LaTeX colours, any hex colour through a
  colour picker, or a colour scheme), marker, line style, line width.
- *Bar charts and histograms*: legend label, **fill colour**, **fill type**
  (solid, none, or patterns such as north east lines, horizontal/vertical lines,
  grid, crosshatch, dots, bricks, stars), fill opacity, **outline colour**,
  **outline style** (solid / dashed / dotted / dash-dotted / none) and outline width.

**Bar chart / Histogram**: bar width, gap between bars, sideways orientation,
histogram bin tick placement.

**Bar value labels** (bar charts only, *Show the value at the end of each bar*):
writes the value of each bar next to it (pgfplots `nodes near coords`). Settings:
**anchor** (default or any TikZ anchor), **text color**, optional **background**
(color and opacity) and **border** (color), **font size**, **rotation**,
**decimals** (or *Default*), **shift X / Y** in pt and **padding**. Also applied
to bars on the second axis. Sideways bars and histograms swap the axes in the exported code
(including error-bar caps, the second axis at the top, and tick settings).

**Error bars**: include them, colour (same as the curve or fixed), line width and
cap style.

**Curve fit**: include the fits that are currently shown as analytic
`\addplot ... {expression}` over their range, with colour and legend label.
Custom fits get their fitted parameter values substituted and common function
names translated to pgfmath (a reminder comment asks you to double-check).

**Integral**: include the shaded regions with colour and opacity, optionally
labelled with the numeric area.

**Figure wrapper**: wrap the plot in `\begin{figure}...\end{figure}` with caption,
label and placement.

### The code viewer
- **Copy to clipboard**
- **Save .tex (+ CSV)...**
- **Compile to PDF...**: finds `lualatex`, `latexmk`, `pdflatex` or `xelatex` on
  your PATH, compiles in a temporary folder and offers to save/open the PDF, or
  shows the full log if it fails.
- **Save as SVG...**: vector SVG via `latex` + `dvisvgm` (or `pdf2svg` /
  Inkscape as a fallback).

You need a LaTeX installation (e.g. TeX Live or MiKTeX) for the compile and SVG
buttons only; generating and copying the code works without it. The generated
document needs the `pgfplots` package (and `pgfplotstable` for the CSV option).
Pattern fills load the TikZ `patterns` library automatically.

---

## 12. Tips and good to know

- The program does not re-draw automatically when you change a selection; press
  **Plot** after choosing different columns, a colour scheme, bar options, etc.
- Fit, integral, cursor and the log-axis boxes are only available for **line
  plots**; for bars and histograms they are hidden.
- Error bars are not available for histograms.
- Colours set in the export dialog do not change the plot on screen, and the
  extra bar/histogram style options (fill type, outline) only affect the exported
  LaTeX.
- Ctrl+V works in the manual-entry table with data copied from a spreadsheet.
- Selection and view history are lost when a new set of files is opened.
