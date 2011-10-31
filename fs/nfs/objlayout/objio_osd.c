/*
 *  pNFS Objects layout implementation over open-osd initiator library
 *
 *  Copyright (C) 2009 Panasas Inc. [year of first publication]
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

#include <linux/module.h>
#include <scsi/osd_ore.h>

#include "objlayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

#define _LLU(x) ((unsigned long long)x)

enum { BIO_MAX_PAGES_KMALLOC =
		(PAGE_SIZE - sizeof(struct bio)) / sizeof(struct bio_vec),
};

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

static struct objio_dev_ent *_dev_list_find(const struct nfs_server *nfss,
	const struct nfs4_deviceid *d_id)
{
	struct nfs4_deviceid_node *d;
	struct objio_dev_ent *de;

	d = nfs4_find_get_deviceid(nfss->pnfs_curr_ld, nfss->nfs_client, d_id);
	if (!d)
		return NULL;

	de = container_of(d, struct objio_dev_ent, id_node);
	return de;
}

static struct objio_dev_ent *
_dev_list_add(const struct nfs_server *nfss,
	const struct nfs4_deviceid *d_id, struct osd_dev *od,
	gfp_t gfp_flags)
{
	struct nfs4_deviceid_node *d;
	struct objio_dev_ent *de = kzalloc(sizeof(*de), gfp_flags);
	struct objio_dev_ent *n;

	if (!de) {
		dprintk("%s: -ENOMEM od=%p\n", __func__, od);
		return NULL;
	}

	dprintk("%s: Adding od=%p\n", __func__, od);
	nfs4_init_deviceid_node(&de->id_node,
				nfss->pnfs_curr_ld,
				nfss->nfs_client,
				d_id);
	de->od.od = od;

	d = nfs4_insert_deviceid_node(&de->id_node);
	n = container_of(d, struct objio_dev_ent, id_node);
	if (n != de) {
		dprintk("%s: Race with other n->od=%p\n", __func__, n->od.od);
		objio_free_deviceid_node(&de->id_node);
		de = n;
	}

	return de;
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

struct objio_state;
typedef int (*objio_done_fn)(struct objio_state *ios);

struct objio_state {
	/* Generic layer */
	struct objlayout_io_res oir;

	struct page **pages;
	unsigned pgbase;
	unsigned nr_pages;
	unsigned long count;
	loff_t offset;
	bool sync;

	struct ore_layout *layout;
	struct ore_components *oc;

	struct kref kref;
	objio_done_fn done;
	void *private;

	unsigned long length;
	unsigned numdevs; /* Actually used devs in this IO */
	/* A per-device variable array of size numdevs */
	struct _objio_per_comp {
		struct bio *bio;
		struct osd_request *or;
		unsigned long length;
		u64 offset;
		unsigned dev;
	} per_dev[];
};

/* Send and wait for a get_device_info of devices in the layout,
   then look them up with the osd_initiator library */
static int objio_devices_lookup(struct pnfs_layout_hdr *pnfslay,
	struct objio_segment *objio_seg, unsigned c, struct nfs4_deviceid *d_id,
	gfp_t gfp_flags)
{
	struct pnfs_osd_deviceaddr *deviceaddr;
	struct objio_dev_ent *ode;
	struct osd_dev *od;
	struct osd_dev_info odi;
	int err;

	ode = _dev_list_find(NFS_SERVER(pnfslay->plh_inode), d_id);
	if (ode) {
		objio_seg->oc.ods[c] = &ode->od; /* must use container_of */
		return 0;
	}

	err = objlayout_get_deviceinfo(pnfslay, d_id, &deviceaddr, gfp_flags);
	if (unlikely(err)) {
		dprintk("%s: objlayout_get_deviceinfo dev(%llx:%llx) =>%d\n",
			__func__, _DEVID_LO(d_id), _DEVID_HI(d_id), err);
		return err;
	}

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

	od = osduld_info_lookup(&odi);
	if (unlikely(IS_ERR(od))) {
		err = PTR_ERR(od);
		dprintk("%s: osduld_info_lookup => %d\n", __func__, err);
		goto out;
	}

	ode = _dev_list_add(NFS_SERVER(pnfslay->plh_inode), d_id, od,
			    gfp_flags);
	objio_seg->oc.ods[c] = &ode->od; /* must use container_of */
	dprintk("Adding new dev_id(%llx:%llx)\n",
		_DEVID_LO(d_id), _DEVID_HI(d_id));
out:
	objlayout_put_deviceinfo(deviceaddr);
	return err;
}

