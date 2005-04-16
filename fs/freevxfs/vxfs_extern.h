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
 *
 */
#ifndef _VXFS_EXTERN_H_
#define _VXFS_EXTERN_H_

/*
 * Veritas filesystem driver - external prototypes.
 *
 * This file contains prototypes for all vxfs functions used
 * outside their respective source files.
 */


struct kmem_cache_s;
struct super_block;
struct vxfs_inode_info;
struct inode;


/* vxfs_bmap.c */
extern daddr_t			vxfs_bmap1(struct inode *, long);

/* vxfs_fshead.c */
extern int			vxfs_read_fshead(struct super_block *);

/* vxfs_inode.c */
extern struct kmem_cache_s	*vxfs_inode_cachep;
extern void			vxfs_dumpi(struct vxfs_inode_info *, ino_t);
extern struct inode *		vxfs_get_fake_inode(struct super_block *,
					struct vxfs_inode_info *);
extern void			vxfs_put_fake_inode(struct inode *);
extern struct vxfs_inode_info *	vxfs_blkiget(struct super_block *, u_long, ino_t);
extern struct vxfs_inode_info *	vxfs_stiget(struct super_block *, ino_t);
extern void			vxfs_read_inode(struct inode *);
extern void			vxfs_clear_inode(struct inode *);

/* vxfs_lookup.c */
extern struct inode_operations	vxfs_dir_inode_ops;
extern struct file_operations	vxfs_dir_operations;

/* vxfs_olt.c */
extern int			vxfs_read_olt(struct super_block *, u_long);

/* vxfs_subr.c */
extern struct page *		vxfs_get_page(struct address_space *, u_long);
extern void			vxfs_put_page(struct page *);
extern struct buffer_head *	vxfs_bread(struct inode *, int);

#endif /* _VXFS_EXTERN_H_ */
