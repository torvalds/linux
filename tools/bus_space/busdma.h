/*-
 * Copyright (c) 2015 Marcel Moolenaar
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

#ifndef _TOOLS_BUS_DMA_H_
#define	_TOOLS_BUS_DMA_H_

int	bd_tag_create(const char *dev, u_long align, u_long bndry,
	    u_long maxaddr, u_long maxsz, u_int nsegs, u_long maxsegsz,
	    u_int datarate, u_int flags);
int	bd_tag_derive(int tid, u_long align, u_long bndry, u_long maxaddr,
	    u_long maxsz, u_int nsegs, u_long maxsegsz, u_int datarate,
	    u_int flags);
int	bd_tag_destroy(int tid);

int	bd_md_create(int tid, u_int flags);
int	bd_md_destroy(int mdid);
int	bd_md_load(int mdid, void *buf, u_long len, u_int flags);
int	bd_md_unload(int mdid);

int	bd_mem_alloc(int tid, u_int flags);
int	bd_mem_free(int mdid);

int	bd_md_first_seg(int mdid, int what);
int	bd_md_next_seg(int mdid, int sid);

int	bd_seg_get_addr(int sid, u_long *);
int	bd_seg_get_size(int sid, u_long *);

int	bd_sync(int mdid, u_int op, u_long ofs, u_long len);

#endif /* _TOOLS_BUS_DMA_H_ */
