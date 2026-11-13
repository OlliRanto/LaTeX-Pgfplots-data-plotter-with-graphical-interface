# Universal Data Plotter

A desktop program for quickly plotting data from text files, typed-in tables or
mathematical functions, analysing it (curve fits, integrals, interpolation) and
exporting publication-quality figures as **LaTeX / pgfplots** code.

It is written in C++ with Qt 6 and QCustomPlot. Build instructions and
technical notes are in [README_TECHNICAL.md](README_TECHNICAL.md).

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

### Enter / edit data...
Opens a spreadsheet-like window for typed data and for viewing and editing CSV files.

- Type values, or click a cell and press **Ctrl+V** to paste straight from
  Excel / LibreOffice / any tab-separated source.
- Tick *First row contains column names* if your first row is a header.
- Buttons to add rows and columns and to delete the selected ones, or clear everything.
- **Open CSV...** shows a CSV / TXT file in the table, or pick one of the files loaded in the plotter and press
  **Edit**. The list also has the data made in the **Data editor** and the manually entered table: they are shown
  as a table too, and **Plot this data** puts the changed table back in place of them. The separator of a file is detected and every line of the file is shown (also comment lines); cells are kept as
  text, so nothing is changed by number parsing. Edit it like any table (add / delete rows and columns, paste).
- **Clean up when opening** (on by default) removes the useless parts of a file: the lines above the data (title,
  instrument settings, ...), `#` comment lines, blank lines, lines after the data and columns without data. The row
  with the column names right above the data is kept (a units row below the names is merged into them: `Time [s]`).
  Untick it to see the file exactly as it is. The **Clean up** button does the same for the table as it is now.
  Saving a cleaned file asks first, because the removed lines are gone from the saved file.
- **Save** writes the table back to the file that is shown (the separator can be changed first); **Save as CSV...**
  writes it to a new file. If the saved file is loaded in the plotter you are asked whether to load it again.
- **Plot this data** hands the table to the plotter as the manual dataset (the file itself is not changed).

Manually entered data shows up next to any loaded files. Entering new data
replaces the previous manual table.

### Data editor...
Opens a window to make new data from loaded data (files, manual data or functions).

- Pick the **dataset**, its **X column** (or the row index) and one or more **Y columns**.
- Set the **Order** (1 = first, 2 = second, 3 = third, ...) and press **Derivative** or **Integral**
  (cumulative, trapezoid rule, 0 at the first point), or press **Abs** or **Flip sign** (these ignore the order).
- The result is added to the data selector of the plot window as a dataset called `<name> (edited)`.
  Its columns get the operation at the end of their name, e.g. `y (derivative)`, `y (2. derivative)`,
  `y (integral)`, `y (2. integral)`, `y (abs)`, `y (sign flipped)`.
- The **Math expression** box changes the data with a formula. `y` is the selected Y column and `x` the X column.
  Start with an operator to apply it to y: `*2` doubles, `+5` adds 5, `/3` divides, `^2` squares, `-1` subtracts 1.
  Or write a full expression such as `y^2 + 1`, `sqrt(y)` or `sin(x)*y`, then press **Apply** (or Enter).
  The column is named e.g. `y (*2)`.
- **Flip sign** changes every `+` into `-` and every `-` into `+` (`-y`). Flipping twice gives the original back.
- A plotted **function is edited as math**, not as points: the result is a new formula that is plotted, fitted and
  exported to LaTeX like any typed function. Flip sign of `x^2 + 3*x` gives `-x^2 - 3*x`, `*2` on `sin(x)` gives
  `sin(x)*2`, Abs gives `abs(sin(x))` and the derivative of `sin(x)` is `cos(x)` (exact, any order). The **integral** is an exact antiderivative, 0 at the start of the domain like the numeric
  one: `sin(x)` gives `-cos(x) + 1`. It is found for polynomials, powers of linear terms, `sin`, `cos`, `tan`, `exp`,
  `sinh`, `cosh`, `tanh`, `sqrt`, `log`, `a^x` and sums / constant multiples of them. When no formula is known
  (e.g. `x*sin(x)`) the result is made as sampled points instead, and the status line says so. Edited results can be edited again.
- Derivatives and integrals are computed along increasing X, so unsorted data is fine; rows with a missing
  X or Y value stay empty in the result.
- The **Edited data** list in the same window shows everything made so far; select entries and press
  **Delete selected**, or **Delete all edited data**.

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
- **Gap between bars**: leaves a space between neighbouring bars, in % of one bin's width (0 = the bars touch). The bars stay centred on their bins. The LaTeX export starts from the same value (it can be changed there) and then draws every bar as its own rectangle.
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

## 3b. 3D plots

Choose **Plot type: 3D plot** to draw in three dimensions. The left panel then shows the 3D controls
instead of the 2D column, fit and axis-limit tools. Pick *What to draw*, then press **Plot**:

