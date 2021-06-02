// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include <linux/module.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/iomap.h>
#include <linux/swap.h>

/* Swapfile activation */

struct iomap_swapfile_info {
	struct iomap iomap;		/* accumulated iomap */
	struct swap_info_struct *sis;
	uint64_t lowest_ppage;		/* lowest physical addr seen (pages) */
	uint64_t highest_ppage;		/* highest physical addr seen (pages) */
	unsigned long nr_pages;		/* number of pages collected */
	int nr_extents;			/* extent count */
	struct file *file;
};

/*
 * Collect physical extents for this swap file.  Physical extents reported to
 * the swap code must be trimmed to align to a page boundary.  The logical
 * offset within the file is irrelevant since the swapfile code maps logical
 * page numbers of the swap device to the physical page-aligned extents.
 */
static int iomap_swapfile_add_extent(struct iomap_swapfile_info *isi)
{
	struct iomap *iomap = &isi->iomap;
	unsigned long nr_pages;
	uint64_t first_ppage;
	uint64_t first_ppage_reported;
	uint64_t next_ppage;
	int error;

	/*
	 * Round the start up and the end down so that the physical
	 * extent aligns to a page boundary.
	 */
	first_ppage = ALIGN(iomap->addr, PAGE_SIZE) >> PAGE_SHIFT;
	next_ppage = ALIGN_DOWN(iomap->addr + iomap->length, PAGE_SIZE) >>
			PAGE_SHIFT;

	/* Skip too-short physical extents. */
	if (first_ppage >= next_ppage)
		return 0;
	nr_pages = next_ppage - first_ppage;

	/*
	 * Calculate how much swap space we're adding; the first page contains
	 * the swap header and doesn't count.  The mm still wants that first
	 * page fed to add_swap_extent, however.
	 */
	first_ppage_reported = first_ppage;
	if (iomap->offset == 0)
		first_ppage_reported++;
	if (isi->lowest_ppage > first_ppage_reported)
		isi->lowest_ppage = first_ppage_reported;
	if (isi->highest_ppage < (next_ppage - 1))
		isi->highest_ppage = next_ppage - 1;

	/* Add extent, set up for the next call. */
	error = add_swap_extent(isi->sis, isi->nr_pages, nr_pages, first_ppage);
	if (error < 0)
		return error;
	isi->nr_extents += error;
	isi->nr_pages += nr_pages;
	return 0;
}

static int iomap_swapfile_fail(struct iomap_swapfile_info *isi, const char *str)
{
	char *buf, *p = ERR_PTR(-ENOMEM);

	buf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (buf)
		p = file_path(isi->file, buf, PATH_MAX);
	pr_err("swapon: file %s %s\n", IS_ERR(p) ? "<unknown>" : p, str);
	kfree(buf);
	return -EINVAL;
}

/*
 * Accumulate iomaps for this swap file.  We have to accumulate iomaps because
 * swap only cares about contiguous page-aligned physical extents and makes no
 * distinction between written and unwritten extents.
 */
static loff_t iomap_swapfile_activate_actor(struct inode *inode, loff_t pos,
		loff_t count, void *data, struct iomap *iomap,
		struct iomap *srcmap)
{
	struct iomap_swapfile_info *isi = data;
	int error;

	switch (iomap->type) {
	case IOMAP_MAPPED:
	case IOMAP_UNWRITTEN:
		/* Only real or unwritten extents. */
		break;
	case IOMAP_INLINE:
		/* No inline data. */
		return iomap_swapfile_fail(isi, "is inline");
	default:
		return iomap_swapfile_fail(isi, "has unallocated extents");
	}

	/* No uncommitted metadata or shared blocks. */
	if (iomap->flags & IOMAP_F_DIRTY)
		return iomap_swapfile_fail(isi, "is not committed");
	if (iomap->flags & IOMAP_F_SHARED)
		return iomap_swapfile_fail(isi, "has shared extents");

	/* Only one bdev per swap file. */
	if (iomap->bdev != isi->sis->bdev)
		return iomap_swapfile_fail(isi, "outside the main device");

	if (isi->iomap.length == 0) {
		/* No accumulated extent, so just store it. */
		memcpy(&isi->iomap, iomap, sizeof(isi->iomap));
	} else if (isi->iomap.addr + isi->iomap.length == iomap->addr) {
		/* Append this to the accumulated extent. */
		isi->iomap.length += iomap->length;
	} else {
		/* Otherwise, add the retained iomap and store this one. */
		error = iomap_swapfile_add_extent(isi);
		if (error)
			return error;
		memcpy(&isi->iomap, iomap, sizeof(isi->iomap));
	}
	return count;
}

/*
 * Iterate a swap file's iomaps to construct physical extents that can be
 * passed to the swapfile subsystem.
 */
int iomap_swapfile_activate(struct swap_info_struct *sis,
		struct file *swap_file, sector_t *pagespan,
		const struct iomap_ops *ops)
{
	struct iomap_swapfile_info isi = {
		.sis = sis,
		.lowest_ppage = (sector_t)-1ULL,
		.file = swap_file,
	};
	struct address_space *mapping = swap_file->f_mapping;
	struct inode *inode = mapping->host;
	loff_t pos = 0;
	loff_t len = ALIGN_DOWN(i_size_read(inode), PAGE_SIZE);
	loff_t ret;

	/*
	 * Persist all file mapping metadata so that we won't have any
	 * IOMAP_F_DIRTY iomaps.
	 */
	ret = vfs_fsync(swap_file, 1);
	if (ret)
		return ret;

	while (len > 0) {
		ret = iomap_apply(inode, pos, len, IOMAP_REPORT,
				ops, &isi, iomap_swapfile_activate_actor);
		if (ret <= 0)
			return ret;

		pos += ret;
		len -= ret;
	}

	if (isi.iomap.length) {
		ret = iomap_swapfile_add_extent(&isi);
		if (ret)
			return ret;
	}

	/*
	 * If this swapfile doesn't contain even a single page-aligned
	 * contiguous range of blocks, reject this useless swapfile to
	 * prevent confusion later on.
	 */
	if (isi.nr_pages == 0) {
		pr_warn("swapon: Cannot find a single usable page in file.\n");
		return -EINVAL;
	}

	*pagespan = 1 + isi.highest_ppage - isi.lowest_ppage;
	sis->max = isi.nr_pages;
	sis->pages = isi.nr_pages - 1;
	sis->highest_bit = isi.nr_pages - 1;
	return isi.nr_extents;
}
EXPORT_SYMBOL_GPL(iomap_swapfile_activate);
