/**
 * GD5 acceptance test for the palette quantization / clone handover.
 *
 * A 5x3 truecolor image containing 14 opaque colors plus one fully
 * transparent marker (15 distinct pixels) is quantized to 8 palette
 * entries both with and without Floyd-Steinberg dithering.  The two
 * public quantization entry points, gdImageTrueColorToPalette() (the
 * in-place "preview/publish" path) and
 * gdImageCreatePaletteFromTrueColor() (the cloning "publish" path),
 * share one quantization body and must therefore produce the same
 * transparent index, the same palette order and the same pixels.
 *
 * Two guarantees are pinned here:
 *  - the transparent marker never becomes a nearest-distance candidate
 *    for ordinary (opaque) colors;
 *  - a clone of the quantized result owns independent palette and pixel
 *    storage, so mutating the clone must never reach the original image,
 *    and the transparent index is preserved by the clone.
 */

#include "gd.h"
#include "gdtest.h"

#define GD5_W 5
#define GD5_H 3
#define GD5_NOPAQUE 14
#define GD5_COLORS_WANTED 8

/* 14 distinct opaque colors filling the first 14 pixels of the 5x3
 * image; the last pixel is the transparent marker. */
static const int opaque_colors[GD5_NOPAQUE][3] = {
    {255, 0, 0},    {0, 255, 0},     {0, 0, 255},   {255, 255, 0},
    {255, 0, 255},  {0, 255, 255},   {255, 128, 0}, {128, 0, 255},
    {0, 128, 255},  {128, 255, 0},   {255, 0, 128}, {0, 255, 128},
    {64, 64, 64},   {192, 192, 192},
};

static gdImagePtr gd5_source(void) {
    gdImagePtr im = gdImageCreateTrueColor(GD5_W, GD5_H);
    int i, marker;

    if (!gdTestAssert(im != NULL)) {
        return NULL;
    }

    for (i = 0; i < GD5_NOPAQUE; i++) {
        int color =
            gdTrueColor(opaque_colors[i][0], opaque_colors[i][1],
                        opaque_colors[i][2]);
        gdImageSetPixel(im, i % GD5_W, i / GD5_W, color);
    }

    /* Transparent marker in the final pixel. */
    marker = gdTrueColor(10, 20, 30);
    gdImageSetPixel(im, GD5_W - 1, GD5_H - 1, marker);
    gdImageColorTransparent(im, marker);

    return im;
}

static void assert_same_quantization(gdImagePtr in_place,
                                     gdImagePtr created, const char *mode) {
    int i, x, y;

    gdTestAssertMsg(!gdImageTrueColor(in_place) && !gdImageTrueColor(created),
                    "%s: both results must be palette images", mode);

    gdTestAssertMsg(gdImageColorsTotal(in_place) ==
                        gdImageColorsTotal(created),
                    "%s: palette size must match (%d vs %d)", mode,
                    gdImageColorsTotal(in_place),
                    gdImageColorsTotal(created));

    gdTestAssertMsg(gdImageGetTransparent(in_place) ==
                        gdImageGetTransparent(created),
                    "%s: transparent index must match (%d vs %d)", mode,
                    gdImageGetTransparent(in_place),
                    gdImageGetTransparent(created));

    for (i = 0; i < gdImageColorsTotal(in_place); i++) {
        gdTestAssertMsg(in_place->red[i] == created->red[i],
                        "%s: red[%d] must match (%d vs %d)", mode, i,
                        in_place->red[i], created->red[i]);
        gdTestAssertMsg(in_place->green[i] == created->green[i],
                        "%s: green[%d] must match (%d vs %d)", mode, i,
                        in_place->green[i], created->green[i]);
        gdTestAssertMsg(in_place->blue[i] == created->blue[i],
                        "%s: blue[%d] must match (%d vs %d)", mode, i,
                        in_place->blue[i], created->blue[i]);
        gdTestAssertMsg(in_place->alpha[i] == created->alpha[i],
                        "%s: alpha[%d] must match (%d vs %d)", mode, i,
                        in_place->alpha[i], created->alpha[i]);
    }

    for (y = 0; y < GD5_H; y++) {
        for (x = 0; x < GD5_W; x++) {
            gdTestAssertMsg(
                gdImageGetPixel(in_place, x, y) ==
                    gdImageGetPixel(created, x, y),
                "%s: pixel (%d,%d) must match (%d vs %d)", mode, x, y,
                gdImageGetPixel(in_place, x, y),
                gdImageGetPixel(created, x, y));
        }
    }
}

