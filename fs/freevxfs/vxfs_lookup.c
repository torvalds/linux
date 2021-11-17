/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 * Copyright (c) 2016 Krzysztof Blaszkowski
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Veritas filesystem driver - lookup and other directory related code.
 */
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>

#include "vxfs.h"
#include "vxfs_dir.h"
#include "vxfs_inode.h"
#include "vxfs_extern.h"

/*
 * Number of VxFS blocks per page.
 */
#define VXFS_BLOCK_PER_PAGE(sbp)  ((PAGE_SIZE / (sbp)->s_blocksize))


static struct dentry *	vxfs_lookup(struct inode *, struct dentry *, unsigned int);
static int		vxfs_readdir(struct file *, struct dir_context *);

const struct inode_operations vxfs_dir_inode_ops = {
	.lookup =		vxfs_lookup,
};

const struct file_operations vxfs_dir_operations = {
	.llseek =		generic_file_llseek,
	.read =			generic_read_dir,
	.iterate_shared =	vxfs_readdir,
};


/**
 * vxfs_find_entry - find a mathing directory entry for a dentry
 * @ip:		directory inode
 * @dp:		dentry for which we want to find a direct
 * @ppp:	gets filled with the page the return value sits in
 *
 * Description:
 *   vxfs_find_entry finds a &struct vxfs_direct for the VFS directory
 *   cache entry @dp.  @ppp will be filled with the page the return
 *   value resides in.
 *
 * Returns:
 *   The wanted direct on success, else a NULL pointer.
 */
static struct vxfs_direct *
vxfs_find_entry(struct inode *ip, struct dentry *dp, struct page **ppp)
{
	u_long bsize = ip->i_sb->s_blocksize;
	const char *name = dp->d_name.name;
	int namelen = dp->d_name.len;
	loff_t limit = VXFS_DIRROUND(ip->i_size);
	struct vxfs_direct *de_exit = NULL;
	loff_t pos = 0;
	struct vxfs_sb_info *sbi = VXFS_SBI(ip->i_sb);

	while (pos < limit) {
		struct page *pp;
		char *kaddr;
		int pg_ofs = pos & ~PAGE_MASK;

		pp = vxfs_get_page(ip->i_mapping, pos >> PAGE_SHIFT);
		if (IS_ERR(pp))
			return NULL;
		kaddr = (char *)page_address(pp);

		while (pg_ofs < PAGE_SIZE && pos < limit) {
			struct vxfs_direct *de;

			if ((pos & (bsize - 1)) < 4) {
				struct vxfs_dirblk *dbp =
					(struct vxfs_dirblk *)
					 (kaddr + (pos & ~PAGE_MASK));
				int overhead = VXFS_DIRBLKOV(sbi, dbp);

				pos += overhead;
				pg_ofs += overhead;
			}
			de = (struct vxfs_direct *)(kaddr + pg_ofs);

			if (!de->d_reclen) {
				pos += bsize - 1;
				pos &= ~(bsize - 1);
				break;
			}

			pg_ofs += fs16_to_cpu(sbi, de->d_reclen);
			pos += fs16_to_cpu(sbi, de->d_reclen);
			if (!de->d_ino)
				continue;

			if (namelen != fs16_to_cpu(sbi, de->d_namelen))
				continue;
			if (!memcmp(name, de->d_name, namelen)) {
				*ppp = pp;
				de_exit = de;
				break;
			}
		}
		if (!de_exit)
			vxfs_put_page(pp);
		else
			break;
	}

	return de_exit;
}

/**
 * vxfs_inode_by_name - find inode number for dentry
 * @dip:	directory to search in
 * @dp:		dentry we search for
 *
 * Description:
 *   vxfs_inode_by_name finds out the inode number of
 *   the path component described by @dp in @dip.
 *
 * Returns:
 *   The wanted inode number on success, else Zero.
 */
