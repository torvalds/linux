/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
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
#ifndef	_H_JFS_LOGMGR
#define _H_JFS_LOGMGR

#include "jfs_filsys.h"
#include "jfs_lock.h"

/*
 *	log manager configuration parameters
 */

/* log page size */
#define	LOGPSIZE	4096
#define	L2LOGPSIZE	12

#define LOGPAGES	16	/* Log pages per mounted file system */

/*
 *	log logical volume
 *
 * a log is used to make the commit operation on journalled
 * files within the same logical volume group atomic.
 * a log is implemented with a logical volume.
 * there is one log per logical volume group.
 *
 * block 0 of the log logical volume is not used (ipl etc).
 * block 1 contains a log "superblock" and is used by logFormat(),
 * lmLogInit(), lmLogShutdown(), and logRedo() to record status
 * of the log but is not otherwise used during normal processing.
 * blocks 2 - (N-1) are used to contain log records.
 *
 * when a volume group is varied-on-line, logRedo() must have
 * been executed before the file systems (logical volumes) in
 * the volume group can be mounted.
 */
/*
 *	log superblock (block 1 of logical volume)
 */
#define	LOGSUPER_B	1
#define	LOGSTART_B	2

#define	LOGMAGIC	0x87654321
#define	LOGVERSION	1

#define MAX_ACTIVE	128	/* Max active file systems sharing log */

struct logsuper {
	__le32 magic;		/* 4: log lv identifier */
	__le32 version;		/* 4: version number */
	__le32 serial;		/* 4: log open/mount counter */
	__le32 size;		/* 4: size in number of LOGPSIZE blocks */
	__le32 bsize;		/* 4: logical block size in byte */
	__le32 l2bsize;		/* 4: log2 of bsize */

	__le32 flag;		/* 4: option */
	__le32 state;		/* 4: state - see below */

	__le32 end;		/* 4: addr of last log record set by logredo */
	char uuid[16];		/* 16: 128-bit journal uuid */
	char label[16];		/* 16: journal label */
	struct {
		char uuid[16];
	} active[MAX_ACTIVE];	/* 2048: active file systems list */
};

#define NULL_UUID "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"

/* log flag: commit option (see jfs_filsys.h) */

/* log state */
#define	LOGMOUNT	0	/* log mounted by lmLogInit() */
#define LOGREDONE	1	/* log shutdown by lmLogShutdown().
				 * log redo completed by logredo().
				 */
#define LOGWRAP		2	/* log wrapped */
#define LOGREADERR	3	/* log read error detected in logredo() */


/*
 *	log logical page
 *
 * (this comment should be rewritten !)
 * the header and trailer structures (h,t) will normally have
 * the same page and eor value.
 * An exception to this occurs when a complete page write is not
 * accomplished on a power failure. Since the hardware may "split write"
 * sectors in the page, any out of order sequence may occur during powerfail
 * and needs to be recognized during log replay.  The xor value is
 * an "exclusive or" of all log words in the page up to eor.  This
 * 32 bit eor is stored with the top 16 bits in the header and the
 * bottom 16 bits in the trailer.  logredo can easily recognize pages
 * that were not completed by reconstructing this eor and checking
 * the log page.
 *
 * Previous versions of the operating system did not allow split
 * writes and detected partially written records in logredo by
 * ordering the updates to the header, trailer, and the move of data
 * into the logdata area.  The order: (1) data is moved (2) header
 * is updated (3) trailer is updated.  In logredo, when the header
 * differed from the trailer, the header and trailer were reconciled
 * as follows: if h.page != t.page they were set to the smaller of
 * the two and h.eor and t.eor set to 8 (i.e. empty page). if (only)
 * h.eor != t.eor they were set to the smaller of their two values.
 */
struct logpage {
	struct {		/* header */
		__le32 page;	/* 4: log sequence page number */
		__le16 rsrvd;	/* 2: */
		__le16 eor;	/* 2: end-of-log offset of lasrt record write */
	} h;

	__le32 data[LOGPSIZE / 4 - 4];	/* log record area */

	struct {		/* trailer */
		__le32 page;	/* 4: normally the same as h.page */
		__le16 rsrvd;	/* 2: */
		__le16 eor;	/* 2: normally the same as h.eor */
	} t;
};