static void assert_transparent_marker(gdImagePtr im, const char *mode) {
    int transparent = gdImageGetTransparent(im);
    int marker_pixel = gdImageGetPixel(im, GD5_W - 1, GD5_H - 1);
    int x, y, i;

    gdTestAssertMsg(transparent >= 0 && transparent < im->colorsTotal,
                    "%s: transparent index must be in palette, got %d", mode,
                    transparent);
    if (transparent < 0 || transparent >= im->colorsTotal) {
        return;
    }

    gdTestAssertMsg(im->alpha[transparent] == gdAlphaTransparent,
                    "%s: transparent entry must be fully transparent", mode);
    gdTestAssertMsg(marker_pixel == transparent,
                    "%s: marker pixel must map to transparent index %d, "
                    "got %d",
                    mode, transparent, marker_pixel);

    /* The marker occupies the reserved final palette entry and must not
     * be used by any ordinary pixel. */
    for (y = 0; y < GD5_H; y++) {
        for (x = 0; x < GD5_W; x++) {
            if (x == GD5_W - 1 && y == GD5_H - 1) {
                continue;
            }
            gdTestAssertMsg(gdImageGetPixel(im, x, y) != transparent,
                            "%s: opaque pixel (%d,%d) must not map to the "
                            "transparent index",
                            mode, x, y);
        }
    }

    /* The reserved transparent entry sits past the ordinary palette
     * entries, so it can never be a nearest-distance candidate. */
    gdTestAssertMsg(transparent == GD5_COLORS_WANTED,
                    "%s: transparent entry must be reserved at index %d, "
                    "got %d",
                    mode, GD5_COLORS_WANTED, transparent);
    for (i = 0; i < transparent; i++) {
        gdTestAssertMsg(im->alpha[i] != gdAlphaTransparent,
                        "%s: ordinary palette entry %d must not be fully "
                        "transparent",
                        mode, i);
    }
}

static void assert_clone_independence(gdImagePtr original,
                                      const char *mode) {
    gdImagePtr clone = gdImageClone(original);
    int saved_red0, saved_green1, saved_pixel, i, x, y;

    if (!gdTestAssertMsg(clone != NULL, "%s: clone must be created", mode)) {
        return;
    }

    gdTestAssertMsg(gdImageGetTransparent(clone) ==
                        gdImageGetTransparent(original),
                    "%s: clone must keep transparent index (%d vs %d)", mode,
                    gdImageGetTransparent(clone),
                    gdImageGetTransparent(original));

    /* Storage must be independent: distinct pixel rows and inline palette
     * arrays living in distinct image structs. */
    gdTestAssertMsg(clone->pixels != original->pixels,
                    "%s: pixel row pointer storage must be distinct", mode);
    for (i = 0; i < original->sy; i++) {
        gdTestAssertMsg(clone->pixels[i] != original->pixels[i],
                        "%s: pixel row %d storage must be distinct", mode, i);
    }
    gdTestAssertMsg(clone != original,
                    "%s: clone must be a distinct image struct", mode);

    saved_red0 = original->red[0];
    saved_green1 = original->green[1];
    saved_pixel = gdImageGetPixel(original, 0, 0);

    /* Mutate the clone only.  Palette channels are int arrays. */
    clone->red[0] += 99;
    clone->green[1] += 7;
    clone->blue[2] += 3;
    clone->alpha[3] =
        clone->alpha[3] == gdAlphaOpaque ? gdAlphaTransparent : gdAlphaOpaque;
    gdImageSetPixel(clone, 0, 0,
                    (saved_pixel + 1) % clone->colorsTotal);

    gdTestAssertMsg(original->red[0] == saved_red0,
                    "%s: original red[0] must be untouched after clone "
                    "mutation",
                    mode);
    gdTestAssertMsg(original->green[1] == saved_green1,
                    "%s: original green[1] must be untouched after clone "
                    "mutation",
                    mode);
    gdTestAssertMsg(gdImageGetPixel(original, 0, 0) == saved_pixel,
                    "%s: original pixel must be untouched after clone "
                    "mutation",
                    mode);

    for (y = 0; y < original->sy; y++) {
        for (x = 0; x < original->sx; x++) {
            if (x == 0 && y == 0) {
                continue;
            }
            gdTestAssertMsg(gdImageGetPixel(clone, x, y) ==
                                gdImageGetPixel(original, x, y),
                            "%s: untouched clone pixel (%d,%d) must match",
                            mode, x, y);
        }
    }

    gdImageDestroy(clone);
}

