/*
 *   Copyright (c) International Business Machines Corp., 2000-2002
 *   Portions Copyright (c) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef	_H_JFS_METAPAGE
#define _H_JFS_METAPAGE

#include <linux/pagemap.h>

struct metapage {
	/* Common logsyncblk prefix (see jfs_logmgr.h) */
	u16 xflag;
	u16 unused;
	lid_t lid;
	int lsn;
	struct list_head synclist;
	/* End of logsyncblk prefix */

	unsigned long flag;	/* See Below */
	unsigned long count;	/* Reference count */
	void *data;		/* Data pointer */

	/* list management stuff */
	struct metapage *hash_prev;
	struct metapage *hash_next;	/* Also used for free list */

	/*
	 * mapping & index become redundant, but we need these here to
	 * add the metapage to the hash before we have the real page
	 */
	struct address_space *mapping;
	unsigned long index;
	wait_queue_head_t wait;

	/* implementation */
	struct page *page;
	unsigned long logical_size;

	/* Journal management */
	int clsn;
	atomic_t nohomeok;
	struct jfs_log *log;
};

/* metapage flag */
#define META_locked	0
#define META_absolute	1
#define META_free	2
#define META_dirty	3
#define META_sync	4
#define META_discard	5
#define META_forced	6
#define META_stale	7

#define mark_metapage_dirty(mp) set_bit(META_dirty, &(mp)->flag)

/* function prototypes */
extern struct metapage *__get_metapage(struct inode *inode,
				  unsigned long lblock, unsigned int size,
				  int absolute, unsigned long new);

#define read_metapage(inode, lblock, size, absolute)\
	 __get_metapage(inode, lblock, size, absolute, FALSE)

#define get_metapage(inode, lblock, size, absolute)\
	 __get_metapage(inode, lblock, size, absolute, TRUE)

extern void release_metapage(struct metapage *);
extern void hold_metapage(struct metapage *, int);

static inline void write_metapage(struct metapage *mp)
{
	set_bit(META_dirty, &mp->flag);
	release_metapage(mp);
}

static inline void flush_metapage(struct metapage *mp)
{
	set_bit(META_sync, &mp->flag);
	write_metapage(mp);
}

static inline void discard_metapage(struct metapage *mp)
{
	clear_bit(META_dirty, &mp->flag);
	set_bit(META_discard, &mp->flag);
	release_metapage(mp);
}

/*
 * This routines invalidate all pages for an extent.
 */
extern void __invalidate_metapages(struct inode *, s64, int);
#define invalidate_pxd_metapages(ip, pxd) \
	__invalidate_metapages((ip), addressPXD(&(pxd)), lengthPXD(&(pxd)))
#define invalidate_dxd_metapages(ip, dxd) \
	__invalidate_metapages((ip), addressDXD(&(dxd)), lengthDXD(&(dxd)))
#define invalidate_xad_metapages(ip, xad) \
	__invalidate_metapages((ip), addressXAD(&(xad)), lengthXAD(&(xad)))

#endif				/* _H_JFS_METAPAGE */
