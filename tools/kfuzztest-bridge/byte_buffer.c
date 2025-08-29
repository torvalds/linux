// SPDX-License-Identifier: GPL-2.0
/*
 * A simple byte buffer implementation for encoding binary data
 *
 * Copyright 2025 Google LLC
 */
#include <asm-generic/errno-base.h>
#include <stdlib.h>
#include <string.h>

#include "byte_buffer.h"

struct byte_buffer *new_byte_buffer(size_t initial_size)
{
	struct byte_buffer *ret;
	size_t alloc_size = initial_size >= 8 ? initial_size : 8;

	ret = malloc(sizeof(*ret));
	if (!ret)
		return NULL;

	ret->alloc_size = alloc_size;
	ret->buffer = malloc(alloc_size);
	if (!ret->buffer) {
		free(ret);
		return NULL;
	}
	ret->num_bytes = 0;
	return ret;
}

void destroy_byte_buffer(struct byte_buffer *buf)
{
	free(buf->buffer);
	free(buf);
}

int append_bytes(struct byte_buffer *buf, const char *bytes, size_t num_bytes)
{
	size_t req_size;
	size_t new_size;
	char *new_ptr;

	req_size = buf->num_bytes + num_bytes;
	new_size = buf->alloc_size;

	while (req_size > new_size)
		new_size *= 2;
	if (new_size != buf->alloc_size) {
		new_ptr = realloc(buf->buffer, new_size);
		if (!buf->buffer)
			return -ENOMEM;
		buf->buffer = new_ptr;
		buf->alloc_size = new_size;
	}
	memcpy(buf->buffer + buf->num_bytes, bytes, num_bytes);
	buf->num_bytes += num_bytes;
	return 0;
}

int append_byte(struct byte_buffer *buf, char c)
{
	return append_bytes(buf, &c, 1);
}

int encode_le(struct byte_buffer *buf, uint64_t value, size_t byte_width)
{
	size_t i;
	int ret;

	for (i = 0; i < byte_width; ++i) {
		if ((ret = append_byte(buf, (uint8_t)((value >> (i * 8)) & 0xFF)))) {
			return ret;
		}
	}
	return 0;
}

int pad(struct byte_buffer *buf, size_t num_padding)
{
	int ret;
	size_t i;
	for (i = 0; i < num_padding; i++)
		if ((ret = append_byte(buf, 0)))
			return ret;
	return 0;
}
