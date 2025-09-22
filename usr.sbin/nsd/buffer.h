/*
 * buffer.h -- generic memory buffer.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 *
 * The buffer module implements a generic buffer.  The API is based on
 * the java.nio.Buffer interface.
 */

#ifndef BUFFER_H
#define BUFFER_H

#include <assert.h>
#include <stdarg.h>
#include <string.h>

#include "region-allocator.h"
#include "util.h"

typedef struct buffer buffer_type;

struct buffer
{
	/*
	 * The current position used for reading/writing.
	 */ 
	size_t   _position;

	/*
	 * The read/write limit.
	 */
	size_t   _limit;

	/*
	 * The amount of data the buffer can contain.
	 */
	size_t   _capacity;

	/*
	 * The data contained in the buffer.
	 */
	uint8_t *_data;

	/*
	 * If the buffer is fixed it cannot be resized.
	 */
	unsigned _fixed : 1;
};

#ifdef NDEBUG
static inline void
buffer_invariant(buffer_type *ATTR_UNUSED(buffer))
{
}
#else
static inline void
buffer_invariant(buffer_type *buffer)
{
	assert(buffer);
	assert(buffer->_position <= buffer->_limit);
	assert(buffer->_limit <= buffer->_capacity);
	assert(buffer->_data);
}
#endif

/*
 * Create a new buffer with the specified capacity.
 */
buffer_type *buffer_create(region_type *region, size_t capacity);

/*
 * Create a buffer with the specified data.  The data is not copied
 * and no memory allocations are done.  The buffer is fixed and cannot
 * be resized using buffer_reserve().
 */
void buffer_create_from(buffer_type *buffer, const void *data, size_t size);

/*
 * Clear the buffer and make it ready for writing.  The buffer's limit
 * is set to the capacity and the position is set to 0.
 */
void buffer_clear(buffer_type *buffer);

/*
 * Make the buffer ready for reading the data that has been written to
 * the buffer.  The buffer's limit is set to the current position and
 * the position is set to 0.
 */
void buffer_flip(buffer_type *buffer);

/*
 * Make the buffer ready for re-reading the data.  The buffer's
 * position is reset to 0.
 */
void buffer_rewind(buffer_type *buffer);

static inline size_t
buffer_position(buffer_type *buffer)
{
	return buffer->_position;
}

/*
 * Set the buffer's position to MARK.  The position must be less than
 * or equal to the buffer's limit.
 */
static inline void
buffer_set_position(buffer_type *buffer, size_t mark)
{
	assert(mark <= buffer->_limit);
	buffer->_position = mark;
}

/*
 * Change the buffer's position by COUNT bytes.  The position must not
 * be moved behind the buffer's limit or before the beginning of the
 * buffer.
 */
static inline void
buffer_skip(buffer_type *buffer, ssize_t count)
{
	assert(buffer->_position + count <= buffer->_limit);
	buffer->_position += count;
}

static inline size_t
buffer_limit(buffer_type *buffer)
{
	return buffer->_limit;
}

/*
 * Change the buffer's limit.  If the buffer's position is greater
 * than the new limit the position is set to the limit.
 */
static inline void
buffer_set_limit(buffer_type *buffer, size_t limit)
{
	assert(limit <= buffer->_capacity);
	buffer->_limit = limit;
	if (buffer->_position > buffer->_limit)
		buffer->_position = buffer->_limit;
}


static inline size_t
buffer_capacity(buffer_type *buffer)
{
	return buffer->_capacity;
}

/*
 * Change the buffer's capacity.  The data is reallocated so any
 * pointers to the data may become invalid.  The buffer's limit is set
 * to the buffer's new capacity.
 */
void buffer_set_capacity(buffer_type *buffer, size_t capacity);

/*
 * Ensure BUFFER can contain at least AMOUNT more bytes.  The buffer's
 * capacity is increased if necessary using buffer_set_capacity().
 *
 * The buffer's limit is always set to the (possibly increased)
 * capacity.
 */
void buffer_reserve(buffer_type *buffer, size_t amount);

/*
 * Return a pointer to the data at the indicated position.
 */
static inline uint8_t *
buffer_at(buffer_type *buffer, size_t at)
{
	assert(at <= buffer->_limit);
	return buffer->_data + at;
}

/*
 * Return a pointer to the beginning of the buffer (the data at
 * position 0).
 */
static inline uint8_t *
buffer_begin(buffer_type *buffer)
{
	return buffer_at(buffer, 0);
}

