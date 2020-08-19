// SPDX-License-Identifier: GPL-2.0
/*
 * dax: direct host memory access
 * Copyright (C) 2020 Red Hat, Inc.
 */

#include "fuse_i.h"

#include <linux/dax.h>
#include <linux/pfn_t.h>

/*
 * Default memory range size.  A power of 2 so it agrees with common FUSE_INIT
 * map_alignment values 4KB and 64KB.
 */
#define FUSE_DAX_SHIFT	21
#define FUSE_DAX_SZ	(1 << FUSE_DAX_SHIFT)
#define FUSE_DAX_PAGES	(FUSE_DAX_SZ / PAGE_SIZE)

/** Translation information for file offsets to DAX window offsets */
struct fuse_dax_mapping {
	/* Will connect in fcd->free_ranges to keep track of free memory */
	struct list_head list;

	/** Position in DAX window */
	u64 window_offset;

	/** Length of mapping, in bytes */
	loff_t length;
};

struct fuse_conn_dax {
	/* DAX device */
	struct dax_device *dev;

	/* DAX Window Free Ranges */
	long nr_free_ranges;
	struct list_head free_ranges;
};

static void fuse_free_dax_mem_ranges(struct list_head *mem_list)
{
	struct fuse_dax_mapping *range, *temp;

	/* Free All allocated elements */
	list_for_each_entry_safe(range, temp, mem_list, list) {
		list_del(&range->list);
		kfree(range);
	}
}

void fuse_dax_conn_free(struct fuse_conn *fc)
{
	if (fc->dax) {
		fuse_free_dax_mem_ranges(&fc->dax->free_ranges);
		kfree(fc->dax);
	}
}

static int fuse_dax_mem_range_init(struct fuse_conn_dax *fcd)
{
	long nr_pages, nr_ranges;
	void *kaddr;
	pfn_t pfn;
	struct fuse_dax_mapping *range;
	int ret, id;
	size_t dax_size = -1;
	unsigned long i;

	INIT_LIST_HEAD(&fcd->free_ranges);
	id = dax_read_lock();
	nr_pages = dax_direct_access(fcd->dev, 0, PHYS_PFN(dax_size), &kaddr,
				     &pfn);
	dax_read_unlock(id);
	if (nr_pages < 0) {
		pr_debug("dax_direct_access() returned %ld\n", nr_pages);
		return nr_pages;
	}

	nr_ranges = nr_pages/FUSE_DAX_PAGES;
	pr_debug("%s: dax mapped %ld pages. nr_ranges=%ld\n",
		__func__, nr_pages, nr_ranges);

	for (i = 0; i < nr_ranges; i++) {
		range = kzalloc(sizeof(struct fuse_dax_mapping), GFP_KERNEL);
		ret = -ENOMEM;
		if (!range)
			goto out_err;

		/* TODO: This offset only works if virtio-fs driver is not
		 * having some memory hidden at the beginning. This needs
		 * better handling
		 */
		range->window_offset = i * FUSE_DAX_SZ;
		range->length = FUSE_DAX_SZ;
		list_add_tail(&range->list, &fcd->free_ranges);
	}

	fcd->nr_free_ranges = nr_ranges;
	return 0;
out_err:
	/* Free All allocated elements */
	fuse_free_dax_mem_ranges(&fcd->free_ranges);
	return ret;
}

int fuse_dax_conn_alloc(struct fuse_conn *fc, struct dax_device *dax_dev)
{
	struct fuse_conn_dax *fcd;
	int err;

	if (!dax_dev)
		return 0;

	fcd = kzalloc(sizeof(*fcd), GFP_KERNEL);
	if (!fcd)
		return -ENOMEM;

	fcd->dev = dax_dev;
	err = fuse_dax_mem_range_init(fcd);
	if (err) {
		kfree(fcd);
		return err;
	}

	fc->dax = fcd;
	return 0;
}

bool fuse_dax_check_alignment(struct fuse_conn *fc, unsigned int map_alignment)
{
	if (fc->dax && (map_alignment > FUSE_DAX_SHIFT)) {
		pr_warn("FUSE: map_alignment %u incompatible with dax mem range size %u\n",
			map_alignment, FUSE_DAX_SZ);
		return false;
	}
	return true;
}
