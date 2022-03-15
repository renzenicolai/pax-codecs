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

// Helpers.
static bool png_decode_quickndirty(pax_buf_t *framebuffer, spng_ctx *ctx, pax_buf_type_t buf_type);

// Decodes a PNG file into a buffer with the specified type.
// Returns 1 on successful decode, refer to pax_last_error otherwise.
bool pax_decode_png(pax_buf_t *framebuffer, FILE *fd, pax_buf_type_t buf_type) {
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
	
	// Try a PNG decode.
	struct spng_ihdr ihdr;
	int err = spng_get_ihdr(ctx, &ihdr);
	if (err) goto error;
	
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
	uint32_t width  = ihdr.width;
	uint32_t height = ihdr.height;
	size_t n_pixels = width * height;
	
	// Fuck up the RGBA into ARGB.
	for (size_t i = 0; i < n_pixels; i++) {
		// Do a rotate right.
		outbuf[i] = (outbuf[i] >> 8) | (outbuf[i] << 24);
	}
	
	// Finally, return a buffer.
	pax_buf_init(framebuffer, outbuf, width, height, PAX_BUF_32_8888ARGB);
	framebuffer->do_free = true;
	
	return true;
	
	error:
	spng_ctx_free(ctx);
	if (outbuf) free(outbuf);
	return false;
}
