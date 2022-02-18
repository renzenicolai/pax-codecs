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

#ifndef PAX_CODECS_H
#define PAX_CODECS_H

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

enum png_filter_type {
	PNG_FILTER_UNKNOWN = -1,
	// Do not filter.
	PNG_FILTER_NONE,
	// Pixel to the left.
	PNG_FILTER_SUB,
	// Pixel above.
	PNG_FILTER_UP,
	// Mean of up and left, rounded down.
	PNG_FILTER_AVERAGE,
	// left, up or up_left, whichever is closest to p=left+up-up_left
	PNG_FILTER_PEATH
};

enum png_color_type {
	PNG_COLOR_UNKNOWN = -1,
	// Greyscale color.
	PNG_COLOR_GREY    = 0x00,
	// RGB color (truecolor).
	PNG_COLOR_RGB     = 0x02,
	// Palette (indexed color).
	PNG_COLOR_PAL     = 0x03,
	// Greyscale with alpha.
	PNG_COLOR_GREY_A  = 0x04,
	// ARGB (truecolor with alpha).
	PNG_COLOR_RGBA    = 0x06,
};

enum png_chunk_type {
	PNG_CHUNK_UNKNOWN = -1,
	// Image header, always the first chunk.
	PNG_CHUNK_IHDR,
	// Image color palette.
	PNG_CHUNK_PLTE,
	// Image data.
	PNG_CHUNK_IDAT,
	// End of PNG; marks the end of the PNG file.
	PNG_CHUNK_IEND,
	// Default background color.
	PNG_CHUNK_BKGD,
	// Skipping over several irrelevant chunks.
	
	// Contains transparency information.
	// For paletted images, one transparency per color,
	// For other images, one transparency per pixel.
	PNG_CHUNK_TRNS
};

typedef enum png_filter_type png_filter_type_t;
typedef enum png_color_type png_color_type_t;
typedef enum png_chunk_type png_chunk_type_t;

#include "pax_gfx.h"
#include <stdio.h>

// Decodes a PNG file into a buffer with the specified type.
// Returns 1 on successful decode, refer to pax_last_error otherwise.
bool pax_decode_png(pax_buf_t *buf, FILE *fd, pax_buf_type_t buf_type);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //PAX_CODECS_H
