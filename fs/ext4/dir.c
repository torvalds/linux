/*
 *  linux/fs/ext4/dir.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/dir.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext4 directory handling functions
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *
 * Hash Tree Directory indexing (c) 2001  Daniel Phillips
 *
 */

#include <linux/fs.h>
#include <linux/jbd2.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include "ext4.h"
#include "xattr.h"

static int ext4_dx_readdir(struct file *, struct dir_context *);

/**
 * Check if the given dir-inode refers to an htree-indexed directory
 * (or a directory which could potentially get converted to use htree
 * indexing).
 *
 * Return 1 if it is a dx dir, 0 if not
 */
static int is_dx_dir(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;

	if (EXT4_HAS_COMPAT_FEATURE(inode->i_sb,
		     EXT4_FEATURE_COMPAT_DIR_INDEX) &&
	    ((ext4_test_inode_flag(inode, EXT4_INODE_INDEX)) ||
	     ((inode->i_size >> sb->s_blocksize_bits) == 1) ||
	     ext4_has_inline_data(inode)))
		return 1;

	return 0;
}

/*
 * Return 0 if the directory entry is OK, and 1 if there is a problem
 *
 * Note: this is the opposite of what ext2 and ext3 historically returned...
 *
 * bh passed here can be an inode block or a dir data block, depending
 * on the inode inline data flag.
 */
int __ext4_check_dir_entry(const char *function, unsigned int line,
			   struct inode *dir, struct file *filp,
			   struct ext4_dir_entry_2 *de,
			   struct buffer_head *bh, char *buf, int size,
			   unsigned int offset)
{
	const char *error_msg = NULL;
	const int rlen = ext4_rec_len_from_disk(de->rec_len,
						dir->i_sb->s_blocksize);

	if (unlikely(rlen < EXT4_DIR_REC_LEN(1)))
		error_msg = "rec_len is smaller than minimal";
	else if (unlikely(rlen % 4 != 0))
		error_msg = "rec_len % 4 != 0";
	else if (unlikely(rlen < EXT4_DIR_REC_LEN(de->name_len)))
		error_msg = "rec_len is too small for name_len";
	else if (unlikely(((char *) de - buf) + rlen > size))
		error_msg = "directory entry across range";
	else if (unlikely(le32_to_cpu(de->inode) >
			le32_to_cpu(EXT4_SB(dir->i_sb)->s_es->s_inodes_count)))
		error_msg = "inode out of bounds";
	else
		return 0;

	if (filp)
		ext4_error_file(filp, function, line, bh->b_blocknr,
				"bad entry in directory: %s - offset=%u(%u), "
				"inode=%u, rec_len=%d, name_len=%d",
				error_msg, (unsigned) (offset % size),
				offset, le32_to_cpu(de->inode),
				rlen, de->name_len);
	else
		ext4_error_inode(dir, function, line, bh->b_blocknr,
				"bad entry in directory: %s - offset=%u(%u), "
				"inode=%u, rec_len=%d, name_len=%d",
				error_msg, (unsigned) (offset % size),
				offset, le32_to_cpu(de->inode),
				rlen, de->name_len);

	return 1;
}

