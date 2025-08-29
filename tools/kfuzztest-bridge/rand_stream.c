// SPDX-License-Identifier: GPL-2.0
/*
 * Implements a cached file-reader for iterating over a byte stream of
 * pseudo-random data
 *
 * Copyright 2025 Google LLC
 */
#include "rand_stream.h"

static int refill(struct rand_stream *rs)
{
	size_t ret = fread(rs->buffer, sizeof(char), rs->buffer_size, rs->source);
	rs->buffer_pos = 0;
	if (ret != rs->buffer_size)
		return -1;
	return 0;
}

struct rand_stream *new_rand_stream(const char *path_to_file, size_t cache_size)
{
	struct rand_stream *rs;

	rs = malloc(sizeof(*rs));
	if (!rs)
		return NULL;

	rs->source = fopen(path_to_file, "rb");
	if (!rs->source) {
		free(rs);
		return NULL;
	}

	rs->buffer = malloc(cache_size);
	if (!rs->buffer) {
		fclose(rs->source);
		free(rs);
		return NULL;
	}

	rs->buffer_size = cache_size;
	if (refill(rs)) {
		free(rs->buffer);
		fclose(rs->source);
		free(rs);
		return NULL;
	}

	return rs;
}

int next_byte(struct rand_stream *rs, char *ret)
{
	int res;
	if (rs->buffer_pos == rs->buffer_size) {
		res = refill(rs);
		if (res)
			return res;
	}
	*ret = rs->buffer[rs->buffer_pos++];
	return 0;
}
