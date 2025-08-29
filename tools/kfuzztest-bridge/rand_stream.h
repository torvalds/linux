// SPDX-License-Identifier: GPL-2.0
/*
 * Implements a cached file-reader for iterating over a byte stream of
 * pseudo-random data
 *
 * Copyright 2025 Google LLC
 */
#ifndef KFUZZTEST_BRIDGE_RAND_STREAM_H
#define KFUZZTEST_BRIDGE_RAND_STREAM_H

#include <stdlib.h>
#include <stdio.h>

/**
 * struct rand_stream - a cached bytestream reader
 *
 * Reads and returns bytes from a file, using cached pre-fetching to amortize
 * the cost of reads.
 */
struct rand_stream {
	FILE *source;
	char *buffer;
	size_t buffer_size;
	size_t buffer_pos;
};

/**
 * new_rand_stream - return a new struct rand_stream
 *
 * @path_to_file: source of the output byte stream.
 * @cache_size: size of the read-ahead cache in bytes.
 */
struct rand_stream *new_rand_stream(const char *path_to_file, size_t cache_size);

/**
 * next_byte - return the next byte from a struct rand_stream
 *
 * @rs: an initialized struct rand_stream.
 * @ret: return pointer.
 *
 * @return 0 on success or a negative value on failure.
 *
 */
int next_byte(struct rand_stream *rs, char *ret);

#endif /* KFUZZTEST_BRIDGE_RAND_STREAM_H */
