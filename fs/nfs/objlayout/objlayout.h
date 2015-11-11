/*
 *  Data types and function declerations for interfacing with the
 *  pNFS standard object layout driver.
 *
 *  Copyright (C) 2007 Panasas Inc. [year of first publication]
 *  All rights reserved.
 *
 *  Benny Halevy <bhalevy@panasas.com>
 *  Boaz Harrosh <ooo@electrozaur.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  See the file COPYING included with this distribution for more details.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the Panasas company nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _OBJLAYOUT_H
#define _OBJLAYOUT_H

#include <linux/nfs_fs.h>
#include <linux/pnfs_osd_xdr.h>
#include "../pnfs.h"

/*
 * per-inode layout
 */
struct objlayout {
	struct pnfs_layout_hdr pnfs_layout;

	 /* for layout_commit */
	enum osd_delta_space_valid_enum {
		OBJ_DSU_INIT = 0,
		OBJ_DSU_VALID,
		OBJ_DSU_INVALID,
	} delta_space_valid;
	s64 delta_space_used;  /* consumed by write ops */

	 /* for layout_return */
	spinlock_t lock;
	struct list_head err_list;
};

static inline struct objlayout *
OBJLAYOUT(struct pnfs_layout_hdr *lo)
{
	return container_of(lo, struct objlayout, pnfs_layout);
}

/*
 * per-I/O operation state
 * embedded in objects provider io_state data structure
 */
struct objlayout_io_res {
	struct objlayout *objlay;

	void *rpcdata;
	int status;             /* res */
	int committed;          /* res */

	/* Error reporting (layout_return) */
	struct list_head err_list;
	unsigned num_comps;
	/* Pointer to array of error descriptors of size num_comps.
	 * It should contain as many entries as devices in the osd_layout
	 * that participate in the I/O. It is up to the io_engine to allocate
	 * needed space and set num_comps.
	 */
	struct pnfs_osd_ioerr *ioerrs;
};

static inline
void objlayout_init_ioerrs(struct objlayout_io_res *oir, unsigned num_comps,
			struct pnfs_osd_ioerr *ioerrs, void *rpcdata,
			struct pnfs_layout_hdr *pnfs_layout_type)
{
	oir->objlay = OBJLAYOUT(pnfs_layout_type);
	oir->rpcdata = rpcdata;
	INIT_LIST_HEAD(&oir->err_list);
	oir->num_comps = num_comps;
	oir->ioerrs = ioerrs;
}

/*
 * Raid engine I/O API
 */
extern int objio_alloc_lseg(struct pnfs_layout_segment **outp,
	struct pnfs_layout_hdr *pnfslay,
	struct pnfs_layout_range *range,
	struct xdr_stream *xdr,
	gfp_t gfp_flags);
extern void objio_free_lseg(struct pnfs_layout_segment *lseg);

/* objio_free_result will free these @oir structs received from
 * objlayout_{read,write}_done
 */
extern void objio_free_result(struct objlayout_io_res *oir);

extern int objio_read_pagelist(struct nfs_pgio_header *rdata);
extern int objio_write_pagelist(struct nfs_pgio_header *wdata, int how);

/*
 * callback API
 */
extern void objlayout_io_set_result(struct objlayout_io_res *oir,
			unsigned index, struct pnfs_osd_objid *pooid,
			int osd_error, u64 offset, u64 length, bool is_write);

static inline void
objlayout_add_delta_space_used(struct objlayout *objlay, s64 space_used)
{
	/* If one of the I/Os errored out and the delta_space_used was
	 * invalid we render the complete report as invalid. Protocol mandate
	 * the DSU be accurate or not reported.
	 */
	spin_lock(&objlay->lock);
	if (objlay->delta_space_valid != OBJ_DSU_INVALID) {
		objlay->delta_space_valid = OBJ_DSU_VALID;
		objlay->delta_space_used += space_used;
	}
	spin_unlock(&objlay->lock);
}

extern void objlayout_read_done(struct objlayout_io_res *oir,
				ssize_t status, bool sync);
extern void objlayout_write_done(struct objlayout_io_res *oir,
				 ssize_t status, bool sync);

/*
 * exported generic objects function vectors
 */

extern struct pnfs_layout_hdr *objlayout_alloc_layout_hdr(struct inode *, gfp_t gfp_flags);
extern void objlayout_free_layout_hdr(struct pnfs_layout_hdr *);

extern struct pnfs_layout_segment *objlayout_alloc_lseg(
	struct pnfs_layout_hdr *,
	struct nfs4_layoutget_res *,
	gfp_t gfp_flags);
extern void objlayout_free_lseg(struct pnfs_layout_segment *);

extern enum pnfs_try_status objlayout_read_pagelist(
	struct nfs_pgio_header *);

extern enum pnfs_try_status objlayout_write_pagelist(
	struct nfs_pgio_header *,
	int how);

extern void objlayout_encode_layoutcommit(
	struct pnfs_layout_hdr *,
	struct xdr_stream *,
	const struct nfs4_layoutcommit_args *);

extern void objlayout_encode_layoutreturn(
	struct pnfs_layout_hdr *,
	struct xdr_stream *,
	const struct nfs4_layoutreturn_args *);

extern int objlayout_autologin(struct pnfs_osd_deviceaddr *deviceaddr);

#endif /* _OBJLAYOUT_H */
