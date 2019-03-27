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
 *
 * $FreeBSD$
 */

// $Id: binstream.h,v 1.1 2006/07/05 10:47:54 ivoras Exp $


#ifndef _BIN_STREAM_
#define _BIN_STREAM_

#ifndef uint8_t
#define uint8_t unsigned char
#endif

typedef struct {
	unsigned char  *data;
	int		pos;
}	bin_stream_t;


/* "Open" a binary stream for reading */
void		bs_open   (bin_stream_t * bs, void *data);

/* "Reset" position in binary stream to zero */
void		bs_reset  (bin_stream_t * bs);


/* Write a zero-terminated string; return next position */
unsigned	bs_write_str(bin_stream_t * bs, char *data);

/* Write an arbitrary buffer; return next position */
unsigned	bs_write_buf(bin_stream_t * bs, char *data, unsigned data_size);

/* Write a 8bit uint; return next position. */
unsigned	bs_write_u8(bin_stream_t * bs, uint8_t data);

/* Write a 16bit uint; return next position. */
unsigned	bs_write_u16(bin_stream_t * bs, uint16_t data);

/* Write a 32bit uint; return next position. */
unsigned	bs_write_u32(bin_stream_t * bs, uint32_t data);

/* Write a 64bit uint; return next position. */
unsigned	bs_write_u64(bin_stream_t * bs, uint64_t data);


/*
 * Read a null-terminated string from stream into a buffer; buf_size is size
 * of the buffer, including the final \0. Returns buf pointer or NULL if
 * garbage input.
 */
char           *bs_read_str(bin_stream_t * bs, char *buf, unsigned buf_size);

/* Read an arbitrary buffer. */
void		bs_read_buf(bin_stream_t * bs, char *buf, unsigned buf_size);

/* Read a 8bit uint * return it */
uint8_t		bs_read_u8(bin_stream_t * bs);

/* Read a 16bit uint * return it */
uint16_t	bs_read_u16(bin_stream_t * bs);

/* Read a 8bit uint * return it */
uint32_t	bs_read_u32(bin_stream_t * bs);

/* Read a 8bit uint * return it */
uint64_t	bs_read_u64(bin_stream_t * bs);

#endif
