// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2021, Alibaba Cloud
 */
#include "internal.h"
#include <linux/prefetch.h>
#include <linux/dax.h>
#include <trace/events/erofs.h>

void erofs_unmap_metabuf(struct erofs_buf *buf)
{
	if (buf->kmap_type == EROFS_KMAP)
		kunmap(buf->page);
	else if (buf->kmap_type == EROFS_KMAP_ATOMIC)
		kunmap_atomic(buf->base);
	buf->base = NULL;
	buf->kmap_type = EROFS_NO_KMAP;
}

void erofs_put_metabuf(struct erofs_buf *buf)
{
	if (!buf->page)
		return;
	erofs_unmap_metabuf(buf);
	put_page(buf->page);
	buf->page = NULL;
}

void *erofs_read_metabuf(struct erofs_buf *buf, struct super_block *sb,
			erofs_blk_t blkaddr, enum erofs_kmap_type type)
{
	struct address_space *const mapping = sb->s_bdev->bd_inode->i_mapping;
	erofs_off_t offset = blknr_to_addr(blkaddr);
	pgoff_t index = offset >> PAGE_SHIFT;
	struct page *page = buf->page;

	if (!page || page->index != index) {
		erofs_put_metabuf(buf);
		page = read_cache_page_gfp(mapping, index,
				mapping_gfp_constraint(mapping, ~__GFP_FS));
		if (IS_ERR(page))
			return page;
		/* should already be PageUptodate, no need to lock page */
		buf->page = page;
	}
	if (buf->kmap_type == EROFS_NO_KMAP) {
		if (type == EROFS_KMAP)
			buf->base = kmap(page);
		else if (type == EROFS_KMAP_ATOMIC)
			buf->base = kmap_atomic(page);
		buf->kmap_type = type;
	} else if (buf->kmap_type != type) {
		DBG_BUGON(1);
		return ERR_PTR(-EFAULT);
	}
	if (type == EROFS_NO_KMAP)
		return NULL;
	return buf->base + (offset & ~PAGE_MASK);
}

static int erofs_map_blocks_flatmode(struct inode *inode,
				     struct erofs_map_blocks *map,
				     int flags)
{
	erofs_blk_t nblocks, lastblk;
	u64 offset = map->m_la;
	struct erofs_inode *vi = EROFS_I(inode);
	bool tailendpacking = (vi->datalayout == EROFS_INODE_FLAT_INLINE);

	nblocks = DIV_ROUND_UP(inode->i_size, EROFS_BLKSIZ);
	lastblk = nblocks - tailendpacking;

	/* there is no hole in flatmode */
	map->m_flags = EROFS_MAP_MAPPED;
	if (offset < blknr_to_addr(lastblk)) {
		map->m_pa = blknr_to_addr(vi->raw_blkaddr) + map->m_la;
		map->m_plen = blknr_to_addr(lastblk) - offset;
	} else if (tailendpacking) {
		/* 2 - inode inline B: inode, [xattrs], inline last blk... */
		struct erofs_sb_info *sbi = EROFS_SB(inode->i_sb);

		map->m_pa = iloc(sbi, vi->nid) + vi->inode_isize +
			vi->xattr_isize + erofs_blkoff(map->m_la);
		map->m_plen = inode->i_size - offset;

		/* inline data should be located in the same meta block */
		if (erofs_blkoff(map->m_pa) + map->m_plen > EROFS_BLKSIZ) {
			erofs_err(inode->i_sb,
				  "inline data cross block boundary @ nid %llu",
				  vi->nid);
			DBG_BUGON(1);
			return -EFSCORRUPTED;
		}
		map->m_flags |= EROFS_MAP_META;
	} else {
		erofs_err(inode->i_sb,
			  "internal error @ nid: %llu (size %llu), m_la 0x%llx",
			  vi->nid, inode->i_size, map->m_la);
		DBG_BUGON(1);
		return -EIO;
	}
	return 0;
}

static int erofs_map_blocks(struct inode *inode,
			    struct erofs_map_blocks *map, int flags)
{
	struct super_block *sb = inode->i_sb;
	struct erofs_inode *vi = EROFS_I(inode);
	struct erofs_inode_chunk_index *idx;
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	u64 chunknr;
	unsigned int unit;
	erofs_off_t pos;
	void *kaddr;
	int err = 0;

	trace_erofs_map_blocks_enter(inode, map, flags);
	map->m_deviceid = 0;
	if (map->m_la >= inode->i_size) {
		/* leave out-of-bound access unmapped */
		map->m_flags = 0;
		map->m_plen = 0;
		goto out;
	}

	if (vi->datalayout != EROFS_INODE_CHUNK_BASED) {
		err = erofs_map_blocks_flatmode(inode, map, flags);
		goto out;
	}

	if (vi->chunkformat & EROFS_CHUNK_FORMAT_INDEXES)
		unit = sizeof(*idx);			/* chunk index */
	else
		unit = EROFS_BLOCK_MAP_ENTRY_SIZE;	/* block map */

