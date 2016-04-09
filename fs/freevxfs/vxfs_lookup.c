/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
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
	.iterate =		vxfs_readdir,
};

static inline u_long
dir_blocks(struct inode *ip)
{
	u_long			bsize = ip->i_sb->s_blocksize;
	return (ip->i_size + bsize - 1) & ~(bsize - 1);
}

/*
 * NOTE! unlike strncmp, vxfs_match returns 1 for success, 0 for failure.
 *
 * len <= VXFS_NAMELEN and de != NULL are guaranteed by caller.
 */
static inline int
vxfs_match(int len, const char * const name, struct vxfs_direct *de)
{
	if (len != de->d_namelen)
		return 0;
	if (!de->d_ino)
		return 0;
	return !memcmp(name, de->d_name, len);
}

static inline struct vxfs_direct *
vxfs_next_entry(struct vxfs_direct *de)
{
	return ((struct vxfs_direct *)((char*)de + de->d_reclen));
}

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
	u_long				npages, page, nblocks, pblocks, block;
	u_long				bsize = ip->i_sb->s_blocksize;
	const char			*name = dp->d_name.name;
	int				namelen = dp->d_name.len;

	npages = dir_pages(ip);
	nblocks = dir_blocks(ip);
	pblocks = VXFS_BLOCK_PER_PAGE(ip->i_sb);
	
	for (page = 0; page < npages; page++) {
		caddr_t			kaddr;
		struct page		*pp;

		pp = vxfs_get_page(ip->i_mapping, page);
		if (IS_ERR(pp))
			continue;
		kaddr = (caddr_t)page_address(pp);

		for (block = 0; block <= nblocks && block <= pblocks; block++) {
			caddr_t			baddr, limit;
			struct vxfs_dirblk	*dbp;
			struct vxfs_direct	*de;

			baddr = kaddr + (block * bsize);
			limit = baddr + bsize - VXFS_DIRLEN(1);
			
			dbp = (struct vxfs_dirblk *)baddr;
			de = (struct vxfs_direct *)(baddr + VXFS_DIRBLKOV(dbp));

			for (; (caddr_t)de <= limit; de = vxfs_next_entry(de)) {
				if (!de->d_reclen)
					break;
				if (!de->d_ino)
					continue;
				if (vxfs_match(namelen, name, de)) {
					*ppp = pp;
					return (de);
				}
			}
		}
		vxfs_put_page(pp);
	}

	return NULL;
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
		ino = de->d_ino;
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
	if (ino) {
		ip = vxfs_iget(dip->i_sb, ino);
		if (IS_ERR(ip))
			return ERR_CAST(ip);
	}
	d_add(dp, ip);
	return NULL;
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
	u_long			page, npages, block, pblocks, nblocks, offset;
	loff_t			pos;

	if (ctx->pos == 0) {
		if (!dir_emit_dot(fp, ctx))
			return 0;
		ctx->pos = 1;
	}
	if (ctx->pos == 1) {
		if (!dir_emit(ctx, "..", 2, VXFS_INO(ip)->vii_dotdot, DT_DIR))
			return 0;
		ctx->pos = 2;
	}
	pos = ctx->pos - 2;
	
	if (pos > VXFS_DIRROUND(ip->i_size))
		return 0;

	npages = dir_pages(ip);
	nblocks = dir_blocks(ip);
	pblocks = VXFS_BLOCK_PER_PAGE(sbp);

	page = pos >> PAGE_SHIFT;
	offset = pos & ~PAGE_MASK;
	block = (u_long)(pos >> sbp->s_blocksize_bits) % pblocks;

	for (; page < npages; page++, block = 0) {
		char			*kaddr;
		struct page		*pp;

		pp = vxfs_get_page(ip->i_mapping, page);
		if (IS_ERR(pp))
			continue;
		kaddr = (char *)page_address(pp);

		for (; block <= nblocks && block <= pblocks; block++) {
			char			*baddr, *limit;
			struct vxfs_dirblk	*dbp;
			struct vxfs_direct	*de;

			baddr = kaddr + (block * bsize);
			limit = baddr + bsize - VXFS_DIRLEN(1);
	
			dbp = (struct vxfs_dirblk *)baddr;
			de = (struct vxfs_direct *)
				(offset ?
				 (kaddr + offset) :
				 (baddr + VXFS_DIRBLKOV(dbp)));

			for (; (char *)de <= limit; de = vxfs_next_entry(de)) {
				if (!de->d_reclen)
					break;
				if (!de->d_ino)
					continue;

				offset = (char *)de - kaddr;
				ctx->pos = ((page << PAGE_SHIFT) | offset) + 2;
				if (!dir_emit(ctx, de->d_name, de->d_namelen,
					de->d_ino, DT_UNKNOWN)) {
					vxfs_put_page(pp);
					return 0;
				}
			}
			offset = 0;
		}
		vxfs_put_page(pp);
		offset = 0;
	}
	ctx->pos = ((page << PAGE_SHIFT) | offset) + 2;
	return 0;
}