static ino_t
vxfs_inode_by_name(struct inode *dip, struct dentry *dp)
{
	struct vxfs_direct		*de;
	struct page			*pp;
	ino_t				ino = 0;

	de = vxfs_find_entry(dip, dp, &pp);
	if (de) {
		ino = fs32_to_cpu(VXFS_SBI(dip->i_sb), de->d_ino);
		kunmap(pp);
		put_page(pp);
	}
	
	return (ino);
}

/**
 * vxfs_lookup - lookup pathname component
 * @dip:	dir in which we lookup
 * @dp:		dentry we lookup
 * @flags:	lookup flags
 *
 * Description:
 *   vxfs_lookup tries to lookup the pathname component described
 *   by @dp in @dip.
 *
 * Returns:
 *   A NULL-pointer on success, else a negative error code encoded
 *   in the return pointer.
 */
static struct dentry *
vxfs_lookup(struct inode *dip, struct dentry *dp, unsigned int flags)
{
	struct inode		*ip = NULL;
	ino_t			ino;
			 
	if (dp->d_name.len > VXFS_NAMELEN)
		return ERR_PTR(-ENAMETOOLONG);
				 
	ino = vxfs_inode_by_name(dip, dp);
	if (ino)
		ip = vxfs_iget(dip->i_sb, ino);
	return d_splice_alias(ip, dp);
}

/**
 * vxfs_readdir - read a directory
 * @fp:		the directory to read
 * @retp:	return buffer
 * @filler:	filldir callback
 *
 * Description:
 *   vxfs_readdir fills @retp with directory entries from @fp
 *   using the VFS supplied callback @filler.
 *
 * Returns:
 *   Zero.
 */
static int
vxfs_readdir(struct file *fp, struct dir_context *ctx)
{
	struct inode		*ip = file_inode(fp);
	struct super_block	*sbp = ip->i_sb;
	u_long			bsize = sbp->s_blocksize;
	loff_t			pos, limit;
	struct vxfs_sb_info	*sbi = VXFS_SBI(sbp);

	if (ctx->pos == 0) {
		if (!dir_emit_dot(fp, ctx))
			goto out;
		ctx->pos++;
	}
	if (ctx->pos == 1) {
		if (!dir_emit(ctx, "..", 2, VXFS_INO(ip)->vii_dotdot, DT_DIR))
			goto out;
		ctx->pos++;
	}

	limit = VXFS_DIRROUND(ip->i_size);
	if (ctx->pos > limit)
		goto out;

	pos = ctx->pos & ~3L;

	while (pos < limit) {
		struct page *pp;
		char *kaddr;
		int pg_ofs = pos & ~PAGE_MASK;
		int rc = 0;

		pp = vxfs_get_page(ip->i_mapping, pos >> PAGE_SHIFT);
		if (IS_ERR(pp))
			return -ENOMEM;

		kaddr = (char *)page_address(pp);

		while (pg_ofs < PAGE_SIZE && pos < limit) {
			struct vxfs_direct *de;

			if ((pos & (bsize - 1)) < 4) {
				struct vxfs_dirblk *dbp =
					(struct vxfs_dirblk *)
					 (kaddr + (pos & ~PAGE_MASK));
				int overhead = VXFS_DIRBLKOV(sbi, dbp);

				pos += overhead;
				pg_ofs += overhead;
			}
			de = (struct vxfs_direct *)(kaddr + pg_ofs);

			if (!de->d_reclen) {
				pos += bsize - 1;
				pos &= ~(bsize - 1);
				break;
			}

			pg_ofs += fs16_to_cpu(sbi, de->d_reclen);
			pos += fs16_to_cpu(sbi, de->d_reclen);
			if (!de->d_ino)
				continue;

			rc = dir_emit(ctx, de->d_name,
					fs16_to_cpu(sbi, de->d_namelen),
					fs32_to_cpu(sbi, de->d_ino),
					DT_UNKNOWN);
			if (!rc) {
				/* the dir entry was not read, fix pos. */
				pos -= fs16_to_cpu(sbi, de->d_reclen);
				break;
			}
		}
		vxfs_put_page(pp);
		if (!rc)
			break;
	}

	ctx->pos = pos | 2;

out:
	return 0;
}