static int ext4_readdir(struct file *file, struct dir_context *ctx)
{
	unsigned int offset;
	int i;
	struct ext4_dir_entry_2 *de;
	int err;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	int dir_has_error = 0;

	if (is_dx_dir(inode)) {
		err = ext4_dx_readdir(file, ctx);
		if (err != ERR_BAD_DX_DIR) {
			return err;
		}
		/*
		 * We don't set the inode dirty flag since it's not
		 * critical that it get flushed back to the disk.
		 */
		ext4_clear_inode_flag(file_inode(file),
				      EXT4_INODE_INDEX);
	}

	if (ext4_has_inline_data(inode)) {
		int has_inline_data = 1;
		int ret = ext4_read_inline_dir(file, ctx,
					   &has_inline_data);
		if (has_inline_data)
			return ret;
	}

	offset = ctx->pos & (sb->s_blocksize - 1);

	while (ctx->pos < inode->i_size) {
		struct ext4_map_blocks map;
		struct buffer_head *bh = NULL;

		map.m_lblk = ctx->pos >> EXT4_BLOCK_SIZE_BITS(sb);
		map.m_len = 1;
		err = ext4_map_blocks(NULL, inode, &map, 0);
		if (err > 0) {
			pgoff_t index = map.m_pblk >>
					(PAGE_CACHE_SHIFT - inode->i_blkbits);
			if (!ra_has_index(&file->f_ra, index))
				page_cache_sync_readahead(
					sb->s_bdev->bd_inode->i_mapping,
					&file->f_ra, file,
					index, 1);
			file->f_ra.prev_pos = (loff_t)index << PAGE_CACHE_SHIFT;
			bh = ext4_bread(NULL, inode, map.m_lblk, 0);
			if (IS_ERR(bh))
				return PTR_ERR(bh);
		}

		if (!bh) {
			if (!dir_has_error) {
				EXT4_ERROR_FILE(file, 0,
						"directory contains a "
						"hole at offset %llu",
					   (unsigned long long) ctx->pos);
				dir_has_error = 1;
			}
			/* corrupt size?  Maybe no more blocks to read */
			if (ctx->pos > inode->i_blocks << 9)
				break;
			ctx->pos += sb->s_blocksize - offset;
			continue;
		}

		/* Check the checksum */
		if (!buffer_verified(bh) &&
		    !ext4_dirent_csum_verify(inode,
				(struct ext4_dir_entry *)bh->b_data)) {
			EXT4_ERROR_FILE(file, 0, "directory fails checksum "
					"at offset %llu",
					(unsigned long long)ctx->pos);
			ctx->pos += sb->s_blocksize - offset;
			brelse(bh);
			continue;
		}
		set_buffer_verified(bh);

		/* If the dir block has changed since the last call to
		 * readdir(2), then we might be pointing to an invalid
		 * dirent right now.  Scan from the start of the block
		 * to make sure. */
		if (file->f_version != inode->i_version) {
			for (i = 0; i < sb->s_blocksize && i < offset; ) {
				de = (struct ext4_dir_entry_2 *)
					(bh->b_data + i);
				/* It's too expensive to do a full
				 * dirent test each time round this
				 * loop, but we do have to test at
				 * least that it is non-zero.  A
				 * failure will be detected in the
				 * dirent test below. */
				if (ext4_rec_len_from_disk(de->rec_len,
					sb->s_blocksize) < EXT4_DIR_REC_LEN(1))
					break;
				i += ext4_rec_len_from_disk(de->rec_len,
							    sb->s_blocksize);
			}
			offset = i;
			ctx->pos = (ctx->pos & ~(sb->s_blocksize - 1))
				| offset;
			file->f_version = inode->i_version;
		}

		while (ctx->pos < inode->i_size
		       && offset < sb->s_blocksize) {
			de = (struct ext4_dir_entry_2 *) (bh->b_data + offset);
			if (ext4_check_dir_entry(inode, file, de, bh,
						 bh->b_data, bh->b_size,
						 offset)) {
				/*
				 * On error, skip to the next block
				 */
				ctx->pos = (ctx->pos |
						(sb->s_blocksize - 1)) + 1;
				break;
			}
			offset += ext4_rec_len_from_disk(de->rec_len,
					sb->s_blocksize);
			if (le32_to_cpu(de->inode)) {
				if (!dir_emit(ctx, de->name,
						de->name_len,
						le32_to_cpu(de->inode),
						get_dtype(sb, de->file_type))) {
					brelse(bh);
					return 0;
				}
			}
			ctx->pos += ext4_rec_len_from_disk(de->rec_len,
						sb->s_blocksize);
		}
		offset = 0;
		brelse(bh);
		if (ctx->pos < inode->i_size) {
			if (!dir_relax(inode))
				return 0;
		}
	}
	return 0;
}

static inline int is_32bit_api(void)
{
#ifdef CONFIG_COMPAT
	return is_compat_task();
#else
	return (BITS_PER_LONG == 32);
#endif
}

/*
 * These functions convert from the major/minor hash to an f_pos
 * value for dx directories
 *
 * Upper layer (for example NFS) should specify FMODE_32BITHASH or
 * FMODE_64BITHASH explicitly. On the other hand, we allow ext4 to be mounted
 * directly on both 32-bit and 64-bit nodes, under such case, neither
 * FMODE_32BITHASH nor FMODE_64BITHASH is specified.
 */
static inline loff_t hash2pos(struct file *filp, __u32 major, __u32 minor)
{
	if ((filp->f_mode & FMODE_32BITHASH) ||
	    (!(filp->f_mode & FMODE_64BITHASH) && is_32bit_api()))
		return major >> 1;
	else
		return ((__u64)(major >> 1) << 32) | (__u64)minor;
}

static inline __u32 pos2maj_hash(struct file *filp, loff_t pos)
{
	if ((filp->f_mode & FMODE_32BITHASH) ||
	    (!(filp->f_mode & FMODE_64BITHASH) && is_32bit_api()))
		return (pos << 1) & 0xffffffff;
	else
		return ((pos >> 32) << 1) & 0xffffffff;
}

static inline __u32 pos2min_hash(struct file *filp, loff_t pos)
{
	if ((filp->f_mode & FMODE_32BITHASH) ||
	    (!(filp->f_mode & FMODE_64BITHASH) && is_32bit_api()))
		return 0;
	else
		return pos & 0xffffffff;
}

