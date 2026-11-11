/**
 * @file octal_output.c
 * @brief octal-explorer: command-line program and its output files.
 *
 *   octal .007 -n 200000            Treblecross, 200k heap sizes
 *   octal .6 -n 1000000 -o officers  Officers into directory "officers"
 *   octal grundy -n 50000            Grundy's game (split into two unequal heaps)
 *   octal couples -n 50000           Couples-are-forever (split any heap except 2)
 *
 * The computation is in octal.c (see compute(), find_period()); this file
 * parses the options, analyses the sequence and writes the results.  See
 * octal.c for the code syntax and the methods.
 *
 * Output directory contains:
 *   summary.txt     rule, timing, period (proved via the octal periodicity theorem),
 *                   records, value frequencies, sparse-space statistics
 *   values.csv      n,G(n)
 *   values.png      G(n) against n (density-shaded; red = new record value)
 *   wrap.png        sequence wrapped in rows of width W, coloured by value
 *   wrapdiff.png    same layout; dark cell where G(n) != G(n-W)
 *   autocorr.png    fraction of n (second half) with G(n) = G(n+p), p = 1..L
 *   histogram.png   frequency of each value (log scale), bars in palette colours
 *   index.html      gallery + interactive wrap explorer (open in a browser)
 *
 * Sections: graphics (PNG) · output (HTML) · command-line program.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include "octal.h"
#include "octal_internal.h"

/** Suppress progress and summary output (`-q`).  Set once by main(). */
static int quiet = 0;

/* -------------------------------------------------------------- graphics */

/** @brief An RGB image in memory (8 bits per channel, rows top to bottom). */
typedef struct {
    int w, h;            /**< width and height in pixels */
    uint8_t *p;          /**< w*h*3 bytes, row-major RGB (owned; free() it) */
} Image;

/** New @p w x @p h image filled with colour (r, g, b). */
static Image img_new(int w, int h, int r, int g, int b) {
    Image im = {w, h, xmalloc((size_t)w * h * 3)};
    for (long i = 0; i < (long)w * h; i++) {
        im.p[3 * i] = r; im.p[3 * i + 1] = g; im.p[3 * i + 2] = b;
    }
    return im;
}

/** Set pixel (x, y) to colour @p c; coordinates outside the image are ignored. */
static inline void px(Image *im, int x, int y, const uint8_t c[3]) {
    if (x < 0 || y < 0 || x >= im->w || y >= im->h) return;
    uint8_t *q = im->p + 3 * ((size_t)y * im->w + x);
    q[0] = c[0]; q[1] = c[1]; q[2] = c[2];
}

/** Fill the rectangle [x0, x1) x [y0, y1) with colour @p c (clipped). */
static void rect(Image *im, int x0, int y0, int x1, int y1, const uint8_t c[3]) {
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) px(im, x, y, c);
}

/** Store @p v big-endian in b[0..3] (PNG byte order). */
static void put32(uint8_t *b, uint32_t v) {
    b[0] = v >> 24; b[1] = v >> 16; b[2] = v >> 8; b[3] = v;
}

/** Write one PNG chunk: length, 4-byte @p type, @p data, CRC-32 of type+data. */
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t h[8];
    put32(h, len);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uLong crc = crc32(0, (const Bytef *)type, 4);
    if (len) crc = crc32(crc, data, len);
    uint8_t c[4];
    put32(c, (uint32_t)crc);
    fwrite(c, 1, 4, f);
}

/**
 * @brief Write @p im as an 8-bit RGB PNG file.
 *
 * Every row uses filter type 0; the image data is compressed with zlib level 6.
 * Dies on I/O or compression errors.
 */
static void png_save(const Image *im, const char *path) {
    size_t stride = (size_t)im->w * 3 + 1, rawlen = stride * im->h;
    uint8_t *raw = xmalloc(rawlen);
    for (int y = 0; y < im->h; y++) {
        raw[y * stride] = 0;
        memcpy(raw + y * stride + 1, im->p + (size_t)y * im->w * 3, (size_t)im->w * 3);
    }
    uLongf zlen = compressBound(rawlen);
    uint8_t *z = xmalloc(zlen);
    if (compress2(z, &zlen, raw, rawlen, 6) != Z_OK) die("zlib compression failed");
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, im->w);
    put32(ihdr + 4, im->h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)zlen);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