#if 0
static int _verify_data_map(struct pnfs_osd_layout *layout)
{
	struct pnfs_osd_data_map *data_map = &layout->olo_map;
	u64 stripe_length;
	u32 group_width;

/* FIXME: Only raid0 for now. if not go through MDS */
	if (data_map->odm_raid_algorithm != PNFS_OSD_RAID_0) {
		printk(KERN_ERR "Only RAID_0 for now\n");
		return -ENOTSUPP;
	}
	if (0 != (data_map->odm_num_comps % (data_map->odm_mirror_cnt + 1))) {
		printk(KERN_ERR "Data Map wrong, num_comps=%u mirrors=%u\n",
			  data_map->odm_num_comps, data_map->odm_mirror_cnt);
		return -EINVAL;
	}

	if (data_map->odm_group_width)
		group_width = data_map->odm_group_width;
	else
		group_width = data_map->odm_num_comps /
						(data_map->odm_mirror_cnt + 1);

	stripe_length = (u64)data_map->odm_stripe_unit * group_width;
	if (stripe_length >= (1ULL << 32)) {
		printk(KERN_ERR "Total Stripe length(0x%llx)"
			  " >= 32bit is not supported\n", _LLU(stripe_length));
		return -ENOTSUPP;
	}

	if (0 != (data_map->odm_stripe_unit & ~PAGE_MASK)) {
		printk(KERN_ERR "Stripe Unit(0x%llx)"
			  " must be Multples of PAGE_SIZE(0x%lx)\n",
			  _LLU(data_map->odm_stripe_unit), PAGE_SIZE);
		return -ENOTSUPP;
	}

	return 0;
}
#endif

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

int __alloc_objio_seg(unsigned numdevs, gfp_t gfp_flags,
		       struct objio_segment **pseg)
{
	struct __alloc_objio_segment {
		struct objio_segment olseg;
		struct ore_dev *ods[numdevs];
		struct ore_comp	comps[numdevs];
	} *aolseg;

	aolseg = kzalloc(sizeof(*aolseg), gfp_flags);
	if (unlikely(!aolseg)) {
		dprintk("%s: Faild allocation numdevs=%d size=%zd\n", __func__,
			numdevs, sizeof(*aolseg));
		return -ENOMEM;
	}

	aolseg->olseg.oc.numdevs = numdevs;
	aolseg->olseg.oc.single_comp = EC_MULTPLE_COMPS;
	aolseg->olseg.oc.comps = aolseg->comps;
	aolseg->olseg.oc.ods = aolseg->ods;

	*pseg = &aolseg->olseg;
	return 0;
}

int objio_alloc_lseg(struct pnfs_layout_segment **outp,
	struct pnfs_layout_hdr *pnfslay,
	struct pnfs_layout_range *range,
	struct xdr_stream *xdr,
	gfp_t gfp_flags)
{
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
		copy_single_comp(&objio_seg->oc, cur_comp, &src_comp);
		err = objio_devices_lookup(pnfslay, objio_seg, cur_comp,
					   &src_comp.oc_object_id.oid_device_id,
					   gfp_flags);
		if (err)
			goto err;
		++cur_comp;
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
objio_alloc_io_state(struct pnfs_layout_hdr *pnfs_layout_type,
	struct pnfs_layout_segment *lseg, struct page **pages, unsigned pgbase,
	loff_t offset, size_t count, void *rpcdata, gfp_t gfp_flags,
	struct objio_state **outp)
{
	struct objio_segment *objio_seg = OBJIO_LSEG(lseg);
	struct objio_state *ios;
	struct __alloc_objio_state {
		struct objio_state objios;
		struct _objio_per_comp per_dev[objio_seg->oc.numdevs];
		struct pnfs_osd_ioerr ioerrs[objio_seg->oc.numdevs];
	} *aos;

	aos = kzalloc(sizeof(*aos), gfp_flags);
	if (unlikely(!aos))
		return -ENOMEM;

	ios = &aos->objios;

	ios->layout = &objio_seg->layout;
	ios->oc = &objio_seg->oc;
	objlayout_init_ioerrs(&aos->objios.oir, objio_seg->oc.numdevs,
			aos->ioerrs, rpcdata, pnfs_layout_type);

	ios->pages = pages;
	ios->pgbase = pgbase;
	ios->nr_pages = (pgbase + count + PAGE_SIZE - 1) >> PAGE_SHIFT;
	ios->offset = offset;
	ios->count = count;
	ios->sync = 0;
	BUG_ON(ios->nr_pages > (pgbase + count + PAGE_SIZE - 1) >> PAGE_SHIFT);

	*outp = ios;
	return 0;
}

void objio_free_result(struct objlayout_io_res *oir)
{
	struct objio_state *ios = container_of(oir, struct objio_state, oir);

	kfree(ios);
}

enum pnfs_osd_errno osd_pri_2_pnfs_err(enum osd_err_priority oep)
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

static void __on_dev_error(struct objio_state *ios, bool is_write,
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
				dev_offset, dev_len, is_write);
}

