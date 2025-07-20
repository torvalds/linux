/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_COMPRESS_H
#define PERF_COMPRESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <linux/compiler.h>
#ifdef HAVE_ZSTD_SUPPORT
#include <zstd.h>
#endif

#ifdef HAVE_ZLIB_SUPPORT
int gzip_decompress_to_file(const char *input, int output_fd);
bool gzip_is_compressed(const char *input);
#endif

#ifdef HAVE_LZMA_SUPPORT
int lzma_decompress_stream_to_file(FILE *input, int output_fd);
int lzma_decompress_to_file(const char *input, int output_fd);
bool lzma_is_compressed(const char *input);
#else
static inline
int lzma_decompress_stream_to_file(FILE *input __maybe_unused,
				   int output_fd __maybe_unused)
{
	return -1;
}
static inline
int lzma_decompress_to_file(const char *input __maybe_unused,
			    int output_fd __maybe_unused)
{
	return -1;
}
static inline int lzma_is_compressed(const char *input __maybe_unused)
{
	return false;
}
#endif

struct zstd_data {
#ifdef HAVE_ZSTD_SUPPORT
	ZSTD_CStream	*cstream;
	ZSTD_DStream	*dstream;
	int comp_level;
#endif
};

#ifdef HAVE_ZSTD_SUPPORT

int zstd_init(struct zstd_data *data, int level);
int zstd_fini(struct zstd_data *data);

ssize_t zstd_compress_stream_to_records(struct zstd_data *data, void *dst, size_t dst_size,
				       void *src, size_t src_size, size_t max_record_size,
				       size_t process_header(void *record, size_t increment));

size_t zstd_decompress_stream(struct zstd_data *data, void *src, size_t src_size,
			      void *dst, size_t dst_size);
#else /* !HAVE_ZSTD_SUPPORT */

static inline int zstd_init(struct zstd_data *data __maybe_unused, int level __maybe_unused)
{
	return 0;
}

static inline int zstd_fini(struct zstd_data *data __maybe_unused)
{
	return 0;
}

static inline
ssize_t zstd_compress_stream_to_records(struct zstd_data *data __maybe_unused,
				       void *dst __maybe_unused, size_t dst_size __maybe_unused,
				       void *src __maybe_unused, size_t src_size __maybe_unused,
				       size_t max_record_size __maybe_unused,
				       size_t process_header(void *record, size_t increment) __maybe_unused)
{
	return 0;
}

static inline size_t zstd_decompress_stream(struct zstd_data *data __maybe_unused, void *src __maybe_unused,
					    size_t src_size __maybe_unused, void *dst __maybe_unused,
					    size_t dst_size __maybe_unused)
{
	return 0;
}
#endif

#endif /* PERF_COMPRESS_H */