/** Convert HSV (all components in [0, 1]) to 8-bit RGB in @p c. */
static void hsv(double h, double s, double v, uint8_t c[3]) {
    double r, g, b, f = h * 6 - floor(h * 6);
    double p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch ((int)floor(h * 6) % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    c[0] = (uint8_t)(255 * r + .5); c[1] = (uint8_t)(255 * g + .5); c[2] = (uint8_t)(255 * b + .5);
}

/**
 * @brief Colour of nim-value @p v, shared by all plots and the HTML explorer.
 *
 * 0 (P-positions) is near-black (25, 25, 35).  v > 0 has hue v * 0.618034 mod 1
 * (golden-ratio steps keep neighbouring values distinct), saturation 0.70 and
 * value 0.92.  The Python package (octalgames.palette) uses the same formula.
 */
static void palette(unsigned v, uint8_t c[3]) {
    if (v == 0) { c[0] = 25; c[1] = 25; c[2] = 35; return; }
    hsv(fmod(v * 0.6180339887, 1.0), 0.70, 0.92, c);
}

/** Plot colours: grid lines, record markers, separator line in wrap plots. */
static const uint8_t GRID[3] = {225, 225, 232},
                     RED[3] = {215, 40, 40}, SEP[3] = {200, 30, 30};

/**
 * @brief values.png: G(n) against n, 1600 x 640 pixels.
 *
 * Points are binned per pixel and shaded by log(count); grey lines mark powers
 * of two; red squares mark each new record value.
 *
 * @param G     nim-values
 * @param N     number of values
 * @param maxG  largest value (top of the y axis)
 * @param path  output file
 */
static void plot_values(const uint16_t *G, long N, unsigned maxG, const char *path) {
    const int W = 1600, H = 640;
    Image im = img_new(W, H, 255, 255, 255);
    unsigned top = maxG ? maxG : 1;
    for (unsigned v = 1; v <= top; v *= 2) { /* gridlines at powers of two */
        int y = H - 1 - (int)((double)v * (H - 1) / top);
        rect(&im, 0, y, W, y + 1, GRID);
    }
    uint32_t *cnt = xcalloc((size_t)W * H, sizeof *cnt);
    uint32_t cmax = 1;
    for (long n = 0; n < N; n++) {
        int x = (int)((double)n * W / N);
        int y = H - 1 - (int)((double)G[n] * (H - 1) / top);
        uint32_t c = ++cnt[(size_t)y * W + x];
        if (c > cmax) cmax = c;
    }
    int dot = N < W ? (int)(W / N) : 1;
    if (dot > 6) dot = 6;
    if (dot < 1) dot = 1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t c = cnt[(size_t)y * W + x];
            if (!c) continue;
            double t = 0.35 + 0.65 * log1p(c) / log1p(cmax);
            uint8_t col[3] = {(uint8_t)(230 - 210 * t), (uint8_t)(235 - 190 * t), (uint8_t)(250 - 110 * t)};
            rect(&im, x, y - dot / 2, x + dot, y - dot / 2 + dot, col);
        }
    unsigned rec = 0;
    for (long n = 0; n < N; n++)
        if (G[n] > rec || n == 0) {
            rec = G[n];
            int x = (int)((double)n * W / N);
            int y = H - 1 - (int)((double)G[n] * (H - 1) / top);
            rect(&im, x - 2, y - 2, x + 3, y + 3, RED);
        }
    png_save(&im, path);
    free(cnt);
    free(im.p);
}

/**
 * @brief Layout shared by wrap.png and wrapdiff.png.
 *
 * Rows of W values; if there are too many rows for about 1800 pixels, the first
 * rows_top and the last rows_bot rows are shown, separated by a red line.
 */
typedef struct {
    int W;               /**< values per row */
    int cs;              /**< cell size in pixels (1..12) */
    int rows_top;        /**< rows drawn from the start */
    int rows_bot;        /**< rows drawn from the end (0 if everything fits) */
    int sep;             /**< height of the separator line in pixels (0 or 4) */
    long rows;           /**< total number of rows, ceil(N / W) */
} WrapLayout;

/** Compute the wrap layout for @p N values in rows of @p W (cell size 1600/W, clamped to 1..12). */
static WrapLayout wrap_layout(long N, int W) {
    WrapLayout L;
    L.W = W;
    L.cs = 1600 / W;
    if (L.cs < 1) L.cs = 1;
    if (L.cs > 12) L.cs = 12;
    L.rows = (N + W - 1) / W;
    long maxrows = 1800 / L.cs;
    if (L.rows <= maxrows) {
        L.rows_top = (int)L.rows; L.rows_bot = 0; L.sep = 0;
    } else {
        L.rows_top = (int)(maxrows / 2); L.rows_bot = (int)(maxrows / 2); L.sep = 4;
    }
    return L;
}

/**
 * @brief wrap.png / wrapdiff.png: the sequence in rows of @p W values.
 *
 * @param G     nim-values
 * @param N     number of values
 * @param W     row width
 * @param diff  0: colour cells by value (palette());
 *              1: dark red where G(n) - G(n-W) != saltus, light otherwise, grey in the first row
 * @param saltus expected difference G(n) - G(n-W) in diff mode (0 = ordinary periodicity)
 * @param path  output file
 */