/*
 * Return a pointer to the end of the buffer (the data at the buffer's
 * limit).
 */
static inline uint8_t *
buffer_end(buffer_type *buffer)
{
	return buffer_at(buffer, buffer->_limit);
}

/*
 * Return a pointer to the data at the buffer's current position.
 */
static inline uint8_t *
buffer_current(buffer_type *buffer)
{
	return buffer_at(buffer, buffer->_position);
}

/*
 * The number of bytes remaining between the indicated position and
 * the limit.
 */
static inline size_t
buffer_remaining_at(buffer_type *buffer, size_t at)
{
	buffer_invariant(buffer);
	assert(at <= buffer->_limit);
	return buffer->_limit - at;
}

/*
 * The number of bytes remaining between the buffer's position and
 * limit.
 */
static inline size_t
buffer_remaining(buffer_type *buffer)
{
	return buffer_remaining_at(buffer, buffer->_position);
}

/*
 * Check if the buffer has at least COUNT more bytes available.
 * Before reading or writing the caller needs to ensure enough space
 * is available!
 */
static inline int
buffer_available_at(buffer_type *buffer, size_t at, size_t count)
{
	return count <= buffer_remaining_at(buffer, at);
}

static inline int
buffer_available(buffer_type *buffer, size_t count)
{
	return buffer_available_at(buffer, buffer->_position, count);
}

static inline void
buffer_write_at(buffer_type *buffer, size_t at, const void *data, size_t count)
{
	assert(buffer_available_at(buffer, at, count));
	memcpy(buffer->_data + at, data, count);
}

static inline void
buffer_write(buffer_type *buffer, const void *data, size_t count)
{
	buffer_write_at(buffer, buffer->_position, data, count);
	buffer->_position += count;
}

static inline int
try_buffer_write_at(buffer_type *buffer, size_t at, const void *data, size_t count)
{
	if(!buffer_available_at(buffer, at, count))
		return 0;
	memcpy(buffer->_data + at, data, count);
	return 1;
}

static inline int
try_buffer_write(buffer_type *buffer, const void *data, size_t count)
{
	if(!try_buffer_write_at(buffer, buffer->_position, data, count))
		return 0;
	buffer->_position += count;
	return 1;
}

static inline void
buffer_write_string_at(buffer_type *buffer, size_t at, const char *str)
{
	buffer_write_at(buffer, at, str, strlen(str));
}

static inline void
buffer_write_string(buffer_type *buffer, const char *str)
{
	buffer_write(buffer, str, strlen(str));
}

static inline int
try_buffer_write_string_at(buffer_type *buffer, size_t at, const char *str)
{
	return try_buffer_write_at(buffer, at, str, strlen(str));
}

static inline int
try_buffer_write_string(buffer_type *buffer, const char *str)
{
	return try_buffer_write(buffer, str, strlen(str));
}

static inline void
buffer_write_u8_at(buffer_type *buffer, size_t at, uint8_t data)
{
	assert(buffer_available_at(buffer, at, sizeof(data)));
	buffer->_data[at] = data;
}

static inline void
buffer_write_u8(buffer_type *buffer, uint8_t data)
{
	buffer_write_u8_at(buffer, buffer->_position, data);
	buffer->_position += sizeof(data);
}

static inline void
buffer_write_u16_at(buffer_type *buffer, size_t at, uint16_t data)
{
	assert(buffer_available_at(buffer, at, sizeof(data)));
	write_uint16(buffer->_data + at, data);
}

static inline void
buffer_write_u16(buffer_type *buffer, uint16_t data)
{
	buffer_write_u16_at(buffer, buffer->_position, data);
	buffer->_position += sizeof(data);
}

static inline void
buffer_write_u32_at(buffer_type *buffer, size_t at, uint32_t data)
{
	assert(buffer_available_at(buffer, at, sizeof(data)));
	write_uint32(buffer->_data + at, data);
}

static inline void
buffer_write_u32(buffer_type *buffer, uint32_t data)
{
	buffer_write_u32_at(buffer, buffer->_position, data);
	buffer->_position += sizeof(data);
}

static inline void
buffer_write_u64_at(buffer_type *buffer, size_t at, uint64_t data)
{
	assert(buffer_available_at(buffer, at, sizeof(data)));
	write_uint64(buffer->_data + at, data);
}

static inline void
buffer_write_u64(buffer_type *buffer, uint64_t data)
{
	buffer_write_u64_at(buffer, buffer->_position, data);
	buffer->_position += sizeof(data);
}

