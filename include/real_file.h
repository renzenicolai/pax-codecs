
#ifndef REAL_FILE_H
#define REAL_FILE_H

#ifdef FAKE_FILE_H
#error "You must not include both fake_file.h and real_file.h!"
#endif //FAKE_FILE_H

#include <stdio.h>

// size_t xread(void *restrict __ptr, size_t __size, size_t __n, FILE *restrict __stream);
#define xread fread

// int xseek(FILE *__stream, long int __off, int __whence);
#define xseek fseek

// int xtell(FILE *__stream);
#define xtell ftell

#endif //REAL_FILE_H
