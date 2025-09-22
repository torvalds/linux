/* $OpenBSD: fusefs_node.h,v 1.5 2024/10/31 13:55:21 claudio Exp $ */
/*
 * Copyright (c) 2012-2013 Sylvestre Gallon <ccna.syl@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _FUSEFS_NODE_H_
#define _FUSEFS_NODE_H_

#include <sys/queue.h>

enum fufh_type {
	FUFH_INVALID = -1,
	FUFH_RDONLY  = 0,
	FUFH_WRONLY  = 1,
	FUFH_RDWR    = 2,
	FUFH_MAXTYPE = 3,
};

struct fusefs_filehandle {
	uint64_t fh_id;
	enum fufh_type fh_type;
};

struct fusefs_mnt;
struct fusefs_node {
	LIST_ENTRY(fusefs_node)	 i_hash; /* Hash chain */
	struct	vnode		*i_vnode;/* Vnode associated with this inode. */
	struct	fusefs_mnt	*i_ump;
	dev_t			 i_dev;	 /* Device associated with the inode. */
	ino_t			 i_number;	/* The identity of the inode. */
	struct	lockf_state	*i_lockf;	/* Byte-level lock state. */
	struct	rrwlock		 i_lock;	/* Inode lock */

	/** I/O **/
	struct     fusefs_filehandle fufh[FUFH_MAXTYPE];

	/** meta **/
	off_t			 filesize;
};

#ifdef ITOV
# undef ITOV
#endif
#define ITOV(ip) ((ip)->i_vnode)

#ifdef VTOI
# undef VTOI
#endif
#define VTOI(vp) ((struct fusefs_node *)(vp)->v_data)

void		 fuse_ihashinit(void);
struct vnode	*fuse_ihashget(dev_t, ino_t);
int		 fuse_ihashins(struct fusefs_node *);
void		 fuse_ihashrem(struct fusefs_node *);

uint64_t fusefs_fd_get(struct fusefs_node *, enum fufh_type);

#endif /* _FUSEFS_NODE_H_ */
