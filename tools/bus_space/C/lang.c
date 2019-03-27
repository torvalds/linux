/*-
 * Copyright (c) 2014 Marcel Moolenaar
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <errno.h>

#include "bus.h"
#include "busdma.h"
#include "libbus.h"

int16_t
bus_read_1(int rid, long ofs)
{
	uint8_t val;

	return ((!bs_read(rid, ofs, &val, sizeof(val))) ? -1 : (int)val);
}

int32_t
bus_read_2(int rid, long ofs)
{
	uint16_t val;

	return ((!bs_read(rid, ofs, &val, sizeof(val))) ? -1 : (int)val);
}

int64_t
bus_read_4(int rid, long ofs)
{
	uint32_t val;

	return ((!bs_read(rid, ofs, &val, sizeof(val))) ? -1 : (int64_t)val);
}

int
bus_write_1(int rid, long ofs, uint8_t val)
{

	return ((!bs_write(rid, ofs, &val, sizeof(val))) ? errno : 0);
}

int
bus_write_2(int rid, long ofs, uint16_t val)
{

	return ((!bs_write(rid, ofs, &val, sizeof(val))) ? errno : 0);
}

int
bus_write_4(int rid, long ofs, uint32_t val)
{

	return ((!bs_write(rid, ofs, &val, sizeof(val))) ? errno : 0);
}

int
bus_map(const char *dev, const char *resource)
{

	return (bs_map(dev, resource));
}

int
bus_unmap(int rid)
{

	return ((!bs_unmap(rid)) ? errno : 0);
}

int
bus_subregion(int rid, long ofs, long sz)
{

	return (bs_subregion(rid, ofs, sz));
}

int
busdma_tag_create(const char *dev, bus_addr_t align, bus_addr_t bndry,
    bus_addr_t maxaddr, bus_size_t maxsz, u_int nsegs, bus_size_t maxsegsz,
    u_int datarate, u_int flags, busdma_tag_t *out_p)
{
	int res;

	res = bd_tag_create(dev, align, bndry, maxaddr, maxsz, nsegs, maxsegsz,
	    datarate, flags);
	if (res == -1)
		return (errno);
	*out_p = res;
	return (0);
}

int
busdma_tag_derive(busdma_tag_t tag, bus_addr_t align, bus_addr_t bndry,
    bus_addr_t maxaddr, bus_size_t maxsz, u_int nsegs, bus_size_t maxsegsz, 
    u_int datarate, u_int flags, busdma_tag_t *out_p)
{
	int res;

	res = bd_tag_derive(tag, align, bndry, maxaddr, maxsz, nsegs, maxsegsz,
	    datarate, flags);
	if (res == -1)
		return (errno);
	*out_p = res;
	return (0);
}

int
busdma_tag_destroy(busdma_tag_t tag)
{

	return (bd_tag_destroy(tag));
}

int
busdma_mem_alloc(busdma_tag_t tag, u_int flags, busdma_md_t *out_p)
{
	int res;

	res = bd_mem_alloc(tag, flags);
	if (res == -1)
		return (errno);
	*out_p = res;
	return (0);
}

int
busdma_mem_free(busdma_md_t md)
{

	return (bd_mem_free(md));
}

int
busdma_md_create(busdma_tag_t tag, u_int flags, busdma_md_t *out_p)
{
	int res;

	res = bd_md_create(tag, flags);
	if (res == -1)
		return (errno);
	*out_p = res;
	return (0);
}

int
busdma_md_destroy(busdma_md_t md)
{

	return (bd_md_destroy(md));
}

int
busdma_md_load(busdma_md_t md, void *buf, size_t len, u_int flags)
{

	return (bd_md_load(md, buf, len, flags));
}

int
busdma_md_unload(busdma_md_t md)
{

	return (bd_md_unload(md));
}

busdma_seg_t
busdma_md_first_seg(busdma_md_t md, int space)
{
	busdma_seg_t seg;

	seg = bd_md_first_seg(md, space);
	return (seg);
}

busdma_seg_t
busdma_md_next_seg(busdma_md_t md, busdma_seg_t seg)
{
 
	seg = bd_md_next_seg(md, seg);
	return (seg);
}

bus_addr_t
busdma_seg_get_addr(busdma_seg_t seg)
{
	u_long addr;
	int error;

	error = bd_seg_get_addr(seg, &addr);
	return ((error) ? ~0UL : addr);
}

bus_size_t
busdma_seg_get_size(busdma_seg_t seg)
{
	u_long size;
	int error;

	error = bd_seg_get_size(seg, &size);
	return ((error) ? ~0UL : size);
}

int
busdma_sync(busdma_md_t md, int op)
{

	return (bd_sync(md, op, 0UL, ~0UL));
}

int
busdma_sync_range(busdma_md_t md, int op, bus_size_t ofs, bus_size_t len)
{

	return (bd_sync(md, op, ofs, len));
}
