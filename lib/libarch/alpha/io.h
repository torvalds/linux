/* $OpenBSD: io.h,v 1.1 2002/07/07 14:24:04 matthieu Exp $ */
/*-
 * Copyright (c) 1998 Doug Rabson
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
 *
 * $FreeBSD: src/lib/libio/io.h,v 1.2 1999/08/28 00:04:44 peter Exp $
 */

struct io_ops {
	int	  (*ioperm)(u_int32_t, u_int32_t, int);
	u_int8_t  (*inb)(u_int32_t);
	u_int16_t (*inw)(u_int32_t);
	u_int32_t (*inl)(u_int32_t);
	void	  (*outb)(u_int32_t, u_int8_t);
	void	  (*outw)(u_int32_t, u_int16_t);
	void	  (*outl)(u_int32_t, u_int32_t);
	void 	 *(*map_memory)(u_int32_t, u_int32_t);
	void	  (*unmap_memory)(void *, u_int32_t);
	u_int8_t  (*readb)(void *, u_int32_t);
	u_int16_t (*readw)(void *, u_int32_t);
	u_int32_t (*readl)(void *, u_int32_t);
	void	  (*writeb)(void *, u_int32_t, u_int8_t);
	void	  (*writew)(void *, u_int32_t, u_int16_t);
	void	  (*writel)(void *, u_int32_t, u_int32_t);
};

extern struct io_ops swiz_io_ops;
extern struct io_ops bwx_io_ops;
