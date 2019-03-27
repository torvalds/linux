/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_DMA_ATTR_H_
#define	_LINUX_DMA_ATTR_H_

#define	DMA_ATTR_WRITE_BARRIER		(1 << 0)
#define	DMA_ATTR_WEAK_ORDERING		(1 << 1)
#define	DMA_ATTR_WRITE_COMBINE		(1 << 2)
#define	DMA_ATTR_NON_CONSISTENT		(1 << 3)
#define	DMA_ATTR_NO_KERNEL_MAPPING	(1 << 4)
#define	DMA_ATTR_SKIP_CPU_SYNC		(1 << 5)
#define	DMA_ATTR_FORCE_CONTIGUOUS	(1 << 6)
#define	DMA_ATTR_ALLOC_SINGLE_PAGES	(1 << 7)
#define	DMA_ATTR_NO_WARN		(1 << 8)
#define	DMA_ATTR_PRIVILEGED		(1 << 9)

struct dma_attrs {
	unsigned long flags;
};

#define DEFINE_DMA_ATTRS(x) struct dma_attrs x = { }

static inline void
init_dma_attrs(struct dma_attrs *attrs)
{
	attrs->flags = 0;
}

#endif	/* _LINUX_DMA_ATTR_H_ */
