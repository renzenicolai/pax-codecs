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
#include "spng.h"

static bool png_decode(pax_buf_t *framebuffer, spng_ctx *ctx, pax_buf_type_t buf_type, int flags);
static bool png_decode_progressive(pax_buf_t *framebuffer, spng_ctx *ctx, struct spng_ihdr ihdr, pax_buf_type_t buf_type, int dx, int dy);

// Decodes a PNG file into a buffer with the specified type.
// Returns 1 on successful decode, refer to pax_last_error otherwise.
bool pax_decode_png_fd(pax_buf_t *framebuffer, FILE *fd, pax_buf_type_t buf_type, int flags) {
	spng_ctx *ctx = spng_ctx_new(0);
	int err = spng_set_png_file(ctx, fd);
	if (err) {
		spng_ctx_free(ctx);
		return false;
	}
	bool ret = png_decode(framebuffer, ctx, buf_type, flags);
	spng_ctx_free(ctx);
	return ret;
}

// Decodes a PNG buffer into a PAX buffer with the specified type.
// Returns 1 on successful decode, refer to pax_last_error otherwise.
bool pax_decode_png_buf(pax_buf_t *framebuffer, void *buf, size_t buf_len, pax_buf_type_t buf_type, int flags) {
	spng_ctx *ctx = spng_ctx_new(0);
	int err = spng_set_png_buffer(ctx, buf, buf_len);
	if (err) {
		spng_ctx_free(ctx);
		return false;
	}
	bool ret = png_decode(framebuffer, ctx, buf_type, flags);
	spng_ctx_free(ctx);
	return ret;
}

// A generic wrapper for decoding PNGs.
// Sets up the framebuffer if required.
static bool png_decode(pax_buf_t *framebuffer, spng_ctx *ctx, pax_buf_type_t buf_type, int flags) {
	bool do_alloc = !(flags & CODEC_FLAG_EXISTING);
	if (do_alloc) {
		framebuffer->width  = 0;
		framebuffer->height = 0;
	}
	
	// Fetch the IHDR.
	struct spng_ihdr ihdr;
	int err = spng_get_ihdr(ctx, &ihdr);
	if (err) {
		ESP_LOGE(TAG, "Failed at spng_get_ihdr");
		ESP_LOGE(TAG, "PNG decode error %d: %s", err, spng_strerror(err));
		return false;
	}
	uint32_t width      = ihdr.width;
	uint32_t height     = ihdr.height;
	if (do_alloc) {
		framebuffer->width  = width;
		framebuffer->height = height;
	}
	
	// Select a good buffer type.
	if (do_alloc && PAX_IS_PALETTE(buf_type) && ihdr.color_type != 3) {
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
		ESP_LOGW(TAG, "Changing buffer type to %08x", buf_type);
	}
	
	// Determine whether to allocate a buffer.
	if (do_alloc) {
		// Allocate some funny.
		ESP_LOGI(TAG, "Decoding PNG %dx%d to %08x", width, height, buf_type);
		pax_buf_init(framebuffer, NULL, width, height, buf_type);
		if (pax_last_error) return false;
	}
	
	// Decd.
	if (!png_decode_progressive(framebuffer, ctx, ihdr, buf_type, 0, 0)) {
		goto error;
	}
	
	// Success.
	return true;
	
	error:
	if (do_alloc) {
		// Clean up in case of erruer.
		pax_buf_destroy(framebuffer);
	}
	return false;
}

