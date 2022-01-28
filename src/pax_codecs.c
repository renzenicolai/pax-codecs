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
#include "fake_file.h"

#include <rom/miniz.h>
#include <string.h>
static const char *png_magic = "\211PNG\r\n\032\n";
static const char *png_types[] = {
	"ihdr", "plte", "idat", "iend", "bkgd", "trns"
};
static inline png_chunk_type_t png_get_chunk_type(char *type) {
	type[0] |= 0x20;
	type[1] |= 0x20;
	type[2] |= 0x20;
	type[3] |= 0x20;
	for (int i = 0; i < sizeof(png_types) / sizeof(char *); i++) {
		if (!strncmp(png_types[i], type, 4)) {
			return i;
		}
	}
	return PNG_CHUNK_UNKNOWN;
}

static const char *png_col_types[] = {
	// Greyscale color.
	"unknown",
	"greyscale",
	"",
	"RGB",
	"indexed",
	"alpha + greyscale",
	"",
	"ARGB",
};
static png_color_type_t png_get_color_type(unsigned int type) {
	if (type == 1 || type == 5 || type >= 7) {
		return PNG_COLOR_UNKNOWN;
	} else {
		return type;
	}
}

static inline unsigned int iabs(int value) {
	if (value < 0) return -value;
	else return value;
}

// Performs a filtering, predicitng the value of the next byte.
// X must already be multiplied by bytes_per_pixel.
// Scanline_size indicates the number of bytes in a scanline.
// The buffer should be twice this size.
// The first half of the buffer is the previous scanline.
// The second half of the buffer is the current scanline.
static inline uint8_t png_predict(uint8_t *predict_buf, int x, int scanline_size,
		int bytes_per_pixel, bool ignore_up, png_filter_type_t filter) {
	
	bool ignore_left = !x;
	uint8_t up = 0, left = 0, up_left = 0;
	
	if (!ignore_left) {
		left = predict_buf[x + scanline_size - bytes_per_pixel];
	}
	
	if (!ignore_left && !ignore_up) {
		up_left = predict_buf[x - bytes_per_pixel];
	}
	
	if (!ignore_up) {
		up = predict_buf[x];
	}
	
	switch (filter) {
			uint8_t paeth_value;
			int paeth_d_up, paeth_d_up_left, paeth_d_left;
		case PNG_FILTER_NONE:
			return 0;
			break;
		case PNG_FILTER_SUB:
			return left;
			break;
		case PNG_FILTER_UP:
			return up;
			break;
		case PNG_FILTER_AVERAGE:
			return (up + left) / 2;
			break;
		case PNG_FILTER_PEATH:
			paeth_value     = left + up - up_left;
			paeth_d_up      = iabs(paeth_value - up);
			paeth_d_up_left = iabs(paeth_value - up_left);
			paeth_d_left    = iabs(paeth_value - left);
			if (paeth_d_left < paeth_d_up && paeth_d_left < paeth_d_up_left) return left;
			else if (paeth_d_up < paeth_d_up_left) return paeth_d_up;
			else return paeth_d_up_left;
			break;
		default:
			return 0;
			break;
	}
}

// Performs the decoding of a scanline of IDAT (before interlace).
// The first half of the predict_buf is the previous scanline.
// The second half will be the current scanline.
// The raw data should be placed in the second half, which will be updated to the decoded value.
// Palette is required only for color types of indexed.
static inline void png_decode_scanline(pax_buf_t *out, uint8_t *predict_buf, int y, int width,
		int bytes_per_pixel, png_filter_type_t filter,
		png_color_type_t color_type, int bits_per_channel, pax_col_t *palette, size_t palette_size) {
	int scanline_size = width * bytes_per_pixel;
	bool ignore_y = !y;
	
	int offset = 0;
	int increment = (bits_per_channel < 8) ? (8 / bits_per_channel) : 1;
	for (int x = 0; x < width; x += increment) {
		// Make a prediction of what the value should be as per PNG spec.
		uint8_t prediction = png_predict(predict_buf, offset, scanline_size, bytes_per_pixel, ignore_y, filter);
		// Add the prediction to the real value.
		predict_buf[offset + scanline_size] += prediction;
		
		// Dump this out.
		if (color_type == PNG_COLOR_PAL || color_type == PNG_COLOR_GREY) {
			// Greyscale might be 16-bit, which is unsupported by PAX.
			// Because of the big endianness, truncation happens by means of ignoring.
			
			// There might be up to 8 pixels here.
			pax_col_t list[8];
			int       list_len = (bits_per_channel == 16) ? 1 : (8 / bits_per_channel);
			uint8_t   byte = predict_buf[offset + scanline_size];
			
			// Extract the lists.
			if (bits_per_channel == 1) {
				for (int i = 0; i < 8; i++) {
					list[i] = -((byte >> i) & 1);
					list[i] &= 0xffffff;
				}
			} else if (bits_per_channel == 2) {
				for (int i = 0; i < 4; i++) {
					uint8_t val = (byte >> (i*2)) & 3;
					list[i] = val * 0x555555;
				}
			} else if (bits_per_channel == 4) {
				list[0] = (byte & 15) * 0x111111;
				list[1] = (byte >> 4) * 0x111111;
			} else {
				list[0] = byte;
			}
			
			// Convert it and output it.
			for (int i = 0; i < list_len; i++) {
				if (color_type == PNG_COLOR_PAL) {
					// Do palette lookup.
					if (list[i] < palette_size) {
						list[i] = palette[list[i]];
					} else {
						list[i] = 0;
					}
				} else {
					// Set alpha to fully opaque for greyscale.
					list[i] |= 0xff000000;
				}
				pax_set_pixel(out, list[i], x * list_len, y);
			}
		} else {
			// Output color.
			pax_col_t col = 0xff000000;
			// Shorthand for the current address in predict_buffer.
			uint8_t *pointer = &predict_buf[offset + scanline_size];
			// Whether or not we're dealing with 16bpc.
			bool is_long = bits_per_channel == 16;
			
			// Decode the color to ARGB.
			switch (color_type) {
				case PNG_COLOR_GREY_A:
					// Turn it into a greyscale thing.
					col = (*pointer * 0x010101) | (pointer[1 + is_long] << 24);
					break;
				case PNG_COLOR_RGBA:
					// Get alpha.
					col = pointer[3<<is_long] << 24;
					// Explicit flow through to RGB.
					goto le_rgb;
				case PNG_COLOR_RGB:
					le_rgb:
					// Get RGB.
					// Do this with bitwise OR so we preserve the optional alpha channel.
					col |= (pointer[0] << 16) | (pointer[1<<is_long] << 8) | pointer[2<<is_long];
					break;
				default:
					break;
			}
			
			// And output it.
			// ESP_LOGE(TAG, "set %08x @ %d, %d", col, x, y);
			pax_set_pixel(out, 0xff0000ff, x, y);
		}
		
		offset += bytes_per_pixel;
	}
}