static void test_dither(enum gdPaletteQuantizationMethod method, int dither) {
    char mode[64];
    gdImagePtr in_place, created;

    snprintf(mode, sizeof(mode), "method=%d dither=%d", method, dither);

    in_place = gd5_source();
    created = gd5_source();
    if (!gdTestAssert(in_place != NULL && created != NULL)) {
        if (in_place) {
            gdImageDestroy(in_place);
        }
        if (created) {
            gdImageDestroy(created);
        }
        return;
    }

    /* Cloning a truecolor source must also preserve its transparent
     * marker; that marker drives the transparent palette entry later. */
    {
        gdImagePtr src_clone = gdImageClone(in_place);
        if (gdTestAssertMsg(src_clone != NULL,
                            "%s: truecolor clone must be created", mode)) {
            gdTestAssertMsg(
                gdImageGetTransparent(src_clone) ==
                    gdImageGetTransparent(in_place),
                "%s: truecolor clone must keep transparent marker (%d vs %d)",
                mode, gdImageGetTransparent(src_clone),
                gdImageGetTransparent(in_place));
            gdImageDestroy(src_clone);
        }
    }

    gdTestAssert(gdImageTrueColorToPaletteSetMethod(in_place, method, 0));
    gdTestAssert(gdImageTrueColorToPaletteSetMethod(created, method, 0));

    gdTestAssertMsg(
        gdImageTrueColorToPalette(in_place, dither, GD5_COLORS_WANTED),
        "%s: in-place quantization must succeed", mode);
    created = gdImageCreatePaletteFromTrueColor(created, dither,
                                                GD5_COLORS_WANTED);
    if (!gdTestAssertMsg(created != NULL,
                         "%s: create-palette quantization must succeed",
                         mode)) {
        gdImageDestroy(in_place);
        return;
    }

    assert_same_quantization(in_place, created, mode);
    assert_transparent_marker(in_place, mode);
    assert_transparent_marker(created, mode);
    assert_clone_independence(created, mode);

    gdImageDestroy(created);
    gdImageDestroy(in_place);
}

/* Toggling dithering may move pixels, but it must not reorder or replace
 * the palette (the reported symptom: unchecking dither changed colors and
 * transparency). */
static void test_palette_stable_across_dither(
    enum gdPaletteQuantizationMethod method) {
    char mode[64];
    gdImagePtr no_dither, with_dither;
    int i;

    snprintf(mode, sizeof(mode), "method=%d dither-stability", method);

    no_dither = gd5_source();
    with_dither = gd5_source();
    if (!gdTestAssert(no_dither != NULL && with_dither != NULL)) {
        if (no_dither) {
            gdImageDestroy(no_dither);
        }
        if (with_dither) {
            gdImageDestroy(with_dither);
        }
        return;
    }

    gdTestAssert(gdImageTrueColorToPaletteSetMethod(no_dither, method, 0));
    gdTestAssert(gdImageTrueColorToPaletteSetMethod(with_dither, method, 0));
    gdTestAssert(
        gdImageTrueColorToPalette(no_dither, 0, GD5_COLORS_WANTED));
    gdTestAssert(
        gdImageTrueColorToPalette(with_dither, 1, GD5_COLORS_WANTED));

    gdTestAssertMsg(
        gdImageColorsTotal(no_dither) == gdImageColorsTotal(with_dither),
        "%s: palette size must not change with dither (%d vs %d)", mode,
        gdImageColorsTotal(no_dither), gdImageColorsTotal(with_dither));
    gdTestAssertMsg(
        gdImageGetTransparent(no_dither) ==
            gdImageGetTransparent(with_dither),
        "%s: transparent index must not change with dither (%d vs %d)", mode,
        gdImageGetTransparent(no_dither),
        gdImageGetTransparent(with_dither));
    for (i = 0; i < gdImageColorsTotal(no_dither); i++) {
        gdTestAssertMsg(
            no_dither->red[i] == with_dither->red[i] &&
                no_dither->green[i] == with_dither->green[i] &&
                no_dither->blue[i] == with_dither->blue[i] &&
                no_dither->alpha[i] == with_dither->alpha[i],
            "%s: palette entry %d must not change with dither", mode, i);
    }

    gdImageDestroy(with_dither);
    gdImageDestroy(no_dither);
}

int main(void) {
    gdSetErrorMethod(gdSilence);

    test_dither(GD_QUANT_JQUANT, 0);
    test_dither(GD_QUANT_JQUANT, 1);
    test_dither(GD_QUANT_NEUQUANT, 0);
    test_dither(GD_QUANT_NEUQUANT, 1);
    test_dither(GD_QUANT_DEFAULT, 0);
    test_dither(GD_QUANT_DEFAULT, 1);
    test_palette_stable_across_dither(GD_QUANT_JQUANT);
    test_palette_stable_across_dither(GD_QUANT_NEUQUANT);
    test_palette_stable_across_dither(GD_QUANT_DEFAULT);
#ifdef HAVE_LIBIMAGEQUANT
    test_dither(GD_QUANT_LIQ, 0);
    test_dither(GD_QUANT_LIQ, 1);
    /* libimagequant is an external, non-deterministic quantizer, so the
     * cross-dither palette stability guarantees only cover the built-in
     * deterministic methods above. */
#endif

    gdClearErrorMethod();

    return gdNumFailures();
}
