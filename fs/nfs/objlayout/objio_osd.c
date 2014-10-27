/*
 *  pNFS Objects layout implementation over open-osd initiator library
 *
 *  Copyright (C) 2009 Panasas Inc. [year of first publication]
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

#include <linux/module.h>
#include <scsi/osd_ore.h>

#include "objlayout.h"
#include "../internal.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

struct objio_dev_ent {
	struct nfs4_deviceid_node id_node;
	struct ore_dev od;
};

static void
objio_free_deviceid_node(struct nfs4_deviceid_node *d)
{
	struct objio_dev_ent *de = container_of(d, struct objio_dev_ent, id_node);

	dprintk("%s: free od=%p\n", __func__, de->od.od);
	osduld_put_device(de->od.od);
	kfree(de);
}

struct objio_segment {
	struct pnfs_layout_segment lseg;

	struct ore_layout layout;
	struct ore_components oc;
};

static inline struct objio_segment *
OBJIO_LSEG(struct pnfs_layout_segment *lseg)
{
	return container_of(lseg, struct objio_segment, lseg);
}

struct objio_state {
	/* Generic layer */
	struct objlayout_io_res oir;

	bool sync;
	/*FIXME: Support for extra_bytes at ore_get_rw_state() */
	struct ore_io_state *ios;
};

/* Send and wait for a get_device_info of devices in the layout,
   then look them up with the osd_initiator library */
struct nfs4_deviceid_node *
objio_alloc_deviceid_node(struct nfs_server *server, struct pnfs_device *pdev,
			gfp_t gfp_flags)
{
	struct pnfs_osd_deviceaddr *deviceaddr;
	struct objio_dev_ent *ode = NULL;
	struct osd_dev *od;
	struct osd_dev_info odi;
	bool retry_flag = true;
	__be32 *p;
	int err;

	deviceaddr = kzalloc(sizeof(*deviceaddr), gfp_flags);
	if (!deviceaddr)
		return NULL;

	p = page_address(pdev->pages[0]);
	pnfs_osd_xdr_decode_deviceaddr(deviceaddr, p);

	odi.systemid_len = deviceaddr->oda_systemid.len;
	if (odi.systemid_len > sizeof(odi.systemid)) {
		dprintk("%s: odi.systemid_len > sizeof(systemid=%zd)\n",
			__func__, sizeof(odi.systemid));
		err = -EINVAL;
		goto out;
	} else if (odi.systemid_len)
		memcpy(odi.systemid, deviceaddr->oda_systemid.data,
		       odi.systemid_len);
	odi.osdname_len	 = deviceaddr->oda_osdname.len;
	odi.osdname	 = (u8 *)deviceaddr->oda_osdname.data;

	if (!odi.osdname_len && !odi.systemid_len) {
		dprintk("%s: !odi.osdname_len && !odi.systemid_len\n",
			__func__);
		err = -ENODEV;
		goto out;
	}

retry_lookup:
	od = osduld_info_lookup(&odi);
	if (unlikely(IS_ERR(od))) {
		err = PTR_ERR(od);
		dprintk("%s: osduld_info_lookup => %d\n", __func__, err);
		if (err == -ENODEV && retry_flag) {
			err = objlayout_autologin(deviceaddr);
			if (likely(!err)) {
				retry_flag = false;
				goto retry_lookup;
			}
		}
		goto out;
	}

	dprintk("Adding new dev_id(%llx:%llx)\n",
		_DEVID_LO(&pdev->dev_id), _DEVID_HI(&pdev->dev_id));

	ode = kzalloc(sizeof(*ode), gfp_flags);
	if (!ode) {
		dprintk("%s: -ENOMEM od=%p\n", __func__, od);
		goto out;
	}

	nfs4_init_deviceid_node(&ode->id_node, server, &pdev->dev_id);
	kfree(deviceaddr);

	ode->od.od = od;
	return &ode->id_node;

out:
	kfree(deviceaddr);
	return NULL;
}

static void copy_single_comp(struct ore_components *oc, unsigned c,
			     struct pnfs_osd_object_cred *src_comp)
{
	struct ore_comp *ocomp = &oc->comps[c];

	WARN_ON(src_comp->oc_cap_key.cred_len > 0); /* libosd is NO_SEC only */
	WARN_ON(src_comp->oc_cap.cred_len > sizeof(ocomp->cred));

	ocomp->obj.partition = src_comp->oc_object_id.oid_partition_id;
	ocomp->obj.id = src_comp->oc_object_id.oid_object_id;