// Decodes data in the way PNG should decode IDAT.
static inline pax_err_t png_decode_idat(pax_buf_t *out, FILE *fd, size_t chunk_len, int width, int height,
		png_color_type_t color_type, int bits_per_channel, pax_col_t *palette, size_t palette_size) {
	// Number of bytes per pixel, rounded up.
	int      bytes_per_pixel = (7 + bits_per_channel) / 8;
	if (color_type == PNG_COLOR_GREY_A) bytes_per_pixel *= 2;
	else if (color_type == PNG_COLOR_RGB) bytes_per_pixel *= 3;
	else if (color_type == PNG_COLOR_RGBA) bytes_per_pixel *= 4;
	
	// Number of bytes per scanline.
	int      scanline_size   = bytes_per_pixel * width;
	// The length of the prediction buffer to be used for the current pass.
	// This is always twice the number of bytes in the scanline.
	int      predict_size    = bytes_per_pixel * width * 2;
	// A small buffer used to help the filtering process.
	uint8_t *predict_buf     = malloc(predict_size);
	if (!predict_buf) return PAX_ERR_NOMEM;
	
	// Let's do some zlib!
	// TODO: convert this to be streaming.
	size_t idat_len = 0;
	uint8_t *idat_buf = malloc(chunk_len);
	if (!idat_buf) {
		// Not enough memory.
		free(predict_buf);
		return PAX_ERR_NOMEM;
	}
	size_t read = xread(idat_buf, 1, chunk_len, fd);
	if (read < chunk_len) {
		// Not enough SHIT.
		free(idat_buf);
		free(predict_buf);
		return PAX_ERR_NODATA;
	}
	// Ask miniz very nicely to decompress for us.
	size_t idat_cap = (scanline_size + 1) * height;
	uint8_t *idat_raw = malloc(idat_cap);
	if (!idat_raw) {
		// Not enough memory.
		free(idat_buf);
		free(predict_buf);
		return PAX_ERR_NOMEM;
	}
	idat_len = tinfl_decompress_mem_to_mem(idat_raw, idat_cap, idat_buf, chunk_len, TINFL_FLAG_PARSE_ZLIB_HEADER);
	ESP_LOGW(TAG, "decompress %zd bytes to %zd bytes (cap %zd)", chunk_len, idat_len, idat_cap);
	for (int i = 0; i < idat_len; i ++) {
		if ((i & 15) != 0 && (i & 3) == 0) printf(" ");
		if (i != 0 && (i & 15) == 0) printf("\r\n");
		printf("%02x ", idat_raw[i]);
	}
	printf("\r\n");
	
	// Start decoding and writing.
	for (int y = 0; y < height; y++) {
		// // Copy the old line back a bit.
		// // We can skip this on the first scanline.
		// if (y) memcpy(predict_buf, predict_buf + scanline_size, scanline_size);
		// // The filter to apply is the first byte in the line.
		// uint8_t filter = idat_buf[y * (scanline_size + 1)];
		// // The rest is the data to decode.
		// memcpy(predict_buf + scanline_size, &idat_buf[y * (scanline_size + 1) + 1], scanline_size);
		// // Decode a line.
		// png_decode_scanline(
		// 	out, predict_buf, y, width, 
		// 	bytes_per_pixel, filter,
		// 	color_type, bits_per_channel, palette, palette_size
		// );
	}
	
	// We're done with our IDAT.
	free(idat_raw);
	free(idat_buf);
	free(predict_buf);
	
	return PAX_OK;
}