| Source | What you give | Result |
|---|---|---|
| Data: line | X, Y and Z column of a loaded file | the points joined in file order |
| Data: points | X, Y and Z column | a 3D scatter plot |
| Data: surface | X, Y and Z column | a regular grid (every x with every y) is used as it is; scattered points are interpolated onto a grid (local plane fit; *Grid points per side*) |
| Expression: surface | `z = f(x, y)` and an x and y range | a surface calculated from the formula, e.g. `sin(x)*cos(y)` |
| Expression: curve | `x(t)`, `y(t)`, `z(t)` and a t range | a space curve, e.g. `cos(t)`, `sin(t)`, `t/5` |

Expressions use the same syntax as the function plots (`+ - * / ^`, `sin cos tan ... sqrt abs`, `pi`, `e`).
Values that are not finite (`1/0`, `sqrt(-1)`) become holes. **Z range** leaves out values outside a range,
which tames formulas with poles such as `1/(x^2+y^2)`.

Appearance, labels and view change the plot immediately: colour map, surface style (filled / filled + mesh /
mesh only), shading, box and grid, colouring a line or points by height (or a fixed colour of your choice; surfaces are always coloured by height), equal axis scaling, box height,
title and axis labels. **Drag** rotates, **right-drag** moves, the **wheel** zooms, **double-click** resets;
the elevation / azimuth boxes follow (and can be typed in).

*Save plot image* writes PNG, JPG or PDF of the 3D view as it is on screen (it is drawn in software with QPainter, so no OpenGL is needed).

**Export LaTeX (pgfplots)** works like the 2D export. The dialog asks:

- **Data source** - for plots made from an expression (*z = f(x, y)* or a curve *x(t), y(t), z(t)*), *Math expression* is the default: the formula itself is written (`\addplot3[surf, domain=.., y domain=.., samples=..] {sin(x)*cos(y)};` or `\addplot3[domain=.., samples=.., samples y=0] ({cos(x)}, {sin(x)}, {x/5});`), like function plots in the 2D export. The code stays short, needs no CSV file, and pgfplots calculates the points while compiling; the number of samples can be changed in the dialog. The expression is translated to pgfmath (trigonometry in radians, `log` = natural log, `power(a,b)` = `pow(a,b)`, every operation parenthesised so precedence is the same as in the app). pgfmath stops with an error where TeX cannot calculate a value (division by zero, `sqrt` of a negative number, `ln` of a number <= 0); restrict the x / y range in that case, or use one of the data options below. Plots made from data columns do not have this option.
- **Data source (data points)** - *Embed data points directly in the code* (a self-contained `.tex`; very large grids and lines are thinned to at most 60 x 60 points / 2000 points so that TeX does not run out of memory), or *Load from CSV file(s)* (`\addplot3 table`): the complete data is written to `<name>_surface.csv`, `<name>_line.csv` and `<name>_points.csv` (only those that exist) next to the `.tex`, with a header row `x,y,z` and your choice of column separator.
- **Figure** - width in cm.
- **Figure wrapper (optional)** - the same as in the other plot types: wrap the plot in a `figure` environment (article class, 1 in margins) with caption, label and placement (`htbp`, `ht`, `h`, `t`, `b`, `p`, `H`; `H` adds the `float` package) instead of a standalone document.

*Generate LaTeX...* then shows the code with the same buttons as the 2D export: **Copy to clipboard**, **Save .tex (+ CSV)**, **Compile to PDF...** (lualatex / latexmk / pdflatex / xelatex, whichever is installed) and **Save as SVG...** (latex -> dvisvgm, or PDF -> pdf2svg / Inkscape). The LaTeX picture uses the same view angle, colour map, labels and surface style as the screen. For SVG the data is always limited to that size (the "latex" engine used by the SVG route has little memory), even when the CSV option is on; the saved .tex and PDF use the full data. Surfaces are always depth-sorted by pgfplots; a line that passes through a surface is drawn on top of it in LaTeX.

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
- **Manual tick values** per axis (`X tick values: 0, 0.5, 1, 2.5`), which replace the tick step of that axis.
- Log scales are written with pgfplots' own `semilogxaxis`, `semilogyaxis` and `loglogaxis` environments. On a log axis the tick step and decimals are ignored (ticks sit at powers of ten); use manual tick values for other positions, and the minor-ticks box switches the automatic 2..9 minor ticks on or off.
- Tick step and tick positions, number of **decimals** per axis (or *Default*, shown before the spin box) with an individual **zero-fill** (`1.20`) per axis, next to that axis' minor ticks.
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

**Legend**: none, a preset corner, or an exact position in axis coordinates. The *Customize legend* tick box next to the position settings adds font size, text colour, border and background (colour and opacity), padding, number of columns (or one row), text alignment and reversed order.

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