static inline int
try_buffer_write_u8_at(buffer_type *buffer, size_t at, uint8_t data)
{
	if(!buffer_available_at(buffer, at, sizeof(data)))
		return 0;
	buffer->_data[at] = data;
	return 1;
}

static inline int
try_buffer_write_u8(buffer_type *buffer, uint8_t data)
{
	if(!try_buffer_write_u8_at(buffer, buffer->_position, data))
		return 0;
	buffer->_position += sizeof(data);
	return 1;
}

static inline int
try_buffer_write_u16_at(buffer_type *buffer, size_t at, uint16_t data)
{
	if(!buffer_available_at(buffer, at, sizeof(data)))
		return 0;
	write_uint16(buffer->_data + at, data);
	return 1;
}

static inline int
try_buffer_write_u16(buffer_type *buffer, uint16_t data)
{
	if(!try_buffer_write_u16_at(buffer, buffer->_position, data))
		return 0;
	buffer->_position += sizeof(data);
	return 1;
}

static inline int
try_buffer_write_u32_at(buffer_type *buffer, size_t at, uint32_t data)
{
	if(!buffer_available_at(buffer, at, sizeof(data)))
		return 0;
	write_uint32(buffer->_data + at, data);
	return 1;
}

static inline int
try_buffer_write_u32(buffer_type *buffer, uint32_t data)
{
	if(!try_buffer_write_u32_at(buffer, buffer->_position, data))
		return 0;
	buffer->_position += sizeof(data);
	return 1;
}

static inline int
try_buffer_write_u64_at(buffer_type *buffer, size_t at, uint64_t data)
{
	if(!buffer_available_at(buffer, at, sizeof(data)))
		return 0;
	write_uint64(buffer->_data + at, data);
	return 1;
}

static inline int
try_buffer_write_u64(buffer_type *buffer, uint64_t data)
{
	if(!try_buffer_write_u64_at(buffer, buffer->_position, data))
		return 0;
	buffer->_position += sizeof(data);
	return 1;
}

static inline void
buffer_read_at(buffer_type *buffer, size_t at, void *data, size_t count)
{
	assert(buffer_available_at(buffer, at, count));
	memcpy(data, buffer->_data + at, count);
}

static inline void
buffer_read(buffer_type *buffer, void *data, size_t count)
{
	buffer_read_at(buffer, buffer->_position, data, count);
	buffer->_position += count;
}

static inline uint8_t
buffer_read_u8_at(buffer_type *buffer, size_t at)
{
	assert(buffer_available_at(buffer, at, sizeof(uint8_t)));
	return buffer->_data[at];
}

static inline uint8_t
buffer_read_u8(buffer_type *buffer)
{
	uint8_t result = buffer_read_u8_at(buffer, buffer->_position);
	buffer->_position += sizeof(uint8_t);
	return result;
}

static inline uint16_t
buffer_read_u16_at(buffer_type *buffer, size_t at)
{
	assert(buffer_available_at(buffer, at, sizeof(uint16_t)));
	return read_uint16(buffer->_data + at);
}

static inline uint16_t
buffer_read_u16(buffer_type *buffer)
{
	uint16_t result = buffer_read_u16_at(buffer, buffer->_position);
	buffer->_position += sizeof(uint16_t);
	return result;
}

static inline uint32_t
buffer_read_u32_at(buffer_type *buffer, size_t at)
{
	assert(buffer_available_at(buffer, at, sizeof(uint32_t)));
	return read_uint32(buffer->_data + at);
}

static inline uint32_t
buffer_read_u32(buffer_type *buffer)
{
	uint32_t result = buffer_read_u32_at(buffer, buffer->_position);
	buffer->_position += sizeof(uint32_t);
	return result;
}

static inline uint64_t
buffer_read_u64_at(buffer_type *buffer, size_t at)
{
	assert(buffer_available_at(buffer, at, sizeof(uint64_t)));
	return read_uint64(buffer->_data + at);
}

static inline uint64_t
buffer_read_u64(buffer_type *buffer)
{
	uint64_t result = buffer_read_u64_at(buffer, buffer->_position);
	buffer->_position += sizeof(uint64_t);
	return result;
}

/*
 * Print to the buffer, increasing the capacity if required using
 * buffer_reserve(). The buffer's position is set to the terminating
 * '\0'. Returns the number of characters written (not including the
 * terminating '\0').
 */
int buffer_printf(buffer_type *buffer, const char *format, ...)
	ATTR_FORMAT(printf, 2, 3);

#endif /* BUFFER_H */
