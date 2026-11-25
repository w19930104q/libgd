#include "gd.h"
#include "gdtest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Regression test for animated GIF read/write consistency, built around the
 * GD4 scenario:
 *   4x2 logical screen, three frames with delays 12/8/20 centiseconds;
 *   frame 1 only covers the right two columns and uses restore-background
 *   disposal.  After a read-immediately-rewrite cycle the frame count,
 *   delays, geometry, disposal semantics and composited canvas state must
 *   all be preserved.
 */

#define SCREEN_W 4
#define SCREEN_H 2

#define IDX_WHITE 0
#define IDX_RED   1
#define IDX_GREEN 2
#define IDX_BLUE  3

static const int EXPECTED_DELAY[3] = {12, 8, 20};
static const int EXPECTED_DISPOSAL[3] = {
	gdDisposalUnknown,
	gdDisposalRestoreBackground,
	gdDisposalUnknown
};

static gdImagePtr make_palette_frame(int width, int height) {
	gdImagePtr im = gdImageCreate(width, height);

	if (im == NULL) {
		return NULL;
	}
	gdImageColorAllocate(im, 255, 255, 255);
	gdImageColorAllocate(im, 255, 0, 0);
	gdImageColorAllocate(im, 0, 200, 0);
	gdImageColorAllocate(im, 0, 0, 255);
	return im;
}

/* Build the GD4 sample with the writer itself, using verbatim encoding for
 * the sub-rectangle frame (no previm for it). */
static char *build_gd4_gif(void) {
	char *file;
	FILE *fp;
	gdImagePtr frame0, frame1, frame2;
	int x, y;

	file = gdTestTempFile("animated_gif_roundtrip_gd4.gif");
	if (file == NULL) {
		return NULL;
	}
	fp = fopen(file, "wb");
	if (fp == NULL) {
		free(file);
		return NULL;
	}

	frame0 = make_palette_frame(SCREEN_W, SCREEN_H);
	frame1 = make_palette_frame(2, SCREEN_H);
	frame2 = make_palette_frame(SCREEN_W, SCREEN_H);
	if (!gdTestAssert(frame0 != NULL && frame1 != NULL && frame2 != NULL)) {
		goto cleanup;
	}

	for (y = 0; y < SCREEN_H; y++) {
		for (x = 0; x < SCREEN_W; x++) {
			gdImageSetPixel(frame0, x, y, IDX_RED);
			gdImageSetPixel(frame2, x, y, IDX_BLUE);
		}
	}
	for (y = 0; y < SCREEN_H; y++) {
		for (x = 0; x < 2; x++) {
			gdImageSetPixel(frame1, x, y, IDX_GREEN);
		}
	}

	/* Global color map, no Netscape looping extension. */
	gdImageGifAnimBegin(frame0, fp, 1, -1);
	gdImageGifAnimAdd(frame0, fp, 0, 0, 0, 12, gdDisposalUnknown, NULL);
	gdImageGifAnimAdd(frame1, fp, 0, 2, 0, 8,
	                  gdDisposalRestoreBackground, NULL);
	gdImageGifAnimAdd(frame2, fp, 0, 0, 0, 20, gdDisposalUnknown, NULL);
	gdImageGifAnimEnd(fp);

cleanup:
	if (frame0 != NULL) {
		gdImageDestroy(frame0);
	}
	if (frame1 != NULL) {
		gdImageDestroy(frame1);
	}
	if (frame2 != NULL) {
		gdImageDestroy(frame2);
	}
	fclose(fp);
	return file;
}

static int truecolor_pixel_is(gdImagePtr im, int x, int y, int r, int g,
                              int b) {
	int c = gdImageGetPixel(im, x, y);

	return gdTrueColorGetRed(c) == r && gdTrueColorGetGreen(c) == g &&
	       gdTrueColorGetBlue(c) == b;
}

