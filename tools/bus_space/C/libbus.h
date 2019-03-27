/*-
 * Copyright (c) 2014, 2015 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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

#ifndef _LIBBUS_SPACE_H_
#define	_LIBBUS_SPACE_H_

int	bus_map(const char *dev, const char *resource);
int16_t	bus_read_1(int rid, long ofs);
int32_t	bus_read_2(int rid, long ofs);
int64_t bus_read_4(int rid, long ofs);
int	bus_subregion(int rid, long ofs, long sz);
int	bus_unmap(int rid);
int	bus_write_1(int rid, long ofs, uint8_t val);
int	bus_write_2(int rid, long ofs, uint16_t val);
int	bus_write_4(int rid, long ofs, uint32_t val);

typedef unsigned long bus_addr_t;
typedef unsigned long bus_size_t;
typedef int busdma_tag_t;
typedef int busdma_md_t;
typedef int busdma_seg_t;

int	busdma_tag_create(const char *dev, bus_addr_t align, bus_addr_t bndry,
	    bus_addr_t maxaddr, bus_size_t maxsz, u_int nsegs,
	    bus_size_t maxsegsz, u_int datarate, u_int flags,
	    busdma_tag_t *out_p);
int	busdma_tag_derive(busdma_tag_t tag, bus_addr_t align, bus_addr_t bndry,
	    bus_addr_t maxaddr, bus_size_t maxsz, u_int nsegs,
	    bus_size_t maxsegsz, u_int datarate, u_int flags,
	    busdma_tag_t *out_p);
int	busdma_tag_destroy(busdma_tag_t tag);

int	busdma_mem_alloc(busdma_tag_t tag, u_int flags, busdma_md_t *out_p);
int	busdma_mem_free(busdma_md_t md);

int	busdma_md_create(busdma_tag_t tag, u_int flags, busdma_md_t *out_p);
int	busdma_md_destroy(busdma_md_t md);
int	busdma_md_load(busdma_md_t md, void *buf, size_t len, u_int flags);
int	busdma_md_unload(busdma_md_t md);

#define	BUSDMA_MD_BUS_SPACE	0
#define	BUSDMA_MD_PHYS_SPACE	1
#define	BUSDMA_MD_VIRT_SPACE	2

int	busdma_md_first_seg(busdma_md_t, int space);
int	busdma_md_next_seg(busdma_md_t, busdma_seg_t seg);

bus_addr_t	busdma_seg_get_addr(busdma_seg_t seg);
bus_size_t	busdma_seg_get_size(busdma_seg_t seg);

#define	BUSDMA_SYNC_PREREAD     1
#define	BUSDMA_SYNC_POSTREAD    2
#define	BUSDMA_SYNC_PREWRITE    4
#define	BUSDMA_SYNC_POSTWRITE   8

int	busdma_sync(busdma_md_t md, int op);
int	busdma_sync_range(busdma_md_t md, int op, bus_size_t, bus_size_t);

#endif /* _LIBBUS_SPACE_H_ */