static void plot_wrap(const uint16_t *G, long N, int W, int diff, long saltus, const char *path) {
    WrapLayout L = wrap_layout(N, W);
    int iw = W * L.cs, ih = (L.rows_top + L.rows_bot) * L.cs + L.sep;
    Image im = img_new(iw, ih, 255, 255, 255);
    for (int part = 0; part < 2; part++) {
        int nrows = part ? L.rows_bot : L.rows_top;
        long r0 = part ? L.rows - L.rows_bot : 0;
        int y0 = part ? L.rows_top * L.cs + L.sep : 0;
        for (int r = 0; r < nrows; r++)
            for (int c = 0; c < W; c++) {
                long n = (r0 + r) * W + c;
                if (n >= N) continue;
                uint8_t col[3];
                if (!diff) palette(G[n], col);
                else if (n < W) { col[0] = col[1] = col[2] = 200; }
                else if ((long)G[n] - (long)G[n - W] == saltus) { col[0] = 248; col[1] = 248; col[2] = 245; }
                else { col[0] = 150; col[1] = 20; col[2] = 30; }
                rect(&im, c * L.cs, y0 + r * L.cs, (c + 1) * L.cs, y0 + (r + 1) * L.cs, col);
            }
    }
    if (L.sep) rect(&im, 0, L.rows_top * L.cs, iw, L.rows_top * L.cs + L.sep, SEP);
    png_save(&im, path);
    free(im.p);
}

/**
 * @brief Autocorrelation of the second half of the sequence.
 *
 * @return newly allocated array a[0..L]; a[p] = fraction of n in [N/2, N-p)
 *         with G(n) = G(n+p); a[0] = 0 (unused).  The caller frees it.
 */
static double *autocorr(const uint16_t *G, long N, int L) {
    double *a = xcalloc(L + 1, sizeof *a);
    long start = N / 2;
    for (int p = 1; p <= L; p++) {
        long hit = 0, tot = 0;
        for (long n = start; n + p < N; n++, tot++) hit += G[n] == G[n + p];
        a[p] = tot ? (double)hit / tot : 0;
    }
    return a;
}

/**
 * @brief autocorr.png: bars of a[1..L] (several lags per pixel use the maximum).
 *
 * @param a     autocorrelation from autocorr()
 * @param L     number of lags
 * @param per   detected period (multiples drawn orange), or 0
 * @param peak  best lag (drawn red)
 * @param path  output file
 */
static void plot_autocorr(const double *a, int L, int per, int peak, const char *path) {
    int W = L * 2 < 1600 ? (L * 2 < 400 ? 400 : L * 2) : 1600, H = 360;
    Image im = img_new(W, H, 255, 255, 255);
    for (int q = 1; q <= 3; q++) rect(&im, 0, H - H * q / 4, W, H - H * q / 4 + 1, GRID);
    for (int x = 0; x < W; x++) {
        int p0 = 1 + (int)((long)x * L / W), p1 = 1 + (int)((long)(x + 1) * L / W);
        if (p1 <= p0) p1 = p0 + 1;
        double v = 0;
        int best = p0;
        for (int p = p0; p < p1 && p <= L; p++)
            if (a[p] > v) { v = a[p]; best = p; }
        uint8_t col[3] = {120, 130, 150};
        if (per && best % per == 0) { col[0] = 235; col[1] = 140; col[2] = 30; }
        if (best == peak) { col[0] = RED[0]; col[1] = RED[1]; col[2] = RED[2]; }
        rect(&im, x, H - (int)(v * (H - 1)), x + 1, H, col);
    }
    png_save(&im, path);
    free(im.p);
}

/**
 * @brief histogram.png: frequency of each value 0..maxG, log10(count+1) scale.
 *
 * Bars use palette() colours, so the histogram doubles as a colour legend.
 */
static void plot_hist(const long *cnt, unsigned maxG, const char *path) {
    int nv = (int)maxG + 1;
    int bw = 1600 / nv;
    if (bw < 1) bw = 1;
    if (bw > 24) bw = 24;
    int W = nv * bw < 1600 ? nv * bw : 1600, H = 320;
    if (W < 200) W = 200;
    Image im = img_new(W, H, 255, 255, 255);
    double lmax = 1;
    for (int v = 0; v < nv; v++)
        if (log10(cnt[v] + 1.0) > lmax) lmax = log10(cnt[v] + 1.0);
    for (int q = 1; q < (int)lmax + 1; q++) {
        int y = H - (int)(q / lmax * (H - 1));
        if (y > 0) rect(&im, 0, y, W, y + 1, GRID);
    }
    for (int v = 0; v < nv; v++) {
        if (!cnt[v]) continue;
        int x0 = (int)((long)v * W / nv), x1 = (int)((long)(v + 1) * W / nv);
        if (x1 <= x0) x1 = x0 + 1;
        int h = (int)(log10(cnt[v] + 1.0) / lmax * (H - 1));
        if (h < 1) h = 1;
        uint8_t col[3];
        palette(v, col);
        rect(&im, x0, H - h, x1 - (bw > 3), H, col);
    }
    png_save(&im, path);
    free(im.p);
}

/* ---------------------------------------------------------------- output */

/** out = dir + "/" + name (truncated to @p sz bytes). */
static void join(char *out, size_t sz, const char *dir, const char *name) {
    snprintf(out, sz, "%s/%s", dir, name);
}

