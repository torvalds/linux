/*	$OpenBSD: bytebuf.c,v 1.8 2015/12/05 18:43:36 mmcc Exp $ */
/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/**@file
 * bytebuffer provides 'byte buffer' helper methods.
 *
 * Example:<pre>
 *	bytebuffer *buf = bytebuffer_create(BUFSIZ);
 *	int sz = read(STDIN_FILENO, bytebuffer_pointer(buf),
 *	    bytebuffer_remaining(buf));
 *	if (sz > 0) {
 *	    bytebuffer_put(buf, BYTEBUFFER_PUT_DIRECT, sz);
 *	    bytebuffer_flip(buf);
 *
 *	    sz = write(STDOUT_FILENO, bytebuffer_pointer(buf),
 *		bytebuffer_remaining(buf));
 *	    bytebuffer_compact(buf);
 *	}</pre>
 *
 * @author Yasuoka Masahiko
 * $Id: bytebuf.c,v 1.8 2015/12/05 18:43:36 mmcc Exp $
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef	BYTEBUF_DEBUG
#include <stdio.h>
#endif

#ifndef	BYTEBUF_ASSERT
#ifdef	BYTEBUF_DEBUG
#define	BYTEBUF_ASSERT(cond)						\
	if (!(cond)) {						\
	    fprintf(stderr,					\
		"\nASSERT(" #cond ") failed on %s() at %s:%d.\n"\
		, __func__, __FILE__, __LINE__);		\
	    abort(); 						\
	}
#else
#define	BYTEBUF_ASSERT(cond)
#endif
#endif

#include "bytebuf.h"

struct _bytebuffer {
	/** current position */
	int 	position;
	/** current limit */
	int	limit;
	/** position mark*/
	int 	mark;
	/** capacity of buffer */
	size_t 	capacity;
	/** allocated memory area */
	void	*data;
};

/**
 * Create a bytebuffer and allocate memory area.
 *
 * @param	capacity	Capacity of allocating memory.  If zero or
 *		    negative value is specified, this function don't allocate
 *		    memory.
 */
bytebuffer *
bytebuffer_create(size_t capacity)
{
	bytebuffer *_this = NULL;

	if ((_this = calloc(1, sizeof(bytebuffer))) == NULL)
		return NULL;

	if (capacity > 0) {
		if ((_this->data = calloc(1, capacity)) == NULL)
			goto fail;
		_this->capacity = capacity;
	} else
		_this->capacity = 0;

	_this->limit = _this->capacity;
	_this->mark = -1;
	return _this;
fail:
	free(_this);
	return NULL;
}

/**
 * Create a bytebuffer using existing memory area.  This memory area will
 * be freed by bytebuffer's destructor.
 *
 * @data		the pointer to existing memory area
 * @param capacity	capacity of 'data'.
 */
bytebuffer *
bytebuffer_wrap(void *data, size_t capacity)
{
	bytebuffer *_this;

	if ((_this = bytebuffer_create(0)) == NULL)
		return NULL;

	_this->data = data;
	_this->capacity = capacity;
	_this->mark = -1;

	return _this;
}

/**
 * Unwrap memory from bytebuffer.
 *
 * @param _this		the bytebuffer object.
 */
void *
bytebuffer_unwrap(bytebuffer *_this)
{
	void *rval;

	rval = _this->data;
	_this->data = NULL;
	_this->capacity = 0;
	_this->position = 0;
	_this->limit = 0;
	_this->mark = -1;

	return rval;
}

/**
 * Change capacity of this buffer.
 *
 * @param _this		the bytebuffer object.
 * @param capacity	new capacity.
 */
int
bytebuffer_realloc(bytebuffer *_this, size_t capacity)
{
	void *new_data;

	BYTEBUF_ASSERT(_this->limit <= capacity);

	if (_this->limit > capacity) {
		errno = EINVAL;
		return -1;
	}

	if ((new_data = realloc(_this->data, capacity)) == NULL)
		return -1;

	_this->data = new_data;
	if (_this->limit == _this->capacity)
		_this->limit = capacity;
	_this->capacity = capacity;

	return 0;
}

/**
 * Compact this buffer.  the bytes between position and limit are copied
 * to the beginning of the buffer.
 *
 * @param _this		the bytebuffer object.
 */
void
bytebuffer_compact(bytebuffer *_this)
{
	int len;

	len = bytebuffer_remaining(_this);

	if (len <= 0)
		len = 0;
	else if (_this->position != 0)
		memmove(_this->data,
		    (const char *)_this->data + _this->position, (size_t)len);

	_this->position = len;
	_this->limit = _this->capacity;
	_this->mark = -1;
}

static int bytebuffer_direct0;
/**
 * BYTEBUFFER_PUT_DIRECT specifies the data that has been written already by
 * direct access.
 */
const void * BYTEBUFFER_PUT_DIRECT = &bytebuffer_direct0;

/**
 * BYTEBUFFER_GET_DIRECT specifies the data that has been read already by
 * direct access.
 */