	chunknr = map->m_la >> vi->chunkbits;
	pos = ALIGN(iloc(EROFS_SB(sb), vi->nid) + vi->inode_isize +
		    vi->xattr_isize, unit) + unit * chunknr;

	kaddr = erofs_read_metabuf(&buf, sb, erofs_blknr(pos), EROFS_KMAP);
	if (IS_ERR(kaddr)) {
		err = PTR_ERR(kaddr);
		goto out;
	}
	map->m_la = chunknr << vi->chunkbits;
	map->m_plen = min_t(erofs_off_t, 1UL << vi->chunkbits,
			    roundup(inode->i_size - map->m_la, EROFS_BLKSIZ));

	/* handle block map */
	if (!(vi->chunkformat & EROFS_CHUNK_FORMAT_INDEXES)) {
		__le32 *blkaddr = kaddr + erofs_blkoff(pos);

		if (le32_to_cpu(*blkaddr) == EROFS_NULL_ADDR) {
			map->m_flags = 0;
		} else {
			map->m_pa = blknr_to_addr(le32_to_cpu(*blkaddr));
			map->m_flags = EROFS_MAP_MAPPED;
		}
		goto out_unlock;
	}
	/* parse chunk indexes */
	idx = kaddr + erofs_blkoff(pos);
	switch (le32_to_cpu(idx->blkaddr)) {
	case EROFS_NULL_ADDR:
		map->m_flags = 0;
		break;
	default:
		map->m_deviceid = le16_to_cpu(idx->device_id) &
			EROFS_SB(sb)->device_id_mask;
		map->m_pa = blknr_to_addr(le32_to_cpu(idx->blkaddr));
		map->m_flags = EROFS_MAP_MAPPED;
		break;
	}
out_unlock:
	erofs_put_metabuf(&buf);
out:
	if (!err)
		map->m_llen = map->m_plen;
	trace_erofs_map_blocks_exit(inode, map, flags, 0);
	return err;
}

int erofs_map_dev(struct super_block *sb, struct erofs_map_dev *map)
{
	struct erofs_dev_context *devs = EROFS_SB(sb)->devs;
	struct erofs_device_info *dif;
	int id;

	/* primary device by default */
	map->m_bdev = sb->s_bdev;
	map->m_daxdev = EROFS_SB(sb)->dax_dev;
	map->m_dax_part_off = EROFS_SB(sb)->dax_part_off;

	if (map->m_deviceid) {
		down_read(&devs->rwsem);
		dif = idr_find(&devs->tree, map->m_deviceid - 1);
		if (!dif) {
			up_read(&devs->rwsem);
			return -ENODEV;
		}
		map->m_bdev = dif->bdev;
		map->m_daxdev = dif->dax_dev;
		map->m_dax_part_off = dif->dax_part_off;
		up_read(&devs->rwsem);
	} else if (devs->extra_devices) {
		down_read(&devs->rwsem);
		idr_for_each_entry(&devs->tree, dif, id) {
			erofs_off_t startoff, length;

			if (!dif->mapped_blkaddr)
				continue;
			startoff = blknr_to_addr(dif->mapped_blkaddr);
			length = blknr_to_addr(dif->blocks);

			if (map->m_pa >= startoff &&
			    map->m_pa < startoff + length) {
				map->m_pa -= startoff;
				map->m_bdev = dif->bdev;
				map->m_daxdev = dif->dax_dev;
				map->m_dax_part_off = dif->dax_part_off;
				break;
			}
		}
		up_read(&devs->rwsem);
	}
	return 0;
}

static int erofs_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
		unsigned int flags, struct iomap *iomap, struct iomap *srcmap)
{
	int ret;
	struct erofs_map_blocks map;
	struct erofs_map_dev mdev;

	map.m_la = offset;
	map.m_llen = length;

	ret = erofs_map_blocks(inode, &map, EROFS_GET_BLOCKS_RAW);
	if (ret < 0)
		return ret;

	mdev = (struct erofs_map_dev) {
		.m_deviceid = map.m_deviceid,
		.m_pa = map.m_pa,
	};
	ret = erofs_map_dev(inode->i_sb, &mdev);
	if (ret)
		return ret;

	iomap->offset = map.m_la;
	if (flags & IOMAP_DAX)
		iomap->dax_dev = mdev.m_daxdev;
	else
		iomap->bdev = mdev.m_bdev;
	iomap->length = map.m_llen;
	iomap->flags = 0;
	iomap->private = NULL;

	if (!(map.m_flags & EROFS_MAP_MAPPED)) {
		iomap->type = IOMAP_HOLE;
		iomap->addr = IOMAP_NULL_ADDR;
		if (!iomap->length)
			iomap->length = length;
		return 0;
	}

	if (map.m_flags & EROFS_MAP_META) {
		void *ptr;
		struct erofs_buf buf = __EROFS_BUF_INITIALIZER;

		iomap->type = IOMAP_INLINE;
		ptr = erofs_read_metabuf(&buf, inode->i_sb,
					 erofs_blknr(mdev.m_pa), EROFS_KMAP);
		if (IS_ERR(ptr))
			return PTR_ERR(ptr);
		iomap->inline_data = ptr + erofs_blkoff(mdev.m_pa);
		iomap->private = buf.base;
	} else {
		iomap->type = IOMAP_MAPPED;
		iomap->addr = mdev.m_pa;
		if (flags & IOMAP_DAX)
			iomap->addr += mdev.m_dax_part_off;
	}
	return 0;
}