	memcpy(ocomp->cred, src_comp->oc_cap.cred, sizeof(ocomp->cred));
}

static int __alloc_objio_seg(unsigned numdevs, gfp_t gfp_flags,
		       struct objio_segment **pseg)
{
/*	This is the in memory structure of the objio_segment
 *
 *	struct __alloc_objio_segment {
 *		struct objio_segment olseg;
 *		struct ore_dev *ods[numdevs];
 *		struct ore_comp	comps[numdevs];
 *	} *aolseg;
 *	NOTE: The code as above compiles and runs perfectly. It is elegant,
 *	type safe and compact. At some Past time Linus has decided he does not
 *	like variable length arrays, For the sake of this principal we uglify
 *	the code as below.
 */
	struct objio_segment *lseg;
	size_t lseg_size = sizeof(*lseg) +
			numdevs * sizeof(lseg->oc.ods[0]) +
			numdevs * sizeof(*lseg->oc.comps);

	lseg = kzalloc(lseg_size, gfp_flags);
	if (unlikely(!lseg)) {
		dprintk("%s: Failed allocation numdevs=%d size=%zd\n", __func__,
			numdevs, lseg_size);
		return -ENOMEM;
	}

	lseg->oc.numdevs = numdevs;
	lseg->oc.single_comp = EC_MULTPLE_COMPS;
	lseg->oc.ods = (void *)(lseg + 1);
	lseg->oc.comps = (void *)(lseg->oc.ods + numdevs);

	*pseg = lseg;
	return 0;
}

int objio_alloc_lseg(struct pnfs_layout_segment **outp,
	struct pnfs_layout_hdr *pnfslay,
	struct pnfs_layout_range *range,
	struct xdr_stream *xdr,
	gfp_t gfp_flags)
{
	struct nfs_server *server = NFS_SERVER(pnfslay->plh_inode);
	struct objio_segment *objio_seg;
	struct pnfs_osd_xdr_decode_layout_iter iter;
	struct pnfs_osd_layout layout;
	struct pnfs_osd_object_cred src_comp;
	unsigned cur_comp;
	int err;

	err = pnfs_osd_xdr_decode_layout_map(&layout, &iter, xdr);
	if (unlikely(err))
		return err;

	err = __alloc_objio_seg(layout.olo_num_comps, gfp_flags, &objio_seg);
	if (unlikely(err))
		return err;

	objio_seg->layout.stripe_unit = layout.olo_map.odm_stripe_unit;
	objio_seg->layout.group_width = layout.olo_map.odm_group_width;
	objio_seg->layout.group_depth = layout.olo_map.odm_group_depth;
	objio_seg->layout.mirrors_p1 = layout.olo_map.odm_mirror_cnt + 1;
	objio_seg->layout.raid_algorithm = layout.olo_map.odm_raid_algorithm;

	err = ore_verify_layout(layout.olo_map.odm_num_comps,
					  &objio_seg->layout);
	if (unlikely(err))
		goto err;

	objio_seg->oc.first_dev = layout.olo_comps_index;
	cur_comp = 0;
	while (pnfs_osd_xdr_decode_layout_comp(&src_comp, &iter, xdr, &err)) {
		struct nfs4_deviceid_node *d;
		struct objio_dev_ent *ode;

		copy_single_comp(&objio_seg->oc, cur_comp, &src_comp);

		d = nfs4_find_get_deviceid(server,
				&src_comp.oc_object_id.oid_device_id,
				pnfslay->plh_lc_cred, gfp_flags);
		if (!d) {
			err = -ENXIO;
			goto err;
		}

		ode = container_of(d, struct objio_dev_ent, id_node);
		objio_seg->oc.ods[cur_comp++] = &ode->od;
	}
	/* pnfs_osd_xdr_decode_layout_comp returns false on error */
	if (unlikely(err))
		goto err;

	*outp = &objio_seg->lseg;
	return 0;

err:
	kfree(objio_seg);
	dprintk("%s: Error: return %d\n", __func__, err);
	*outp = NULL;
	return err;
}

void objio_free_lseg(struct pnfs_layout_segment *lseg)
{
	int i;
	struct objio_segment *objio_seg = OBJIO_LSEG(lseg);

	for (i = 0; i < objio_seg->oc.numdevs; i++) {
		struct ore_dev *od = objio_seg->oc.ods[i];
		struct objio_dev_ent *ode;

		if (!od)
			break;
		ode = container_of(od, typeof(*ode), od);
		nfs4_put_deviceid_node(&ode->id_node);
	}
	kfree(objio_seg);
}