#define LOGPHDRSIZE	8	/* log page header size */
#define LOGPTLRSIZE	8	/* log page trailer size */


/*
 *	log record
 *
 * (this comment should be rewritten !)
 * jfs uses only "after" log records (only a single writer is allowed
 * in a page, pages are written to temporary paging space if
 * if they must be written to disk before commit, and i/o is
 * scheduled for modified pages to their home location after
 * the log records containing the after values and the commit
 * record is written to the log on disk, undo discards the copy
 * in main-memory.)
 *
 * a log record consists of a data area of variable length followed by
 * a descriptor of fixed size LOGRDSIZE bytes.
 * the data area is rounded up to an integral number of 4-bytes and
 * must be no longer than LOGPSIZE.
 * the descriptor is of size of multiple of 4-bytes and aligned on a
 * 4-byte boundary.
 * records are packed one after the other in the data area of log pages.
 * (sometimes a DUMMY record is inserted so that at least one record ends
 * on every page or the longest record is placed on at most two pages).
 * the field eor in page header/trailer points to the byte following
 * the last record on a page.
 */

/* log record types */
#define LOG_COMMIT		0x8000
#define LOG_SYNCPT		0x4000
#define LOG_MOUNT		0x2000
#define LOG_REDOPAGE		0x0800
#define LOG_NOREDOPAGE		0x0080
#define LOG_NOREDOINOEXT	0x0040
#define LOG_UPDATEMAP		0x0008
#define LOG_NOREDOFILE		0x0001

/* REDOPAGE/NOREDOPAGE log record data type */
#define	LOG_INODE		0x0001
#define	LOG_XTREE		0x0002
#define	LOG_DTREE		0x0004
#define	LOG_BTROOT		0x0010
#define	LOG_EA			0x0020
#define	LOG_ACL			0x0040
#define	LOG_DATA		0x0080
#define	LOG_NEW			0x0100
#define	LOG_EXTEND		0x0200
#define LOG_RELOCATE		0x0400
#define LOG_DIR_XTREE		0x0800	/* Xtree is in directory inode */

/* UPDATEMAP log record descriptor type */
#define	LOG_ALLOCXADLIST	0x0080
#define	LOG_ALLOCPXDLIST	0x0040
#define	LOG_ALLOCXAD		0x0020
#define	LOG_ALLOCPXD		0x0010
#define	LOG_FREEXADLIST		0x0008
#define	LOG_FREEPXDLIST		0x0004
#define	LOG_FREEXAD		0x0002
#define	LOG_FREEPXD		0x0001


struct lrd {
	/*
	 * type independent area
	 */
	__le32 logtid;		/* 4: log transaction identifier */
	__le32 backchain;	/* 4: ptr to prev record of same transaction */
	__le16 type;		/* 2: record type */
	__le16 length;		/* 2: length of data in record (in byte) */
	__le32 aggregate;	/* 4: file system lv/aggregate */
	/* (16) */

	/*
	 * type dependent area (20)
	 */
	union {

		/*
		 *	COMMIT: commit
		 *
		 * transaction commit: no type-dependent information;
		 */

		/*
		 *	REDOPAGE: after-image
		 *
		 * apply after-image;
		 *
		 * N.B. REDOPAGE, NOREDOPAGE, and UPDATEMAP must be same format;
		 */
		struct {
			__le32 fileset;	/* 4: fileset number */
			__le32 inode;	/* 4: inode number */
			__le16 type;	/* 2: REDOPAGE record type */
			__le16 l2linesize;	/* 2: log2 of line size */
			pxd_t pxd;	/* 8: on-disk page pxd */
		} redopage;	/* (20) */

		/*
		 *	NOREDOPAGE: the page is freed
		 *
		 * do not apply after-image records which precede this record
		 * in the log with the same page block number to this page.
		 *
		 * N.B. REDOPAGE, NOREDOPAGE, and UPDATEMAP must be same format;
		 */
		struct {
			__le32 fileset;	/* 4: fileset number */
			__le32 inode;	/* 4: inode number */
			__le16 type;	/* 2: NOREDOPAGE record type */
			__le16 rsrvd;	/* 2: reserved */
			pxd_t pxd;	/* 8: on-disk page pxd */
		} noredopage;	/* (20) */