static void _clear_bio(struct bio *bio)
{
	struct bio_vec *bv;
	unsigned i;

	__bio_for_each_segment(bv, bio, i, 0) {
		unsigned this_count = bv->bv_len;

		if (likely(PAGE_SIZE == this_count))
			clear_highpage(bv->bv_page);
		else
			zero_user(bv->bv_page, bv->bv_offset, this_count);
	}
}

static int _io_check(struct objio_state *ios, bool is_write)
{
	enum osd_err_priority oep = OSD_ERR_PRI_NO_ERROR;
	int lin_ret = 0;
	int i;

	for (i = 0; i <  ios->numdevs; i++) {
		struct osd_sense_info osi;
		struct osd_request *or = ios->per_dev[i].or;
		int ret;

		if (!or)
			continue;

		ret = osd_req_decode_sense(or, &osi);
		if (likely(!ret))
			continue;

		if (OSD_ERR_PRI_CLEAR_PAGES == osi.osd_err_pri) {
			/* start read offset passed endof file */
			BUG_ON(is_write);
			_clear_bio(ios->per_dev[i].bio);
			dprintk("%s: start read offset passed end of file "
				"offset=0x%llx, length=0x%lx\n", __func__,
				_LLU(ios->per_dev[i].offset),
				ios->per_dev[i].length);

			continue; /* we recovered */
		}
		__on_dev_error(ios, is_write, ios->oc->ods[i],
				ios->per_dev[i].dev, osi.osd_err_pri,
				ios->per_dev[i].offset, ios->per_dev[i].length);

		if (osi.osd_err_pri >= oep) {
			oep = osi.osd_err_pri;
			lin_ret = ret;
		}
	}

	return lin_ret;
}

/*
 * Common IO state helpers.
 */
static void _io_free(struct objio_state *ios)
{
	unsigned i;

	for (i = 0; i < ios->numdevs; i++) {
		struct _objio_per_comp *per_dev = &ios->per_dev[i];

		if (per_dev->or) {
			osd_end_request(per_dev->or);
			per_dev->or = NULL;
		}

		if (per_dev->bio) {
			bio_put(per_dev->bio);
			per_dev->bio = NULL;
		}
	}
}

struct osd_dev *_io_od(struct objio_state *ios, unsigned dev)
{
	unsigned min_dev = ios->oc->first_dev;
	unsigned max_dev = min_dev + ios->oc->numdevs;

	BUG_ON(dev < min_dev || max_dev <= dev);
	return ios->oc->ods[dev - min_dev]->od;
}

struct _striping_info {
	u64 obj_offset;
	u64 group_length;
	unsigned dev;
	unsigned unit_off;
};

static void _calc_stripe_info(struct objio_state *ios, u64 file_offset,
			      struct _striping_info *si)
{
	u32	stripe_unit = ios->layout->stripe_unit;
	u32	group_width = ios->layout->group_width;
	u64	group_depth = ios->layout->group_depth;
	u32	U = stripe_unit * group_width;

	u64	T = U * group_depth;
	u64	S = T * ios->layout->group_count;
	u64	M = div64_u64(file_offset, S);

	/*
	G = (L - (M * S)) / T
	H = (L - (M * S)) % T
	*/
	u64	LmodU = file_offset - M * S;
	u32	G = div64_u64(LmodU, T);
	u64	H = LmodU - G * T;

	u32	N = div_u64(H, U);

	div_u64_rem(file_offset, stripe_unit, &si->unit_off);
	si->obj_offset = si->unit_off + (N * stripe_unit) +
				  (M * group_depth * stripe_unit);

	/* "H - (N * U)" is just "H % U" so it's bound to u32 */
	si->dev = (u32)(H - (N * U)) / stripe_unit + G * group_width;
	si->dev *= ios->layout->mirrors_p1;

	si->group_length = T - H;
}