static int erofs_iomap_end(struct inode *inode, loff_t pos, loff_t length,
		ssize_t written, unsigned int flags, struct iomap *iomap)
{
	void *ptr = iomap->private;

	if (ptr) {
		struct erofs_buf buf = {
			.page = kmap_to_page(ptr),
			.base = ptr,
			.kmap_type = EROFS_KMAP,
		};

		DBG_BUGON(iomap->type != IOMAP_INLINE);
		erofs_put_metabuf(&buf);
	} else {
		DBG_BUGON(iomap->type == IOMAP_INLINE);
	}
	return written;
}

static const struct iomap_ops erofs_iomap_ops = {
	.iomap_begin = erofs_iomap_begin,
	.iomap_end = erofs_iomap_end,
};

int erofs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		 u64 start, u64 len)
{
	if (erofs_inode_is_data_compressed(EROFS_I(inode)->datalayout)) {
#ifdef CONFIG_EROFS_FS_ZIP
		return iomap_fiemap(inode, fieinfo, start, len,
				    &z_erofs_iomap_report_ops);
#else
		return -EOPNOTSUPP;
#endif
	}
	return iomap_fiemap(inode, fieinfo, start, len, &erofs_iomap_ops);
}

/*
 * since we dont have write or truncate flows, so no inode
 * locking needs to be held at the moment.
 */
static int erofs_readpage(struct file *file, struct page *page)
{
	return iomap_readpage(page, &erofs_iomap_ops);
}

static void erofs_readahead(struct readahead_control *rac)
{
	return iomap_readahead(rac, &erofs_iomap_ops);
}

static sector_t erofs_bmap(struct address_space *mapping, sector_t block)
{
	return iomap_bmap(mapping, block, &erofs_iomap_ops);
}

static int erofs_prepare_dio(struct kiocb *iocb, struct iov_iter *to)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	loff_t align = iocb->ki_pos | iov_iter_count(to) |
		iov_iter_alignment(to);
	struct block_device *bdev = inode->i_sb->s_bdev;
	unsigned int blksize_mask;

	if (bdev)
		blksize_mask = (1 << ilog2(bdev_logical_block_size(bdev))) - 1;
	else
		blksize_mask = (1 << inode->i_blkbits) - 1;

	if (align & blksize_mask)
		return -EINVAL;
	return 0;
}

static ssize_t erofs_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	/* no need taking (shared) inode lock since it's a ro filesystem */
	if (!iov_iter_count(to))
		return 0;

#ifdef CONFIG_FS_DAX
	if (IS_DAX(iocb->ki_filp->f_mapping->host))
		return dax_iomap_rw(iocb, to, &erofs_iomap_ops);
#endif
	if (iocb->ki_flags & IOCB_DIRECT) {
		int err = erofs_prepare_dio(iocb, to);

		if (!err)
			return iomap_dio_rw(iocb, to, &erofs_iomap_ops,
					    NULL, 0, 0);
		if (err < 0)
			return err;
	}
	return filemap_read(iocb, to, 0);
}

/* for uncompressed (aligned) files and raw access for other files */
const struct address_space_operations erofs_raw_access_aops = {
	.readpage = erofs_readpage,
	.readahead = erofs_readahead,
	.bmap = erofs_bmap,
	.direct_IO = noop_direct_IO,
};

#ifdef CONFIG_FS_DAX
static vm_fault_t erofs_dax_huge_fault(struct vm_fault *vmf,
		enum page_entry_size pe_size)
{
	return dax_iomap_fault(vmf, pe_size, NULL, NULL, &erofs_iomap_ops);
}

static vm_fault_t erofs_dax_fault(struct vm_fault *vmf)
{
	return erofs_dax_huge_fault(vmf, PE_SIZE_PTE);
}

static const struct vm_operations_struct erofs_dax_vm_ops = {
	.fault		= erofs_dax_fault,
	.huge_fault	= erofs_dax_huge_fault,
};

static int erofs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (!IS_DAX(file_inode(file)))
		return generic_file_readonly_mmap(file, vma);

	if ((vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_MAYWRITE))
		return -EINVAL;

	vma->vm_ops = &erofs_dax_vm_ops;
	vma->vm_flags |= VM_HUGEPAGE;
	return 0;
}
#else
#define erofs_file_mmap	generic_file_readonly_mmap
#endif

const struct file_operations erofs_file_fops = {
	.llseek		= generic_file_llseek,
	.read_iter	= erofs_file_read_iter,
	.mmap		= erofs_file_mmap,
	.splice_read	= generic_file_splice_read,
};