/**
 * @brief Write DIR/index.html: summary, plots, records and the interactive explorer.
 *
 * The first @p embed values are embedded as a JavaScript Uint16Array, so the page
 * works from file:// without a server.  The explorer's JavaScript uses the same
 * palette as palette().
 *
 * @param dir    output directory
 * @param R      rule
 * @param res    computed sequence
 * @param P      period result
 * @param maxG   largest value
 * @param maxat  first n with G(n) = maxG
 * @param wrapW  default wrap width
 * @param peak   best autocorrelation lag
 * @param L      number of autocorrelation lags
 * @param embed  maximum number of values to embed
 * @param recn   heap sizes of the record values
 * @param recv   the record values
 * @param nrec   number of records
 */
static void write_html(const char *dir, const Rule *R, const Result *res, const Period *P,
                       unsigned maxG, long maxat, int wrapW, int peak, int L, long embed,
                       const long *recn, const unsigned *recv, int nrec) {
    char path[4096];
    join(path, sizeof path, dir, "index.html");
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(1); }
    long M = res->N < embed ? res->N : embed;
    char per[256];
    if (P->found && P->saltus && P->proven)
        snprintf(per, sizeof per, "Proved (Austin's Theorem 6.8, with period %ld and saltus %ld): "
                 "arithmetic period %ld, saltus %ld, preperiod %ld: G(n+%ld) = G(n) + %ld",
                 P->proof_per, P->proof_saltus, P->per, P->saltus, P->pre, P->per, P->saltus);
    else if (P->found && P->saltus)
        snprintf(per, sizeof per, "Apparent (unproved) arithmetic period %ld, saltus %ld, preperiod %ld: "
                 "G(n+%ld) = G(n) + %ld", P->per, P->saltus, P->pre, P->per, P->saltus);
    else if (P->found)
        snprintf(per, sizeof per, "%s period %ld, preperiod %ld", P->proven ? "Proved:" : "Apparent (unproved):",
                 P->per, P->pre);
    else
        snprintf(per, sizeof per, "No period found up to n = %ld", res->N);

    fprintf(f,
"<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"<title>%s — nim-sequence explorer</title>\n"
"<style>\n"
":root{--bg:#fafafa;--fg:#1b1d22;--mut:#666b75;--card:#fff;--bd:#dfe1e6;--acc:#2f5fd0}\n"
"@media (prefers-color-scheme:dark){:root{--bg:#15171b;--fg:#e6e7ea;--mut:#9aa0aa;--card:#1d2026;--bd:#30343c;--acc:#7aa2ff}}\n"
"body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif}\n"
"main{max-width:1680px;margin:0 auto;padding:16px}\n"
"h1{font-size:22px;margin:8px 0 4px}h2{font-size:17px;margin:28px 0 6px}\n"
".mut{color:var(--mut)}.card{background:var(--card);border:1px solid var(--bd);border-radius:8px;padding:12px;margin:10px 0;overflow:auto}\n"
"img{max-width:100%%;height:auto;image-rendering:pixelated;border:1px solid var(--bd);background:#fff}\n"
"table{border-collapse:collapse}td,th{padding:3px 12px 3px 0;text-align:left;vertical-align:top}\n"
".ctl{display:flex;flex-wrap:wrap;gap:10px 18px;align-items:center;margin-bottom:10px}\n"
".ctl label{display:flex;gap:6px;align-items:center}input[type=number]{width:90px}\n"
"button,select,input{font:inherit}#cvwrap{overflow:auto;max-height:80vh;border:1px solid var(--bd)}\n"
"#cv{image-rendering:pixelated;display:block;background:#fff}#info{font-family:ui-monospace,monospace;min-height:1.5em}\n"
"code{font-family:ui-monospace,monospace}\n"
"</style></head><body><main>\n"
"<h1>%s</h1>\n<div class=\"mut\">%s</div>\n"
"<div class=\"card\"><table>\n"
"<tr><th>Heap sizes</th><td>0 … %ld</td></tr>\n"
"<tr><th>Periodicity</th><td>%s</td></tr>\n"
"<tr><th>Largest value</th><td>%u (first at n = %ld)</td></tr>\n"
"<tr><th>Computation</th><td>%.2f s, %s method%s</td></tr>\n"
"</table></div>\n",
            R->code, R->code, R->name, res->N - 1, per, maxG, maxat, res->secs,
            res->sparse ? "sparse-space" : method_name[res->method], "");
    if (res->sparse)
        fprintf(f, "<p class=\"mut\">Sparse space: mask 0x%x makes %ld heap sizes (%.3f%%) rare. "
                   "Choose <em>rare</em> in the explorer to see them.</p>\n",
                res->mask, res->nrare, 100.0 * res->nrare / res->N);

    fprintf(f,
"<h2>Interactive wrap explorer</h2>\n"
"<p class=\"mut\">Row r, column c shows n = start + r·W + c. When W is a period (or a multiple of one), "
"the picture turns into vertical stripes, and in <em>diff</em> mode it turns blank. Arrow keys ←/→ change W. "
"Embedded data: first %ld values.</p>\n"
"<div class=\"card\"><div class=\"ctl\">\n"
"<label>Width W <input id=\"w\" type=\"number\" min=\"1\" max=\"5000\" value=\"%d\"></label>\n"
"<button id=\"wm\">−1</button><button id=\"wp\">+1</button><button id=\"wd\">×2</button><button id=\"wh\">÷2</button>\n"
"%s"
"<label>Cell <input id=\"cs\" type=\"number\" min=\"1\" max=\"24\" value=\"4\"> px</label>\n"
"<label>Start n <input id=\"st\" type=\"number\" min=\"0\" value=\"0\" step=\"1\"></label>\n"
"<label>Mode <select id=\"mode\"><option value=\"val\">values</option><option value=\"diff\">diff: G(n) − G(n−W) ≠ saltus</option>"
"<option value=\"zero\">P-positions (G = 0)</option><option value=\"bit\">bit plane</option>"
"<option value=\"rare\">rare values</option><option value=\"rec\">≥ threshold</option></select></label>\n"
"<label>Bit / threshold <input id=\"bit\" type=\"number\" min=\"0\" value=\"0\"></label>\n"
"<label title=\"diff mode compares G(n) - G(n-W) with this value (arithmetic periodicity)\">Saltus <input id=\"sal\" type=\"number\" value=\"0\"></label>\n"
"</div><div id=\"info\" class=\"mut\">hover over the picture</div><div id=\"cvwrap\"><canvas id=\"cv\"></canvas></div></div>\n",
            M, wrapW,
            P->found ? "<button id=\"wper\">use period</button>\n" : "");

    fprintf(f,
"<h2>Values</h2><div class=\"card\"><img src=\"values.png\" alt=\"G(n) against n\">\n"
"<p class=\"mut\">x: n from 0 to %ld. y: G(n) from 0 (bottom) to %u (top), with grey lines at powers of two. "
"Darker means more points in that pixel. Red squares mark new record values.</p></div>\n"
"<h2>Wrapped at W = %d</h2><div class=\"card\"><img src=\"wrap.png\" alt=\"wrapped sequence\">\n"
"<p class=\"mut\">Each row holds W consecutive values, coloured by value (0 is near-black; colours match the histogram). "
"If the sequence is long, the first and last rows are shown either side of a red line.</p>\n"
"<img src=\"wrapdiff.png\" alt=\"difference with previous row\">\n"
"<p class=\"mut\">Dark red: G(n) − G(n−W) differs from the saltus (0, or the detected saltus when W is the detected period). "
"Once the preperiod ends, an (arithmetic) period W leaves only light cells.</p></div>\n"
"<h2>Autocorrelation</h2><div class=\"card\"><img src=\"autocorr.png\" alt=\"autocorrelation\">\n"
"<p class=\"mut\">For lag p = 1 … %d (left to right), bar height is the fraction of n in the second half with G(n) = G(n+p). "
"The best lag is red (p = %d)%s.</p></div>\n"
"<h2>Value frequencies</h2><div class=\"card\"><img src=\"histogram.png\" alt=\"histogram\">\n"
"<p class=\"mut\">Values 0 … %u, left to right. Height is log₁₀(count+1). Short bars are rare values, which the sparse-space method exploits.</p></div>\n",
            res->N - 1, maxG, wrapW, L, peak, P->found ? "; multiples of the period are orange" : "", maxG);

    fprintf(f, "<h2>Record values</h2><div class=\"card\"><table><tr><th>n</th><th>G(n)</th></tr>\n");
    for (int i = 0; i < nrec; i++) fprintf(f, "<tr><td>%ld</td><td>%u</td></tr>\n", recn[i], recv[i]);
    fprintf(f, "</table></div>\n");

    fprintf(f, "<script>\nconst G=new Uint16Array([");
    for (long n = 0; n < M; n++) fprintf(f, "%s%u", n ? (n % 40 ? "," : ",\n") : "", res->G[n]);
    fprintf(f, "]);\nconst PER=%ld, SALTUS=%ld, MASK=%u;\n", P->found ? P->per : 0L,
            P->found ? P->saltus : 0L, res->sparse ? res->mask : 0u);
    fputs(
"function hsv(h,s,v){const i=Math.floor(h*6),f=h*6-i,p=v*(1-s),q=v*(1-f*s),t=v*(1-(1-f)*s);\n"
" const m=[[v,t,p],[q,v,p],[p,v,t],[p,q,v],[t,p,v],[v,p,q]][i%6];return m.map(x=>Math.round(255*x));}\n"
"const PAL=new Map();function col(v){if(v===0)return[25,25,35];let c=PAL.get(v);if(!c){c=hsv((v*0.6180339887)%1,0.70,0.92);PAL.set(v,c);}return c;}\n"
"function par(x){x^=x>>8;x^=x>>4;x^=x>>2;x^=x>>1;return x&1;}\n"
"const $=id=>document.getElementById(id),cv=$('cv'),ctx=cv.getContext('2d');\n"
"let cs=4,W=1,start=0;\n"
"function draw(){W=Math.max(1,+$('w').value|0);cs=Math.max(1,+$('cs').value|0);if(W*cs>6000)cs=Math.max(1,Math.floor(6000/W));\n"
" start=Math.max(0,+$('st').value|0);const mode=$('mode').value,b=+$('bit').value|0,sal=+$('sal').value|0;\n"
" const rows=Math.max(1,Math.min(Math.ceil((G.length-start)/W),Math.floor(3000/cs)));\n"
" cv.width=W*cs;cv.height=rows*cs;const img=ctx.createImageData(cv.width,cv.height),d=img.data;\n"
" for(let r=0;r<rows;r++)for(let c=0;c<W;c++){const n=start+r*W+c;let k;\n"
"  if(n>=G.length)k=[255,255,255];else{const g=G[n];\n"
"   if(mode==='val')k=col(g);\n"
"   else if(mode==='diff')k=n<W?[200,200,200]:(g-G[n-W]===sal?[248,248,245]:[150,20,30]);\n"
"   else if(mode==='zero')k=g===0?[20,20,20]:[245,245,245];\n"
"   else if(mode==='bit')k=(g>>b)&1?[20,20,20]:[245,245,245];\n"
"   else if(mode==='rare')k=MASK&&par(g&MASK)?[200,30,30]:[240,240,240];\n"
"   else k=g>=b?col(g):[245,245,245];}\n"
"  for(let y=0;y<cs;y++){let o=((r*cs+y)*cv.width+c*cs)*4;for(let x=0;x<cs;x++,o+=4){d[o]=k[0];d[o+1]=k[1];d[o+2]=k[2];d[o+3]=255;}}}\n"
" ctx.putImageData(img,0,0);}\n"
"cv.addEventListener('mousemove',e=>{const R=cv.getBoundingClientRect(),x=(e.clientX-R.left)*cv.width/R.width,y=(e.clientY-R.top)*cv.height/R.height;\n"
" const n=start+Math.floor(y/cs)*W+Math.floor(x/cs);$('info').textContent=n<G.length?`n = ${n}   G(n) = ${G[n]}   row ${Math.floor(y/cs)}, column ${Math.floor(x/cs)}`:'';});\n"
"for(const id of['w','cs','st','mode','bit','sal'])$(id).addEventListener('input',draw);\n"
"const setW=v=>{$('w').value=Math.max(1,v);draw();};\n"
"$('wm').onclick=()=>setW(W-1);$('wp').onclick=()=>setW(W+1);$('wd').onclick=()=>setW(W*2);$('wh').onclick=()=>setW(Math.floor(W/2));\n"
"if($('wper'))$('wper').onclick=()=>{$('sal').value=SALTUS;setW(PER);};\n"
"if(+$('w').value===PER)$('sal').value=SALTUS;\n"
"document.addEventListener('keydown',e=>{if(e.target.tagName==='INPUT'||e.target.tagName==='SELECT')return;\n"
" if(e.key==='ArrowLeft')setW(W-1);if(e.key==='ArrowRight')setW(W+1);});\n"
"draw();\n</script>\n</main></body></html>\n", f);
    fclose(f);
}