static int _add_stripe_unit(struct objio_state *ios,  unsigned *cur_pg,
		unsigned pgbase, struct _objio_per_comp *per_dev, int len,
		gfp_t gfp_flags)
{
	unsigned pg = *cur_pg;
	int cur_len = len;
	struct request_queue *q =
			osd_request_queue(_io_od(ios, per_dev->dev));

	if (per_dev->bio == NULL) {
		unsigned pages_in_stripe = ios->layout->group_width *
				      (ios->layout->stripe_unit / PAGE_SIZE);
		unsigned bio_size = (ios->nr_pages + pages_in_stripe) /
				    ios->layout->group_width;

		if (BIO_MAX_PAGES_KMALLOC < bio_size)
			bio_size = BIO_MAX_PAGES_KMALLOC;

		per_dev->bio = bio_kmalloc(gfp_flags, bio_size);
		if (unlikely(!per_dev->bio)) {
			dprintk("Faild to allocate BIO size=%u\n", bio_size);
			return -ENOMEM;
		}
	}

	while (cur_len > 0) {
		unsigned pglen = min_t(unsigned, PAGE_SIZE - pgbase, cur_len);
		unsigned added_len;

		BUG_ON(ios->nr_pages <= pg);
		cur_len -= pglen;

		added_len = bio_add_pc_page(q, per_dev->bio,
					ios->pages[pg], pglen, pgbase);
		if (unlikely(pglen != added_len))
			return -ENOMEM;
		pgbase = 0;
		++pg;
	}
	BUG_ON(cur_len);

	per_dev->length += len;
	*cur_pg = pg;
	return 0;
}

static int _prepare_one_group(struct objio_state *ios, u64 length,
			      struct _striping_info *si, unsigned *last_pg,
			      gfp_t gfp_flags)
{
	unsigned stripe_unit = ios->layout->stripe_unit;
	unsigned mirrors_p1 = ios->layout->mirrors_p1;
	unsigned devs_in_group = ios->layout->group_width * mirrors_p1;
	unsigned dev = si->dev;
	unsigned first_dev = dev - (dev % devs_in_group);
	unsigned max_comp = ios->numdevs ? ios->numdevs - mirrors_p1 : 0;
	unsigned cur_pg = *last_pg;
	int ret = 0;

	while (length) {
		struct _objio_per_comp *per_dev = &ios->per_dev[dev - first_dev];
		unsigned cur_len, page_off = 0;

		if (!per_dev->length) {
			per_dev->dev = dev;
			if (dev < si->dev) {
				per_dev->offset = si->obj_offset + stripe_unit -
								   si->unit_off;
				cur_len = stripe_unit;
			} else if (dev == si->dev) {
				per_dev->offset = si->obj_offset;
				cur_len = stripe_unit - si->unit_off;
				page_off = si->unit_off & ~PAGE_MASK;
				BUG_ON(page_off &&
				      (page_off != ios->pgbase));
			} else { /* dev > si->dev */
				per_dev->offset = si->obj_offset - si->unit_off;
				cur_len = stripe_unit;
			}

			if (max_comp < dev - first_dev)
				max_comp = dev - first_dev;
		} else {
			cur_len = stripe_unit;
		}
		if (cur_len >= length)
			cur_len = length;

		ret = _add_stripe_unit(ios, &cur_pg, page_off , per_dev,
				       cur_len, gfp_flags);
		if (unlikely(ret))
			goto out;

		dev += mirrors_p1;
		dev = (dev % devs_in_group) + first_dev;

		length -= cur_len;
		ios->length += cur_len;
	}
out:
	ios->numdevs = max_comp + mirrors_p1;
	*last_pg = cur_pg;
	return ret;
}

static int _io_rw_pagelist(struct objio_state *ios, gfp_t gfp_flags)
{
	u64 length = ios->count;
	u64 offset = ios->offset;
	struct _striping_info si;
	unsigned last_pg = 0;
	int ret = 0;

	while (length) {
		_calc_stripe_info(ios, offset, &si);

		if (length < si.group_length)
			si.group_length = length;

		ret = _prepare_one_group(ios, si.group_length, &si, &last_pg, gfp_flags);
		if (unlikely(ret))
			goto out;

		offset += si.group_length;
		length -= si.group_length;
	}

out:
	if (!ios->length)
		return ret;

	return 0;
}

static int _sync_done(struct objio_state *ios)
{
	struct completion *waiting = ios->private;

	complete(waiting);
	return 0;
}

