// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2021, Alibaba Cloud
 */
#include "internal.h"
#include <linux/filelock.h>
#include <linux/sched/mm.h>
#include <trace/events/erofs.h>

void erofs_unmap_metabuf(struct erofs_buf *buf)
{
	if (!buf->base)
		return;
	kunmap_local(buf->base);
	buf->base = NULL;
}

void erofs_put_metabuf(struct erofs_buf *buf)
{
	if (!buf->page)
		return;
	erofs_unmap_metabuf(buf);
	folio_put(page_folio(buf->page));
	buf->page = NULL;
}

void *erofs_bread(struct erofs_buf *buf, erofs_off_t offset, bool need_kmap)
{
	pgoff_t index = (buf->off + offset) >> PAGE_SHIFT;
	struct folio *folio = NULL;

	if (buf->page) {
		folio = page_folio(buf->page);
		if (folio_file_page(folio, index) != buf->page)
			erofs_unmap_metabuf(buf);
	}
	if (!folio || !folio_contains(folio, index)) {
		erofs_put_metabuf(buf);
		folio = read_mapping_folio(buf->mapping, index, buf->file);
		if (IS_ERR(folio))
			return folio;
	}
	buf->page = folio_file_page(folio, index);
	if (!need_kmap)
		return NULL;
	if (!buf->base)
		buf->base = kmap_local_page(buf->page);
	return buf->base + (offset & ~PAGE_MASK);
}

int erofs_init_metabuf(struct erofs_buf *buf, struct super_block *sb,
		       bool in_metabox)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);

	buf->file = NULL;
	if (in_metabox) {
		if (unlikely(!sbi->metabox_inode))
			return -EFSCORRUPTED;
		buf->mapping = sbi->metabox_inode->i_mapping;
		return 0;
	}
	buf->off = sbi->dif0.fsoff;
	if (erofs_is_fileio_mode(sbi)) {
		buf->file = sbi->dif0.file;	/* some fs like FUSE needs it */
		buf->mapping = buf->file->f_mapping;
	} else if (erofs_is_fscache_mode(sb))
		buf->mapping = sbi->dif0.fscache->inode->i_mapping;
	else
		buf->mapping = sb->s_bdev->bd_mapping;
	return 0;
}

void *erofs_read_metabuf(struct erofs_buf *buf, struct super_block *sb,
			 erofs_off_t offset, bool in_metabox)
{
	int err;

	err = erofs_init_metabuf(buf, sb, in_metabox);
	if (err)
		return ERR_PTR(err);
	return erofs_bread(buf, offset, true);
}

int erofs_map_blocks(struct inode *inode, struct erofs_map_blocks *map)
{
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	struct super_block *sb = inode->i_sb;
	unsigned int unit, blksz = sb->s_blocksize;
	struct erofs_inode *vi = EROFS_I(inode);
	struct erofs_inode_chunk_index *idx;
	erofs_blk_t startblk, addrmask;
	bool tailpacking;
	erofs_off_t pos;
	u64 chunknr;
	int err = 0;

	trace_erofs_map_blocks_enter(inode, map, 0);
	map->m_deviceid = 0;
	map->m_flags = 0;
	if (map->m_la >= inode->i_size)
		goto out;

	if (vi->datalayout != EROFS_INODE_CHUNK_BASED) {
		tailpacking = (vi->datalayout == EROFS_INODE_FLAT_INLINE);
		if (!tailpacking && vi->startblk == EROFS_NULL_ADDR)
			goto out;
		pos = erofs_pos(sb, erofs_iblks(inode) - tailpacking);

		map->m_flags = EROFS_MAP_MAPPED;
		if (map->m_la < pos) {
			map->m_pa = erofs_pos(sb, vi->startblk) + map->m_la;
			map->m_llen = pos - map->m_la;
		} else {
			map->m_pa = erofs_iloc(inode) + vi->inode_isize +
				vi->xattr_isize + erofs_blkoff(sb, map->m_la);
			map->m_llen = inode->i_size - map->m_la;
			map->m_flags |= EROFS_MAP_META;
		}
		goto out;
	}

	if (vi->chunkformat & EROFS_CHUNK_FORMAT_INDEXES)
		unit = sizeof(*idx);			/* chunk index */
	else
		unit = EROFS_BLOCK_MAP_ENTRY_SIZE;	/* block map */

	chunknr = map->m_la >> vi->chunkbits;
	pos = ALIGN(erofs_iloc(inode) + vi->inode_isize +
		    vi->xattr_isize, unit) + unit * chunknr;

	idx = erofs_read_metabuf(&buf, sb, pos, erofs_inode_in_metabox(inode));
	if (IS_ERR(idx)) {
		err = PTR_ERR(idx);
		goto out;
	}
	map->m_la = chunknr << vi->chunkbits;
	map->m_llen = min_t(erofs_off_t, 1UL << vi->chunkbits,
			    round_up(inode->i_size - map->m_la, blksz));
	if (vi->chunkformat & EROFS_CHUNK_FORMAT_INDEXES) {
		addrmask = (vi->chunkformat & EROFS_CHUNK_FORMAT_48BIT) ?
			BIT_ULL(48) - 1 : BIT_ULL(32) - 1;
		startblk = (((u64)le16_to_cpu(idx->startblk_hi) << 32) |
			    le32_to_cpu(idx->startblk_lo)) & addrmask;
		if ((startblk ^ EROFS_NULL_ADDR) & addrmask) {
			map->m_deviceid = le16_to_cpu(idx->device_id) &
				EROFS_SB(sb)->device_id_mask;
			map->m_pa = erofs_pos(sb, startblk);
			map->m_flags = EROFS_MAP_MAPPED;
		}
	} else {
		startblk = le32_to_cpu(*(__le32 *)idx);
		if (startblk != (u32)EROFS_NULL_ADDR) {
			map->m_pa = erofs_pos(sb, startblk);
			map->m_flags = EROFS_MAP_MAPPED;
		}
	}
	erofs_put_metabuf(&buf);
out:
	if (!err) {
		map->m_plen = map->m_llen;
		/* inline data should be located in the same meta block */
		if ((map->m_flags & EROFS_MAP_META) &&
		    erofs_blkoff(sb, map->m_pa) + map->m_plen > blksz) {
			erofs_err(sb, "inline data across blocks @ nid %llu", vi->nid);
			DBG_BUGON(1);
			return -EFSCORRUPTED;
		}
	}
	trace_erofs_map_blocks_exit(inode, map, 0, err);
	return err;
}

