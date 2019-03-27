/*
   FastLZ - lightning-fast lossless compression library

   Copyright (C) 2007 Ariya Hidayat (ariya@kde.org)
   Copyright (C) 2006 Ariya Hidayat (ariya@kde.org)
   Copyright (C) 2005 Ariya Hidayat (ariya@kde.org)

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.

   $FreeBSD$
   */
#ifndef FASTLZ_H
#define FASTLZ_H

#define FASTLZ_VERSION 0x000100

#define FASTLZ_VERSION_MAJOR	 0
#define FASTLZ_VERSION_MINOR	 0
#define FASTLZ_VERSION_REVISION  0

#define FASTLZ_VERSION_STRING "0.1.0"

struct cudbg_buffer;

int fastlz_compress(const void *input, int length, void *output);
int fastlz_compress_level(int level, const void *input, int length,
			  void *output);
int fastlz_decompress(const void *input, int length, void *output, int maxout);

/* prototypes */

int write_magic(struct cudbg_buffer *);
int detect_magic(struct cudbg_buffer *);

int write_to_buf(void *, u32, u32 *, void *, u32);
int read_from_buf(void *, u32, u32 *, void *,  u32);

int write_chunk_header(struct cudbg_buffer *, int, int, unsigned long,
		       unsigned long, unsigned long);

int read_chunk_header(struct cudbg_buffer *, int* , int*, unsigned long*,
		      unsigned long*, unsigned long*);

unsigned long block_compress(const unsigned char *, unsigned long length,
			     unsigned char *);
#endif /* FASTLZ_H */