static void assert_raw_metadata(const char *path) {
	FILE *fp = fopen(path, "rb");
	gdGifReadPtr gif;
	gdGifInfo info;
	gdGifFrameInfo fi;
	gdImagePtr frame = NULL;
	int count = 0;
	static const int expected_w[3] = {4, 2, 4};
	static const int expected_x[3] = {0, 2, 0};

	gdTestAssert(fp != NULL);
	if (fp == NULL) {
		return;
	}
	gif = gdGifReadOpen(fp);
	gdTestAssert(gif != NULL);
	if (gif == NULL) {
		fclose(fp);
		return;
	}

	gdTestAssert(gdGifReadGetInfo(gif, &info) == 1);
	gdTestAssertMsg(info.width == SCREEN_W && info.height == SCREEN_H,
	                "logical screen must stay %dx%d", SCREEN_W, SCREEN_H);
	gdTestAssertMsg(info.loop_count_present == 0,
	                "absent Netscape extension must not be invented");

	while (gdGifReadNextFrame(gif, &fi, &frame) == 1) {
		gdTestAssertMsg(count < 3, "expected exactly three frames");
		if (count >= 3) {
			gdImageDestroy(frame);
			break;
		}
		gdTestAssertMsg(fi.frame_index == count, "frame order changed");
		gdTestAssertMsg(fi.delay == EXPECTED_DELAY[count],
		                "frame %d delay changed: got %d want %d", count,
		                fi.delay, EXPECTED_DELAY[count]);
		gdTestAssertMsg(fi.disposal == EXPECTED_DISPOSAL[count],
		                "frame %d disposal changed: got %d want %d", count,
		                fi.disposal, EXPECTED_DISPOSAL[count]);
		gdTestAssertMsg(fi.x == expected_x[count] && fi.y == 0,
		                "frame %d offset changed: (%d,%d)", count, fi.x,
		                fi.y);
		gdTestAssertMsg(fi.width == expected_w[count] &&
		                    fi.height == SCREEN_H,
		                "frame %d geometry changed: %dx%d", count, fi.width,
		                fi.height);
		gdImageDestroy(frame);
		frame = NULL;
		count++;
	}
	gdTestAssertMsg(count == 3, "expected 3 frames, got %d", count);
	gdGifReadClose(gif);
	fclose(fp);
}

static void assert_composited_frames(const char *path) {
	FILE *fp = fopen(path, "rb");
	gdGifReadPtr gif;
	gdGifFrameInfo fi;
	gdImagePtr images[3] = {0};
	int count = 0, x, y;

	gdTestAssert(fp != NULL);
	if (fp == NULL) {
		return;
	}
	gif = gdGifReadOpen(fp);
	gdTestAssert(gif != NULL);
	if (gif == NULL) {
		fclose(fp);
		return;
	}

	while (count < 3 &&
	       gdGifReadNextImage(gif, &fi, &images[count]) == 1) {
		gdTestAssertMsg(images[count]->sx == SCREEN_W &&
		                    images[count]->sy == SCREEN_H,
		                "composited frame %d must use logical screen size",
		                count);
		count++;
	}
	gdTestAssertMsg(count == 3, "expected 3 composited frames, got %d",
	                count);

	if (count == 3) {
		/* Frame 0: red fills the whole screen. */
		for (y = 0; y < SCREEN_H; y++) {
			for (x = 0; x < SCREEN_W; x++) {
				gdTestAssertMsg(
				    truecolor_pixel_is(images[0], x, y, 255, 0, 0),
				    "frame 0 pixel (%d,%d) should be red", x, y);
			}
		}
		/* Frame 1: red left half, green right half only. */
		for (y = 0; y < SCREEN_H; y++) {
			gdTestAssert(truecolor_pixel_is(images[1], 0, y, 255, 0, 0));
			gdTestAssert(truecolor_pixel_is(images[1], 1, y, 255, 0, 0));
			gdTestAssert(truecolor_pixel_is(images[1], 2, y, 0, 200, 0));
			gdTestAssert(truecolor_pixel_is(images[1], 3, y, 0, 200, 0));
		}
		/* Frame 2: restore-background must have removed the temporary
		 * green pixels; blue fills the whole screen with no ghost. */
		for (y = 0; y < SCREEN_H; y++) {
			for (x = 0; x < SCREEN_W; x++) {
				gdTestAssertMsg(
				    truecolor_pixel_is(images[2], x, y, 0, 0, 255),
				    "frame 2 pixel (%d,%d) should be blue, no frame 1 "
				    "residue",
				    x, y);
			}
		}
	}

	for (x = 0; x < count; x++) {
		if (images[x] != NULL) {
			gdImageDestroy(images[x]);
		}
	}
	gdGifReadClose(gif);
	fclose(fp);
}

/* Raw-frame consumer: sub-rectangle palette frames are re-encoded with the
 * writer's previm optimization available, mirroring a messaging client that
 * generates a preview while streaming the source. */
