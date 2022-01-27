
#include "fake_file.h"
#include <stdio.h>
#include <esp_system.h>
#include <esp_log.h>

#include <string.h>
#include <malloc.h>

// Sort-of implementation of fopen for a memory buffer.
// You cannot use this with the read stdio.
FILE *xopenmem(void *memory, size_t size) {
    xfile_t *file = (xfile_t *) malloc(sizeof(xfile_t));
    *file = (xfile_t) {
        .pos = 0,
        .len = size,
        .raw_mem = memory
    };
    return (FILE *) file;
}

// Close this file.
// Only works for opened by xopenmem.
void xclose(FILE *fd) {
    free(fd);
}

// Fake implementation of fread.
// Always reads in bytes (as if size == 1).
size_t xread(void *restrict ptr, size_t size, size_t n, FILE *restrict stream) {
    // Convert our fake pointer.
    xfile_t *xstream = (xfile_t *) stream;
    
    // Fix count.
    size_t real_n = n * size;
    
    // The amount of bytes to read.
    size_t avl   = xstream->len - xstream->pos;
    size_t count = (avl < real_n) ? avl : real_n;
    
    // Perform a memcopy.
    memcpy(ptr, &xstream->buf[xstream->pos], count);
    xstream->pos += count;
    // ESP_LOGW("xread", "read(%zd, %zd) %zd bytes; %zd elem, new pos %zd", size, n, count, count / size, xstream->pos);
    return count / size;
}

// Fake implementation of fseek.
int xseek(FILE *stream, long off, int whence) {
    // Convert our fake pointer.
    xfile_t *xstream = (xfile_t *) stream;
    size_t old = xstream->pos;
    
    // Modify the position.
    if (whence == SEEK_CUR) {
        if (off < 0 && -off > xstream->pos) off = -(signed) xstream->pos;
        xstream->pos += off;
        // ESP_LOGW("xseek", "seek(%ld, SEEK_CUR) from %zd to %zd", off, old, xstream->pos);
    } else if (whence == SEEK_END) {
        xstream->pos = xstream->len;
        // ESP_LOGW("xseek", "seek(%ld, SEEK_END) from %zd to %zd", off, old, xstream->pos);
    } else if (whence == SEEK_SET) {
        if (off < 0) off = 0;
        xstream->pos = (size_t) off;
        // ESP_LOGW("xseek", "seek(%ld, SEEK_SET) from %zd to %zd", off, old, xstream->pos);
    }
    
    // Fix the position.
    if (xstream->pos > xstream->len) xstream->pos = xstream->len;
    return 0;
}

// Fake implementation of ftell.
int xtell(FILE *stream) {
    // Convert our fake pointer.
    xfile_t *xstream = (xfile_t *) stream;
    
    return xstream->pos;
}
