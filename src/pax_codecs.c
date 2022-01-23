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
    
    uint8_t buf[16];
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
    // The image's color type.
    int color_type;
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
        png_chunk_type_t chunk_type = png_get_chunk_type((char *) &buf[4]);
        
        // Skip over chunks we don't know.
        if (!chunk_is_public || !chunk_reserved) {
            chunk_type = PNG_CHUNK_UNKNOWN;
        }
        
        if (chunk_type == PNG_CHUNK_IHDR) {
            // IHDR chunk.
            has_size = true;
            read = fread(buf, 1, 13, fd);
            if (chunk_len != 13 || read < 13) goto ohshit_outofdata;
            width  = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
            height = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
            bit_depth = buf[8];
            color_type = buf[9];
            if (buf[10] || buf[11]) goto ohshit_decode;
            use_adam7 = buf[12];
            ESP_LOGW(TAG, "PNG (%dx%d) bit depth %d color mode %d", width, height, bit_depth, color_type);
            
            // Now it's time to create the BUFFER.
            pax_buf_init(framebuffer, NULL, width, height, buf_type);
            if (pax_last_error) return false;
        } else if (!has_size) {
            // IHDR must be the first chunk, but it's not.
            goto ohshit_decode;
        } else if (chunk_type == PNG_CHUNK_IDAT) {
            return true;
            // Let's do some zlib!
            // TODO: convert this to an external function which reads the file directly.
            size_t idat_len = 0;
            uint8_t *idat_buf = malloc(chunk_len);
            read = fread(idat_buf, 1, chunk_len, fd);
            if (read < chunk_len) {
                // Not enough SHIT.
                free(idat_buf);
                goto ohshit_outofdata;
            }
            // Ask miniz very nicely to decompress for us.
            size_t idat_cap = width * height * 4;
            uint8_t *idat_raw = malloc(idat_cap);
            idat_len = tinfl_decompress_mem_to_mem(idat_raw, idat_cap, idat_buf, chunk_len, TINFL_FLAG_PARSE_ZLIB_HEADER);
            for (int i = 0; i < idat_len; i ++) {
                printf("%02x ", idat_raw[i]);
                if ((i & 15) != 0 && (i & 3) == 0) printf(" ");
                if (i != 0 && (i & 15) == 0) printf("\r\n");
            }
            printf("\r\n");
            // We're done with out IDAT.
            free(idat_raw);
        } else if (chunk_type == PNG_CHUNK_IEND) {
            // End of the image.
            PAX_SUCCESS();
            return true;
        } else if (chunk_is_critical) {
            // We don't recognise a critical chunk.
            goto ohshit_decode;
        } else {
            // Skip the chunk.
            fseek(fd, chunk_len, SEEK_CUR);
        }
        // Skip the CRC.
        fseek(fd, 4, SEEK_CUR);
    }
    
    pax_err_t error = PAX_ERR_UNKNOWN;
    
    // Jumped to if, while decoding chunks, we run out of data in fd.
    ohshit_outofdata:
    error = PAX_ERR_NOMEM;
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
