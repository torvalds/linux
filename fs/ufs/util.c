// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ufs/util.c
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 */
 
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>

#include "ufs_fs.h"
#include "ufs.h"
#include "swab.h"
#include "util.h"

struct ufs_buffer_head * _ubh_bread_ (struct ufs_sb_private_info * uspi,
	struct super_block *sb, u64 fragment, u64 size)
{
	struct ufs_buffer_head * ubh;
	unsigned i, j ;
	u64  count = 0;
	if (size & ~uspi->s_fmask)
		return NULL;
	count = size >> uspi->s_fshift;
	if (count > UFS_MAXFRAG)
		return NULL;
	ubh = kmalloc (sizeof (struct ufs_buffer_head), GFP_NOFS);
	if (!ubh)
		return NULL;
	ubh->fragment = fragment;
	ubh->count = count;
	for (i = 0; i < count; i++)
		if (!(ubh->bh[i] = sb_bread(sb, fragment + i)))
			goto failed;
	for (; i < UFS_MAXFRAG; i++)
		ubh->bh[i] = NULL;
	return ubh;
failed:
	for (j = 0; j < i; j++)
		brelse (ubh->bh[j]);
	kfree(ubh);
	return NULL;
}

struct ufs_buffer_head * ubh_bread_uspi (struct ufs_sb_private_info * uspi,
	struct super_block *sb, u64 fragment, u64 size)
{
	unsigned i, j;
	u64 count = 0;
	if (size & ~uspi->s_fmask)
		return NULL;
	count = size >> uspi->s_fshift;
	if (count <= 0 || count > UFS_MAXFRAG)
		return NULL;
	USPI_UBH(uspi)->fragment = fragment;
	USPI_UBH(uspi)->count = count;
	for (i = 0; i < count; i++)
		if (!(USPI_UBH(uspi)->bh[i] = sb_bread(sb, fragment + i)))
			goto failed;
	for (; i < UFS_MAXFRAG; i++)
		USPI_UBH(uspi)->bh[i] = NULL;
	return USPI_UBH(uspi);
failed:
	for (j = 0; j < i; j++)
		brelse (USPI_UBH(uspi)->bh[j]);
	return NULL;
}

void ubh_brelse (struct ufs_buffer_head * ubh)
{
	unsigned i;
	if (!ubh)
		return;
	for (i = 0; i < ubh->count; i++)
		brelse (ubh->bh[i]);
	kfree (ubh);
}

void ubh_brelse_uspi (struct ufs_sb_private_info * uspi)
{
	unsigned i;
	if (!USPI_UBH(uspi))
		return;
	for ( i = 0; i < USPI_UBH(uspi)->count; i++ ) {
		brelse (USPI_UBH(uspi)->bh[i]);
		USPI_UBH(uspi)->bh[i] = NULL;
	}
}

void ubh_mark_buffer_dirty (struct ufs_buffer_head * ubh)
{
	unsigned i;
	if (!ubh)
		return;
	for ( i = 0; i < ubh->count; i++ )
		mark_buffer_dirty (ubh->bh[i]);
}

void ubh_mark_buffer_uptodate (struct ufs_buffer_head * ubh, int flag)
{
	unsigned i;
	if (!ubh)
		return;
	if (flag) {
		for ( i = 0; i < ubh->count; i++ )
			set_buffer_uptodate (ubh->bh[i]);
	} else {
		for ( i = 0; i < ubh->count; i++ )
			clear_buffer_uptodate (ubh->bh[i]);
	}
}

void ubh_sync_block(struct ufs_buffer_head *ubh)
{
	if (ubh) {
		unsigned i;

		for (i = 0; i < ubh->count; i++)
			write_dirty_buffer(ubh->bh[i], 0);

		for (i = 0; i < ubh->count; i++)
			wait_on_buffer(ubh->bh[i]);
	}
}

void ubh_bforget (struct ufs_buffer_head * ubh)
{
	unsigned i;
	if (!ubh) 
		return;
	for ( i = 0; i < ubh->count; i++ ) if ( ubh->bh[i] ) 
		bforget (ubh->bh[i]);
}
 
int ubh_buffer_dirty (struct ufs_buffer_head * ubh)
{
	unsigned i;
	unsigned result = 0;
	if (!ubh)
		return 0;
	for ( i = 0; i < ubh->count; i++ )
		result |= buffer_dirty(ubh->bh[i]);
	return result;
}