/* ---------------------------------------------------- command-line program */

/** Describe a rare-value mask: "mask 0x1ee", or "position mask 0xd (n odd)" (see RV_POS). */
static const char *rare_mask_text(unsigned mask, char *buf, size_t len) {
    if (mask & RV_POS)
        snprintf(buf, len, "position mask 0x%x (rare: parity of G(n) & mask = parity of n%s)", mask & RV_MASK,
                 mask & RV_PAR ? " + 1" : "");
    else
        snprintf(buf, len, "mask 0x%x", mask);
    return buf;
}

/** Print command-line help to stderr and exit with status 2. */
static void usage(void) {
    fprintf(stderr,
"usage: octal CODE [options]\n"
"  CODE        octal/hex code such as .007  0.6  .137  4.  .f6, or 'grundy' / 'couples'\n"
"  -n N        number of heap sizes to compute (default 10000)\n"
"  -o DIR      output directory (default out_CODE)\n"
"  -w W        wrap width for wrap.png (default: period, else best autocorrelation lag)\n"
"  -L LAGS     maximum autocorrelation lag (default 1000)\n"
"  -e M        values embedded in index.html (default 200000)\n"
"  --blocked   blocked multi-threaded method, all cores (the default; same values\n"
"              as brute force)\n"
"  --brute     brute force on one core\n"
"  --sparse    sparse-space method (experimental, one core; usually not faster)\n"
"  -j THREADS  threads for --blocked and --rare (default: all cores)\n"
"  --gpu       blocked method with the parallel part on the GPU (Vulkan)\n"
"  --rocm      same, on an AMD GPU with ROCm (HIP)\n"
"  --rare      rare-value algorithm (Grossman 2017); much faster for games with\n"
"              few rare values (.6, grundy, couples), not for .007.  Speculates\n"
"              and checks in parallel on -j threads; -j 1 is the plain algorithm\n"
"  --check     compare the chosen method (default --blocked) with brute force\n"
"  -p          print the sequence to stdout and skip output files\n"
"  -q          quiet\n");
    exit(2);
}

