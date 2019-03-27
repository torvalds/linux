/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Ivan Voras <ivoras@gmail.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

// $Id: binstream.c,v 1.1 2006/07/05 10:47:54 ivoras Exp $

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/param.h>

#include <geom/virstor/binstream.h>


/* "Open" a binary stream for reading */
void
bs_open(bin_stream_t * bs, void *data)
{
	bs->data = (char *)data;
	bs->pos = 0;
}


/* "Reset" position in binary stream to zero */
void
bs_reset(bin_stream_t * bs)
{
	bs->pos = 0;
}

/* Write a zero-terminated string; return next position */
unsigned
bs_write_str(bin_stream_t * bs, char *data)
{
	int		len = 0;
	do {
		*(bs->data + bs->pos + len) = *data;
		len++;
	} while (*(data++) != '\0');
	bs->pos += len;
	return bs->pos;
}


/* Write an arbitrary buffer; return next position */
unsigned
bs_write_buf(bin_stream_t * bs, char *data, unsigned data_size)
{
	unsigned	i;
	for (i = 0; i < data_size; i++)
		*(bs->data + bs->pos + i) = *(data + i);
	bs->pos += data_size;
	return bs->pos;
}


/* Write a 8bit uint; return next position. */
unsigned
bs_write_u8(bin_stream_t * bs, uint8_t data)
{
	*((uint8_t *) (bs->data + bs->pos)) = data;
	return ++(bs->pos);
}


/* Write a 16bit uint; return next position. */
unsigned
bs_write_u16(bin_stream_t * bs, uint16_t data)
{
	le16enc(bs->data + bs->pos, data);
	return (bs->pos += 2);
}


/* Write a 32bit uint; return next position. */
unsigned
bs_write_u32(bin_stream_t * bs, uint32_t data)
{
	le32enc(bs->data + bs->pos, data);
	return (bs->pos += 4);
}


/* Write a 64bit uint; return next position. */
unsigned
bs_write_u64(bin_stream_t * bs, uint64_t data)
{
	le64enc(bs->data + bs->pos, data);
	return (bs->pos += 8);
}


/* Read a 8bit uint & return it */
uint8_t
bs_read_u8(bin_stream_t * bs)
{
	uint8_t		data = *((uint8_t *) (bs->data + bs->pos));
	bs->pos++;
	return data;
}


/*
 * Read a null-terminated string from stream into a buffer; buf_size is size
 * of the buffer, including the final \0. Returns buf pointer or NULL if
 * garbage input.
 */
char*
bs_read_str(bin_stream_t * bs, char *buf, unsigned buf_size)
{
	unsigned	len = 0;
	char           *work_buf = buf;
	if (buf == NULL || buf_size < 1)
		return NULL;
	do {
		*work_buf = *(bs->data + bs->pos + len);
	} while (len++ < buf_size - 1 && *(work_buf++) != '\0');
	*(buf + buf_size - 1) = '\0';
	bs->pos += len;
	return buf;
}


/* Read an arbitrary buffer. */
void
bs_read_buf(bin_stream_t * bs, char *buf, unsigned buf_size)
{
	unsigned	i;
	for (i = 0; i < buf_size; i++)
		*(buf + i) = *(bs->data + bs->pos + i);
	bs->pos += buf_size;
}


/* Read a 16bit uint & return it */
uint16_t
bs_read_u16(bin_stream_t * bs)
{
	uint16_t	data = le16dec(bs->data + bs->pos);
	bs->pos += 2;
	return data;
}


/* Read a 32bit uint & return it */
uint32_t
bs_read_u32(bin_stream_t * bs)
{
	uint32_t	data = le32dec(bs->data + bs->pos);
	bs->pos += 4;
	return data;
}


/* Read a 64bit uint & return it */
uint64_t
bs_read_u64(bin_stream_t * bs)
{
	uint64_t	data = le64dec(bs->data + bs->pos);
	bs->pos += 8;
	return data;
}
