/*
 *  Module for the pnfs nfs4 file layout driver.
 *  Defines all I/O and Policy interface operations, plus code
 *  to register itself with the pNFS client.
 *
 *  Copyright (c) 2002
 *  The Regents of the University of Michigan
 *  All Rights Reserved
 *
 *  Dean Hildebrand <dhildebz@umich.edu>
 *
 *  Permission is granted to use, copy, create derivative works, and
 *  redistribute this software and such derivative works for any purpose,
 *  so long as the name of the University of Michigan is not used in
 *  any advertising or publicity pertaining to the use or distribution
 *  of this software without specific, written prior authorization. If
 *  the above copyright notice or any other identification of the
 *  University of Michigan is included in any copy of any portion of
 *  this software, then the disclaimer below must also be included.
 *
 *  This software is provided as is, without representation or warranty
 *  of any kind either express or implied, including without limitation
 *  the implied warranties of merchantability, fitness for a particular
 *  purpose, or noninfringement.  The Regents of the University of
 *  Michigan shall not be liable for any damages, including special,
 *  indirect, incidental, or consequential damages, with respect to any
 *  claim arising out of or in connection with the use of the software,
 *  even if it has been or is hereafter advised of the possibility of
 *  such damages.
 */

#include <linux/nfs_fs.h>

#include "internal.h"
#include "nfs4filelayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dean Hildebrand <dhildebz@umich.edu>");
MODULE_DESCRIPTION("The NFSv4 file layout driver");

static int
filelayout_set_layoutdriver(struct nfs_server *nfss)
{
	int status = pnfs_alloc_init_deviceid_cache(nfss->nfs_client,
						nfs4_fl_free_deviceid_callback);
	if (status) {
		printk(KERN_WARNING "%s: deviceid cache could not be "
			"initialized\n", __func__);
		return status;
	}
	dprintk("%s: deviceid cache has been initialized successfully\n",
		__func__);
	return 0;
}

/* Clear out the layout by destroying its device list */
static int
filelayout_clear_layoutdriver(struct nfs_server *nfss)
{
	dprintk("--> %s\n", __func__);

	if (nfss->nfs_client->cl_devid_cache)
		pnfs_put_deviceid_cache(nfss->nfs_client);
	return 0;
}

/*
 * filelayout_check_layout()
 *
 * Make sure layout segment parameters are sane WRT the device.
 * At this point no generic layer initialization of the lseg has occurred,
 * and nothing has been added to the layout_hdr cache.
 *
 */
static int
filelayout_check_layout(struct pnfs_layout_hdr *lo,
			struct nfs4_filelayout_segment *fl,
			struct nfs4_layoutget_res *lgr,
			struct nfs4_deviceid *id)
{
	struct nfs4_file_layout_dsaddr *dsaddr;
	int status = -EINVAL;
	struct nfs_server *nfss = NFS_SERVER(lo->plh_inode);

	dprintk("--> %s\n", __func__);

	if (fl->pattern_offset > lgr->range.offset) {
		dprintk("%s pattern_offset %lld to large\n",
				__func__, fl->pattern_offset);
		goto out;
	}

	if (fl->stripe_unit % PAGE_SIZE) {
		dprintk("%s Stripe unit (%u) not page aligned\n",
			__func__, fl->stripe_unit);
		goto out;
	}

	/* find and reference the deviceid */
	dsaddr = nfs4_fl_find_get_deviceid(nfss->nfs_client, id);
	if (dsaddr == NULL) {
		dsaddr = get_device_info(lo->plh_inode, id);
		if (dsaddr == NULL)
			goto out;
	}
	fl->dsaddr = dsaddr;

	if (fl->first_stripe_index < 0 ||
	    fl->first_stripe_index >= dsaddr->stripe_count) {
		dprintk("%s Bad first_stripe_index %d\n",
				__func__, fl->first_stripe_index);
		goto out_put;
	}

	if ((fl->stripe_type == STRIPE_SPARSE &&
	    fl->num_fh > 1 && fl->num_fh != dsaddr->ds_num) ||
	    (fl->stripe_type == STRIPE_DENSE &&
	    fl->num_fh != dsaddr->stripe_count)) {
		dprintk("%s num_fh %u not valid for given packing\n",
			__func__, fl->num_fh);
		goto out_put;
	}

	if (fl->stripe_unit % nfss->rsize || fl->stripe_unit % nfss->wsize) {
		dprintk("%s Stripe unit (%u) not aligned with rsize %u "
			"wsize %u\n", __func__, fl->stripe_unit, nfss->rsize,
			nfss->wsize);
	}

	status = 0;
out:
	dprintk("--> %s returns %d\n", __func__, status);
	return status;
out_put:
	pnfs_put_deviceid(nfss->nfs_client->cl_devid_cache, &dsaddr->deviceid);
	goto out;
}

static void filelayout_free_fh_array(struct nfs4_filelayout_segment *fl)
{
	int i;

	for (i = 0; i < fl->num_fh; i++) {
		if (!fl->fh_array[i])
			break;
		kfree(fl->fh_array[i]);
	}
	kfree(fl->fh_array);
	fl->fh_array = NULL;
}