static void erofs_fill_from_devinfo(struct erofs_map_dev *map,
		struct super_block *sb, struct erofs_device_info *dif)
{
	map->m_sb = sb;
	map->m_dif = dif;
	map->m_bdev = NULL;
	if (dif->file && S_ISBLK(file_inode(dif->file)->i_mode))
		map->m_bdev = file_bdev(dif->file);
}

int erofs_map_dev(struct super_block *sb, struct erofs_map_dev *map)
{
	struct erofs_dev_context *devs = EROFS_SB(sb)->devs;
	struct erofs_device_info *dif;
	erofs_off_t startoff;
	int id;

	erofs_fill_from_devinfo(map, sb, &EROFS_SB(sb)->dif0);
	map->m_bdev = sb->s_bdev;	/* use s_bdev for the primary device */
	if (map->m_deviceid) {
		down_read(&devs->rwsem);
		dif = idr_find(&devs->tree, map->m_deviceid - 1);
		if (!dif) {
			up_read(&devs->rwsem);
			return -ENODEV;
		}
		if (devs->flatdev) {
			map->m_pa += erofs_pos(sb, dif->uniaddr);
			up_read(&devs->rwsem);
			return 0;
		}
		erofs_fill_from_devinfo(map, sb, dif);
		up_read(&devs->rwsem);
	} else if (devs->extra_devices && !devs->flatdev) {
		down_read(&devs->rwsem);
		idr_for_each_entry(&devs->tree, dif, id) {
			if (!dif->uniaddr)
				continue;

			startoff = erofs_pos(sb, dif->uniaddr);
			if (map->m_pa >= startoff &&
			    map->m_pa < startoff + erofs_pos(sb, dif->blocks)) {
				map->m_pa -= startoff;
				erofs_fill_from_devinfo(map, sb, dif);
				break;
			}
		}
		up_read(&devs->rwsem);
	}
	return 0;
}

/*
 * bit 30: I/O error occurred on this folio
 * bit 29: CPU has dirty data in D-cache (needs aliasing handling);
 * bit 0 - 29: remaining parts to complete this folio
 */
#define EROFS_ONLINEFOLIO_EIO		30
#define EROFS_ONLINEFOLIO_DIRTY		29

void erofs_onlinefolio_init(struct folio *folio)
{
	union {
		atomic_t o;
		void *v;
	} u = { .o = ATOMIC_INIT(1) };

	folio->private = u.v;	/* valid only if file-backed folio is locked */
}

void erofs_onlinefolio_split(struct folio *folio)
{
	atomic_inc((atomic_t *)&folio->private);
}

