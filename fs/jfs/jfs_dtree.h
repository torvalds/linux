/*
 *   Copyright (C) International Business Machines Corp., 2000-2002
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
#ifndef _H_JFS_DTREE
#define	_H_JFS_DTREE

/*
 *	jfs_dtree.h: directory B+-tree manager
 */

#include "jfs_btree.h"

typedef union {
	struct {
		tid_t tid;
		struct inode *ip;
		u32 ino;
	} leaf;
	pxd_t xd;
} ddata_t;


/*
 *	entry segment/slot
 *
 * an entry consists of type dependent head/only segment/slot and
 * additional segments/slots linked vi next field;
 * N.B. last/only segment of entry is terminated by next = -1;
 */
/*
 *	directory page slot
 */
struct dtslot {
	s8 next;		/* 1: */
	s8 cnt;			/* 1: */
	__le16 name[15];	/* 30: */
};				/* (32) */


#define DATASLOTSIZE	16
#define L2DATASLOTSIZE	4
#define	DTSLOTSIZE	32
#define	L2DTSLOTSIZE	5
#define DTSLOTHDRSIZE	2
#define DTSLOTDATASIZE	30
#define DTSLOTDATALEN	15

/*
 *	 internal node entry head/only segment
 */
struct idtentry {
	pxd_t xd;		/* 8: child extent descriptor */

	s8 next;		/* 1: */
	u8 namlen;		/* 1: */
	__le16 name[11];	/* 22: 2-byte aligned */
};				/* (32) */

#define DTIHDRSIZE	10
#define DTIHDRDATALEN	11

/* compute number of slots for entry */
#define	NDTINTERNAL(klen) (DIV_ROUND_UP((4 + (klen)), 15))


/*
 *	leaf node entry head/only segment
 *
 *	For legacy filesystems, name contains 13 wchars -- no index field
 */
struct ldtentry {
	__le32 inumber;		/* 4: 4-byte aligned */
	s8 next;		/* 1: */
	u8 namlen;		/* 1: */
	__le16 name[11];	/* 22: 2-byte aligned */
	__le32 index;		/* 4: index into dir_table */
};				/* (32) */

#define DTLHDRSIZE	6
#define DTLHDRDATALEN_LEGACY	13	/* Old (OS/2) format */
#define DTLHDRDATALEN	11

/*
 * dir_table used for directory traversal during readdir
 */

/*
 * Keep persistent index for directory entries
 */
#define DO_INDEX(INODE) (JFS_SBI((INODE)->i_sb)->mntflag & JFS_DIR_INDEX)

/*
 * Maximum entry in inline directory table
 */
#define MAX_INLINE_DIRTABLE_ENTRY 13

struct dir_table_slot {
	u8 rsrvd;		/* 1: */
	u8 flag;		/* 1: 0 if free */
	u8 slot;		/* 1: slot within leaf page of entry */
	u8 addr1;		/* 1: upper 8 bits of leaf page address */
	__le32 addr2;		/* 4: lower 32 bits of leaf page address -OR-
				   index of next entry when this entry was deleted */
};				/* (8) */

/*
 * flag values
 */
#define DIR_INDEX_VALID 1
#define DIR_INDEX_FREE 0

#define DTSaddress(dir_table_slot, address64)\
{\
	(dir_table_slot)->addr1 = ((u64)address64) >> 32;\
	(dir_table_slot)->addr2 = __cpu_to_le32((address64) & 0xffffffff);\
}

#define addressDTS(dts)\
	( ((s64)((dts)->addr1)) << 32 | __le32_to_cpu((dts)->addr2) )

/* compute number of slots for entry */
#define	NDTLEAF_LEGACY(klen)	(DIV_ROUND_UP((2 + (klen)), 15))
#define	NDTLEAF	NDTINTERNAL


/*
 *	directory root page (in-line in on-disk inode):
 *
 * cf. dtpage_t below.
 */
