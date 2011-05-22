/*
 *  pNFS Objects layout driver high level definitions
 *
 *  Copyright (C) 2007 Panasas Inc. [year of first publication]
 *  All rights reserved.
 *
 *  Benny Halevy <bhalevy@panasas.com>
 *  Boaz Harrosh <bharrosh@panasas.com>
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

#include <scsi/osd_initiator.h>
#include "objlayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD
/*
 * Unmarshall layout and store it in pnfslay.
 */
struct pnfs_layout_segment *
objlayout_alloc_lseg(struct pnfs_layout_hdr *pnfslay,
		     struct nfs4_layoutget_res *lgr,
		     gfp_t gfp_flags)
{
	int status = -ENOMEM;
	struct xdr_stream stream;
	struct xdr_buf buf = {
		.pages =  lgr->layoutp->pages,
		.page_len =  lgr->layoutp->len,
		.buflen =  lgr->layoutp->len,
		.len = lgr->layoutp->len,
	};
	struct page *scratch;
	struct pnfs_layout_segment *lseg;

	dprintk("%s: Begin pnfslay %p\n", __func__, pnfslay);

	scratch = alloc_page(gfp_flags);
	if (!scratch)
		goto err_nofree;

	xdr_init_decode(&stream, &buf, NULL);
	xdr_set_scratch_buffer(&stream, page_address(scratch), PAGE_SIZE);

	status = objio_alloc_lseg(&lseg, pnfslay, &lgr->range, &stream, gfp_flags);
	if (unlikely(status)) {
		dprintk("%s: objio_alloc_lseg Return err %d\n", __func__,
			status);
		goto err;
	}

	__free_page(scratch);

	dprintk("%s: Return %p\n", __func__, lseg);
	return lseg;

err:
	__free_page(scratch);
err_nofree:
	dprintk("%s: Err Return=>%d\n", __func__, status);
	return ERR_PTR(status);
}

/*
 * Free a layout segement
 */
void
objlayout_free_lseg(struct pnfs_layout_segment *lseg)
{
	dprintk("%s: freeing layout segment %p\n", __func__, lseg);

	if (unlikely(!lseg))
		return;

	objio_free_lseg(lseg);
}