void erofs_onlinefolio_end(struct folio *folio, int err, bool dirty)
{
	int orig, v;

	do {
		orig = atomic_read((atomic_t *)&folio->private);
		DBG_BUGON(orig <= 0);
		v = dirty << EROFS_ONLINEFOLIO_DIRTY;
		v |= (orig - 1) | (!!err << EROFS_ONLINEFOLIO_EIO);
	} while (atomic_cmpxchg((atomic_t *)&folio->private, orig, v) != orig);

	if (v & (BIT(EROFS_ONLINEFOLIO_DIRTY) - 1))
		return;
	folio->private = 0;
	if (v & BIT(EROFS_ONLINEFOLIO_DIRTY))
		flush_dcache_folio(folio);
	folio_end_read(folio, !(v & BIT(EROFS_ONLINEFOLIO_EIO)));
}

struct erofs_iomap_iter_ctx {
	struct page *page;
	void *base;
	struct inode *realinode;
};

static int erofs_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
		unsigned int flags, struct iomap *iomap, struct iomap *srcmap)
{
	struct iomap_iter *iter = container_of(iomap, struct iomap_iter, iomap);
	struct erofs_iomap_iter_ctx *ctx = iter->private;
	struct inode *realinode = ctx ? ctx->realinode : inode;
	struct super_block *sb = realinode->i_sb;
	struct erofs_map_blocks map;
	struct erofs_map_dev mdev;
	int ret;

	map.m_la = offset;
	map.m_llen = length;
	ret = erofs_map_blocks(realinode, &map);
	if (ret < 0)
		return ret;

	iomap->offset = map.m_la;
	iomap->length = map.m_llen;
	iomap->flags = 0;
	iomap->addr = IOMAP_NULL_ADDR;
	if (!(map.m_flags & EROFS_MAP_MAPPED)) {
		iomap->type = IOMAP_HOLE;
		return 0;
	}

	if (!(map.m_flags & EROFS_MAP_META) || !erofs_inode_in_metabox(realinode)) {
		mdev = (struct erofs_map_dev) {
			.m_deviceid = map.m_deviceid,
			.m_pa = map.m_pa,
		};
		ret = erofs_map_dev(sb, &mdev);
		if (ret)
			return ret;

		if (flags & IOMAP_DAX)
			iomap->dax_dev = mdev.m_dif->dax_dev;
		else
			iomap->bdev = mdev.m_bdev;
		iomap->addr = mdev.m_dif->fsoff + mdev.m_pa;
		if (flags & IOMAP_DAX)
			iomap->addr += mdev.m_dif->dax_part_off;
	}

	if (map.m_flags & EROFS_MAP_META) {
		iomap->type = IOMAP_INLINE;
		/* read context should read the inlined data */
		if (ctx) {
			struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
			void *ptr;

			ptr = erofs_read_metabuf(&buf, sb, map.m_pa,
						 erofs_inode_in_metabox(realinode));
			if (IS_ERR(ptr))
				return PTR_ERR(ptr);
			iomap->inline_data = ptr;
			ctx->page = buf.page;
			ctx->base = buf.base;
		}
	} else {
		iomap->type = IOMAP_MAPPED;
	}
	return 0;
}

static int erofs_iomap_end(struct inode *inode, loff_t pos, loff_t length,
		ssize_t written, unsigned int flags, struct iomap *iomap)
{
	struct iomap_iter *iter = container_of(iomap, struct iomap_iter, iomap);
	struct erofs_iomap_iter_ctx *ctx = iter->private;

	if (ctx && ctx->base) {
		struct erofs_buf buf = {
			.page = ctx->page,
			.base = ctx->base,
		};

		DBG_BUGON(iomap->type != IOMAP_INLINE);
		erofs_put_metabuf(&buf);
		ctx->base = NULL;
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
		if (!IS_ENABLED(CONFIG_EROFS_FS_ZIP))
			return -EOPNOTSUPP;
		return iomap_fiemap(inode, fieinfo, start, len,
				    &z_erofs_iomap_report_ops);
	}
	return iomap_fiemap(inode, fieinfo, start, len, &erofs_iomap_ops);
}

/*
 * since we dont have write or truncate flows, so no inode
 * locking needs to be held at the moment.
 */
static int erofs_read_folio(struct file *file, struct folio *folio)
{
	struct iomap_read_folio_ctx read_ctx = {
		.ops		= &iomap_bio_read_ops,
		.cur_folio	= folio,
	};
	bool need_iput;
	struct erofs_iomap_iter_ctx iter_ctx = {
		.realinode = erofs_real_inode(folio_inode(folio), &need_iput),
	};

	trace_erofs_read_folio(iter_ctx.realinode, folio, true);
	iomap_read_folio(&erofs_iomap_ops, &read_ctx, &iter_ctx);
	if (need_iput)
		iput(iter_ctx.realinode);
	return 0;
}