		/*
		 *	UPDATEMAP: update block allocation map
		 *
		 * either in-line PXD,
		 * or     out-of-line  XADLIST;
		 *
		 * N.B. REDOPAGE, NOREDOPAGE, and UPDATEMAP must be same format;
		 */
		struct {
			__le32 fileset;	/* 4: fileset number */
			__le32 inode;	/* 4: inode number */
			__le16 type;	/* 2: UPDATEMAP record type */
			__le16 nxd;	/* 2: number of extents */
			pxd_t pxd;	/* 8: pxd */
		} updatemap;	/* (20) */

		/*
		 *	NOREDOINOEXT: the inode extent is freed
		 *
		 * do not apply after-image records which precede this
		 * record in the log with the any of the 4 page block
		 * numbers in this inode extent.
		 *
		 * NOTE: The fileset and pxd fields MUST remain in
		 *       the same fields in the REDOPAGE record format.
		 *
		 */
		struct {
			__le32 fileset;	/* 4: fileset number */
			__le32 iagnum;	/* 4: IAG number     */
			__le32 inoext_idx;	/* 4: inode extent index */
			pxd_t pxd;	/* 8: on-disk page pxd */
		} noredoinoext;	/* (20) */

		/*
		 *	SYNCPT: log sync point
		 *
		 * replay log up to syncpt address specified;
		 */
		struct {
			__le32 sync;	/* 4: syncpt address (0 = here) */
		} syncpt;

		/*
		 *	MOUNT: file system mount
		 *
		 * file system mount: no type-dependent information;
		 */

		/*
		 *	? FREEXTENT: free specified extent(s)
		 *
		 * free specified extent(s) from block allocation map
		 * N.B.: nextents should be length of data/sizeof(xad_t)
		 */
		struct {
			__le32 type;	/* 4: FREEXTENT record type */
			__le32 nextent;	/* 4: number of extents */

			/* data: PXD or XAD list */
		} freextent;

		/*
		 *	? NOREDOFILE: this file is freed
		 *
		 * do not apply records which precede this record in the log
		 * with the same inode number.
		 *
		 * NOREDOFILE must be the first to be written at commit
		 * (last to be read in logredo()) - it prevents
		 * replay of preceding updates of all preceding generations
		 * of the inumber esp. the on-disk inode itself.
		 */
		struct {
			__le32 fileset;	/* 4: fileset number */
			__le32 inode;	/* 4: inode number */
		} noredofile;

		/*
		 *	? NEWPAGE:
		 *
		 * metadata type dependent
		 */
		struct {
			__le32 fileset;	/* 4: fileset number */
			__le32 inode;	/* 4: inode number */
			__le32 type;	/* 4: NEWPAGE record type */
			pxd_t pxd;	/* 8: on-disk page pxd */
		} newpage;

		/*
		 *	? DUMMY: filler
		 *
		 * no type-dependent information
		 */
	} log;
};					/* (36) */

#define	LOGRDSIZE	(sizeof(struct lrd))

/*
 *	line vector descriptor
 */
struct lvd {
	__le16 offset;
	__le16 length;
};


/*
 *	log logical volume
 */
struct jfs_log {

	struct list_head sb_list;/*  This is used to sync metadata
				 *    before writing syncpt.
				 */
	struct list_head journal_list; /* Global list */
	struct block_device *bdev; /* 4: log lv pointer */
	int serial;		/* 4: log mount serial number */

	s64 base;		/* @8: log extent address (inline log ) */
	int size;		/* 4: log size in log page (in page) */
	int l2bsize;		/* 4: log2 of bsize */

	unsigned long flag;	/* 4: flag */

	struct lbuf *lbuf_free;	/* 4: free lbufs */
	wait_queue_head_t free_wait;	/* 4: */

	/* log write */
	int logtid;		/* 4: log tid */
	int page;		/* 4: page number of eol page */
	int eor;		/* 4: eor of last record in eol page */
	struct lbuf *bp;	/* 4: current log page buffer */

	struct mutex loglock;	/* 4: log write serialization lock */

	/* syncpt */
	int nextsync;		/* 4: bytes to write before next syncpt */
	int active;		/* 4: */
	wait_queue_head_t syncwait;	/* 4: */

