
#ifndef FAKE_FILE_H
#define FAKE_FILE_H

#ifdef REAL_FILE_H
#error "You must not include both fake_file.h and real_file.h!"
#endif //REAL_FILE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct xfile {
    size_t pos;
    size_t len;
    union {
        void    *raw_mem;
        uint8_t *buf;
    };
} xfile_t;

// Sort-of implementation of fopen for a memory buffer.
// You cannot use this with the real stdio.
FILE *xopenmem(void *memory, size_t size);

// Close this file.
// Only works for opened by xopenmem.
void xclose(FILE *fd);

// Fake implementation of fread.
// Always reads in bytes (as if size == 1).
size_t xread(void *__ptr, size_t __size, size_t __n, FILE *__stream);

// Fake implementation of fseek.
int xseek(FILE *__stream, long int __off, int __whence);

// Fake implementation of ftell.
int xtell(FILE *__stream);

#endif //FAKE_FILE_H