static void
_filelayout_free_lseg(struct nfs4_filelayout_segment *fl)
{
	filelayout_free_fh_array(fl);
	kfree(fl);
}

static int
filelayout_decode_layout(struct pnfs_layout_hdr *flo,
			 struct nfs4_filelayout_segment *fl,
			 struct nfs4_layoutget_res *lgr,
			 struct nfs4_deviceid *id)
{
	uint32_t *p = (uint32_t *)lgr->layout.buf;
	uint32_t nfl_util;
	int i;

	dprintk("%s: set_layout_map Begin\n", __func__);

	memcpy(id, p, sizeof(*id));
	p += XDR_QUADLEN(NFS4_DEVICEID4_SIZE);
	print_deviceid(id);

	nfl_util = be32_to_cpup(p++);
	if (nfl_util & NFL4_UFLG_COMMIT_THRU_MDS)
		fl->commit_through_mds = 1;
	if (nfl_util & NFL4_UFLG_DENSE)
		fl->stripe_type = STRIPE_DENSE;
	else
		fl->stripe_type = STRIPE_SPARSE;
	fl->stripe_unit = nfl_util & ~NFL4_UFLG_MASK;

	fl->first_stripe_index = be32_to_cpup(p++);
	p = xdr_decode_hyper(p, &fl->pattern_offset);
	fl->num_fh = be32_to_cpup(p++);

	dprintk("%s: nfl_util 0x%X num_fh %u fsi %u po %llu\n",
		__func__, nfl_util, fl->num_fh, fl->first_stripe_index,
		fl->pattern_offset);

	fl->fh_array = kzalloc(fl->num_fh * sizeof(struct nfs_fh *),
			       GFP_KERNEL);
	if (!fl->fh_array)
		return -ENOMEM;

	for (i = 0; i < fl->num_fh; i++) {
		/* Do we want to use a mempool here? */
		fl->fh_array[i] = kmalloc(sizeof(struct nfs_fh), GFP_KERNEL);
		if (!fl->fh_array[i]) {
			filelayout_free_fh_array(fl);
			return -ENOMEM;
		}
		fl->fh_array[i]->size = be32_to_cpup(p++);
		if (sizeof(struct nfs_fh) < fl->fh_array[i]->size) {
			printk(KERN_ERR "Too big fh %d received %d\n",
			       i, fl->fh_array[i]->size);
			filelayout_free_fh_array(fl);
			return -EIO;
		}
		memcpy(fl->fh_array[i]->data, p, fl->fh_array[i]->size);
		p += XDR_QUADLEN(fl->fh_array[i]->size);
		dprintk("DEBUG: %s: fh len %d\n", __func__,
			fl->fh_array[i]->size);
	}

	return 0;
}

static struct pnfs_layout_segment *
filelayout_alloc_lseg(struct pnfs_layout_hdr *layoutid,
		      struct nfs4_layoutget_res *lgr)
{
	struct nfs4_filelayout_segment *fl;
	int rc;
	struct nfs4_deviceid id;

	dprintk("--> %s\n", __func__);
	fl = kzalloc(sizeof(*fl), GFP_KERNEL);
	if (!fl)
		return NULL;

	rc = filelayout_decode_layout(layoutid, fl, lgr, &id);
	if (rc != 0 || filelayout_check_layout(layoutid, fl, lgr, &id)) {
		_filelayout_free_lseg(fl);
		return NULL;
	}
	return &fl->generic_hdr;
}

static void
filelayout_free_lseg(struct pnfs_layout_segment *lseg)
{
	struct nfs_server *nfss = NFS_SERVER(lseg->pls_layout->plh_inode);
	struct nfs4_filelayout_segment *fl = FILELAYOUT_LSEG(lseg);

	dprintk("--> %s\n", __func__);
	pnfs_put_deviceid(nfss->nfs_client->cl_devid_cache,
			  &fl->dsaddr->deviceid);
	_filelayout_free_lseg(fl);
}

static struct pnfs_layoutdriver_type filelayout_type = {
	.id = LAYOUT_NFSV4_1_FILES,
	.name = "LAYOUT_NFSV4_1_FILES",
	.owner = THIS_MODULE,
	.set_layoutdriver = filelayout_set_layoutdriver,
	.clear_layoutdriver = filelayout_clear_layoutdriver,
	.alloc_lseg              = filelayout_alloc_lseg,
	.free_lseg               = filelayout_free_lseg,
};

static int __init nfs4filelayout_init(void)
{
	printk(KERN_INFO "%s: NFSv4 File Layout Driver Registering...\n",
	       __func__);
	return pnfs_register_layoutdriver(&filelayout_type);
}

static void __exit nfs4filelayout_exit(void)
{
	printk(KERN_INFO "%s: NFSv4 File Layout Driver Unregistering...\n",
	       __func__);
	pnfs_unregister_layoutdriver(&filelayout_type);
}

module_init(nfs4filelayout_init);
module_exit(nfs4filelayout_exit);
