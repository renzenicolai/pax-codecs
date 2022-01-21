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

#include <string.h>
static const char *png_magic = "\211PNG\r\n\032\n";
static const char *png_types = {
    "ihdr", "plte", "idat", "iend", "bkgd", "trns"
};
static inline png_chunk_type_t png_get_chunk_type(char *type) {
    
}

// Decodes a PNG file into a buffer with the specified type.
// Returns 1 on successful decode, refer to pax_last_error otherwise.
bool pax_decode_png(pax_buf_t *framebuffer, FILE *fd, pax_buf_type_t buf_type) {
    if (!framebuffer) {
        PAX_ERROR1("pax_decode_png", PAX_ERR_PARAM, false);
    } else if (framebuffer->type != buf_type) {
        if (framebuffer->buf) {
            PAX_ERROR1("pax_decode_png", PAX_ERR_PARAM, false);
        }
    }
    
    uint8_t buf[8];
    size_t read;
    
    // Check header.
    read = fread(buf, 1, 8, fd);
    if (read < 8) PAX_ERROR1("pax_decode_png (header)", PAX_ERR_NOMEM, false);
    if (memcmp(buf, png_magic, 8)) PAX_ERROR1("pax_decode_png (header)", PAX_ERR_DECODE, false);
    
    // Used to assert there is exactly one IHDR chunk, which is located at the beginning of the file.
    bool has_size = false;
    // The image's dimensions.
    int width, height;
    // The image's bit depth; the number of bits per sample or per palette index (not per pixel).
    int bit_depth;
    // Whether or not to use the ADAM7 interlacing method.
    bool use_adam7;
    
    // Iterate over chunks.
    while (true) {
        read = fread(buf, 8, 1, fd);
        if (read < 8) goto ohshit_outofdata;
        // Chunk length: big endian to host endianness.
        size_t chunk_len = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
        // Chunk flags: based on case of chunk identifier.
        bool chunk_is_critical = !(buf[4] & 0x20);
        bool chunk_is_public   = !(buf[5] & 0x20);
        bool chunk_reserved    = !(buf[5] & 0x20);
        // Find the chunk's type.
        png_chunk_type_t chunk_type = png_get_chunk_type(buf + 4);
        
        
    }
    
    return true;
    
    // Jumped to if, while decoding chunks, we run out of data in fd.
    ohshit_outofdata:
    if (framebuffer->buf) {
        pax_buf_destroy(framebuffer);
    }
    PAX_ERROR1("pax_decode_png (chunks)", PAX_ERR_NOMEM, false);
}
