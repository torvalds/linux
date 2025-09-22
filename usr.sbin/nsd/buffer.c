/*
 * buffer.c -- generic memory buffer .
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"
#include <stdlib.h>
#include <stdio.h>

#include "buffer.h"

static void
buffer_cleanup(void *arg)
{
	buffer_type *buffer = (buffer_type *) arg;
	assert(!buffer->_fixed);
	free(buffer->_data);
}

buffer_type *
buffer_create(region_type *region, size_t capacity)
{
	buffer_type *buffer
		= (buffer_type *) region_alloc(region, sizeof(buffer_type));
	if (!buffer)
		return NULL;

	buffer->_data = (uint8_t *) xalloc(capacity);
	buffer->_position = 0;
	buffer->_limit = buffer->_capacity = capacity;
	buffer->_fixed = 0;
	buffer_invariant(buffer);
	
	region_add_cleanup(region, buffer_cleanup, buffer);

	return buffer;
}

void
buffer_create_from(buffer_type *buffer, const void *data, size_t size)
{
	assert(data);

	buffer->_position = 0;
	buffer->_limit = buffer->_capacity = size;
	buffer->_data = (uint8_t *) data;
	buffer->_fixed = 1;
	
	buffer_invariant(buffer);
}

void
buffer_clear(buffer_type *buffer)
{
	buffer_invariant(buffer);
	
	buffer->_position = 0;
	buffer->_limit = buffer->_capacity;
}

void
buffer_flip(buffer_type *buffer)
{
	buffer_invariant(buffer);
	
	buffer->_limit = buffer->_position;
	buffer->_position = 0;
}

void
buffer_rewind(buffer_type *buffer)
{
	buffer_invariant(buffer);
	
	buffer->_position = 0;
}

void
buffer_set_capacity(buffer_type *buffer, size_t capacity)
{
	buffer_invariant(buffer);
	assert(buffer->_position <= capacity);
	buffer->_data = (uint8_t *) xrealloc(buffer->_data, capacity);
	buffer->_limit = buffer->_capacity = capacity;
}

void
buffer_reserve(buffer_type *buffer, size_t amount)
{
	buffer_invariant(buffer);
	assert(!buffer->_fixed);
	if (buffer->_capacity < buffer->_position + amount) {
		size_t new_capacity = buffer->_capacity * 3 / 2;
		if (new_capacity < buffer->_position + amount) {
			new_capacity = buffer->_position + amount;
		}
		buffer_set_capacity(buffer, new_capacity);
	}
	buffer->_limit = buffer->_capacity;
}

int
buffer_printf(buffer_type *buffer, const char *format, ...)
{
	va_list args;
	int written;
	size_t remaining;
	
	buffer_invariant(buffer);
	assert(buffer->_limit == buffer->_capacity);

	remaining = buffer_remaining(buffer);
	va_start(args, format);
	written = vsnprintf((char *) buffer_current(buffer), remaining,
			    format, args);
	va_end(args);
	if (written >= 0 && (size_t) written >= remaining) {
		buffer_reserve(buffer, written + 1);
		va_start(args, format);
		written = vsnprintf((char *) buffer_current(buffer),
				    buffer_remaining(buffer),
				    format, args);
		va_end(args);
	}
	buffer->_position += written;
	return written;
}