// Decodes a PNG file into a buffer with the specified type.
// Returns 1 on successful decode, refer to pax_last_error otherwise.
bool pax_decode_png(pax_buf_t *framebuffer, FILE *fd, pax_buf_type_t buf_type) {
	framebuffer->buf = NULL;
	uint8_t buf[16];
	size_t read;
	
	// Check header.
	read = xread(buf, 1, 8, fd);
	if (read < 8) PAX_ERROR1("pax_decode_png (header)", PAX_ERR_NOMEM, false);
	if (memcmp(buf, png_magic, 8)) PAX_ERROR1("pax_decode_png (header)", PAX_ERR_DECODE, false);
	
	// Used to assert there is exactly one IHDR chunk, which is located at the beginning of the file.
	bool     has_size = false;
	// The image's dimensions.
	int      width = 0, height = 0;
	// The image's bit depth; the number of bits per sample or per palette index (not per pixel).
	int      bit_depth = 0;
	// The image's color type.
	int      color_type = PNG_COLOR_UNKNOWN;
	// Whether or not to use the ADAM7 interlacing method.
	bool     use_adam7 = false;
	
	pax_err_t error = PAX_ERR_UNKNOWN;
	
	// Iterate over chunks.
	while (true) {
		read = xread(buf, 1, 8, fd);
		if (read != 8) {
			size_t cur = xtell(fd);
			ESP_LOGW(TAG, "Expected 8, got %zd at %zd", read, cur);
			goto ohshit_outofdata;
		}
		// Chunk length: big endian to host endianness.
		size_t chunk_len = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
		// Chunk flags: based on case of chunk identifier.
		bool chunk_is_critical = !(buf[4] & 0x20);
		bool chunk_is_public   = !(buf[5] & 0x20);
		bool chunk_reserved    = !(buf[5] & 0x20);
		// Find the chunk's type.
		png_chunk_type_t chunk_type = png_get_chunk_type((char *) &buf[4]);
		
		// Skip over chunks we don't know.
		if (!chunk_is_public || !chunk_reserved) {
			chunk_type = PNG_CHUNK_UNKNOWN;
		}
		
		if (chunk_type == PNG_CHUNK_IHDR) {
			// IHDR chunk.
			has_size = true;
			read = xread(buf, 1, 13, fd);
			if (chunk_len != 13) goto ohshit_decode;
			if (read != 13) {
				size_t cur = xtell(fd);
				ESP_LOGW(TAG, "Expected 13, got %zd at %zd", read, cur);
				goto ohshit_outofdata;
			}
			width  = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
			height = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
			bit_depth = buf[8];
			color_type = png_get_color_type(buf[9]);
			if (buf[10] || buf[11]) goto ohshit_decode;
			use_adam7 = buf[12];
			ESP_LOGW(TAG, "PNG (%dx%d) bit depth %d color mode %d (%s)", width, height, bit_depth, color_type, png_col_types[color_type+1]);
			
			// Now it's time to create the BUFFER.
			pax_buf_init(framebuffer, NULL, width, height, buf_type);
			if (pax_last_error) return false;
		} else if (!has_size) {
			// IHDR must be the first chunk, but it's not.
			goto ohshit_decode;
		} else if (chunk_type == PNG_CHUNK_IDAT) {
			// Decode the IDAT chunk.
			// This is enough to justify it's own function.
			pax_err_t res = png_decode_idat(framebuffer, fd, chunk_len, width, height, color_type, bit_depth, NULL, 0);
			if (res != PAX_OK) {
				error = res;
				goto ohshit;
			}
		} else if (chunk_type == PNG_CHUNK_IEND) {
			// End of the image.
			PAX_SUCCESS();
			return true;
		} else if (chunk_is_critical) {
			// We don't recognise a critical chunk.
			goto ohshit_decode;
		} else {
			// Skip the chunk.
			if (chunk_type == PNG_CHUNK_UNKNOWN) {
				ESP_LOGW(TAG, "chunk skip %zu (unknown type)", chunk_len);
			} else {
				ESP_LOGW(TAG, "chunk skip %zu (%s)", chunk_len, png_types[chunk_type]);
			}
			xseek(fd, chunk_len, SEEK_CUR);
		}
		// Skip the CRC.
		ESP_LOGW(TAG, "CRC skip 4");
		xseek(fd, 4, SEEK_CUR);
	}
	
	// Jumped to if, while decoding chunks, we run out of data in fd.
	ohshit_outofdata:
	error = PAX_ERR_NODATA;
	goto ohshit;
	
	// Jumped to if, while decoding chunks, we fail to decode something.
	ohshit_decode:
	error = PAX_ERR_DECODE;
	
	ohshit:
	if (framebuffer->buf) {
		pax_buf_destroy(framebuffer);
	}
	PAX_ERROR1("pax_decode_png (chunk)", error, false);
}