/*
 * Return 32- or 64-bit end-of-file for dx directories
 */
static inline loff_t ext4_get_htree_eof(struct file *filp)
{
	if ((filp->f_mode & FMODE_32BITHASH) ||
	    (!(filp->f_mode & FMODE_64BITHASH) && is_32bit_api()))
		return EXT4_HTREE_EOF_32BIT;
	else
		return EXT4_HTREE_EOF_64BIT;
}


/*
 * ext4_dir_llseek() calls generic_file_llseek_size to handle htree
 * directories, where the "offset" is in terms of the filename hash
 * value instead of the byte offset.
 *
 * Because we may return a 64-bit hash that is well beyond offset limits,
 * we need to pass the max hash as the maximum allowable offset in
 * the htree directory case.
 *
 * For non-htree, ext4_llseek already chooses the proper max offset.
 */
static loff_t ext4_dir_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;
	int dx_dir = is_dx_dir(inode);
	loff_t htree_max = ext4_get_htree_eof(file);

	if (likely(dx_dir))
		return generic_file_llseek_size(file, offset, whence,
						    htree_max, htree_max);
	else
		return ext4_llseek(file, offset, whence);
}

/*
 * This structure holds the nodes of the red-black tree used to store
 * the directory entry in hash order.
 */
struct fname {
	__u32		hash;
	__u32		minor_hash;
	struct rb_node	rb_hash;
	struct fname	*next;
	__u32		inode;
	__u8		name_len;
	__u8		file_type;
	char		name[0];
};

/*
 * This functoin implements a non-recursive way of freeing all of the
 * nodes in the red-black tree.
 */
static void free_rb_tree_fname(struct rb_root *root)
{
	struct fname *fname, *next;

	rbtree_postorder_for_each_entry_safe(fname, next, root, rb_hash)
		while (fname) {
			struct fname *old = fname;
			fname = fname->next;
			kfree(old);
		}

	*root = RB_ROOT;
}


static struct dir_private_info *ext4_htree_create_dir_info(struct file *filp,
							   loff_t pos)
{
	struct dir_private_info *p;

	p = kzalloc(sizeof(struct dir_private_info), GFP_KERNEL);
	if (!p)
		return NULL;
	p->curr_hash = pos2maj_hash(filp, pos);
	p->curr_minor_hash = pos2min_hash(filp, pos);
	return p;
}

void ext4_htree_free_dir_info(struct dir_private_info *p)
{
	free_rb_tree_fname(&p->root);
	kfree(p);
}

/*
 * Given a directory entry, enter it into the fname rb tree.
 */
int ext4_htree_store_dirent(struct file *dir_file, __u32 hash,
			     __u32 minor_hash,
			     struct ext4_dir_entry_2 *dirent)
{
	struct rb_node **p, *parent = NULL;
	struct fname *fname, *new_fn;
	struct dir_private_info *info;
	int len;

	info = dir_file->private_data;
	p = &info->root.rb_node;

	/* Create and allocate the fname structure */
	len = sizeof(struct fname) + dirent->name_len + 1;
	new_fn = kzalloc(len, GFP_KERNEL);
	if (!new_fn)
		return -ENOMEM;
	new_fn->hash = hash;
	new_fn->minor_hash = minor_hash;
	new_fn->inode = le32_to_cpu(dirent->inode);
	new_fn->name_len = dirent->name_len;
	new_fn->file_type = dirent->file_type;
	memcpy(new_fn->name, dirent->name, dirent->name_len);
	new_fn->name[dirent->name_len] = 0;

	while (*p) {
		parent = *p;
		fname = rb_entry(parent, struct fname, rb_hash);

		/*
		 * If the hash and minor hash match up, then we put
		 * them on a linked list.  This rarely happens...
		 */
		if ((new_fn->hash == fname->hash) &&
		    (new_fn->minor_hash == fname->minor_hash)) {
			new_fn->next = fname->next;
			fname->next = new_fn;
			return 0;
		}

		if (new_fn->hash < fname->hash)
			p = &(*p)->rb_left;
		else if (new_fn->hash > fname->hash)
			p = &(*p)->rb_right;
		else if (new_fn->minor_hash < fname->minor_hash)
			p = &(*p)->rb_left;
		else /* if (new_fn->minor_hash > fname->minor_hash) */
			p = &(*p)->rb_right;
	}

	rb_link_node(&new_fn->rb_hash, parent, p);
	rb_insert_color(&new_fn->rb_hash, &info->root);
	return 0;
}



/*
 * This is a helper function for ext4_dx_readdir.  It calls filldir
 * for all entres on the fname linked list.  (Normally there is only
 * one entry on the linked list, unless there are 62 bit hash collisions.)
 */