void _ubh_ubhcpymem_(struct ufs_sb_private_info * uspi, 
	unsigned char * mem, struct ufs_buffer_head * ubh, unsigned size)
{
	unsigned len, bhno;
	if (size > (ubh->count << uspi->s_fshift))
		size = ubh->count << uspi->s_fshift;
	bhno = 0;
	while (size) {
		len = min_t(unsigned int, size, uspi->s_fsize);
		memcpy (mem, ubh->bh[bhno]->b_data, len);
		mem += uspi->s_fsize;
		size -= len;
		bhno++;
	}
}

void _ubh_memcpyubh_(struct ufs_sb_private_info * uspi, 
	struct ufs_buffer_head * ubh, unsigned char * mem, unsigned size)
{
	unsigned len, bhno;
	if (size > (ubh->count << uspi->s_fshift))
		size = ubh->count << uspi->s_fshift;
	bhno = 0;
	while (size) {
		len = min_t(unsigned int, size, uspi->s_fsize);
		memcpy (ubh->bh[bhno]->b_data, mem, len);
		mem += uspi->s_fsize;
		size -= len;
		bhno++;
	}
}

dev_t
ufs_get_inode_dev(struct super_block *sb, struct ufs_inode_info *ufsi)
{
	__u32 fs32;
	dev_t dev;

	if ((UFS_SB(sb)->s_flags & UFS_ST_MASK) == UFS_ST_SUNx86)
		fs32 = fs32_to_cpu(sb, ufsi->i_u1.i_data[1]);
	else
		fs32 = fs32_to_cpu(sb, ufsi->i_u1.i_data[0]);
	switch (UFS_SB(sb)->s_flags & UFS_ST_MASK) {
	case UFS_ST_SUNx86:
	case UFS_ST_SUN:
		if ((fs32 & 0xffff0000) == 0 ||
		    (fs32 & 0xffff0000) == 0xffff0000)
			dev = old_decode_dev(fs32 & 0x7fff);
		else
			dev = MKDEV(sysv_major(fs32), sysv_minor(fs32));
		break;

	default:
		dev = old_decode_dev(fs32);
		break;
	}
	return dev;
}

void
ufs_set_inode_dev(struct super_block *sb, struct ufs_inode_info *ufsi, dev_t dev)
{
	__u32 fs32;

	switch (UFS_SB(sb)->s_flags & UFS_ST_MASK) {
	case UFS_ST_SUNx86:
	case UFS_ST_SUN:
		fs32 = sysv_encode_dev(dev);
		if ((fs32 & 0xffff8000) == 0) {
			fs32 = old_encode_dev(dev);
		}
		break;

	default:
		fs32 = old_encode_dev(dev);
		break;
	}
	if ((UFS_SB(sb)->s_flags & UFS_ST_MASK) == UFS_ST_SUNx86)
		ufsi->i_u1.i_data[1] = cpu_to_fs32(sb, fs32);
	else
		ufsi->i_u1.i_data[0] = cpu_to_fs32(sb, fs32);
}

/**
 * ufs_get_locked_folio() - locate, pin and lock a pagecache folio, if not exist
 * read it from disk.
 * @mapping: the address_space to search
 * @index: the page index
 *
 * Locates the desired pagecache folio, if not exist we'll read it,
 * locks it, increments its reference
 * count and returns its address.
 *
 */
struct folio *ufs_get_locked_folio(struct address_space *mapping,
				 pgoff_t index)
{
	struct inode *inode = mapping->host;
	struct folio *folio = filemap_lock_folio(mapping, index);
	if (!folio) {
		folio = read_mapping_folio(mapping, index, NULL);

		if (IS_ERR(folio)) {
			printk(KERN_ERR "ufs_change_blocknr: read_mapping_folio error: ino %lu, index: %lu\n",
			       mapping->host->i_ino, index);
			return folio;
		}

		folio_lock(folio);

		if (unlikely(folio->mapping == NULL)) {
			/* Truncate got there first */
			folio_unlock(folio);
			folio_put(folio);
			return NULL;
		}
	}
	if (!folio_buffers(folio))
		create_empty_buffers(folio, 1 << inode->i_blkbits, 0);
	return folio;
}