static int
objio_alloc_io_state(struct pnfs_layout_hdr *pnfs_layout_type, bool is_reading,
	struct pnfs_layout_segment *lseg, struct page **pages, unsigned pgbase,
	loff_t offset, size_t count, void *rpcdata, gfp_t gfp_flags,
	struct objio_state **outp)
{
	struct objio_segment *objio_seg = OBJIO_LSEG(lseg);
	struct ore_io_state *ios;
	int ret;
	struct __alloc_objio_state {
		struct objio_state objios;
		struct pnfs_osd_ioerr ioerrs[objio_seg->oc.numdevs];
	} *aos;

	aos = kzalloc(sizeof(*aos), gfp_flags);
	if (unlikely(!aos))
		return -ENOMEM;

	objlayout_init_ioerrs(&aos->objios.oir, objio_seg->oc.numdevs,
			aos->ioerrs, rpcdata, pnfs_layout_type);

	ret = ore_get_rw_state(&objio_seg->layout, &objio_seg->oc, is_reading,
			       offset, count, &ios);
	if (unlikely(ret)) {
		kfree(aos);
		return ret;
	}

	ios->pages = pages;
	ios->pgbase = pgbase;
	ios->private = aos;
	BUG_ON(ios->nr_pages > (pgbase + count + PAGE_SIZE - 1) >> PAGE_SHIFT);

	aos->objios.sync = 0;
	aos->objios.ios = ios;
	*outp = &aos->objios;
	return 0;
}

void objio_free_result(struct objlayout_io_res *oir)
{
	struct objio_state *objios = container_of(oir, struct objio_state, oir);

	ore_put_io_state(objios->ios);
	kfree(objios);
}

static enum pnfs_osd_errno osd_pri_2_pnfs_err(enum osd_err_priority oep)
{
	switch (oep) {
	case OSD_ERR_PRI_NO_ERROR:
		return (enum pnfs_osd_errno)0;

	case OSD_ERR_PRI_CLEAR_PAGES:
		BUG_ON(1);
		return 0;

	case OSD_ERR_PRI_RESOURCE:
		return PNFS_OSD_ERR_RESOURCE;
	case OSD_ERR_PRI_BAD_CRED:
		return PNFS_OSD_ERR_BAD_CRED;
	case OSD_ERR_PRI_NO_ACCESS:
		return PNFS_OSD_ERR_NO_ACCESS;
	case OSD_ERR_PRI_UNREACHABLE:
		return PNFS_OSD_ERR_UNREACHABLE;
	case OSD_ERR_PRI_NOT_FOUND:
		return PNFS_OSD_ERR_NOT_FOUND;
	case OSD_ERR_PRI_NO_SPACE:
		return PNFS_OSD_ERR_NO_SPACE;
	default:
		WARN_ON(1);
		/* fallthrough */
	case OSD_ERR_PRI_EIO:
		return PNFS_OSD_ERR_EIO;
	}
}

static void __on_dev_error(struct ore_io_state *ios,
	struct ore_dev *od, unsigned dev_index, enum osd_err_priority oep,
	u64 dev_offset, u64  dev_len)
{
	struct objio_state *objios = ios->private;
	struct pnfs_osd_objid pooid;
	struct objio_dev_ent *ode = container_of(od, typeof(*ode), od);
	/* FIXME: what to do with more-then-one-group layouts. We need to
	 * translate from ore_io_state index to oc->comps index
	 */
	unsigned comp = dev_index;

	pooid.oid_device_id = ode->id_node.deviceid;
	pooid.oid_partition_id = ios->oc->comps[comp].obj.partition;
	pooid.oid_object_id = ios->oc->comps[comp].obj.id;

	objlayout_io_set_result(&objios->oir, comp,
				&pooid, osd_pri_2_pnfs_err(oep),
				dev_offset, dev_len, !ios->reading);
}

/*
 * read
 */
static void _read_done(struct ore_io_state *ios, void *private)
{
	struct objio_state *objios = private;
	ssize_t status;
	int ret = ore_check_io(ios, &__on_dev_error);

	/* FIXME: _io_free(ios) can we dealocate the libosd resources; */

	if (likely(!ret))
		status = ios->length;
	else
		status = ret;

	objlayout_read_done(&objios->oir, status, objios->sync);
}