static void _last_io(struct kref *kref)
{
	struct objio_state *ios = container_of(kref, struct objio_state, kref);

	ios->done(ios);
}

static void _done_io(struct osd_request *or, void *p)
{
	struct objio_state *ios = p;

	kref_put(&ios->kref, _last_io);
}

static int _io_exec(struct objio_state *ios)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	int ret = 0;
	unsigned i;
	objio_done_fn saved_done_fn = ios->done;
	bool sync = ios->sync;

	if (sync) {
		ios->done = _sync_done;
		ios->private = &wait;
	}

	kref_init(&ios->kref);

	for (i = 0; i < ios->numdevs; i++) {
		struct osd_request *or = ios->per_dev[i].or;

		if (!or)
			continue;

		kref_get(&ios->kref);
		osd_execute_request_async(or, _done_io, ios);
	}

	kref_put(&ios->kref, _last_io);

	if (sync) {
		wait_for_completion(&wait);
		ret = saved_done_fn(ios);
	}

	return ret;
}

/*
 * read
 */
static int _read_done(struct objio_state *ios)
{
	ssize_t status;
	int ret = _io_check(ios, false);

	_io_free(ios);

	if (likely(!ret))
		status = ios->length;
	else
		status = ret;

	objlayout_read_done(&ios->oir, status, ios->sync);
	return ret;
}

static int _read_mirrors(struct objio_state *ios, unsigned cur_comp)
{
	struct osd_request *or = NULL;
	struct _objio_per_comp *per_dev = &ios->per_dev[cur_comp];
	unsigned dev = per_dev->dev;
	struct ore_comp *cred =
			&ios->oc->comps[cur_comp];
	struct osd_obj_id obj = cred->obj;
	int ret;

	or = osd_start_request(_io_od(ios, dev), GFP_KERNEL);
	if (unlikely(!or)) {
		ret = -ENOMEM;
		goto err;
	}
	per_dev->or = or;

	osd_req_read(or, &obj, per_dev->offset, per_dev->bio, per_dev->length);

	ret = osd_finalize_request(or, 0, cred->cred, NULL);
	if (ret) {
		dprintk("%s: Faild to osd_finalize_request() => %d\n",
			__func__, ret);
		goto err;
	}

	dprintk("%s:[%d] dev=%d obj=0x%llx start=0x%llx length=0x%lx\n",
		__func__, cur_comp, dev, obj.id, _LLU(per_dev->offset),
		per_dev->length);

err:
	return ret;
}

static int _read_exec(struct objio_state *ios)
{
	unsigned i;
	int ret;

	for (i = 0; i < ios->numdevs; i += ios->layout->mirrors_p1) {
		if (!ios->per_dev[i].length)
			continue;
		ret = _read_mirrors(ios, i);
		if (unlikely(ret))
			goto err;
	}

	ios->done = _read_done;
	return _io_exec(ios);

err:
	_io_free(ios);
	return ret;
}

int objio_read_pagelist(struct nfs_read_data *rdata)
{
	struct objio_state *ios;
	int ret;

	ret = objio_alloc_io_state(NFS_I(rdata->inode)->layout,
			rdata->lseg, rdata->args.pages, rdata->args.pgbase,
			rdata->args.offset, rdata->args.count, rdata,
			GFP_KERNEL, &ios);
	if (unlikely(ret))
		return ret;

	ret = _io_rw_pagelist(ios, GFP_KERNEL);
	if (unlikely(ret))
		return ret;

	return _read_exec(ios);
}

/*
 * write
 */
static int _write_done(struct objio_state *ios)
{
	ssize_t status;
	int ret = _io_check(ios, true);

	_io_free(ios);

	if (likely(!ret)) {
		/* FIXME: should be based on the OSD's persistence model
		 * See OSD2r05 Section 4.13 Data persistence model */
		ios->oir.committed = NFS_FILE_SYNC;
		status = ios->length;
	} else {
		status = ret;
	}

	objlayout_write_done(&ios->oir, status, ios->sync);
	return ret;
}

