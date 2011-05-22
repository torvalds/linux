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
#include <scsi/osd_initiator.h>

#include "objlayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

#define _LLU(x) ((unsigned long long)x)

struct caps_buffers {
	u8 caps_key[OSD_CRYPTO_KEYID_SIZE];
	u8 creds[OSD_CAP_LEN];
};

struct objio_segment {
	struct pnfs_layout_segment lseg;

	struct pnfs_osd_object_cred *comps;

	unsigned mirrors_p1;
	unsigned stripe_unit;
	unsigned group_width;	/* Data stripe_units without integrity comps */
	u64 group_depth;
	unsigned group_count;

	unsigned comps_index;
	unsigned num_comps;
	/* variable length */
	struct objio_dev_ent *ods[];
};

static inline struct objio_segment *
OBJIO_LSEG(struct pnfs_layout_segment *lseg)
{
	return container_of(lseg, struct objio_segment, lseg);
}

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

static void copy_single_comp(struct pnfs_osd_object_cred *cur_comp,
			     struct pnfs_osd_object_cred *src_comp,
			     struct caps_buffers *caps_p)
{
	WARN_ON(src_comp->oc_cap_key.cred_len > sizeof(caps_p->caps_key));
	WARN_ON(src_comp->oc_cap.cred_len > sizeof(caps_p->creds));

	*cur_comp = *src_comp;

	memcpy(caps_p->caps_key, src_comp->oc_cap_key.cred,
	       sizeof(caps_p->caps_key));
	cur_comp->oc_cap_key.cred = caps_p->caps_key;

	memcpy(caps_p->creds, src_comp->oc_cap.cred,
	       sizeof(caps_p->creds));
	cur_comp->oc_cap.cred = caps_p->creds;
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
	struct pnfs_osd_object_cred *cur_comp, src_comp;
	struct caps_buffers *caps_p;
	int err;

	err = pnfs_osd_xdr_decode_layout_map(&layout, &iter, xdr);
	if (unlikely(err))
		return err;

	err = _verify_data_map(&layout);
	if (unlikely(err))
		return err;

	objio_seg = kzalloc(sizeof(*objio_seg) +
			    sizeof(objio_seg->ods[0]) * layout.olo_num_comps +
			    sizeof(*objio_seg->comps) * layout.olo_num_comps +
			    sizeof(struct caps_buffers) * layout.olo_num_comps,
			    gfp_flags);
	if (!objio_seg)
		return -ENOMEM;

	objio_seg->comps = (void *)(objio_seg->ods + layout.olo_num_comps);
	cur_comp = objio_seg->comps;
	caps_p = (void *)(cur_comp + layout.olo_num_comps);
	while (pnfs_osd_xdr_decode_layout_comp(&src_comp, &iter, xdr, &err))
		copy_single_comp(cur_comp++, &src_comp, caps_p++);
	if (unlikely(err))
		goto err;

	objio_seg->num_comps = layout.olo_num_comps;
	objio_seg->comps_index = layout.olo_comps_index;

	objio_seg->mirrors_p1 = layout.olo_map.odm_mirror_cnt + 1;
	objio_seg->stripe_unit = layout.olo_map.odm_stripe_unit;
	if (layout.olo_map.odm_group_width) {
		objio_seg->group_width = layout.olo_map.odm_group_width;
		objio_seg->group_depth = layout.olo_map.odm_group_depth;
		objio_seg->group_count = layout.olo_map.odm_num_comps /
						objio_seg->mirrors_p1 /
						objio_seg->group_width;
	} else {
		objio_seg->group_width = layout.olo_map.odm_num_comps /
						objio_seg->mirrors_p1;
		objio_seg->group_depth = -1;
		objio_seg->group_count = 1;
	}

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
	struct objio_segment *objio_seg = OBJIO_LSEG(lseg);

	kfree(objio_seg);
}


static struct pnfs_layoutdriver_type objlayout_type = {
	.id = LAYOUT_OSD2_OBJECTS,
	.name = "LAYOUT_OSD2_OBJECTS",

	.alloc_lseg              = objlayout_alloc_lseg,
	.free_lseg               = objlayout_free_lseg,
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

module_init(objlayout_init);
module_exit(objlayout_exit);