int objio_read_pagelist(struct nfs_pgio_header *hdr)
{
	struct objio_state *objios;
	int ret;

	ret = objio_alloc_io_state(NFS_I(hdr->inode)->layout, true,
			hdr->lseg, hdr->args.pages, hdr->args.pgbase,
			hdr->args.offset, hdr->args.count, hdr,
			GFP_KERNEL, &objios);
	if (unlikely(ret))
		return ret;

	objios->ios->done = _read_done;
	dprintk("%s: offset=0x%llx length=0x%x\n", __func__,
		hdr->args.offset, hdr->args.count);
	ret = ore_read(objios->ios);
	if (unlikely(ret))
		objio_free_result(&objios->oir);
	return ret;
}

/*
 * write
 */
static void _write_done(struct ore_io_state *ios, void *private)
{
	struct objio_state *objios = private;
	ssize_t status;
	int ret = ore_check_io(ios, &__on_dev_error);

	/* FIXME: _io_free(ios) can we dealocate the libosd resources; */

	if (likely(!ret)) {
		/* FIXME: should be based on the OSD's persistence model
		 * See OSD2r05 Section 4.13 Data persistence model */
		objios->oir.committed = NFS_FILE_SYNC;
		status = ios->length;
	} else {
		status = ret;
	}

	objlayout_write_done(&objios->oir, status, objios->sync);
}

static struct page *__r4w_get_page(void *priv, u64 offset, bool *uptodate)
{
	struct objio_state *objios = priv;
	struct nfs_pgio_header *hdr = objios->oir.rpcdata;
	struct address_space *mapping = hdr->inode->i_mapping;
	pgoff_t index = offset / PAGE_SIZE;
	struct page *page;
	loff_t i_size = i_size_read(hdr->inode);

	if (offset >= i_size) {
		*uptodate = true;
		dprintk("%s: g_zero_page index=0x%lx\n", __func__, index);
		return ZERO_PAGE(0);
	}

	page = find_get_page(mapping, index);
	if (!page) {
		page = find_or_create_page(mapping, index, GFP_NOFS);
		if (unlikely(!page)) {
			dprintk("%s: grab_cache_page Failed index=0x%lx\n",
				__func__, index);
			return NULL;
		}
		unlock_page(page);
	}
	if (PageDirty(page) || PageWriteback(page))
		*uptodate = true;
	else
		*uptodate = PageUptodate(page);
	dprintk("%s: index=0x%lx uptodate=%d\n", __func__, index, *uptodate);
	return page;
}

static void __r4w_put_page(void *priv, struct page *page)
{
	dprintk("%s: index=0x%lx\n", __func__,
		(page == ZERO_PAGE(0)) ? -1UL : page->index);
	if (ZERO_PAGE(0) != page)
		page_cache_release(page);
	return;
}

static const struct _ore_r4w_op _r4w_op = {
	.get_page = &__r4w_get_page,
	.put_page = &__r4w_put_page,
};

int objio_write_pagelist(struct nfs_pgio_header *hdr, int how)
{
	struct objio_state *objios;
	int ret;

	ret = objio_alloc_io_state(NFS_I(hdr->inode)->layout, false,
			hdr->lseg, hdr->args.pages, hdr->args.pgbase,
			hdr->args.offset, hdr->args.count, hdr, GFP_NOFS,
			&objios);
	if (unlikely(ret))
		return ret;

	objios->sync = 0 != (how & FLUSH_SYNC);
	objios->ios->r4w = &_r4w_op;

	if (!objios->sync)
		objios->ios->done = _write_done;

	dprintk("%s: offset=0x%llx length=0x%x\n", __func__,
		hdr->args.offset, hdr->args.count);
	ret = ore_write(objios->ios);
	if (unlikely(ret)) {
		objio_free_result(&objios->oir);
		return ret;
	}

	if (objios->sync)
		_write_done(objios->ios, objios);

	return 0;
}

/*
 * Return 0 if @req cannot be coalesced into @pgio, otherwise return the number
 * of bytes (maximum @req->wb_bytes) that can be coalesced.
 */
static size_t objio_pg_test(struct nfs_pageio_descriptor *pgio,
			  struct nfs_page *prev, struct nfs_page *req)
{
	unsigned int size;

	size = pnfs_generic_pg_test(pgio, prev, req);

	if (!size || pgio->pg_count + req->wb_bytes >
	    (unsigned long)pgio->pg_layout_private)
		return 0;

	return min(size, req->wb_bytes);
}