static int call_filldir(struct file *file, struct dir_context *ctx,
			struct fname *fname)
{
	struct dir_private_info *info = file->private_data;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;

	if (!fname) {
		ext4_msg(sb, KERN_ERR, "%s:%d: inode #%lu: comm %s: "
			 "called with null fname?!?", __func__, __LINE__,
			 inode->i_ino, current->comm);
		return 0;
	}
	ctx->pos = hash2pos(file, fname->hash, fname->minor_hash);
	while (fname) {
		if (!dir_emit(ctx, fname->name,
				fname->name_len,
				fname->inode,
				get_dtype(sb, fname->file_type))) {
			info->extra_fname = fname;
			return 1;
		}
		fname = fname->next;
	}
	return 0;
}

static int ext4_dx_readdir(struct file *file, struct dir_context *ctx)
{
	struct dir_private_info *info = file->private_data;
	struct inode *inode = file_inode(file);
	struct fname *fname;
	int	ret;

	if (!info) {
		info = ext4_htree_create_dir_info(file, ctx->pos);
		if (!info)
			return -ENOMEM;
		file->private_data = info;
	}

	if (ctx->pos == ext4_get_htree_eof(file))
		return 0;	/* EOF */

	/* Some one has messed with f_pos; reset the world */
	if (info->last_pos != ctx->pos) {
		free_rb_tree_fname(&info->root);
		info->curr_node = NULL;
		info->extra_fname = NULL;
		info->curr_hash = pos2maj_hash(file, ctx->pos);
		info->curr_minor_hash = pos2min_hash(file, ctx->pos);
	}

	/*
	 * If there are any leftover names on the hash collision
	 * chain, return them first.
	 */
	if (info->extra_fname) {
		if (call_filldir(file, ctx, info->extra_fname))
			goto finished;
		info->extra_fname = NULL;
		goto next_node;
	} else if (!info->curr_node)
		info->curr_node = rb_first(&info->root);

	while (1) {
		/*
		 * Fill the rbtree if we have no more entries,
		 * or the inode has changed since we last read in the
		 * cached entries.
		 */
		if ((!info->curr_node) ||
		    (file->f_version != inode->i_version)) {
			info->curr_node = NULL;
			free_rb_tree_fname(&info->root);
			file->f_version = inode->i_version;
			ret = ext4_htree_fill_tree(file, info->curr_hash,
						   info->curr_minor_hash,
						   &info->next_hash);
			if (ret < 0)
				return ret;
			if (ret == 0) {
				ctx->pos = ext4_get_htree_eof(file);
				break;
			}
			info->curr_node = rb_first(&info->root);
		}

		fname = rb_entry(info->curr_node, struct fname, rb_hash);
		info->curr_hash = fname->hash;
		info->curr_minor_hash = fname->minor_hash;
		if (call_filldir(file, ctx, fname))
			break;
	next_node:
		info->curr_node = rb_next(info->curr_node);
		if (info->curr_node) {
			fname = rb_entry(info->curr_node, struct fname,
					 rb_hash);
			info->curr_hash = fname->hash;
			info->curr_minor_hash = fname->minor_hash;
		} else {
			if (info->next_hash == ~0) {
				ctx->pos = ext4_get_htree_eof(file);
				break;
			}
			info->curr_hash = info->next_hash;
			info->curr_minor_hash = 0;
		}
	}
finished:
	info->last_pos = ctx->pos;
	return 0;
}

static int ext4_release_dir(struct inode *inode, struct file *filp)
{
	if (filp->private_data)
		ext4_htree_free_dir_info(filp->private_data);

	return 0;
}

int ext4_check_all_de(struct inode *dir, struct buffer_head *bh, void *buf,
		      int buf_size)
{
	struct ext4_dir_entry_2 *de;
	int nlen, rlen;
	unsigned int offset = 0;
	char *top;

	de = (struct ext4_dir_entry_2 *)buf;
	top = buf + buf_size;
	while ((char *) de < top) {
		if (ext4_check_dir_entry(dir, NULL, de, bh,
					 buf, buf_size, offset))
			return -EIO;
		nlen = EXT4_DIR_REC_LEN(de->name_len);
		rlen = ext4_rec_len_from_disk(de->rec_len, buf_size);
		de = (struct ext4_dir_entry_2 *)((char *)de + rlen);
		offset += rlen;
	}
	if ((char *) de > top)
		return -EIO;

	return 0;
}

const struct file_operations ext4_dir_operations = {
	.llseek		= ext4_dir_llseek,
	.read		= generic_read_dir,
	.iterate	= ext4_readdir,
	.unlocked_ioctl = ext4_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ext4_compat_ioctl,
#endif
	.fsync		= ext4_sync_file,
	.release	= ext4_release_dir,
};