	/* commit */
	uint cflag;		/* 4: */
	struct list_head cqueue; /* FIFO commit queue */
	struct tblock *flush_tblk; /* tblk we're waiting on for flush */
	int gcrtc;		/* 4: GC_READY transaction count */
	struct tblock *gclrt;	/* 4: latest GC_READY transaction */
	spinlock_t gclock;	/* 4: group commit lock */
	int logsize;		/* 4: log data area size in byte */
	int lsn;		/* 4: end-of-log */
	int clsn;		/* 4: clsn */
	int syncpt;		/* 4: addr of last syncpt record */
	int sync;		/* 4: addr from last logsync() */
	struct list_head synclist;	/* 8: logsynclist anchor */
	spinlock_t synclock;	/* 4: synclist lock */
	struct lbuf *wqueue;	/* 4: log pageout queue */
	int count;		/* 4: count */
	char uuid[16];		/* 16: 128-bit uuid of log device */

	int no_integrity;	/* 3: flag to disable journaling to disk */
};

/*
 * Log flag
 */
#define log_INLINELOG	1
#define log_SYNCBARRIER	2
#define log_QUIESCE	3
#define log_FLUSH	4

/*
 * group commit flag
 */
/* jfs_log */
#define logGC_PAGEOUT	0x00000001

/* tblock/lbuf */
#define tblkGC_QUEUE		0x0001
#define tblkGC_READY		0x0002
#define tblkGC_COMMIT		0x0004
#define tblkGC_COMMITTED	0x0008
#define tblkGC_EOP		0x0010
#define tblkGC_FREE		0x0020
#define tblkGC_LEADER		0x0040
#define tblkGC_ERROR		0x0080
#define tblkGC_LAZY		0x0100	// D230860
#define tblkGC_UNLOCKED		0x0200	// D230860

/*
 *		log cache buffer header
 */
struct lbuf {
	struct jfs_log *l_log;	/* 4: log associated with buffer */

	/*
	 * data buffer base area
	 */
	uint l_flag;		/* 4: pageout control flags */

	struct lbuf *l_wqnext;	/* 4: write queue link */
	struct lbuf *l_freelist;	/* 4: freelistlink */

	int l_pn;		/* 4: log page number */
	int l_eor;		/* 4: log record eor */
	int l_ceor;		/* 4: committed log record eor */

	s64 l_blkno;		/* 8: log page block number */
	caddr_t l_ldata;	/* 4: data page */
	struct page *l_page;	/* The page itself */
	uint l_offset;		/* Offset of l_ldata within the page */

	wait_queue_head_t l_ioevent;	/* 4: i/o done event */
};

/* Reuse l_freelist for redrive list */
#define l_redrive_next l_freelist

/*
 *	logsynclist block
 *
 * common logsyncblk prefix for jbuf_t and tblock
 */
struct logsyncblk {
	u16 xflag;		/* flags */
	u16 flag;		/* only meaninful in tblock */
	lid_t lid;		/* lock id */
	s32 lsn;		/* log sequence number */
	struct list_head synclist;	/* log sync list link */
};

/*
 *	logsynclist serialization (per log)
 */

#define LOGSYNC_LOCK_INIT(log) spin_lock_init(&(log)->synclock)
#define LOGSYNC_LOCK(log, flags) spin_lock_irqsave(&(log)->synclock, flags)
#define LOGSYNC_UNLOCK(log, flags) \
	spin_unlock_irqrestore(&(log)->synclock, flags)

/* compute the difference in bytes of lsn from sync point */
#define logdiff(diff, lsn, log)\
{\
	diff = (lsn) - (log)->syncpt;\
	if (diff < 0)\
		diff += (log)->logsize;\
}

extern int lmLogOpen(struct super_block *sb);
extern int lmLogClose(struct super_block *sb);
extern int lmLogShutdown(struct jfs_log * log);
extern int lmLogInit(struct jfs_log * log);
extern int lmLogFormat(struct jfs_log *log, s64 logAddress, int logSize);
extern int lmGroupCommit(struct jfs_log *, struct tblock *);
extern int jfsIOWait(void *);
extern void jfs_flush_journal(struct jfs_log * log, int wait);
extern void jfs_syncpt(struct jfs_log *log, int hard_sync);

#endif				/* _H_JFS_LOGMGR */