void * BYTEBUFFER_GET_DIRECT = &bytebuffer_direct0;

/**
 * Write the given data to the buffer.
 * If buffer is too small, this function returns <code>NULL</code> and
 * <code>errno</code> is <code>ENOBUFS</code>
 *
 * @param _this		the bytebuffer object.
 * @param src		source data.  To specify the data that has been
 *			written already by direct access, use
 *			{@link ::BYTEBUFFER_PUT_DIRECT} for putting the data.
 *			NULL is the same {@link ::BYTEBUFFER_PUT_DIRECT}
 *			at least on this version, but using NULL is deprecated.
 * @param srclen	length of the source data.
 * @see ::BYTEBUFFER_PUT_DIRECT
 */
void *
bytebuffer_put(bytebuffer *_this, const void *src, size_t srclen)
{
	void *rval;

	BYTEBUF_ASSERT(_this != NULL);
	BYTEBUF_ASSERT(srclen > 0);

	if (srclen > bytebuffer_remaining(_this)) {
		errno = ENOBUFS;
		return NULL;
	}
	rval = (char *)_this->data + _this->position;

	if (src != NULL && src != BYTEBUFFER_PUT_DIRECT)
		memcpy(rval, src, srclen);

	_this->position += srclen;

	return rval;
}

/*
 * Transfer data from this buffer to the given destination memory.
 * If the given buffer is too small, this function returns <code>NULL</code>
 * and <code>errno</code> is <code>ENOBUFS</code>
 *
 * @param	dst	pointer of the destination memory.  Specify NULL
 *			to skip transferring the data.
 * @param	dstlne	memory size of the destination.
 */
void *
bytebuffer_get(bytebuffer *_this, void *dst, size_t dstlen)
{
	BYTEBUF_ASSERT(_this != NULL);
	BYTEBUF_ASSERT(dstlen > 0);

	if (dstlen > bytebuffer_remaining(_this)) {
		errno = ENOBUFS;
		return NULL;
	}
	if (dst != NULL && dst != BYTEBUFFER_GET_DIRECT)
		memcpy(dst, (char *)_this->data + _this->position, dstlen);

	_this->position += dstlen;

	return dst;
}

/** Returns this buffer's position */
int
bytebuffer_position(bytebuffer *_this)
{
	BYTEBUF_ASSERT(_this != NULL);

	return _this->position;
}

/** Returns this buffer's limit */
int
bytebuffer_limit(bytebuffer *_this)
{
	BYTEBUF_ASSERT(_this != NULL);

	return _this->limit;
}

/** Returns this buffer's capacity */
int
bytebuffer_capacity(bytebuffer *_this)
{
	BYTEBUF_ASSERT(_this != NULL);

	return _this->capacity;
}

/** Returns a pointer to current position */
void *
bytebuffer_pointer(bytebuffer *_this)
{
	BYTEBUF_ASSERT(_this != NULL);

	return (char *)_this->data + _this->position;
}

/** Returns the number of byte between current position and the limit*/
size_t
bytebuffer_remaining(bytebuffer *_this)
{
	BYTEBUF_ASSERT(_this != NULL);
	BYTEBUF_ASSERT(_this->limit >= _this->position);

	return _this->limit - _this->position;
}

/** Returns whether there are data between current position and the limit */
int
bytebuffer_has_remaining(bytebuffer *_this)
{
	BYTEBUF_ASSERT(_this != NULL);

	return bytebuffer_remaining(_this) > 0;
}

/**
 * Flip this buffer.
 * The limit is set to the position and the position is set zero.
 */
void
bytebuffer_flip(bytebuffer *_this)
{
	BYTEBUF_ASSERT(_this != NULL);

	_this->limit = _this->position;
	_this->position = 0;
	_this->mark = -1;
}

/**
 * Rewind this buffer.
 * The position is set to zero.
 */
void
bytebuffer_rewind(bytebuffer *_this)
{
	BYTEBUF_ASSERT(_this != NULL);

	_this->position = 0;
	_this->mark = -1;
}

/**
 * Clear this buffer.
 * The position is set to zero.
 */
void
bytebuffer_clear(bytebuffer *_this)
{
	BYTEBUF_ASSERT(_this != NULL);

	_this->limit = _this->capacity;
	_this->position = 0;
	_this->mark = -1;
}

/** mark the current position.  */
void
bytebuffer_mark(bytebuffer *_this)
{
	BYTEBUF_ASSERT(_this != NULL);

	_this->mark = _this->position;
}

/** reset the position to the mark.  */
void
bytebuffer_reset(bytebuffer *_this)
{
	BYTEBUF_ASSERT(_this != NULL);
	BYTEBUF_ASSERT(_this->mark >= 0);

	if (_this->mark >= 0)
		_this->position = _this->mark;
}

/**
 * Destroy bytebuffer object.
 */
void
bytebuffer_destroy(bytebuffer *_this)
{
	BYTEBUF_ASSERT(_this != NULL);

	if (_this != NULL) {
		free(_this->data);
		free(_this);
	}
}
