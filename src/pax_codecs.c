/*
	MIT License

	Copyright (c) 2022 Julian Scheffers

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.
*/

#include "pax_codecs.h"
#include "pax_internal.h"
#include "real_file.h"
#include "spng.h"

static bool png_decode_quickndirty(pax_buf_t *framebuffer, spng_ctx *ctx, pax_buf_type_t buf_type);
static bool png_decode_layered(pax_buf_t *framebuffer, spng_ctx *ctx, pax_buf_type_t buf_type);

// Decodes a PNG file into a buffer with the specified type.
// Returns 1 on successful decode, refer to pax_last_error otherwise.
bool pax_decode_png_fd(pax_buf_t *framebuffer, FILE *fd, pax_buf_type_t buf_type) {
	spng_ctx *ctx = spng_ctx_new(0);
	int err = spng_set_png_file(ctx, fd);
	if (err) {
		spng_ctx_free(ctx);
		return false;
	}
	return png_decode_quickndirty(framebuffer, ctx, buf_type);
}

// Decodes a PNG buffer into a PAX buffer with the specified type.
// Returns 1 on successful decode, refer to pax_last_error otherwise.
bool pax_decode_png_buf(pax_buf_t *framebuffer, void *buf, size_t buf_len, pax_buf_type_t buf_type) {
	spng_ctx *ctx = spng_ctx_new(0);
	int err = spng_set_png_buffer(ctx, buf, buf_len);
	if (err) {
		spng_ctx_free(ctx);
		return false;
	}
	return png_decode_quickndirty(framebuffer, ctx, buf_type);
}

// A quick'n'dirty approach to PNG decode.
// TODO: Replace with a more memory-conservative, more versatile approach.
static bool png_decode_quickndirty(pax_buf_t *framebuffer, spng_ctx *ctx, pax_buf_type_t buf_type) {
	pax_col_t *outbuf = NULL;
	framebuffer->width  = 0;
	framebuffer->height = 0;
	
	// Try a PNG decode.
	struct spng_ihdr ihdr;
	int err = spng_get_ihdr(ctx, &ihdr);
	if (err) goto error;
	uint32_t width      = ihdr.width;
	uint32_t height     = ihdr.height;
	framebuffer->width  = width;
	framebuffer->height = height;
	size_t n_pixels     = width * height;
	
	enum spng_format png_fmt = SPNG_FMT_RGBA8;
	
	// Get image dims.
	size_t size;
	err = spng_decoded_image_size(ctx, png_fmt, &size);
	if (err) goto error;
	outbuf = malloc(size);
	
	// Decode it.
	err = spng_decode_image(ctx, outbuf, size, png_fmt, 0);
	if (err) goto error;
	spng_ctx_free(ctx);
	
	// Finally, return a buffer.
	pax_buf_init(framebuffer, outbuf, width, height, PAX_BUF_32_8888ARGB);
	framebuffer->do_free = true;
	
	return true;
	
	error:
	spng_ctx_free(ctx);
	if (outbuf) free(outbuf);
	return false;
}

// A WIP decode inator.
static bool png_decode_layered(pax_buf_t *framebuffer, spng_ctx *ctx, pax_buf_type_t buf_type) {
	pax_col_t *outbuf = NULL;
	framebuffer->width  = 0;
	framebuffer->height = 0;
	
	// Try a PNG decode.
	struct spng_ihdr ihdr;
	int err = spng_get_ihdr(ctx, &ihdr);
	if (err) goto error;
	uint32_t width      = ihdr.width;
	uint32_t height     = ihdr.height;
	framebuffer->width  = width;
	framebuffer->height = height;
	size_t n_pixels     = width * height;
	
	// Select a good buffer type.
	if (PAX_IS_PALETTE(buf_type) && ihdr.color_type != 3) {
		// This is not a palleted image, change the output type.
		int bpp = PAX_GET_BPP(buf_type);
		if (bpp == 1) {
			// For 1BPP, the only option is greyscale.
			buf_type = PAX_BUF_1_GREY;
		} else if (bpp == 2) {
			// For 2BPP, the only option is also greyscale.
			buf_type = PAX_BUF_2_PAL;
		} else if (bpp == 4) {
			if ((ihdr.color_type & 4) || (ihdr.color_type & 2)) {
				// With alpha and/or color.
				buf_type = PAX_BUF_4_1111ARGB;
			} else {
				// Greyscale.
				buf_type = PAX_BUF_4_GREY;
			}
		} else if (bpp == 8) {
			if (ihdr.color_type & 4) {
				// With alpha and/or color.
				buf_type = PAX_BUF_8_2222ARGB;
			} else if (ihdr.color_type & 2) {
				// With color.
				buf_type = PAX_BUF_8_332RGB;
			} else {
				// Greyscale.
				buf_type = PAX_BUF_8_GREY;
			}
		} else {
			if (ihdr.color_type & 4) {
				// With alpha and/or color.
				buf_type = PAX_BUF_16_4444ARGB;
			} else if (ihdr.color_type & 2) {
				// With color.
				buf_type = PAX_BUF_16_565RGB;
			} else {
				// Greyscale.
				buf_type = PAX_BUF_8_GREY;
			}
		}
	}
	
	// Set the image to decode progressive.
	spng_decode_image(ctx, NULL, 0, SPNG_FMT_RAW, SPNG_DECODE_PROGRESSIVE);
	
	return true;
	
	error:
	spng_ctx_free(ctx);
	if (outbuf) free(outbuf);
	return false;
}