typedef union {
	struct {
		struct dasd DASD; /* 16: DASD limit/usage info */

		u8 flag;	/* 1: */
		u8 nextindex;	/* 1: next free entry in stbl */
		s8 freecnt;	/* 1: free count */
		s8 freelist;	/* 1: freelist header */

		__le32 idotdot;	/* 4: parent inode number */

		s8 stbl[8];	/* 8: sorted entry index table */
	} header;		/* (32) */

	struct dtslot slot[9];
} dtroot_t;

#define PARENT(IP) \
	(le32_to_cpu(JFS_IP(IP)->i_dtroot.header.idotdot))

#define DTROOTMAXSLOT	9

#define	dtEmpty(IP) (JFS_IP(IP)->i_dtroot.header.nextindex == 0)


/*
 *	directory regular page:
 *
 *	entry slot array of 32 byte slot
 *
 * sorted entry slot index table (stbl):
 * contiguous slots at slot specified by stblindex,
 * 1-byte per entry
 *   512 byte block:  16 entry tbl (1 slot)
 *  1024 byte block:  32 entry tbl (1 slot)
 *  2048 byte block:  64 entry tbl (2 slot)
 *  4096 byte block: 128 entry tbl (4 slot)
 *
 * data area:
 *   512 byte block:  16 - 2 =  14 slot
 *  1024 byte block:  32 - 2 =  30 slot
 *  2048 byte block:  64 - 3 =  61 slot
 *  4096 byte block: 128 - 5 = 123 slot
 *
 * N.B. index is 0-based; index fields refer to slot index
 * except nextindex which refers to entry index in stbl;
 * end of entry stot list or freelist is marked with -1.
 */
typedef union {
	struct {
		__le64 next;	/* 8: next sibling */
		__le64 prev;	/* 8: previous sibling */

		u8 flag;	/* 1: */
		u8 nextindex;	/* 1: next entry index in stbl */
		s8 freecnt;	/* 1: */
		s8 freelist;	/* 1: slot index of head of freelist */

		u8 maxslot;	/* 1: number of slots in page slot[] */
		u8 stblindex;	/* 1: slot index of start of stbl */
		u8 rsrvd[2];	/* 2: */

		pxd_t self;	/* 8: self pxd */
	} header;		/* (32) */

	struct dtslot slot[128];
} dtpage_t;

#define DTPAGEMAXSLOT        128

#define DT8THPGNODEBYTES     512
#define DT8THPGNODETSLOTS      1
#define DT8THPGNODESLOTS      16

#define DTQTRPGNODEBYTES    1024
#define DTQTRPGNODETSLOTS      1
#define DTQTRPGNODESLOTS      32

#define DTHALFPGNODEBYTES   2048
#define DTHALFPGNODETSLOTS     2
#define DTHALFPGNODESLOTS     64

#define DTFULLPGNODEBYTES   4096
#define DTFULLPGNODETSLOTS     4
#define DTFULLPGNODESLOTS    128

#define DTENTRYSTART	1

/* get sorted entry table of the page */
#define DT_GETSTBL(p) ( ((p)->header.flag & BT_ROOT) ?\
	((dtroot_t *)(p))->header.stbl : \
	(s8 *)&(p)->slot[(p)->header.stblindex] )

/*
 * Flags for dtSearch
 */
#define JFS_CREATE 1
#define JFS_LOOKUP 2
#define JFS_REMOVE 3
#define JFS_RENAME 4

/*
 * Maximum file offset for directories.
 */
#define DIREND	INT_MAX

/*
 *	external declarations
 */
extern void dtInitRoot(tid_t tid, struct inode *ip, u32 idotdot);

extern int dtSearch(struct inode *ip, struct component_name * key,
		    ino_t * data, struct btstack * btstack, int flag);

extern int dtInsert(tid_t tid, struct inode *ip, struct component_name * key,
		    ino_t * ino, struct btstack * btstack);

extern int dtDelete(tid_t tid, struct inode *ip, struct component_name * key,
		    ino_t * data, int flag);

extern int dtModify(tid_t tid, struct inode *ip, struct component_name * key,
		    ino_t * orig_ino, ino_t new_ino, int flag);

extern int jfs_readdir(struct file *filp, void *dirent, filldir_t filldir);
#endif				/* !_H_JFS_DTREE */
