/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_COMPRESS_H
#define PERF_COMPRESS_H

#ifdef HAVE_ZLIB_SUPPORT
int gzip_decompress_to_file(const char *input, int output_fd);
#endif

#ifdef HAVE_LZMA_SUPPORT
int lzma_decompress_to_file(const char *input, int output_fd);
#endif

#endif /* PERF_COMPRESS_H */