static char *rewrite_raw_frames(const char *src) {
	FILE *in = fopen(src, "rb");
	FILE *out;
	gdGifReadPtr gif;
	gdGifInfo info;
	gdGifFrameInfo fi;
	gdImagePtr frame = NULL, prev = NULL, screen;
	char *dst;
	int n = 0;

	if (!gdTestAssert(in != NULL)) {
		return NULL;
	}
	dst = gdTestTempFile("animated_gif_roundtrip_raw.gif");
	if (dst == NULL) {
		fclose(in);
		return NULL;
	}
	out = fopen(dst, "wb");
	if (out == NULL) {
		fclose(in);
		free(dst);
		return NULL;
	}

	gif = gdGifReadOpen(in);
	gdTestAssert(gif != NULL);
	if (gif == NULL) {
		fclose(out);
		fclose(in);
		free(dst);
		return NULL;
	}
	gdGifReadGetInfo(gif, &info);

	screen = make_palette_frame(info.width, info.height);
	gdImageGifAnimBegin(screen, out, 1, -1);

	while (gdGifReadNextFrame(gif, &fi, &frame) == 1) {
		gdImageGifAnimAdd(frame, out, 0, fi.x, fi.y, fi.delay, fi.disposal,
		                  n == 0 ? NULL : prev);
		if (prev != NULL) {
			gdImageDestroy(prev);
		}
		prev = frame;
		n++;
	}
	gdImageGifAnimEnd(out);
	gdTestAssertMsg(n == 3, "raw rewrite must keep 3 frames, got %d", n);

	if (prev != NULL) {
		gdImageDestroy(prev);
	}
	gdImageDestroy(screen);
	gdGifReadClose(gif);
	fclose(out);
	fclose(in);
	return dst;
}

/* Composited consumer: full-canvas true-color frames re-encoded with previm.
 * Quantization gives each frame its own palette, so local color tables are
 * required and the frames must remain full canvas in order. */
static char *rewrite_composited_frames(const char *src) {
	FILE *in = fopen(src, "rb");
	FILE *out;
	gdGifReadPtr gif;
	gdGifFrameInfo fi;
	gdImagePtr im = NULL, prev = NULL;
	char *dst;
	int n = 0;
	int delays[3] = {0, 0, 0};

	if (!gdTestAssert(in != NULL)) {
		return NULL;
	}
	dst = gdTestTempFile("animated_gif_roundtrip_comp.gif");
	if (dst == NULL) {
		fclose(in);
		return NULL;
	}
	out = fopen(dst, "wb");
	if (out == NULL) {
		fclose(in);
		free(dst);
		return NULL;
	}

	gif = gdGifReadOpen(in);
	gdTestAssert(gif != NULL);
	if (gif == NULL) {
		fclose(out);
		fclose(in);
		free(dst);
		return NULL;
	}

	while (n < 3 && gdGifReadNextImage(gif, &fi, &im) == 1) {
		if (n == 0) {
			gdImagePtr header = gdImageCreate(im->sx, im->sy);
			gdImageColorAllocate(header, 255, 255, 255);
			gdImageGifAnimBegin(header, out, 1, -1);
			gdImageDestroy(header);
		}
		gdImageGifAnimAdd(im, out, 0, 0, 0, fi.delay, fi.disposal,
		                  n == 0 ? NULL : prev);
		delays[n] = fi.delay;
		if (prev != NULL) {
			gdImageDestroy(prev);
		}
		prev = im;
		n++;
	}
	gdImageGifAnimEnd(out);
	gdTestAssertMsg(n == 3, "composited rewrite must keep 3 frames, got %d",
	                n);
	gdTestAssertMsg(delays[0] == 12 && delays[1] == 8 && delays[2] == 20,
	                "composited rewrite must preserve delays");

	if (prev != NULL) {
		gdImageDestroy(prev);
	}
	gdGifReadClose(gif);
	fclose(out);
	fclose(in);
	return dst;
}