static void objio_init_read(struct nfs_pageio_descriptor *pgio, struct nfs_page *req)
{
	pnfs_generic_pg_init_read(pgio, req);
	if (unlikely(pgio->pg_lseg == NULL))
		return; /* Not pNFS */

	pgio->pg_layout_private = (void *)
				OBJIO_LSEG(pgio->pg_lseg)->layout.max_io_length;
}

static bool aligned_on_raid_stripe(u64 offset, struct ore_layout *layout,
				   unsigned long *stripe_end)
{
	u32 stripe_off;
	unsigned stripe_size;

	if (layout->raid_algorithm == PNFS_OSD_RAID_0)
		return true;

	stripe_size = layout->stripe_unit *
				(layout->group_width - layout->parity);

	div_u64_rem(offset, stripe_size, &stripe_off);
	if (!stripe_off)
		return true;

	*stripe_end = stripe_size - stripe_off;
	return false;
}

static void objio_init_write(struct nfs_pageio_descriptor *pgio, struct nfs_page *req)
{
	unsigned long stripe_end = 0;
	u64 wb_size;

	if (pgio->pg_dreq == NULL)
		wb_size = i_size_read(pgio->pg_inode) - req_offset(req);
	else
		wb_size = nfs_dreq_bytes_left(pgio->pg_dreq);

	pnfs_generic_pg_init_write(pgio, req, wb_size);
	if (unlikely(pgio->pg_lseg == NULL))
		return; /* Not pNFS */

	if (req->wb_offset ||
	    !aligned_on_raid_stripe(req->wb_index * PAGE_SIZE,
			       &OBJIO_LSEG(pgio->pg_lseg)->layout,
			       &stripe_end)) {
		pgio->pg_layout_private = (void *)stripe_end;
	} else {
		pgio->pg_layout_private = (void *)
				OBJIO_LSEG(pgio->pg_lseg)->layout.max_io_length;
	}
}

static const struct nfs_pageio_ops objio_pg_read_ops = {
	.pg_init = objio_init_read,
	.pg_test = objio_pg_test,
	.pg_doio = pnfs_generic_pg_readpages,
};

static const struct nfs_pageio_ops objio_pg_write_ops = {
	.pg_init = objio_init_write,
	.pg_test = objio_pg_test,
	.pg_doio = pnfs_generic_pg_writepages,
};

static struct pnfs_layoutdriver_type objlayout_type = {
	.id = LAYOUT_OSD2_OBJECTS,
	.name = "LAYOUT_OSD2_OBJECTS",
	.flags                   = PNFS_LAYOUTRET_ON_SETATTR |
				   PNFS_LAYOUTRET_ON_ERROR,

	.max_deviceinfo_size	 = PAGE_SIZE,
	.owner		       	 = THIS_MODULE,
	.alloc_layout_hdr        = objlayout_alloc_layout_hdr,
	.free_layout_hdr         = objlayout_free_layout_hdr,

	.alloc_lseg              = objlayout_alloc_lseg,
	.free_lseg               = objlayout_free_lseg,

	.read_pagelist           = objlayout_read_pagelist,
	.write_pagelist          = objlayout_write_pagelist,
	.pg_read_ops             = &objio_pg_read_ops,
	.pg_write_ops            = &objio_pg_write_ops,

	.free_deviceid_node	 = objio_free_deviceid_node,

	.encode_layoutcommit	 = objlayout_encode_layoutcommit,
	.encode_layoutreturn     = objlayout_encode_layoutreturn,
};

MODULE_DESCRIPTION("pNFS Layout Driver for OSD2 objects");
MODULE_AUTHOR("Benny Halevy <bhalevy@panasas.com>");
MODULE_LICENSE("GPL");

static int __init
objlayout_init(void)
{
	int ret = pnfs_register_layoutdriver(&objlayout_type);

	if (ret)
		printk(KERN_INFO
			"NFS: %s: Registering OSD pNFS Layout Driver failed: error=%d\n",
			__func__, ret);
	else
		printk(KERN_INFO "NFS: %s: Registered OSD pNFS Layout Driver\n",
			__func__);
	return ret;
}

static void __exit
objlayout_exit(void)
{
	pnfs_unregister_layoutdriver(&objlayout_type);
	printk(KERN_INFO "NFS: %s: Unregistered OSD pNFS Layout Driver\n",
	       __func__);
}

MODULE_ALIAS("nfs-layouttype4-2");

module_init(objlayout_init);
module_exit(objlayout_exit);