static int _write_mirrors(struct objio_state *ios, unsigned cur_comp)
{
	struct _objio_per_comp *master_dev = &ios->per_dev[cur_comp];
	unsigned dev = ios->per_dev[cur_comp].dev;
	unsigned last_comp = cur_comp + ios->layout->mirrors_p1;
	int ret;

	for (; cur_comp < last_comp; ++cur_comp, ++dev) {
		struct osd_request *or = NULL;
		struct ore_comp *cred = &ios->oc->comps[cur_comp];
		struct osd_obj_id obj = cred->obj;
		struct _objio_per_comp *per_dev = &ios->per_dev[cur_comp];
		struct bio *bio;

		or = osd_start_request(_io_od(ios, dev), GFP_NOFS);
		if (unlikely(!or)) {
			ret = -ENOMEM;
			goto err;
		}
		per_dev->or = or;

		if (per_dev != master_dev) {
			bio = bio_kmalloc(GFP_NOFS,
					  master_dev->bio->bi_max_vecs);
			if (unlikely(!bio)) {
				dprintk("Faild to allocate BIO size=%u\n",
					master_dev->bio->bi_max_vecs);
				ret = -ENOMEM;
				goto err;
			}

			__bio_clone(bio, master_dev->bio);
			bio->bi_bdev = NULL;
			bio->bi_next = NULL;
			per_dev->bio = bio;
			per_dev->dev = dev;
			per_dev->length = master_dev->length;
			per_dev->offset =  master_dev->offset;
		} else {
			bio = master_dev->bio;
			bio->bi_rw |= REQ_WRITE;
		}

		osd_req_write(or, &obj, per_dev->offset, bio, per_dev->length);

		ret = osd_finalize_request(or, 0, cred->cred, NULL);
		if (ret) {
			dprintk("%s: Faild to osd_finalize_request() => %d\n",
				__func__, ret);
			goto err;
		}

		dprintk("%s:[%d] dev=%d obj=0x%llx start=0x%llx length=0x%lx\n",
			__func__, cur_comp, dev, obj.id, _LLU(per_dev->offset),
			per_dev->length);
	}

err:
	return ret;
}

static int _write_exec(struct objio_state *ios)
{
	unsigned i;
	int ret;

	for (i = 0; i < ios->numdevs; i += ios->layout->mirrors_p1) {
		if (!ios->per_dev[i].length)
			continue;
		ret = _write_mirrors(ios, i);
		if (unlikely(ret))
			goto err;
	}

	ios->done = _write_done;
	return _io_exec(ios);

err:
	_io_free(ios);
	return ret;
}

int objio_write_pagelist(struct nfs_write_data *wdata, int how)
{
	struct objio_state *ios;
	int ret;

	ret = objio_alloc_io_state(NFS_I(wdata->inode)->layout,
			wdata->lseg, wdata->args.pages, wdata->args.pgbase,
			wdata->args.offset, wdata->args.count, wdata, GFP_NOFS,
			&ios);
	if (unlikely(ret))
		return ret;

	ios->sync = 0 != (how & FLUSH_SYNC);

	/* TODO: ios->stable = stable; */
	ret = _io_rw_pagelist(ios, GFP_NOFS);
	if (unlikely(ret))
		return ret;

	return _write_exec(ios);
}

static bool objio_pg_test(struct nfs_pageio_descriptor *pgio,
			  struct nfs_page *prev, struct nfs_page *req)
{
	if (!pnfs_generic_pg_test(pgio, prev, req))
		return false;

	return pgio->pg_count + req->wb_bytes <=
			OBJIO_LSEG(pgio->pg_lseg)->layout.max_io_length;
}

static const struct nfs_pageio_ops objio_pg_read_ops = {
	.pg_init = pnfs_generic_pg_init_read,
	.pg_test = objio_pg_test,
	.pg_doio = pnfs_generic_pg_readpages,
};

static const struct nfs_pageio_ops objio_pg_write_ops = {
	.pg_init = pnfs_generic_pg_init_write,
	.pg_test = objio_pg_test,
	.pg_doio = pnfs_generic_pg_writepages,
};

static struct pnfs_layoutdriver_type objlayout_type = {
	.id = LAYOUT_OSD2_OBJECTS,
	.name = "LAYOUT_OSD2_OBJECTS",
	.flags                   = PNFS_LAYOUTRET_ON_SETATTR,

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
			"%s: Registering OSD pNFS Layout Driver failed: error=%d\n",
			__func__, ret);
	else
		printk(KERN_INFO "%s: Registered OSD pNFS Layout Driver\n",
			__func__);
	return ret;
}

static void __exit
objlayout_exit(void)
{
	pnfs_unregister_layoutdriver(&objlayout_type);
	printk(KERN_INFO "%s: Unregistered OSD pNFS Layout Driver\n",
	       __func__);
}

MODULE_ALIAS("nfs-layouttype4-2");

module_init(objlayout_init);
module_exit(objlayout_exit);