static void assert_composited_rewrite_rendering(const char *path) {
	FILE *fp = fopen(path, "rb");
	gdGifReadPtr gif;
	gdGifFrameInfo fi;
	gdImagePtr images[3] = {0};
	int count = 0, x, y;
	int uniform;
	int first;

	gdTestAssert(fp != NULL);
	if (fp == NULL) {
		return;
	}
	gif = gdGifReadOpen(fp);
	gdTestAssert(gif != NULL);
	if (gif == NULL) {
		fclose(fp);
		return;
	}
	while (count < 3 &&
	       gdGifReadNextImage(gif, &fi, &images[count]) == 1) {
		gdTestAssertMsg(fi.width == SCREEN_W && fi.height == SCREEN_H &&
		                    fi.x == 0 && fi.y == 0,
		                "composited rewrite frame %d lost full-canvas "
		                "geometry",
		                count);
		count++;
	}
	gdTestAssertMsg(count == 3, "expected 3 rewritten frames, got %d",
	                count);

	if (count == 3) {
		/* Frame 1 still splits left/right into distinct colors. */
		for (y = 0; y < SCREEN_H; y++) {
			gdTestAssertMsg(
			    gdImageGetPixel(images[1], 0, y) !=
			        gdImageGetPixel(images[1], 2, y),
			    "frame 1 should still distinguish left and right halves");
		}
		/* Frame 2 must be uniform: no residue from frame 1. */
		first = gdImageGetPixel(images[2], 0, 0);
		uniform = 1;
		for (y = 0; y < SCREEN_H && uniform; y++) {
			for (x = 0; x < SCREEN_W; x++) {
				if (gdImageGetPixel(images[2], x, y) != first) {
					uniform = 0;
					break;
				}
			}
		}
		gdTestAssertMsg(uniform,
		                "frame 2 must be uniform after restore-background");
	}

	for (x = 0; x < count; x++) {
		if (images[x] != NULL) {
			gdImageDestroy(images[x]);
		}
	}
	gdGifReadClose(gif);
	fclose(fp);
}

/* Reserved/out-of-range disposal codes must be normalized to encodable GIF
 * disposal methods rather than written verbatim. */
static void test_disposal_normalization(void) {
	char *file = gdTestTempFile("animated_gif_roundtrip_disp.gif");
	FILE *fp;
	gdImagePtr im;
	gdGifReadPtr gif;
	gdGifFrameInfo fi;
	gdImagePtr frame;
	int disposals[3];
	int n = 0;

	gdTestAssert(file != NULL);
	if (file == NULL) {
		return;
	}
	fp = fopen(file, "wb");
	gdTestAssert(fp != NULL);
	if (fp == NULL) {
		free(file);
		return;
	}

	im = make_palette_frame(2, 1);
	gdImageGifAnimBegin(im, fp, 1, -1);
	/* Valid restore-previous, reserved code 7, negative garbage. */
	gdImageGifAnimAdd(im, fp, 0, 0, 0, 5,
	                  gdDisposalRestorePrevious, NULL);
	gdImageGifAnimAdd(im, fp, 0, 0, 0, 5, 7, NULL);
	gdImageGifAnimAdd(im, fp, 0, 0, 0, 5, -1, NULL);
	gdImageGifAnimEnd(fp);
	fclose(fp);
	gdImageDestroy(im);

	fp = fopen(file, "rb");
	gif = gdGifReadOpen(fp);
	gdTestAssert(gif != NULL);
	if (gif == NULL) {
		fclose(fp);
		free(file);
		return;
	}
	while (n < 3 && gdGifReadNextFrame(gif, &fi, &frame) == 1) {
		disposals[n++] = fi.disposal;
		gdImageDestroy(frame);
	}
	gdGifReadClose(gif);
	fclose(fp);

	gdTestAssertMsg(n == 3, "expected 3 frames, got %d", n);
	if (n == 3) {
		gdTestAssertMsg(disposals[0] == gdDisposalRestorePrevious,
		                "valid disposal must pass through");
		gdTestAssertMsg(disposals[1] == gdDisposalNone,
		                "reserved disposal 7 must normalize to none");
		gdTestAssertMsg(disposals[2] == gdDisposalNone,
		                "negative disposal must normalize to none");
	}
	free(file);
}

int main(void) {
	char *source, *rawRewrite, *compositedRewrite;

	source = build_gd4_gif();
	if (!gdTestAssert(source != NULL)) {
		return gdNumFailures();
	}

	assert_raw_metadata(source);
	assert_composited_frames(source);

	rawRewrite = rewrite_raw_frames(source);
	if (gdTestAssert(rawRewrite != NULL)) {
		assert_raw_metadata(rawRewrite);
		assert_composited_frames(rawRewrite);
		free(rawRewrite);
	}

	compositedRewrite = rewrite_composited_frames(source);
	if (gdTestAssert(compositedRewrite != NULL)) {
		assert_composited_rewrite_rendering(compositedRewrite);
		free(compositedRewrite);
	}

	test_disposal_normalization();

	free(source);
	return gdNumFailures();
}