// A WIP decode inator.
static bool png_decode_progressive(pax_buf_t *framebuffer, spng_ctx *ctx, struct spng_ihdr ihdr, pax_buf_type_t buf_type, int x_offset, int y_offset) {
	int err = 0;
	uint8_t          *row  = NULL;
	struct spng_plte *plte = NULL;
	struct spng_trns *trns = NULL;
	
	// Get image parameters.
	uint32_t width    = ihdr.width;
	uint32_t height   = ihdr.height;
	
	// Reduce 16pbc back to 8pbc.
	int png_fmt;
	int bits_per_pixel;
	uint32_t channel_mask;
	uint_fast8_t shift_max = 0;
	switch (ihdr.color_type) {
		case 0:
			// Greyscale.
			png_fmt = SPNG_FMT_G8;
			bits_per_pixel = 1 * 8;
			channel_mask = 0x000000ff;
			break;
		case 2:
			// RGB.
			png_fmt = SPNG_FMT_RGB8;
			bits_per_pixel = 3 * 8;
			channel_mask = 0x00ffffff;
			break;
		case 3:
			// Palette.
			png_fmt = SPNG_FMT_RAW;
			bits_per_pixel = 1 * ihdr.bit_depth;
			channel_mask = (1 << bits_per_pixel) - 1;
			shift_max = 8 - ihdr.bit_depth;
			break;
		case 4:
			// Greyscale and alpha.
			png_fmt = SPNG_FMT_GA8;
			bits_per_pixel = 2 * 8;
			channel_mask = 0x0000ffff;
			break;
		case 6:
		default:
			// RGBA.
			png_fmt = SPNG_FMT_RGBA8;
			bits_per_pixel = 4 * 8;
			channel_mask = 0xffffffff;
			break;
	}
	ESP_LOGI(TAG, "PNG FMT %d", png_fmt);
	
	// Get the size for the fancy buffer.
	size_t   decd_len = 0;
	err = spng_decoded_image_size(ctx, png_fmt, &decd_len);
	if (err) {
		ESP_LOGE(TAG, "Failed at spng_decoded_image_size");
		goto error;
	}
	size_t   row_size = decd_len / height;
	row = malloc(row_size);
	err = spng_decode_chunks(ctx);
	if (err) {
		ESP_LOGE(TAG, "Failed at spng_decode_chunks (1)");
		goto error;
	}
	
	// Get the palette, if any.
	bool has_palette = ihdr.color_type == 3;
	bool has_trns    = has_palette;
	plte = malloc(sizeof(struct spng_plte));
	trns = malloc(sizeof(struct spng_trns));
	if (has_palette) {
		// Color part of palette.
		err = spng_get_plte(ctx, plte);
		if (err && err != SPNG_ECHUNKAVAIL) goto error;
		
		// Alpha part of palette.
		err = spng_get_trns(ctx, trns);
		if (err == SPNG_ECHUNKAVAIL) has_trns = false;
		else if (err) goto error;
	}
	
	// Set the image to decode progressive.
	err = spng_decode_image(ctx, NULL, 0, png_fmt, SPNG_DECODE_PROGRESSIVE);
	if (err) {
		ESP_LOGE(TAG, "Failed at spng_decode_image");
		goto error;
	}
	
	// Decoding time!
	struct spng_row_info info;
	while (1) {
		// Get row metadata.
		err = spng_get_row_info(ctx, &info);
		if (err && err != SPNG_EOI) goto error;
		
		// Decode a row's data.
		err = spng_decode_scanline(ctx, row, row_size);
		if (err && err != SPNG_EOI) goto error;
		
		// Have it sharted out.
		int    dx = 1;
		size_t offset = 0;
		int x = 0;
		for (; x < width; x += dx) {
			// Get the raw data.
			size_t address = row + (offset / 8);
			// A slightly complicated bit extraction.
			uint32_t raw = channel_mask & (*(uint32_t *) address >> (shift_max - offset % 8));
			// Fix endianness.
			if (bits_per_pixel == 16) raw = (raw << 8) | (raw >> 8);
			else if (bits_per_pixel == 24) raw = (raw << 16) | (raw >> 16);
			else if (bits_per_pixel == 32) raw = (raw << 24) | ((raw << 8) & 0x00ff0000) | ((raw >> 8) & 0x0000ff00) | (raw >> 24);
			offset += bits_per_pixel * dx;
			
			// Decode color information.
			pax_col_t color = 0;
			if (has_palette && PAX_IS_PALETTE(buf_type)) {
				color = raw;
			} else if (has_palette) {
				if (raw >= plte->n_entries) raw = 0;
				// Alpha palette.
				if (has_trns && raw < trns->n_type3_entries) {
					color = trns->type3_alpha[raw] << 24;
				} else {
					color = 0xff000000;
				}
				// Non-alpha palette.
				struct spng_plte_entry entry = plte->entries[raw];
				color |= (entry.red << 16) | (entry.green << 8) | entry.blue;
			} else if (ihdr.color_type == 0) {
				// Greyscale.
				color = 0xff000000 | (raw * 0x010101);
			} else if (ihdr.color_type == 2) {
				// RGB.
				color = 0xff000000 | raw;
			} else if (ihdr.color_type == 4) {
				// Greyscale and alpha.
				color = (raw << 24) | ((raw >> 8) * 0x00010101);
			} else if (ihdr.color_type == 6) {
				// RGBA.
				color = (raw >> 8) | (raw << 24);
			}
			
			// Output the pixel to the right spot.
			pax_set_pixel(framebuffer, color, x, info.row_num);
			// ESP_LOGW(TAG, "Plot %08x @ %d,%d", color, x, info.row_num);
		}
		
		if (err == SPNG_EOI) break;
	}
	
	err = spng_decode_chunks(ctx);
	if (err) {
		ESP_LOGE(TAG, "Failed at spng_decode_chunks (2)");
	}
	
	// Get the palette, attempt two.
	if (has_palette) {
		// Color part of palette.
		err = spng_get_plte(ctx, plte);
		if (err) {
			ESP_LOGE(TAG, "spng_get_plte 2");
			goto error;
		}
	}
	
	if (has_palette && PAX_IS_PALETTE(buf_type)) {
		// Copy over the palette.
		pax_col_t *palette = malloc(sizeof(pax_col_t) * plte->n_entries);
		for (size_t i = 0; i < plte->n_entries; i++) {
			if (has_trns && i < trns->n_type3_entries) {
				palette[i] = trns->type3_alpha[i] << 24;
			} else {
				palette[i] = 0xff000000;
			}
			struct spng_plte_entry entry = plte->entries[i];
			palette[i] |= (entry.red << 16) | (entry.green << 8) | entry.blue;
		}
		framebuffer->pallette = palette;
		framebuffer->pallette_size = plte->n_entries;
	}
	
	free(plte);
	free(trns);
	free(row);
	return true;
	
	error:
	if (row)  free(row);
	if (plte) free(plte);
	if (trns) free(trns);
	ESP_LOGE(TAG, "PNG decode error %d: %s", err, spng_strerror(err));
	return false;
}