static void erofs_readahead(struct readahead_control *rac)
{
	struct iomap_read_folio_ctx read_ctx = {
		.ops		= &iomap_bio_read_ops,
		.rac		= rac,
	};
	bool need_iput;
	struct erofs_iomap_iter_ctx iter_ctx = {
		.realinode = erofs_real_inode(rac->mapping->host, &need_iput),
	};

	trace_erofs_readahead(iter_ctx.realinode, readahead_index(rac),
			      readahead_count(rac), true);
	iomap_readahead(&erofs_iomap_ops, &read_ctx, &iter_ctx);
	if (need_iput)
		iput(iter_ctx.realinode);
}

static sector_t erofs_bmap(struct address_space *mapping, sector_t block)
{
	return iomap_bmap(mapping, block, &erofs_iomap_ops);
}

static ssize_t erofs_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct inode *inode = file_inode(iocb->ki_filp);

	/* no need taking (shared) inode lock since it's a ro filesystem */
	if (!iov_iter_count(to))
		return 0;

	if (IS_ENABLED(CONFIG_FS_DAX) && IS_DAX(inode))
		return dax_iomap_rw(iocb, to, &erofs_iomap_ops);

	if ((iocb->ki_flags & IOCB_DIRECT) && inode->i_sb->s_bdev) {
		struct erofs_iomap_iter_ctx iter_ctx = {
			.realinode = inode,
		};

		return iomap_dio_rw(iocb, to, &erofs_iomap_ops,
				    NULL, 0, &iter_ctx, 0);
	}
	return filemap_read(iocb, to, 0);
}

/* for uncompressed (aligned) files and raw access for other files */
const struct address_space_operations erofs_aops = {
	.read_folio = erofs_read_folio,
	.readahead = erofs_readahead,
	.bmap = erofs_bmap,
	.direct_IO = noop_direct_IO,
	.release_folio = iomap_release_folio,
	.invalidate_folio = iomap_invalidate_folio,
};

#ifdef CONFIG_FS_DAX
static vm_fault_t erofs_dax_huge_fault(struct vm_fault *vmf,
		unsigned int order)
{
	return dax_iomap_fault(vmf, order, NULL, NULL, &erofs_iomap_ops);
}

static vm_fault_t erofs_dax_fault(struct vm_fault *vmf)
{
	return erofs_dax_huge_fault(vmf, 0);
}

static const struct vm_operations_struct erofs_dax_vm_ops = {
	.fault		= erofs_dax_fault,
	.huge_fault	= erofs_dax_huge_fault,
};

static int erofs_file_mmap_prepare(struct vm_area_desc *desc)
{
	if (!IS_DAX(file_inode(desc->file)))
		return generic_file_readonly_mmap_prepare(desc);

	if ((desc->vm_flags & VM_SHARED) && (desc->vm_flags & VM_MAYWRITE))
		return -EINVAL;

	desc->vm_ops = &erofs_dax_vm_ops;
	desc->vm_flags |= VM_HUGEPAGE;
	return 0;
}
#else
#define erofs_file_mmap_prepare	generic_file_readonly_mmap_prepare
#endif

static loff_t erofs_file_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;
	const struct iomap_ops *ops = &erofs_iomap_ops;

	if (erofs_inode_is_data_compressed(EROFS_I(inode)->datalayout)) {
		if (!IS_ENABLED(CONFIG_EROFS_FS_ZIP))
			return generic_file_llseek(file, offset, whence);
		ops = &z_erofs_iomap_report_ops;
	}

	if (whence == SEEK_HOLE)
		offset = iomap_seek_hole(inode, offset, ops);
	else if (whence == SEEK_DATA)
		offset = iomap_seek_data(inode, offset, ops);
	else
		return generic_file_llseek(file, offset, whence);

	if (offset < 0)
		return offset;
	return vfs_setpos(file, offset, inode->i_sb->s_maxbytes);
}

const struct file_operations erofs_file_fops = {
	.llseek		= erofs_file_llseek,
	.read_iter	= erofs_file_read_iter,
	.unlocked_ioctl = erofs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = erofs_compat_ioctl,
#endif
	.mmap_prepare	= erofs_file_mmap_prepare,
	.get_unmapped_area = thp_get_unmapped_area,
	.splice_read	= filemap_splice_read,
	.setlease	= generic_setlease,
};
