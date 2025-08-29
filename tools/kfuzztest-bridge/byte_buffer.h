// SPDX-License-Identifier: GPL-2.0
/*
 * A simple byte buffer implementation for encoding binary data
 *
 * Copyright 2025 Google LLC
 */
#ifndef KFUZZTEST_BRIDGE_BYTE_BUFFER_H
#define KFUZZTEST_BRIDGE_BYTE_BUFFER_H

#include <stdint.h>
#include <stdlib.h>

struct byte_buffer {
	char *buffer;
	size_t num_bytes;
	size_t alloc_size;
};

struct byte_buffer *new_byte_buffer(size_t initial_size);

void destroy_byte_buffer(struct byte_buffer *buf);

int append_bytes(struct byte_buffer *buf, const char *bytes, size_t num_bytes);

int append_byte(struct byte_buffer *buf, char c);

int encode_le(struct byte_buffer *buf, uint64_t value, size_t byte_width);

int pad(struct byte_buffer *buf, size_t num_padding);

#endif /* KFUZZTEST_BRIDGE_BYTE_BUFFER_H */