/**
 * @brief Command-line program: compute, analyse and write all output files.
 *
 * Steps: parse options and the code; compute(); optionally `--check` against
 * brute force or `-p` print; statistics (max value, histogram, records);
 * find_period(); autocorrelation and best lag; choose the wrap width; write
 * values.csv, the PNG plots, index.html and summary.txt.  See usage().
 */
int main(int argc, char **argv) {
    char mtext[120];                /* rare_mask_text() */
    long N = 10000, embed = 200000;
    int W = 0, L = 1000, check = 0, print = 0, threads = 0;
    Method method = METHOD_BLOCKED;
    const char *dir = NULL;
    static struct option lo[] = {{"brute", no_argument, 0, 'b'}, {"sparse", no_argument, 0, 's'},
                                 {"check", no_argument, 0, 'c'}, {"blocked", no_argument, 0, 'B'},
                                 {"gpu", no_argument, 0, 'G'}, {"rocm", no_argument, 0, 'R'},
                                 {"rare", no_argument, 0, 'A'},
                                 {"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};
    int c;
    while ((c = getopt_long(argc, argv, "n:o:w:L:e:j:pqh", lo, NULL)) != -1) {
        switch (c) {
        case 'n': N = atol(optarg); break;
        case 'o': dir = optarg; break;
        case 'w': W = atoi(optarg); break;
        case 'L': L = atoi(optarg); break;
        case 'e': embed = atol(optarg); break;
        case 'b': method = METHOD_BRUTE; break;
        case 's': method = METHOD_SPARSE; break;
        case 'c': check = 1; break;
        case 'B': method = METHOD_BLOCKED; break;
        case 'G': method = METHOD_VULKAN; break;
        case 'R': method = METHOD_ROCM; break;
        case 'A': method = METHOD_RARE; break;
        case 'j': threads = atoi(optarg); break;
        case 'p': print = 1; break;
        case 'q': quiet = 1; break;
        default: usage();
        }
    }
    if (optind != argc - 1) usage();
    if (N < 2 || N > 2000000000L) die("N must be between 2 and 2e9");
    Rule R;
    if (parse_rule(argv[optind], &R)) die("cannot parse code (examples: .007  0.6  .137  4.  grundy)");

    Result res = compute(&R, N, method, threads, !quiet);

    if (check) {
        Result b = compute(&R, N, METHOD_BRUTE, 0, !quiet);
        const char *m = method_name[method];
        for (long n = 0; n < N; n++)
            if (b.G[n] != res.G[n]) {
                printf("MISMATCH at n = %ld: %s %u, brute %u\n", n, m, res.G[n], b.G[n]);
                return 1;
            }
        if (method == METHOD_BLOCKED)
            printf("check OK: %s, n < %ld, blocked %.2f s (%d threads), brute %.2f s\n", R.code, N,
                   res.secs, res.threads, b.secs);
        else if (method == METHOD_SPARSE)
            printf("check OK: %s, n < %ld, sparse %.2f s (%s), brute %.2f s\n", R.code, N, res.secs,
                   res.sparse ? "active" : "inactive", b.secs);
        else if (method == METHOD_RARE)
            printf("check OK: %s, n < %ld, rare %.2f s (%s, %s, %ld rare heaps, %d threads, "
                   "%lld restarts), brute %.2f s\n", R.code, N, res.secs,
                   res.rare.active ? "active" : "inactive", rare_mask_text(res.rare.mask, mtext, sizeof mtext),
                   res.rare.nrare, res.rare.threads,
                   res.rare.restarts, b.secs);
        else
            printf("check OK: %s, n < %ld, %s %.2f s, brute %.2f s\n", R.code, N, m, res.secs, b.secs);
        return 0;
    }

    if (print) {
        for (long n = 0; n < N; n++) printf("%u%c", res.G[n], (n % 30 == 29 || n == N - 1) ? '\n' : ' ');
        return 0;
    }

    /* statistics */
    unsigned maxG = 0;
    long maxat = 0;
    for (long n = 0; n < N; n++)
        if (res.G[n] > maxG) { maxG = res.G[n]; maxat = n; }
    long *cnt = xcalloc(maxG + 1, sizeof *cnt);
    for (long n = 0; n < N; n++) cnt[res.G[n]]++;
    int nrec = 0;
    long *recn = xmalloc(sizeof(long) * (maxG + 2));
    unsigned *recv = xmalloc(sizeof(unsigned) * (maxG + 2));
    for (long n = 0, rec = -1; n < N; n++)
        if ((long)res.G[n] > rec) { rec = res.G[n]; recn[nrec] = n; recv[nrec++] = res.G[n]; }

    Period P = find_period(res.G, N, &R, 1);
    if (L > N / 3) L = (int)(N / 3);
    if (L < 1) L = 1;
    double *ac = autocorr(res.G, N, L);
    int peak = 1;
    double amax = 0;
    for (int p = 1; p <= L; p++) if (ac[p] > amax) amax = ac[p];
    for (int p = 1; p <= L; p++) if (ac[p] >= amax - 1e-9) { peak = p; break; }
    if (W <= 0) W = P.found && P.per <= 5000 ? (int)P.per : peak;
    if (W > 5000) W = 5000;

    char defdir[200];
    if (!dir) {
        snprintf(defdir, sizeof defdir, "out_%s", R.code);
        dir = defdir;
    }
    if (mkdir(dir, 0755) && errno != EEXIST) { perror(dir); return 1; }

    char path[4096];
    join(path, sizeof path, dir, "values.csv");
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return 1; }
    fprintf(f, "n,G\n");
    for (long n = 0; n < N; n++) fprintf(f, "%ld,%u\n", n, res.G[n]);
    fclose(f);

    join(path, sizeof path, dir, "values.png");    plot_values(res.G, N, maxG, path);
    long wsal = P.found && W == P.per ? P.saltus : 0;   /* saltus for the diff picture */
    join(path, sizeof path, dir, "wrap.png");      plot_wrap(res.G, N, W, 0, 0, path);
    join(path, sizeof path, dir, "wrapdiff.png");  plot_wrap(res.G, N, W, 1, wsal, path);
    join(path, sizeof path, dir, "autocorr.png");  plot_autocorr(ac, L, P.found ? (int)P.per : 0, peak, path);
    join(path, sizeof path, dir, "histogram.png"); plot_hist(cnt, maxG, path);
    write_html(dir, &R, &res, &P, maxG, maxat, W, peak, L, embed, recn, recv, nrec);

    /* summary */
    join(path, sizeof path, dir, "summary.txt");
    FILE *s = fopen(path, "w");
    if (!s) { perror(path); return 1; }
    FILE *outs[2] = {s, quiet ? NULL : stdout};
    for (int o = 0; o < 2; o++) {
        FILE *g = outs[o];
        if (!g) continue;
        fprintf(g, "%s\n", R.name);
        fprintf(g, "heap sizes      0 .. %ld\n", N - 1);
        if (res.method == METHOD_VULKAN || res.method == METHOD_ROCM)
            fprintf(g, "time            %.2f s (%s)\n", res.secs, method_name[res.method]);
        else if (res.method == METHOD_BLOCKED)
            fprintf(g, "time            %.2f s (blocked, %d threads)\n", res.secs, res.threads);
        else if (res.method == METHOD_RARE) {
            fprintf(g, "time            %.2f s (rare-value algorithm, %s, %d threads)\n", res.secs,
                    res.rare.active ? "active" : "inactive: no mask with few rare values", res.rare.threads);
            if (res.rare.active)
                fprintf(g, "rare values     %s, %ld rare heap sizes; pairs examined %lld (rare heaps) "
                        "+ %lld (witnesses), %lld full scans\n", rare_mask_text(res.rare.mask, mtext, sizeof mtext),
                        res.rare.nrare,
                        res.rare.scan1, res.rare.scan2, res.rare.full);
            if (res.rare.speculated)
                fprintf(g, "speculation     %lld values speculated (%lld with one byte per value) by a team of %d "
                        "(baton passing), %lld chunks failed the check (a new rare value)\n", res.rare.speculated,
                        res.rare.bytes, res.rare.baton, res.rare.restarts);
        }
        else
            fprintf(g, "time            %.2f s (%s)\n", res.secs, res.sparse ? "sparse-space" : "brute force");
        if (res.sparse)
            fprintf(g, "rare mask       0x%x, %ld rare heap sizes (%.3f%%)\n", res.mask, res.nrare,
                    100.0 * res.nrare / N);
        fprintf(g, "max value       %u at n = %ld\n", maxG, maxat);
        if (P.found && P.saltus && P.proven)
            fprintf(g, "period          %ld, saltus %ld, preperiod %ld (arithmetic; PROVED by Austin's "
                    "Theorem 6.8 with period %ld, saltus %ld)\n",
                    P.per, P.saltus, P.pre, P.proof_per, P.proof_saltus);
        else if (P.found && P.saltus)
            fprintf(g, "period          %ld, saltus %ld, preperiod %ld (arithmetic; apparent only, not proved)\n",
                    P.per, P.saltus, P.pre);
        else if (P.found)
            fprintf(g, "period          %ld, preperiod %ld (%s)\n", P.per, P.pre,
                    P.proven ? "PROVED by the periodicity theorem" : "apparent only, not proved");
        else
            fprintf(g, "period          none found\n");
        fprintf(g, "best lag        %d (%.4f of tail values repeat)\n", peak, amax);
        fprintf(g, "first values    ");
        for (long n = 0; n < N && n < 60; n++) fprintf(g, "%u ", res.G[n]);
        fprintf(g, "\nrecords         ");
        for (int i = 0; i < nrec; i++) fprintf(g, "%u@%ld ", recv[i], recn[i]);
        fprintf(g, "\n");
        if (g == s) {
            fprintf(g, "value frequencies (value: count)\n");
            for (unsigned v = 0; v <= maxG; v++)
                if (cnt[v]) fprintf(g, "  %u: %ld\n", v, cnt[v]);
        }
    }
    fclose(s);
    if (!quiet) printf("output          %s/index.html\n", dir);

    free(ac); free(cnt); free(recn); free(recv); free(res.G);
    return 0;
}
