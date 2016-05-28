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

/*
 *	jfs_logmgr.c: log manager
 *
 * for related information, see transaction manager (jfs_txnmgr.c), and
 * recovery manager (jfs_logredo.c).
 *
 * note: for detail, RTFS.
 *
 *	log buffer manager:
 * special purpose buffer manager supporting log i/o requirements.
 * per log serial pageout of logpage
 * queuing i/o requests and redrive i/o at iodone
 * maintain current logpage buffer
 * no caching since append only
 * appropriate jfs buffer cache buffers as needed
 *
 *	group commit:
 * transactions which wrote COMMIT records in the same in-memory
 * log page during the pageout of previous/current log page(s) are
 * committed together by the pageout of the page.
 *
 *	TBD lazy commit:
 * transactions are committed asynchronously when the log page
 * containing it COMMIT is paged out when it becomes full;
 *
 *	serialization:
 * . a per log lock serialize log write.
 * . a per log lock serialize group commit.
 * . a per log lock serialize log open/close;
 *
 *	TBD log integrity:
 * careful-write (ping-pong) of last logpage to recover from crash
 * in overwrite.
 * detection of split (out-of-order) write of physical sectors
 * of last logpage via timestamp at end of each sector
 * with its mirror data array at trailer).
 *
 *	alternatives:
 * lsn - 64-bit monotonically increasing integer vs
 * 32-bit lspn and page eor.
 */

#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/buffer_head.h>		/* for sync_blockdev() */
#include <linux/bio.h>
#include <linux/freezer.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_metapage.h"
#include "jfs_superblock.h"
#include "jfs_txnmgr.h"
#include "jfs_debug.h"


/*
 * lbuf's ready to be redriven.  Protected by log_redrive_lock (jfsIO thread)
 */
static struct lbuf *log_redrive_list;
static DEFINE_SPINLOCK(log_redrive_lock);


/*
 *	log read/write serialization (per log)
 */
#define LOG_LOCK_INIT(log)	mutex_init(&(log)->loglock)
#define LOG_LOCK(log)		mutex_lock(&((log)->loglock))
#define LOG_UNLOCK(log)		mutex_unlock(&((log)->loglock))


/*
 *	log group commit serialization (per log)
 */

#define LOGGC_LOCK_INIT(log)	spin_lock_init(&(log)->gclock)
#define LOGGC_LOCK(log)		spin_lock_irq(&(log)->gclock)
#define LOGGC_UNLOCK(log)	spin_unlock_irq(&(log)->gclock)
#define LOGGC_WAKEUP(tblk)	wake_up_all(&(tblk)->gcwait)

/*
 *	log sync serialization (per log)
 */
#define	LOGSYNC_DELTA(logsize)		min((logsize)/8, 128*LOGPSIZE)
#define	LOGSYNC_BARRIER(logsize)	((logsize)/4)
/*
#define	LOGSYNC_DELTA(logsize)		min((logsize)/4, 256*LOGPSIZE)
#define	LOGSYNC_BARRIER(logsize)	((logsize)/2)
*/


/*
 *	log buffer cache synchronization
 */
static DEFINE_SPINLOCK(jfsLCacheLock);

#define	LCACHE_LOCK(flags)	spin_lock_irqsave(&jfsLCacheLock, flags)
#define	LCACHE_UNLOCK(flags)	spin_unlock_irqrestore(&jfsLCacheLock, flags)

/*
 * See __SLEEP_COND in jfs_locks.h
 */
#define LCACHE_SLEEP_COND(wq, cond, flags)	\
do {						\
	if (cond)				\
		break;				\
	__SLEEP_COND(wq, cond, LCACHE_LOCK(flags), LCACHE_UNLOCK(flags)); \
} while (0)

#define	LCACHE_WAKEUP(event)	wake_up(event)


/*
 *	lbuf buffer cache (lCache) control
 */
/* log buffer manager pageout control (cumulative, inclusive) */
#define	lbmREAD		0x0001
#define	lbmWRITE	0x0002	/* enqueue at tail of write queue;
				 * init pageout if at head of queue;
				 */
#define	lbmRELEASE	0x0004	/* remove from write queue
				 * at completion of pageout;
				 * do not free/recycle it yet:
				 * caller will free it;
				 */
#define	lbmSYNC		0x0008	/* do not return to freelist
				 * when removed from write queue;
				 */
#define lbmFREE		0x0010	/* return to freelist
				 * at completion of pageout;
				 * the buffer may be recycled;
				 */
#define	lbmDONE		0x0020
#define	lbmERROR	0x0040
#define lbmGC		0x0080	/* lbmIODone to perform post-GC processing
				 * of log page
				 */
#define lbmDIRECT	0x0100

/*
 * Global list of active external journals
 */
static LIST_HEAD(jfs_external_logs);
static struct jfs_log *dummy_log;
static DEFINE_MUTEX(jfs_log_mutex);

/*
 * forward references
 */
static int lmWriteRecord(struct jfs_log * log, struct tblock * tblk,
			 struct lrd * lrd, struct tlock * tlck);

static int lmNextPage(struct jfs_log * log);
static int lmLogFileSystem(struct jfs_log * log, struct jfs_sb_info *sbi,
			   int activate);

static int open_inline_log(struct super_block *sb);
static int open_dummy_log(struct super_block *sb);
static int lbmLogInit(struct jfs_log * log);
static void lbmLogShutdown(struct jfs_log * log);
static struct lbuf *lbmAllocate(struct jfs_log * log, int);
static void lbmFree(struct lbuf * bp);
static void lbmfree(struct lbuf * bp);
static int lbmRead(struct jfs_log * log, int pn, struct lbuf ** bpp);
static void lbmWrite(struct jfs_log * log, struct lbuf * bp, int flag, int cant_block);
static void lbmDirectWrite(struct jfs_log * log, struct lbuf * bp, int flag);
static int lbmIOWait(struct lbuf * bp, int flag);
static bio_end_io_t lbmIODone;
static void lbmStartIO(struct lbuf * bp);
static void lmGCwrite(struct jfs_log * log, int cant_block);
static int lmLogSync(struct jfs_log * log, int hard_sync);



/*
 *	statistics
 */
#ifdef CONFIG_JFS_STATISTICS
static struct lmStat {
	uint commit;		/* # of commit */
	uint pagedone;		/* # of page written */
	uint submitted;		/* # of pages submitted */
	uint full_page;		/* # of full pages submitted */
	uint partial_page;	/* # of partial pages submitted */
} lmStat;
#endif

static void write_special_inodes(struct jfs_log *log,
				 int (*writer)(struct address_space *))
{
	struct jfs_sb_info *sbi;

	list_for_each_entry(sbi, &log->sb_list, log_list) {
		writer(sbi->ipbmap->i_mapping);
		writer(sbi->ipimap->i_mapping);
		writer(sbi->direct_inode->i_mapping);
	}
}

/*
 * NAME:	lmLog()
 *
 * FUNCTION:	write a log record;
 *
 * PARAMETER:
 *
 * RETURN:	lsn - offset to the next log record to write (end-of-log);
 *		-1  - error;
 *
 * note: todo: log error handler
 */
int lmLog(struct jfs_log * log, struct tblock * tblk, struct lrd * lrd,
	  struct tlock * tlck)
{
	int lsn;
	int diffp, difft;
	struct metapage *mp = NULL;
	unsigned long flags;

	jfs_info("lmLog: log:0x%p tblk:0x%p, lrd:0x%p tlck:0x%p",
		 log, tblk, lrd, tlck);

	LOG_LOCK(log);

	/* log by (out-of-transaction) JFS ? */
	if (tblk == NULL)
		goto writeRecord;

	/* log from page ? */
	if (tlck == NULL ||
	    tlck->type & tlckBTROOT || (mp = tlck->mp) == NULL)
		goto writeRecord;

	/*
	 *	initialize/update page/transaction recovery lsn
	 */
	lsn = log->lsn;

	LOGSYNC_LOCK(log, flags);

	/*
	 * initialize page lsn if first log write of the page
	 */
	if (mp->lsn == 0) {
		mp->log = log;
		mp->lsn = lsn;
		log->count++;

		/* insert page at tail of logsynclist */
		list_add_tail(&mp->synclist, &log->synclist);
	}

	/*
	 *	initialize/update lsn of tblock of the page
	 *
	 * transaction inherits oldest lsn of pages associated
	 * with allocation/deallocation of resources (their
	 * log records are used to reconstruct allocation map
	 * at recovery time: inode for inode allocation map,
	 * B+-tree index of extent descriptors for block
	 * allocation map);
	 * allocation map pages inherit transaction lsn at
	 * commit time to allow forwarding log syncpt past log
	 * records associated with allocation/deallocation of
	 * resources only after persistent map of these map pages
	 * have been updated and propagated to home.
	 */
	/*
	 * initialize transaction lsn:
	 */
	if (tblk->lsn == 0) {
		/* inherit lsn of its first page logged */
		tblk->lsn = mp->lsn;
		log->count++;

		/* insert tblock after the page on logsynclist */
		list_add(&tblk->synclist, &mp->synclist);
	}
	/*
	 * update transaction lsn:
	 */
	else {
		/* inherit oldest/smallest lsn of page */
		logdiff(diffp, mp->lsn, log);
		logdiff(difft, tblk->lsn, log);
		if (diffp < difft) {
			/* update tblock lsn with page lsn */
			tblk->lsn = mp->lsn;

			/* move tblock after page on logsynclist */
			list_move(&tblk->synclist, &mp->synclist);
		}
	}

	LOGSYNC_UNLOCK(log, flags);

	/*
	 *	write the log record
	 */
      writeRecord:
	lsn = lmWriteRecord(log, tblk, lrd, tlck);

	/*
	 * forward log syncpt if log reached next syncpt trigger
	 */
	logdiff(diffp, lsn, log);
	if (diffp >= log->nextsync)
		lsn = lmLogSync(log, 0);

	/* update end-of-log lsn */
	log->lsn = lsn;

	LOG_UNLOCK(log);

	/* return end-of-log address */
	return lsn;
}

/*
 * NAME:	lmWriteRecord()
 *
 * FUNCTION:	move the log record to current log page
 *
 * PARAMETER:	cd	- commit descriptor
 *
 * RETURN:	end-of-log address
 *
 * serialization: LOG_LOCK() held on entry/exit
 */
static int
lmWriteRecord(struct jfs_log * log, struct tblock * tblk, struct lrd * lrd,
	      struct tlock * tlck)
{
	int lsn = 0;		/* end-of-log address */
	struct lbuf *bp;	/* dst log page buffer */
	struct logpage *lp;	/* dst log page */
	caddr_t dst;		/* destination address in log page */
	int dstoffset;		/* end-of-log offset in log page */
	int freespace;		/* free space in log page */
	caddr_t p;		/* src meta-data page */
	caddr_t src;
	int srclen;
	int nbytes;		/* number of bytes to move */
	int i;
	int len;
	struct linelock *linelock;
	struct lv *lv;
	struct lvd *lvd;
	int l2linesize;

	len = 0;

	/* retrieve destination log page to write */
	bp = (struct lbuf *) log->bp;
	lp = (struct logpage *) bp->l_ldata;
	dstoffset = log->eor;

	/* any log data to write ? */
	if (tlck == NULL)
		goto moveLrd;

	/*
	 *	move log record data
	 */
	/* retrieve source meta-data page to log */
	if (tlck->flag & tlckPAGELOCK) {
		p = (caddr_t) (tlck->mp->data);
		linelock = (struct linelock *) & tlck->lock;
	}
	/* retrieve source in-memory inode to log */
	else if (tlck->flag & tlckINODELOCK) {
		if (tlck->type & tlckDTREE)
			p = (caddr_t) &JFS_IP(tlck->ip)->i_dtroot;
		else
			p = (caddr_t) &JFS_IP(tlck->ip)->i_xtroot;
		linelock = (struct linelock *) & tlck->lock;
	}
#ifdef	_JFS_WIP
	else if (tlck->flag & tlckINLINELOCK) {

		inlinelock = (struct inlinelock *) & tlck;
		p = (caddr_t) & inlinelock->pxd;
		linelock = (struct linelock *) & tlck;
	}
#endif				/* _JFS_WIP */
	else {
		jfs_err("lmWriteRecord: UFO tlck:0x%p", tlck);
		return 0;	/* Probably should trap */
	}
	l2linesize = linelock->l2linesize;

      moveData:
	ASSERT(linelock->index <= linelock->maxcnt);

	lv = linelock->lv;
	for (i = 0; i < linelock->index; i++, lv++) {
		if (lv->length == 0)
			continue;

		/* is page full ? */
		if (dstoffset >= LOGPSIZE - LOGPTLRSIZE) {
			/* page become full: move on to next page */
			lmNextPage(log);

			bp = log->bp;
			lp = (struct logpage *) bp->l_ldata;
			dstoffset = LOGPHDRSIZE;
		}

		/*
		 * move log vector data
		 */
		src = (u8 *) p + (lv->offset << l2linesize);
		srclen = lv->length << l2linesize;
		len += srclen;
		while (srclen > 0) {
			freespace = (LOGPSIZE - LOGPTLRSIZE) - dstoffset;
			nbytes = min(freespace, srclen);
			dst = (caddr_t) lp + dstoffset;
			memcpy(dst, src, nbytes);
			dstoffset += nbytes;

			/* is page not full ? */
			if (dstoffset < LOGPSIZE - LOGPTLRSIZE)
				break;

			/* page become full: move on to next page */
			lmNextPage(log);

			bp = (struct lbuf *) log->bp;
			lp = (struct logpage *) bp->l_ldata;
			dstoffset = LOGPHDRSIZE;

			srclen -= nbytes;
			src += nbytes;
		}

		/*
		 * move log vector descriptor
		 */
		len += 4;
		lvd = (struct lvd *) ((caddr_t) lp + dstoffset);
		lvd->offset = cpu_to_le16(lv->offset);
		lvd->length = cpu_to_le16(lv->length);
		dstoffset += 4;
		jfs_info("lmWriteRecord: lv offset:%d length:%d",
			 lv->offset, lv->length);
	}

	if ((i = linelock->next)) {
		linelock = (struct linelock *) lid_to_tlock(i);
		goto moveData;
	}

	/*
	 *	move log record descriptor
	 */
      moveLrd:
	lrd->length = cpu_to_le16(len);

	src = (caddr_t) lrd;
	srclen = LOGRDSIZE;

	while (srclen > 0) {
		freespace = (LOGPSIZE - LOGPTLRSIZE) - dstoffset;
		nbytes = min(freespace, srclen);
		dst = (caddr_t) lp + dstoffset;
		memcpy(dst, src, nbytes);

		dstoffset += nbytes;
		srclen -= nbytes;

		/* are there more to move than freespace of page ? */
		if (srclen)
			goto pageFull;

		/*
		 * end of log record descriptor
		 */

		/* update last log record eor */
		log->eor = dstoffset;
		bp->l_eor = dstoffset;
		lsn = (log->page << L2LOGPSIZE) + dstoffset;

		if (lrd->type & cpu_to_le16(LOG_COMMIT)) {
			tblk->clsn = lsn;
			jfs_info("wr: tclsn:0x%x, beor:0x%x", tblk->clsn,
				 bp->l_eor);

			INCREMENT(lmStat.commit);	/* # of commit */

			/*
			 * enqueue tblock for group commit:
			 *
			 * enqueue tblock of non-trivial/synchronous COMMIT
			 * at tail of group commit queue
			 * (trivial/asynchronous COMMITs are ignored by
			 * group commit.)
			 */
			LOGGC_LOCK(log);

			/* init tblock gc state */
			tblk->flag = tblkGC_QUEUE;
			tblk->bp = log->bp;
			tblk->pn = log->page;
			tblk->eor = log->eor;

			/* enqueue transaction to commit queue */
			list_add_tail(&tblk->cqueue, &log->cqueue);

			LOGGC_UNLOCK(log);
		}

		jfs_info("lmWriteRecord: lrd:0x%04x bp:0x%p pn:%d eor:0x%x",
			le16_to_cpu(lrd->type), log->bp, log->page, dstoffset);

		/* page not full ? */
		if (dstoffset < LOGPSIZE - LOGPTLRSIZE)
			return lsn;

	      pageFull:
		/* page become full: move on to next page */
		lmNextPage(log);

		bp = (struct lbuf *) log->bp;
		lp = (struct logpage *) bp->l_ldata;
		dstoffset = LOGPHDRSIZE;
		src += nbytes;
	}

	return lsn;
}


/*
 * NAME:	lmNextPage()
 *
 * FUNCTION:	write current page and allocate next page.
 *
 * PARAMETER:	log
 *
 * RETURN:	0
 *
 * serialization: LOG_LOCK() held on entry/exit
 */
static int lmNextPage(struct jfs_log * log)
{
	struct logpage *lp;
	int lspn;		/* log sequence page number */
	int pn;			/* current page number */
	struct lbuf *bp;
	struct lbuf *nextbp;
	struct tblock *tblk;

	/* get current log page number and log sequence page number */
	pn = log->page;
	bp = log->bp;
	lp = (struct logpage *) bp->l_ldata;
	lspn = le32_to_cpu(lp->h.page);

	LOGGC_LOCK(log);

	/*
	 *	write or queue the full page at the tail of write queue
	 */
	/* get the tail tblk on commit queue */
	if (list_empty(&log->cqueue))
		tblk = NULL;
	else
		tblk = list_entry(log->cqueue.prev, struct tblock, cqueue);

	/* every tblk who has COMMIT record on the current page,
	 * and has not been committed, must be on commit queue
	 * since tblk is queued at commit queueu at the time
	 * of writing its COMMIT record on the page before
	 * page becomes full (even though the tblk thread
	 * who wrote COMMIT record may have been suspended
	 * currently);
	 */

	/* is page bound with outstanding tail tblk ? */
	if (tblk && tblk->pn == pn) {
		/* mark tblk for end-of-page */
		tblk->flag |= tblkGC_EOP;

		if (log->cflag & logGC_PAGEOUT) {
			/* if page is not already on write queue,
			 * just enqueue (no lbmWRITE to prevent redrive)
			 * buffer to wqueue to ensure correct serial order
			 * of the pages since log pages will be added
			 * continuously
			 */
			if (bp->l_wqnext == NULL)
				lbmWrite(log, bp, 0, 0);
		} else {
			/*
			 * No current GC leader, initiate group commit
			 */
			log->cflag |= logGC_PAGEOUT;
			lmGCwrite(log, 0);
		}
	}
	/* page is not bound with outstanding tblk:
	 * init write or mark it to be redriven (lbmWRITE)
	 */
	else {
		/* finalize the page */
		bp->l_ceor = bp->l_eor;
		lp->h.eor = lp->t.eor = cpu_to_le16(bp->l_ceor);
		lbmWrite(log, bp, lbmWRITE | lbmRELEASE | lbmFREE, 0);
	}
	LOGGC_UNLOCK(log);

	/*
	 *	allocate/initialize next page
	 */
	/* if log wraps, the first data page of log is 2
	 * (0 never used, 1 is superblock).
	 */
	log->page = (pn == log->size - 1) ? 2 : pn + 1;
	log->eor = LOGPHDRSIZE;	/* ? valid page empty/full at logRedo() */

	/* allocate/initialize next log page buffer */
	nextbp = lbmAllocate(log, log->page);
	nextbp->l_eor = log->eor;
	log->bp = nextbp;

	/* initialize next log page */
	lp = (struct logpage *) nextbp->l_ldata;
	lp->h.page = lp->t.page = cpu_to_le32(lspn + 1);
	lp->h.eor = lp->t.eor = cpu_to_le16(LOGPHDRSIZE);

	return 0;
}


/*
 * NAME:	lmGroupCommit()
 *
 * FUNCTION:	group commit
 *	initiate pageout of the pages with COMMIT in the order of
 *	page number - redrive pageout of the page at the head of
 *	pageout queue until full page has been written.
 *
 * RETURN:
 *
 * NOTE:
 *	LOGGC_LOCK serializes log group commit queue, and
 *	transaction blocks on the commit queue.
 *	N.B. LOG_LOCK is NOT held during lmGroupCommit().
 */
int lmGroupCommit(struct jfs_log * log, struct tblock * tblk)
{
	int rc = 0;

	LOGGC_LOCK(log);

	/* group committed already ? */
	if (tblk->flag & tblkGC_COMMITTED) {
		if (tblk->flag & tblkGC_ERROR)
			rc = -EIO;

		LOGGC_UNLOCK(log);
		return rc;
	}
	jfs_info("lmGroup Commit: tblk = 0x%p, gcrtc = %d", tblk, log->gcrtc);

	if (tblk->xflag & COMMIT_LAZY)
		tblk->flag |= tblkGC_LAZY;

	if ((!(log->cflag & logGC_PAGEOUT)) && (!list_empty(&log->cqueue)) &&
	    (!(tblk->xflag & COMMIT_LAZY) || test_bit(log_FLUSH, &log->flag)
	     || jfs_tlocks_low)) {
		/*
		 * No pageout in progress
		 *
		 * start group commit as its group leader.
		 */
		log->cflag |= logGC_PAGEOUT;

		lmGCwrite(log, 0);
	}

	if (tblk->xflag & COMMIT_LAZY) {
		/*
		 * Lazy transactions can leave now
		 */
		LOGGC_UNLOCK(log);
		return 0;
	}

	/* lmGCwrite gives up LOGGC_LOCK, check again */

	if (tblk->flag & tblkGC_COMMITTED) {
		if (tblk->flag & tblkGC_ERROR)
			rc = -EIO;

		LOGGC_UNLOCK(log);
		return rc;
	}

	/* upcount transaction waiting for completion
	 */
	log->gcrtc++;
	tblk->flag |= tblkGC_READY;

	__SLEEP_COND(tblk->gcwait, (tblk->flag & tblkGC_COMMITTED),
		     LOGGC_LOCK(log), LOGGC_UNLOCK(log));

	/* removed from commit queue */
	if (tblk->flag & tblkGC_ERROR)
		rc = -EIO;

	LOGGC_UNLOCK(log);
	return rc;
}

/*
 * NAME:	lmGCwrite()
 *
 * FUNCTION:	group commit write
 *	initiate write of log page, building a group of all transactions
 *	with commit records on that page.
 *
 * RETURN:	None
 *
 * NOTE:
 *	LOGGC_LOCK must be held by caller.
 *	N.B. LOG_LOCK is NOT held during lmGroupCommit().
 */
static void lmGCwrite(struct jfs_log * log, int cant_write)
{
	struct lbuf *bp;
	struct logpage *lp;
	int gcpn;		/* group commit page number */
	struct tblock *tblk;
	struct tblock *xtblk = NULL;

	/*
	 * build the commit group of a log page
	 *
	 * scan commit queue and make a commit group of all
	 * transactions with COMMIT records on the same log page.
	 */
	/* get the head tblk on the commit queue */
	gcpn = list_entry(log->cqueue.next, struct tblock, cqueue)->pn;

	list_for_each_entry(tblk, &log->cqueue, cqueue) {
		if (tblk->pn != gcpn)
			break;

		xtblk = tblk;

		/* state transition: (QUEUE, READY) -> COMMIT */
		tblk->flag |= tblkGC_COMMIT;
	}
	tblk = xtblk;		/* last tblk of the page */

	/*
	 * pageout to commit transactions on the log page.
	 */
	bp = (struct lbuf *) tblk->bp;
	lp = (struct logpage *) bp->l_ldata;
	/* is page already full ? */
	if (tblk->flag & tblkGC_EOP) {
		/* mark page to free at end of group commit of the page */
		tblk->flag &= ~tblkGC_EOP;
		tblk->flag |= tblkGC_FREE;
		bp->l_ceor = bp->l_eor;
		lp->h.eor = lp->t.eor = cpu_to_le16(bp->l_ceor);
		lbmWrite(log, bp, lbmWRITE | lbmRELEASE | lbmGC,
			 cant_write);
		INCREMENT(lmStat.full_page);
	}
	/* page is not yet full */
	else {
		bp->l_ceor = tblk->eor;	/* ? bp->l_ceor = bp->l_eor; */
		lp->h.eor = lp->t.eor = cpu_to_le16(bp->l_ceor);
		lbmWrite(log, bp, lbmWRITE | lbmGC, cant_write);
		INCREMENT(lmStat.partial_page);
	}
}

/*
 * NAME:	lmPostGC()
 *
 * FUNCTION:	group commit post-processing
 *	Processes transactions after their commit records have been written
 *	to disk, redriving log I/O if necessary.
 *
 * RETURN:	None
 *
 * NOTE:
 *	This routine is called a interrupt time by lbmIODone
 */
static void lmPostGC(struct lbuf * bp)
{
	unsigned long flags;
	struct jfs_log *log = bp->l_log;
	struct logpage *lp;
	struct tblock *tblk, *temp;

	//LOGGC_LOCK(log);
	spin_lock_irqsave(&log->gclock, flags);
	/*
	 * current pageout of group commit completed.
	 *
	 * remove/wakeup transactions from commit queue who were
	 * group committed with the current log page
	 */
	list_for_each_entry_safe(tblk, temp, &log->cqueue, cqueue) {
		if (!(tblk->flag & tblkGC_COMMIT))
			break;
		/* if transaction was marked GC_COMMIT then
		 * it has been shipped in the current pageout
		 * and made it to disk - it is committed.
		 */

		if (bp->l_flag & lbmERROR)
			tblk->flag |= tblkGC_ERROR;

		/* remove it from the commit queue */
		list_del(&tblk->cqueue);
		tblk->flag &= ~tblkGC_QUEUE;

		if (tblk == log->flush_tblk) {
			/* we can stop flushing the log now */
			clear_bit(log_FLUSH, &log->flag);
			log->flush_tblk = NULL;
		}

		jfs_info("lmPostGC: tblk = 0x%p, flag = 0x%x", tblk,
			 tblk->flag);

		if (!(tblk->xflag & COMMIT_FORCE))
			/*
			 * Hand tblk over to lazy commit thread
			 */
			txLazyUnlock(tblk);
		else {
			/* state transition: COMMIT -> COMMITTED */
			tblk->flag |= tblkGC_COMMITTED;

			if (tblk->flag & tblkGC_READY)
				log->gcrtc--;

			LOGGC_WAKEUP(tblk);
		}

		/* was page full before pageout ?
		 * (and this is the last tblk bound with the page)
		 */
		if (tblk->flag & tblkGC_FREE)
			lbmFree(bp);
		/* did page become full after pageout ?
		 * (and this is the last tblk bound with the page)
		 */
		else if (tblk->flag & tblkGC_EOP) {
			/* finalize the page */
			lp = (struct logpage *) bp->l_ldata;
			bp->l_ceor = bp->l_eor;
			lp->h.eor = lp->t.eor = cpu_to_le16(bp->l_eor);
			jfs_info("lmPostGC: calling lbmWrite");
			lbmWrite(log, bp, lbmWRITE | lbmRELEASE | lbmFREE,
				 1);
		}

	}

	/* are there any transactions who have entered lnGroupCommit()
	 * (whose COMMITs are after that of the last log page written.
	 * They are waiting for new group commit (above at (SLEEP 1))
	 * or lazy transactions are on a full (queued) log page,
	 * select the latest ready transaction as new group leader and
	 * wake her up to lead her group.
	 */
	if ((!list_empty(&log->cqueue)) &&
	    ((log->gcrtc > 0) || (tblk->bp->l_wqnext != NULL) ||
	     test_bit(log_FLUSH, &log->flag) || jfs_tlocks_low))
		/*
		 * Call lmGCwrite with new group leader
		 */
		lmGCwrite(log, 1);

	/* no transaction are ready yet (transactions are only just
	 * queued (GC_QUEUE) and not entered for group commit yet).
	 * the first transaction entering group commit
	 * will elect herself as new group leader.
	 */
	else
		log->cflag &= ~logGC_PAGEOUT;

	//LOGGC_UNLOCK(log);
	spin_unlock_irqrestore(&log->gclock, flags);
	return;
}

/*
 * NAME:	lmLogSync()
 *
 * FUNCTION:	write log SYNCPT record for specified log
 *	if new sync address is available
 *	(normally the case if sync() is executed by back-ground
 *	process).
 *	calculate new value of i_nextsync which determines when
 *	this code is called again.
 *
 * PARAMETERS:	log	- log structure
 *		hard_sync - 1 to force all metadata to be written
 *
 * RETURN:	0
 *
 * serialization: LOG_LOCK() held on entry/exit
 */
static int lmLogSync(struct jfs_log * log, int hard_sync)
{
	int logsize;
	int written;		/* written since last syncpt */
	int free;		/* free space left available */
	int delta;		/* additional delta to write normally */
	int more;		/* additional write granted */
	struct lrd lrd;
	int lsn;
	struct logsyncblk *lp;
	unsigned long flags;

	/* push dirty metapages out to disk */
	if (hard_sync)
		write_special_inodes(log, filemap_fdatawrite);
	else
		write_special_inodes(log, filemap_flush);

	/*
	 *	forward syncpt
	 */
	/* if last sync is same as last syncpt,
	 * invoke sync point forward processing to update sync.
	 */

	if (log->sync == log->syncpt) {
		LOGSYNC_LOCK(log, flags);
		if (list_empty(&log->synclist))
			log->sync = log->lsn;
		else {
			lp = list_entry(log->synclist.next,
					struct logsyncblk, synclist);
			log->sync = lp->lsn;
		}
		LOGSYNC_UNLOCK(log, flags);

	}

	/* if sync is different from last syncpt,
	 * write a SYNCPT record with syncpt = sync.
	 * reset syncpt = sync
	 */
	if (log->sync != log->syncpt) {
		lrd.logtid = 0;
		lrd.backchain = 0;
		lrd.type = cpu_to_le16(LOG_SYNCPT);
		lrd.length = 0;
		lrd.log.syncpt.sync = cpu_to_le32(log->sync);
		lsn = lmWriteRecord(log, NULL, &lrd, NULL);

		log->syncpt = log->sync;
	} else
		lsn = log->lsn;

	/*
	 *	setup next syncpt trigger (SWAG)
	 */
	logsize = log->logsize;

	logdiff(written, lsn, log);
	free = logsize - written;
	delta = LOGSYNC_DELTA(logsize);
	more = min(free / 2, delta);
	if (more < 2 * LOGPSIZE) {
		jfs_warn("\n ... Log Wrap ... Log Wrap ... Log Wrap ...\n");
		/*
		 *	log wrapping
		 *
		 * option 1 - panic ? No.!
		 * option 2 - shutdown file systems
		 *	      associated with log ?
		 * option 3 - extend log ?
		 * option 4 - second chance
		 *
		 * mark log wrapped, and continue.
		 * when all active transactions are completed,
		 * mark log valid for recovery.
		 * if crashed during invalid state, log state
		 * implies invalid log, forcing fsck().
		 */
		/* mark log state log wrap in log superblock */
		/* log->state = LOGWRAP; */

		/* reset sync point computation */
		log->syncpt = log->sync = lsn;
		log->nextsync = delta;
	} else
		/* next syncpt trigger = written + more */
		log->nextsync = written + more;

	/* if number of bytes written from last sync point is more
	 * than 1/4 of the log size, stop new transactions from
	 * starting until all current transactions are completed
	 * by setting syncbarrier flag.
	 */
	if (!test_bit(log_SYNCBARRIER, &log->flag) &&
	    (written > LOGSYNC_BARRIER(logsize)) && log->active) {
		set_bit(log_SYNCBARRIER, &log->flag);
		jfs_info("log barrier on: lsn=0x%x syncpt=0x%x", lsn,
			 log->syncpt);
		/*
		 * We may have to initiate group commit
		 */
		jfs_flush_journal(log, 0);
	}

	return lsn;
}

/*
 * NAME:	jfs_syncpt
 *
 * FUNCTION:	write log SYNCPT record for specified log
 *
 * PARAMETERS:	log	  - log structure
 *		hard_sync - set to 1 to force metadata to be written
 */
void jfs_syncpt(struct jfs_log *log, int hard_sync)
{	LOG_LOCK(log);
	if (!test_bit(log_QUIESCE, &log->flag))
		lmLogSync(log, hard_sync);
	LOG_UNLOCK(log);
}

/*
 * NAME:	lmLogOpen()
 *
 * FUNCTION:	open the log on first open;
 *	insert filesystem in the active list of the log.
 *
 * PARAMETER:	ipmnt	- file system mount inode
 *		iplog	- log inode (out)
 *
 * RETURN:
 *
 * serialization:
 */
int lmLogOpen(struct super_block *sb)
{
	int rc;
	struct block_device *bdev;
	struct jfs_log *log;
	struct jfs_sb_info *sbi = JFS_SBI(sb);

	if (sbi->flag & JFS_NOINTEGRITY)
		return open_dummy_log(sb);

	if (sbi->mntflag & JFS_INLINELOG)
		return open_inline_log(sb);

	mutex_lock(&jfs_log_mutex);
	list_for_each_entry(log, &jfs_external_logs, journal_list) {
		if (log->bdev->bd_dev == sbi->logdev) {
			if (memcmp(log->uuid, sbi->loguuid,
				   sizeof(log->uuid))) {
				jfs_warn("wrong uuid on JFS journal");
				mutex_unlock(&jfs_log_mutex);
				return -EINVAL;
			}
			/*
			 * add file system to log active file system list
			 */
			if ((rc = lmLogFileSystem(log, sbi, 1))) {
				mutex_unlock(&jfs_log_mutex);
				return rc;
			}
			goto journal_found;
		}
	}

	if (!(log = kzalloc(sizeof(struct jfs_log), GFP_KERNEL))) {
		mutex_unlock(&jfs_log_mutex);
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&log->sb_list);
	init_waitqueue_head(&log->syncwait);

	/*
	 *	external log as separate logical volume
	 *
	 * file systems to log may have n-to-1 relationship;
	 */

	bdev = blkdev_get_by_dev(sbi->logdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL,
				 log);
	if (IS_ERR(bdev)) {
		rc = PTR_ERR(bdev);
		goto free;
	}

	log->bdev = bdev;
	memcpy(log->uuid, sbi->loguuid, sizeof(log->uuid));

	/*
	 * initialize log:
	 */
	if ((rc = lmLogInit(log)))
		goto close;

	list_add(&log->journal_list, &jfs_external_logs);

	/*
	 * add file system to log active file system list
	 */
	if ((rc = lmLogFileSystem(log, sbi, 1)))
		goto shutdown;

journal_found:
	LOG_LOCK(log);
	list_add(&sbi->log_list, &log->sb_list);
	sbi->log = log;
	LOG_UNLOCK(log);

	mutex_unlock(&jfs_log_mutex);
	return 0;

	/*
	 *	unwind on error
	 */
      shutdown:		/* unwind lbmLogInit() */
	list_del(&log->journal_list);
	lbmLogShutdown(log);

      close:		/* close external log device */
	blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);

      free:		/* free log descriptor */
	mutex_unlock(&jfs_log_mutex);
	kfree(log);

	jfs_warn("lmLogOpen: exit(%d)", rc);
	return rc;
}

static int open_inline_log(struct super_block *sb)
{
	struct jfs_log *log;
	int rc;

	if (!(log = kzalloc(sizeof(struct jfs_log), GFP_KERNEL)))
		return -ENOMEM;
	INIT_LIST_HEAD(&log->sb_list);
	init_waitqueue_head(&log->syncwait);

	set_bit(log_INLINELOG, &log->flag);
	log->bdev = sb->s_bdev;
	log->base = addressPXD(&JFS_SBI(sb)->logpxd);
	log->size = lengthPXD(&JFS_SBI(sb)->logpxd) >>
	    (L2LOGPSIZE - sb->s_blocksize_bits);
	log->l2bsize = sb->s_blocksize_bits;
	ASSERT(L2LOGPSIZE >= sb->s_blocksize_bits);

	/*
	 * initialize log.
	 */
	if ((rc = lmLogInit(log))) {
		kfree(log);
		jfs_warn("lmLogOpen: exit(%d)", rc);
		return rc;
	}

	list_add(&JFS_SBI(sb)->log_list, &log->sb_list);
	JFS_SBI(sb)->log = log;

	return rc;
}

static int open_dummy_log(struct super_block *sb)
{
	int rc;

	mutex_lock(&jfs_log_mutex);
	if (!dummy_log) {
		dummy_log = kzalloc(sizeof(struct jfs_log), GFP_KERNEL);
		if (!dummy_log) {
			mutex_unlock(&jfs_log_mutex);
			return -ENOMEM;
		}
		INIT_LIST_HEAD(&dummy_log->sb_list);
		init_waitqueue_head(&dummy_log->syncwait);
		dummy_log->no_integrity = 1;
		/* Make up some stuff */
		dummy_log->base = 0;
		dummy_log->size = 1024;
		rc = lmLogInit(dummy_log);
		if (rc) {
			kfree(dummy_log);
			dummy_log = NULL;
			mutex_unlock(&jfs_log_mutex);
			return rc;
		}
	}

	LOG_LOCK(dummy_log);
	list_add(&JFS_SBI(sb)->log_list, &dummy_log->sb_list);
	JFS_SBI(sb)->log = dummy_log;
	LOG_UNLOCK(dummy_log);
	mutex_unlock(&jfs_log_mutex);

	return 0;
}

/*
 * NAME:	lmLogInit()
 *
 * FUNCTION:	log initialization at first log open.
 *
 *	logredo() (or logformat()) should have been run previously.
 *	initialize the log from log superblock.
 *	set the log state in the superblock to LOGMOUNT and
 *	write SYNCPT log record.
 *
 * PARAMETER:	log	- log structure
 *
 * RETURN:	0	- if ok
 *		-EINVAL	- bad log magic number or superblock dirty
 *		error returned from logwait()
 *
 * serialization: single first open thread
 */
int lmLogInit(struct jfs_log * log)
{
	int rc = 0;
	struct lrd lrd;
	struct logsuper *logsuper;
	struct lbuf *bpsuper;
	struct lbuf *bp;
	struct logpage *lp;
	int lsn = 0;

	jfs_info("lmLogInit: log:0x%p", log);

	/* initialize the group commit serialization lock */
	LOGGC_LOCK_INIT(log);

	/* allocate/initialize the log write serialization lock */
	LOG_LOCK_INIT(log);

	LOGSYNC_LOCK_INIT(log);

	INIT_LIST_HEAD(&log->synclist);

	INIT_LIST_HEAD(&log->cqueue);
	log->flush_tblk = NULL;

	log->count = 0;

	/*
	 * initialize log i/o
	 */
	if ((rc = lbmLogInit(log)))
		return rc;

	if (!test_bit(log_INLINELOG, &log->flag))
		log->l2bsize = L2LOGPSIZE;

	/* check for disabled journaling to disk */
	if (log->no_integrity) {
		/*
		 * Journal pages will still be filled.  When the time comes
		 * to actually do the I/O, the write is not done, and the
		 * endio routine is called directly.
		 */
		bp = lbmAllocate(log , 0);
		log->bp = bp;
		bp->l_pn = bp->l_eor = 0;
	} else {
		/*
		 * validate log superblock
		 */
		if ((rc = lbmRead(log, 1, &bpsuper)))
			goto errout10;

		logsuper = (struct logsuper *) bpsuper->l_ldata;

		if (logsuper->magic != cpu_to_le32(LOGMAGIC)) {
			jfs_warn("*** Log Format Error ! ***");
			rc = -EINVAL;
			goto errout20;
		}

		/* logredo() should have been run successfully. */
		if (logsuper->state != cpu_to_le32(LOGREDONE)) {
			jfs_warn("*** Log Is Dirty ! ***");
			rc = -EINVAL;
			goto errout20;
		}

		/* initialize log from log superblock */
		if (test_bit(log_INLINELOG,&log->flag)) {
			if (log->size != le32_to_cpu(logsuper->size)) {
				rc = -EINVAL;
				goto errout20;
			}
			jfs_info("lmLogInit: inline log:0x%p base:0x%Lx size:0x%x",
				 log, (unsigned long long)log->base, log->size);
		} else {
			if (memcmp(logsuper->uuid, log->uuid, 16)) {
				jfs_warn("wrong uuid on JFS log device");
				goto errout20;
			}
			log->size = le32_to_cpu(logsuper->size);
			log->l2bsize = le32_to_cpu(logsuper->l2bsize);
			jfs_info("lmLogInit: external log:0x%p base:0x%Lx size:0x%x",
				 log, (unsigned long long)log->base, log->size);
		}

		log->page = le32_to_cpu(logsuper->end) / LOGPSIZE;
		log->eor = le32_to_cpu(logsuper->end) - (LOGPSIZE * log->page);

		/*
		 * initialize for log append write mode
		 */
		/* establish current/end-of-log page/buffer */
		if ((rc = lbmRead(log, log->page, &bp)))
			goto errout20;

		lp = (struct logpage *) bp->l_ldata;

		jfs_info("lmLogInit: lsn:0x%x page:%d eor:%d:%d",
			 le32_to_cpu(logsuper->end), log->page, log->eor,
			 le16_to_cpu(lp->h.eor));

		log->bp = bp;
		bp->l_pn = log->page;
		bp->l_eor = log->eor;

		/* if current page is full, move on to next page */
		if (log->eor >= LOGPSIZE - LOGPTLRSIZE)
			lmNextPage(log);

		/*
		 * initialize log syncpoint
		 */
		/*
		 * write the first SYNCPT record with syncpoint = 0
		 * (i.e., log redo up to HERE !);
		 * remove current page from lbm write queue at end of pageout
		 * (to write log superblock update), but do not release to
		 * freelist;
		 */
		lrd.logtid = 0;
		lrd.backchain = 0;
		lrd.type = cpu_to_le16(LOG_SYNCPT);
		lrd.length = 0;
		lrd.log.syncpt.sync = 0;
		lsn = lmWriteRecord(log, NULL, &lrd, NULL);
		bp = log->bp;
		bp->l_ceor = bp->l_eor;
		lp = (struct logpage *) bp->l_ldata;
		lp->h.eor = lp->t.eor = cpu_to_le16(bp->l_eor);
		lbmWrite(log, bp, lbmWRITE | lbmSYNC, 0);
		if ((rc = lbmIOWait(bp, 0)))
			goto errout30;

		/*
		 * update/write superblock
		 */
		logsuper->state = cpu_to_le32(LOGMOUNT);
		log->serial = le32_to_cpu(logsuper->serial) + 1;
		logsuper->serial = cpu_to_le32(log->serial);
		lbmDirectWrite(log, bpsuper, lbmWRITE | lbmRELEASE | lbmSYNC);
		if ((rc = lbmIOWait(bpsuper, lbmFREE)))
			goto errout30;
	}

	/* initialize logsync parameters */
	log->logsize = (log->size - 2) << L2LOGPSIZE;
	log->lsn = lsn;
	log->syncpt = lsn;
	log->sync = log->syncpt;
	log->nextsync = LOGSYNC_DELTA(log->logsize);

	jfs_info("lmLogInit: lsn:0x%x syncpt:0x%x sync:0x%x",
		 log->lsn, log->syncpt, log->sync);

	/*
	 * initialize for lazy/group commit
	 */
	log->clsn = lsn;

	return 0;

	/*
	 *	unwind on error
	 */
      errout30:		/* release log page */
	log->wqueue = NULL;
	bp->l_wqnext = NULL;
	lbmFree(bp);

      errout20:		/* release log superblock */
	lbmFree(bpsuper);

      errout10:		/* unwind lbmLogInit() */
	lbmLogShutdown(log);

	jfs_warn("lmLogInit: exit(%d)", rc);
	return rc;
}


/*
 * NAME:	lmLogClose()
 *
 * FUNCTION:	remove file system <ipmnt> from active list of log <iplog>
 *		and close it on last close.
 *
 * PARAMETER:	sb	- superblock
 *
 * RETURN:	errors from subroutines
 *
 * serialization:
 */
int lmLogClose(struct super_block *sb)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	struct jfs_log *log = sbi->log;
	struct block_device *bdev;
	int rc = 0;

	jfs_info("lmLogClose: log:0x%p", log);

	mutex_lock(&jfs_log_mutex);
	LOG_LOCK(log);
	list_del(&sbi->log_list);
	LOG_UNLOCK(log);
	sbi->log = NULL;

	/*
	 * We need to make sure all of the "written" metapages
	 * actually make it to disk
	 */
	sync_blockdev(sb->s_bdev);

	if (test_bit(log_INLINELOG, &log->flag)) {
		/*
		 *	in-line log in host file system
		 */
		rc = lmLogShutdown(log);
		kfree(log);
		goto out;
	}

	if (!log->no_integrity)
		lmLogFileSystem(log, sbi, 0);

	if (!list_empty(&log->sb_list))
		goto out;

	/*
	 * TODO: ensure that the dummy_log is in a state to allow
	 * lbmLogShutdown to deallocate all the buffers and call
	 * kfree against dummy_log.  For now, leave dummy_log & its
	 * buffers in memory, and resuse if another no-integrity mount
	 * is requested.
	 */
	if (log->no_integrity)
		goto out;

	/*
	 *	external log as separate logical volume
	 */
	list_del(&log->journal_list);
	bdev = log->bdev;
	rc = lmLogShutdown(log);

	blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);

	kfree(log);

      out:
	mutex_unlock(&jfs_log_mutex);
	jfs_info("lmLogClose: exit(%d)", rc);
	return rc;
}


/*
 * NAME:	jfs_flush_journal()
 *
 * FUNCTION:	initiate write of any outstanding transactions to the journal
 *		and optionally wait until they are all written to disk
 *
 *		wait == 0  flush until latest txn is committed, don't wait
 *		wait == 1  flush until latest txn is committed, wait
 *		wait > 1   flush until all txn's are complete, wait
 */
void jfs_flush_journal(struct jfs_log *log, int wait)
{
	int i;
	struct tblock *target = NULL;

	/* jfs_write_inode may call us during read-only mount */
	if (!log)
		return;

	jfs_info("jfs_flush_journal: log:0x%p wait=%d", log, wait);

	LOGGC_LOCK(log);

	if (!list_empty(&log->cqueue)) {
		/*
		 * This ensures that we will keep writing to the journal as long
		 * as there are unwritten commit records
		 */
		target = list_entry(log->cqueue.prev, struct tblock, cqueue);

		if (test_bit(log_FLUSH, &log->flag)) {
			/*
			 * We're already flushing.
			 * if flush_tblk is NULL, we are flushing everything,
			 * so leave it that way.  Otherwise, update it to the
			 * latest transaction
			 */
			if (log->flush_tblk)
				log->flush_tblk = target;
		} else {
			/* Only flush until latest transaction is committed */
			log->flush_tblk = target;
			set_bit(log_FLUSH, &log->flag);

			/*
			 * Initiate I/O on outstanding transactions
			 */
			if (!(log->cflag & logGC_PAGEOUT)) {
				log->cflag |= logGC_PAGEOUT;
				lmGCwrite(log, 0);
			}
		}
	}
	if ((wait > 1) || test_bit(log_SYNCBARRIER, &log->flag)) {
		/* Flush until all activity complete */
		set_bit(log_FLUSH, &log->flag);
		log->flush_tblk = NULL;
	}

	if (wait && target && !(target->flag & tblkGC_COMMITTED)) {
		DECLARE_WAITQUEUE(__wait, current);

		add_wait_queue(&target->gcwait, &__wait);
		set_current_state(TASK_UNINTERRUPTIBLE);
		LOGGC_UNLOCK(log);
		schedule();
		LOGGC_LOCK(log);
		remove_wait_queue(&target->gcwait, &__wait);
	}
	LOGGC_UNLOCK(log);

	if (wait < 2)
		return;

	write_special_inodes(log, filemap_fdatawrite);

	/*
	 * If there was recent activity, we may need to wait
	 * for the lazycommit thread to catch up
	 */
	if ((!list_empty(&log->cqueue)) || !list_empty(&log->synclist)) {
		for (i = 0; i < 200; i++) {	/* Too much? */
			msleep(250);
			write_special_inodes(log, filemap_fdatawrite);
			if (list_empty(&log->cqueue) &&
			    list_empty(&log->synclist))
				break;
		}
	}
	assert(list_empty(&log->cqueue));

#ifdef CONFIG_JFS_DEBUG
	if (!list_empty(&log->synclist)) {
		struct logsyncblk *lp;

		printk(KERN_ERR "jfs_flush_journal: synclist not empty\n");
		list_for_each_entry(lp, &log->synclist, synclist) {
			if (lp->xflag & COMMIT_PAGE) {
				struct metapage *mp = (struct metapage *)lp;
				print_hex_dump(KERN_ERR, "metapage: ",
					       DUMP_PREFIX_ADDRESS, 16, 4,
					       mp, sizeof(struct metapage), 0);
				print_hex_dump(KERN_ERR, "page: ",
					       DUMP_PREFIX_ADDRESS, 16,
					       sizeof(long), mp->page,
					       sizeof(struct page), 0);
			} else
				print_hex_dump(KERN_ERR, "tblock:",
					       DUMP_PREFIX_ADDRESS, 16, 4,
					       lp, sizeof(struct tblock), 0);
		}
	}
#else
	WARN_ON(!list_empty(&log->synclist));
#endif
	clear_bit(log_FLUSH, &log->flag);
}

/*
 * NAME:	lmLogShutdown()
 *
 * FUNCTION:	log shutdown at last LogClose().
 *
 *		write log syncpt record.
 *		update super block to set redone flag to 0.
 *
 * PARAMETER:	log	- log inode
 *
 * RETURN:	0	- success
 *
 * serialization: single last close thread
 */
int lmLogShutdown(struct jfs_log * log)
{
	int rc;
	struct lrd lrd;
	int lsn;
	struct logsuper *logsuper;
	struct lbuf *bpsuper;
	struct lbuf *bp;
	struct logpage *lp;

	jfs_info("lmLogShutdown: log:0x%p", log);

	jfs_flush_journal(log, 2);

	/*
	 * write the last SYNCPT record with syncpoint = 0
	 * (i.e., log redo up to HERE !)
	 */
	lrd.logtid = 0;
	lrd.backchain = 0;
	lrd.type = cpu_to_le16(LOG_SYNCPT);
	lrd.length = 0;
	lrd.log.syncpt.sync = 0;

	lsn = lmWriteRecord(log, NULL, &lrd, NULL);
	bp = log->bp;
	lp = (struct logpage *) bp->l_ldata;
	lp->h.eor = lp->t.eor = cpu_to_le16(bp->l_eor);
	lbmWrite(log, log->bp, lbmWRITE | lbmRELEASE | lbmSYNC, 0);
	lbmIOWait(log->bp, lbmFREE);
	log->bp = NULL;

	/*
	 * synchronous update log superblock
	 * mark log state as shutdown cleanly
	 * (i.e., Log does not need to be replayed).
	 */
	if ((rc = lbmRead(log, 1, &bpsuper)))
		goto out;

	logsuper = (struct logsuper *) bpsuper->l_ldata;
	logsuper->state = cpu_to_le32(LOGREDONE);
	logsuper->end = cpu_to_le32(lsn);
	lbmDirectWrite(log, bpsuper, lbmWRITE | lbmRELEASE | lbmSYNC);
	rc = lbmIOWait(bpsuper, lbmFREE);

	jfs_info("lmLogShutdown: lsn:0x%x page:%d eor:%d",
		 lsn, log->page, log->eor);

      out:
	/*
	 * shutdown per log i/o
	 */
	lbmLogShutdown(log);

	if (rc) {
		jfs_warn("lmLogShutdown: exit(%d)", rc);
	}
	return rc;
}


/*
 * NAME:	lmLogFileSystem()
 *
 * FUNCTION:	insert (<activate> = true)/remove (<activate> = false)
 *	file system into/from log active file system list.
 *
 * PARAMETE:	log	- pointer to logs inode.
 *		fsdev	- kdev_t of filesystem.
 *		serial	- pointer to returned log serial number
 *		activate - insert/remove device from active list.
 *
 * RETURN:	0	- success
 *		errors returned by vms_iowait().
 */
static int lmLogFileSystem(struct jfs_log * log, struct jfs_sb_info *sbi,
			   int activate)
{
	int rc = 0;
	int i;
	struct logsuper *logsuper;
	struct lbuf *bpsuper;
	char *uuid = sbi->uuid;

	/*
	 * insert/remove file system device to log active file system list.
	 */
	if ((rc = lbmRead(log, 1, &bpsuper)))
		return rc;

	logsuper = (struct logsuper *) bpsuper->l_ldata;
	if (activate) {
		for (i = 0; i < MAX_ACTIVE; i++)
			if (!memcmp(logsuper->active[i].uuid, NULL_UUID, 16)) {
				memcpy(logsuper->active[i].uuid, uuid, 16);
				sbi->aggregate = i;
				break;
			}
		if (i == MAX_ACTIVE) {
			jfs_warn("Too many file systems sharing journal!");
			lbmFree(bpsuper);
			return -EMFILE;	/* Is there a better rc? */
		}
	} else {
		for (i = 0; i < MAX_ACTIVE; i++)
			if (!memcmp(logsuper->active[i].uuid, uuid, 16)) {
				memcpy(logsuper->active[i].uuid, NULL_UUID, 16);
				break;
			}
		if (i == MAX_ACTIVE) {
			jfs_warn("Somebody stomped on the journal!");
			lbmFree(bpsuper);
			return -EIO;
		}

	}

	/*
	 * synchronous write log superblock:
	 *
	 * write sidestream bypassing write queue:
	 * at file system mount, log super block is updated for
	 * activation of the file system before any log record
	 * (MOUNT record) of the file system, and at file system
	 * unmount, all meta data for the file system has been
	 * flushed before log super block is updated for deactivation
	 * of the file system.
	 */
	lbmDirectWrite(log, bpsuper, lbmWRITE | lbmRELEASE | lbmSYNC);
	rc = lbmIOWait(bpsuper, lbmFREE);

	return rc;
}

/*
 *		log buffer manager (lbm)
 *		------------------------
 *
 * special purpose buffer manager supporting log i/o requirements.
 *
 * per log write queue:
 * log pageout occurs in serial order by fifo write queue and
 * restricting to a single i/o in pregress at any one time.
 * a circular singly-linked list
 * (log->wrqueue points to the tail, and buffers are linked via
 * bp->wrqueue field), and
 * maintains log page in pageout ot waiting for pageout in serial pageout.
 */

/*
 *	lbmLogInit()
 *
 * initialize per log I/O setup at lmLogInit()
 */
static int lbmLogInit(struct jfs_log * log)
{				/* log inode */
	int i;
	struct lbuf *lbuf;

	jfs_info("lbmLogInit: log:0x%p", log);

	/* initialize current buffer cursor */
	log->bp = NULL;

	/* initialize log device write queue */
	log->wqueue = NULL;

	/*
	 * Each log has its own buffer pages allocated to it.  These are
	 * not managed by the page cache.  This ensures that a transaction
	 * writing to the log does not block trying to allocate a page from
	 * the page cache (for the log).  This would be bad, since page
	 * allocation waits on the kswapd thread that may be committing inodes
	 * which would cause log activity.  Was that clear?  I'm trying to
	 * avoid deadlock here.
	 */
	init_waitqueue_head(&log->free_wait);

	log->lbuf_free = NULL;

	for (i = 0; i < LOGPAGES;) {
		char *buffer;
		uint offset;
		struct page *page = alloc_page(GFP_KERNEL | __GFP_ZERO);

		if (!page)
			goto error;
		buffer = page_address(page);
		for (offset = 0; offset < PAGE_SIZE; offset += LOGPSIZE) {
			lbuf = kmalloc(sizeof(struct lbuf), GFP_KERNEL);
			if (lbuf == NULL) {
				if (offset == 0)
					__free_page(page);
				goto error;
			}
			if (offset) /* we already have one reference */
				get_page(page);
			lbuf->l_offset = offset;
			lbuf->l_ldata = buffer + offset;
			lbuf->l_page = page;
			lbuf->l_log = log;
			init_waitqueue_head(&lbuf->l_ioevent);

			lbuf->l_freelist = log->lbuf_free;
			log->lbuf_free = lbuf;
			i++;
		}
	}

	return (0);

      error:
	lbmLogShutdown(log);
	return -ENOMEM;
}


/*
 *	lbmLogShutdown()
 *
 * finalize per log I/O setup at lmLogShutdown()
 */
static void lbmLogShutdown(struct jfs_log * log)
{
	struct lbuf *lbuf;

	jfs_info("lbmLogShutdown: log:0x%p", log);

	lbuf = log->lbuf_free;
	while (lbuf) {
		struct lbuf *next = lbuf->l_freelist;
		__free_page(lbuf->l_page);
		kfree(lbuf);
		lbuf = next;
	}
}


/*
 *	lbmAllocate()
 *
 * allocate an empty log buffer
 */
static struct lbuf *lbmAllocate(struct jfs_log * log, int pn)
{
	struct lbuf *bp;
	unsigned long flags;

	/*
	 * recycle from log buffer freelist if any
	 */
	LCACHE_LOCK(flags);
	LCACHE_SLEEP_COND(log->free_wait, (bp = log->lbuf_free), flags);
	log->lbuf_free = bp->l_freelist;
	LCACHE_UNLOCK(flags);

	bp->l_flag = 0;

	bp->l_wqnext = NULL;
	bp->l_freelist = NULL;

	bp->l_pn = pn;
	bp->l_blkno = log->base + (pn << (L2LOGPSIZE - log->l2bsize));
	bp->l_ceor = 0;

	return bp;
}


/*
 *	lbmFree()
 *
 * release a log buffer to freelist
 */
static void lbmFree(struct lbuf * bp)
{
	unsigned long flags;

	LCACHE_LOCK(flags);

	lbmfree(bp);

	LCACHE_UNLOCK(flags);
}

static void lbmfree(struct lbuf * bp)
{
	struct jfs_log *log = bp->l_log;

	assert(bp->l_wqnext == NULL);

	/*
	 * return the buffer to head of freelist
	 */
	bp->l_freelist = log->lbuf_free;
	log->lbuf_free = bp;

	wake_up(&log->free_wait);
	return;
}


/*
 * NAME:	lbmRedrive
 *
 * FUNCTION:	add a log buffer to the log redrive list
 *
 * PARAMETER:
 *	bp	- log buffer
 *
 * NOTES:
 *	Takes log_redrive_lock.
 */
static inline void lbmRedrive(struct lbuf *bp)
{
	unsigned long flags;

	spin_lock_irqsave(&log_redrive_lock, flags);
	bp->l_redrive_next = log_redrive_list;
	log_redrive_list = bp;
	spin_unlock_irqrestore(&log_redrive_lock, flags);

	wake_up_process(jfsIOthread);
}


/*
 *	lbmRead()
 */
static int lbmRead(struct jfs_log * log, int pn, struct lbuf ** bpp)
{
	struct bio *bio;
	struct lbuf *bp;

	/*
	 * allocate a log buffer
	 */
	*bpp = bp = lbmAllocate(log, pn);
	jfs_info("lbmRead: bp:0x%p pn:0x%x", bp, pn);

	bp->l_flag |= lbmREAD;

	bio = bio_alloc(GFP_NOFS, 1);

	bio->bi_iter.bi_sector = bp->l_blkno << (log->l2bsize - 9);
	bio->bi_bdev = log->bdev;

	bio_add_page(bio, bp->l_page, LOGPSIZE, bp->l_offset);
	BUG_ON(bio->bi_iter.bi_size != LOGPSIZE);

	bio->bi_end_io = lbmIODone;
	bio->bi_private = bp;
	/*check if journaling to disk has been disabled*/
	if (log->no_integrity) {
		bio->bi_iter.bi_size = 0;
		lbmIODone(bio);
	} else {
		submit_bio(READ_SYNC, bio);
	}

	wait_event(bp->l_ioevent, (bp->l_flag != lbmREAD));

	return 0;
}


/*
 *	lbmWrite()
 *
 * buffer at head of pageout queue stays after completion of
 * partial-page pageout and redriven by explicit initiation of
 * pageout by caller until full-page pageout is completed and
 * released.
 *
 * device driver i/o done redrives pageout of new buffer at
 * head of pageout queue when current buffer at head of pageout
 * queue is released at the completion of its full-page pageout.
 *
 * LOGGC_LOCK() serializes lbmWrite() by lmNextPage() and lmGroupCommit().
 * LCACHE_LOCK() serializes xflag between lbmWrite() and lbmIODone()
 */
static void lbmWrite(struct jfs_log * log, struct lbuf * bp, int flag,
		     int cant_block)
{
	struct lbuf *tail;
	unsigned long flags;

	jfs_info("lbmWrite: bp:0x%p flag:0x%x pn:0x%x", bp, flag, bp->l_pn);

	/* map the logical block address to physical block address */
	bp->l_blkno =
	    log->base + (bp->l_pn << (L2LOGPSIZE - log->l2bsize));

	LCACHE_LOCK(flags);		/* disable+lock */

	/*
	 * initialize buffer for device driver
	 */
	bp->l_flag = flag;

	/*
	 *	insert bp at tail of write queue associated with log
	 *
	 * (request is either for bp already/currently at head of queue
	 * or new bp to be inserted at tail)
	 */
	tail = log->wqueue;

	/* is buffer not already on write queue ? */
	if (bp->l_wqnext == NULL) {
		/* insert at tail of wqueue */
		if (tail == NULL) {
			log->wqueue = bp;
			bp->l_wqnext = bp;
		} else {
			log->wqueue = bp;
			bp->l_wqnext = tail->l_wqnext;
			tail->l_wqnext = bp;
		}

		tail = bp;
	}

	/* is buffer at head of wqueue and for write ? */
	if ((bp != tail->l_wqnext) || !(flag & lbmWRITE)) {
		LCACHE_UNLOCK(flags);	/* unlock+enable */
		return;
	}

	LCACHE_UNLOCK(flags);	/* unlock+enable */

	if (cant_block)
		lbmRedrive(bp);
	else if (flag & lbmSYNC)
		lbmStartIO(bp);
	else {
		LOGGC_UNLOCK(log);
		lbmStartIO(bp);
		LOGGC_LOCK(log);
	}
}


/*
 *	lbmDirectWrite()
 *
 * initiate pageout bypassing write queue for sidestream
 * (e.g., log superblock) write;
 */
static void lbmDirectWrite(struct jfs_log * log, struct lbuf * bp, int flag)
{
	jfs_info("lbmDirectWrite: bp:0x%p flag:0x%x pn:0x%x",
		 bp, flag, bp->l_pn);

	/*
	 * initialize buffer for device driver
	 */
	bp->l_flag = flag | lbmDIRECT;

	/* map the logical block address to physical block address */
	bp->l_blkno =
	    log->base + (bp->l_pn << (L2LOGPSIZE - log->l2bsize));

	/*
	 *	initiate pageout of the page
	 */
	lbmStartIO(bp);
}


/*
 * NAME:	lbmStartIO()
 *
 * FUNCTION:	Interface to DD strategy routine
 *
 * RETURN:	none
 *
 * serialization: LCACHE_LOCK() is NOT held during log i/o;
 */
static void lbmStartIO(struct lbuf * bp)
{
	struct bio *bio;
	struct jfs_log *log = bp->l_log;

	jfs_info("lbmStartIO");

	bio = bio_alloc(GFP_NOFS, 1);
	bio->bi_iter.bi_sector = bp->l_blkno << (log->l2bsize - 9);
	bio->bi_bdev = log->bdev;

	bio_add_page(bio, bp->l_page, LOGPSIZE, bp->l_offset);
	BUG_ON(bio->bi_iter.bi_size != LOGPSIZE);

	bio->bi_end_io = lbmIODone;
	bio->bi_private = bp;

	/* check if journaling to disk has been disabled */
	if (log->no_integrity) {
		bio->bi_iter.bi_size = 0;
		lbmIODone(bio);
	} else {
		submit_bio(WRITE_SYNC, bio);
		INCREMENT(lmStat.submitted);
	}
}


/*
 *	lbmIOWait()
 */
static int lbmIOWait(struct lbuf * bp, int flag)
{
	unsigned long flags;
	int rc = 0;

	jfs_info("lbmIOWait1: bp:0x%p flag:0x%x:0x%x", bp, bp->l_flag, flag);

	LCACHE_LOCK(flags);		/* disable+lock */

	LCACHE_SLEEP_COND(bp->l_ioevent, (bp->l_flag & lbmDONE), flags);

	rc = (bp->l_flag & lbmERROR) ? -EIO : 0;

	if (flag & lbmFREE)
		lbmfree(bp);

	LCACHE_UNLOCK(flags);	/* unlock+enable */

	jfs_info("lbmIOWait2: bp:0x%p flag:0x%x:0x%x", bp, bp->l_flag, flag);
	return rc;
}

/*
 *	lbmIODone()
 *
 * executed at INTIODONE level
 */
static void lbmIODone(struct bio *bio)
{
	struct lbuf *bp = bio->bi_private;
	struct lbuf *nextbp, *tail;
	struct jfs_log *log;
	unsigned long flags;

	/*
	 * get back jfs buffer bound to the i/o buffer
	 */
	jfs_info("lbmIODone: bp:0x%p flag:0x%x", bp, bp->l_flag);

	LCACHE_LOCK(flags);		/* disable+lock */

	bp->l_flag |= lbmDONE;

	if (bio->bi_error) {
		bp->l_flag |= lbmERROR;

		jfs_err("lbmIODone: I/O error in JFS log");
	}

	bio_put(bio);

	/*
	 *	pagein completion
	 */
	if (bp->l_flag & lbmREAD) {
		bp->l_flag &= ~lbmREAD;

		LCACHE_UNLOCK(flags);	/* unlock+enable */

		/* wakeup I/O initiator */
		LCACHE_WAKEUP(&bp->l_ioevent);

		return;
	}

	/*
	 *	pageout completion
	 *
	 * the bp at the head of write queue has completed pageout.
	 *
	 * if single-commit/full-page pageout, remove the current buffer
	 * from head of pageout queue, and redrive pageout with
	 * the new buffer at head of pageout queue;
	 * otherwise, the partial-page pageout buffer stays at
	 * the head of pageout queue to be redriven for pageout
	 * by lmGroupCommit() until full-page pageout is completed.
	 */
	bp->l_flag &= ~lbmWRITE;
	INCREMENT(lmStat.pagedone);

	/* update committed lsn */
	log = bp->l_log;
	log->clsn = (bp->l_pn << L2LOGPSIZE) + bp->l_ceor;

	if (bp->l_flag & lbmDIRECT) {
		LCACHE_WAKEUP(&bp->l_ioevent);
		LCACHE_UNLOCK(flags);
		return;
	}

	tail = log->wqueue;

	/* single element queue */
	if (bp == tail) {
		/* remove head buffer of full-page pageout
		 * from log device write queue
		 */
		if (bp->l_flag & lbmRELEASE) {
			log->wqueue = NULL;
			bp->l_wqnext = NULL;
		}
	}
	/* multi element queue */
	else {
		/* remove head buffer of full-page pageout
		 * from log device write queue
		 */
		if (bp->l_flag & lbmRELEASE) {
			nextbp = tail->l_wqnext = bp->l_wqnext;
			bp->l_wqnext = NULL;

			/*
			 * redrive pageout of next page at head of write queue:
			 * redrive next page without any bound tblk
			 * (i.e., page w/o any COMMIT records), or
			 * first page of new group commit which has been
			 * queued after current page (subsequent pageout
			 * is performed synchronously, except page without
			 * any COMMITs) by lmGroupCommit() as indicated
			 * by lbmWRITE flag;
			 */
			if (nextbp->l_flag & lbmWRITE) {
				/*
				 * We can't do the I/O at interrupt time.
				 * The jfsIO thread can do it
				 */
				lbmRedrive(nextbp);
			}
		}
	}

	/*
	 *	synchronous pageout:
	 *
	 * buffer has not necessarily been removed from write queue
	 * (e.g., synchronous write of partial-page with COMMIT):
	 * leave buffer for i/o initiator to dispose
	 */
	if (bp->l_flag & lbmSYNC) {
		LCACHE_UNLOCK(flags);	/* unlock+enable */

		/* wakeup I/O initiator */
		LCACHE_WAKEUP(&bp->l_ioevent);
	}

	/*
	 *	Group Commit pageout:
	 */
	else if (bp->l_flag & lbmGC) {
		LCACHE_UNLOCK(flags);
		lmPostGC(bp);
	}

	/*
	 *	asynchronous pageout:
	 *
	 * buffer must have been removed from write queue:
	 * insert buffer at head of freelist where it can be recycled
	 */
	else {
		assert(bp->l_flag & lbmRELEASE);
		assert(bp->l_flag & lbmFREE);
		lbmfree(bp);

		LCACHE_UNLOCK(flags);	/* unlock+enable */
	}
}

int jfsIOWait(void *arg)
{
	struct lbuf *bp;

	do {
		spin_lock_irq(&log_redrive_lock);
		while ((bp = log_redrive_list)) {
			log_redrive_list = bp->l_redrive_next;
			bp->l_redrive_next = NULL;
			spin_unlock_irq(&log_redrive_lock);
			lbmStartIO(bp);
			spin_lock_irq(&log_redrive_lock);
		}

		if (freezing(current)) {
			spin_unlock_irq(&log_redrive_lock);
			try_to_freeze();
		} else {
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irq(&log_redrive_lock);
			schedule();
		}
	} while (!kthread_should_stop());

	jfs_info("jfsIOWait being killed!");
	return 0;
}

/*
 * NAME:	lmLogFormat()/jfs_logform()
 *
 * FUNCTION:	format file system log
 *
 * PARAMETERS:
 *	log	- volume log
 *	logAddress - start address of log space in FS block
 *	logSize	- length of log space in FS block;
 *
 * RETURN:	0	- success
 *		-EIO	- i/o error
 *
 * XXX: We're synchronously writing one page at a time.  This needs to
 *	be improved by writing multiple pages at once.
 */
int lmLogFormat(struct jfs_log *log, s64 logAddress, int logSize)
{
	int rc = -EIO;
	struct jfs_sb_info *sbi;
	struct logsuper *logsuper;
	struct logpage *lp;
	int lspn;		/* log sequence page number */
	struct lrd *lrd_ptr;
	int npages = 0;
	struct lbuf *bp;

	jfs_info("lmLogFormat: logAddress:%Ld logSize:%d",
		 (long long)logAddress, logSize);

	sbi = list_entry(log->sb_list.next, struct jfs_sb_info, log_list);

	/* allocate a log buffer */
	bp = lbmAllocate(log, 1);

	npages = logSize >> sbi->l2nbperpage;

	/*
	 *	log space:
	 *
	 * page 0 - reserved;
	 * page 1 - log superblock;
	 * page 2 - log data page: A SYNC log record is written
	 *	    into this page at logform time;
	 * pages 3-N - log data page: set to empty log data pages;
	 */
	/*
	 *	init log superblock: log page 1
	 */
	logsuper = (struct logsuper *) bp->l_ldata;

	logsuper->magic = cpu_to_le32(LOGMAGIC);
	logsuper->version = cpu_to_le32(LOGVERSION);
	logsuper->state = cpu_to_le32(LOGREDONE);
	logsuper->flag = cpu_to_le32(sbi->mntflag);	/* ? */
	logsuper->size = cpu_to_le32(npages);
	logsuper->bsize = cpu_to_le32(sbi->bsize);
	logsuper->l2bsize = cpu_to_le32(sbi->l2bsize);
	logsuper->end = cpu_to_le32(2 * LOGPSIZE + LOGPHDRSIZE + LOGRDSIZE);

	bp->l_flag = lbmWRITE | lbmSYNC | lbmDIRECT;
	bp->l_blkno = logAddress + sbi->nbperpage;
	lbmStartIO(bp);
	if ((rc = lbmIOWait(bp, 0)))
		goto exit;

	/*
	 *	init pages 2 to npages-1 as log data pages:
	 *
	 * log page sequence number (lpsn) initialization:
	 *
	 * pn:   0     1     2     3                 n-1
	 *       +-----+-----+=====+=====+===.....===+=====+
	 * lspn:             N-1   0     1           N-2
	 *                   <--- N page circular file ---->
	 *
	 * the N (= npages-2) data pages of the log is maintained as
	 * a circular file for the log records;
	 * lpsn grows by 1 monotonically as each log page is written
	 * to the circular file of the log;
	 * and setLogpage() will not reset the page number even if
	 * the eor is equal to LOGPHDRSIZE. In order for binary search
	 * still work in find log end process, we have to simulate the
	 * log wrap situation at the log format time.
	 * The 1st log page written will have the highest lpsn. Then
	 * the succeeding log pages will have ascending order of
	 * the lspn starting from 0, ... (N-2)
	 */
	lp = (struct logpage *) bp->l_ldata;
	/*
	 * initialize 1st log page to be written: lpsn = N - 1,
	 * write a SYNCPT log record is written to this page
	 */
	lp->h.page = lp->t.page = cpu_to_le32(npages - 3);
	lp->h.eor = lp->t.eor = cpu_to_le16(LOGPHDRSIZE + LOGRDSIZE);

	lrd_ptr = (struct lrd *) &lp->data;
	lrd_ptr->logtid = 0;
	lrd_ptr->backchain = 0;
	lrd_ptr->type = cpu_to_le16(LOG_SYNCPT);
	lrd_ptr->length = 0;
	lrd_ptr->log.syncpt.sync = 0;

	bp->l_blkno += sbi->nbperpage;
	bp->l_flag = lbmWRITE | lbmSYNC | lbmDIRECT;
	lbmStartIO(bp);
	if ((rc = lbmIOWait(bp, 0)))
		goto exit;

	/*
	 *	initialize succeeding log pages: lpsn = 0, 1, ..., (N-2)
	 */
	for (lspn = 0; lspn < npages - 3; lspn++) {
		lp->h.page = lp->t.page = cpu_to_le32(lspn);
		lp->h.eor = lp->t.eor = cpu_to_le16(LOGPHDRSIZE);

		bp->l_blkno += sbi->nbperpage;
		bp->l_flag = lbmWRITE | lbmSYNC | lbmDIRECT;
		lbmStartIO(bp);
		if ((rc = lbmIOWait(bp, 0)))
			goto exit;
	}

	rc = 0;
exit:
	/*
	 *	finalize log
	 */
	/* release the buffer */
	lbmFree(bp);

	return rc;
}

#ifdef CONFIG_JFS_STATISTICS
static int jfs_lmstats_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m,
		       "JFS Logmgr stats\n"
		       "================\n"
		       "commits = %d\n"
		       "writes submitted = %d\n"
		       "writes completed = %d\n"
		       "full pages submitted = %d\n"
		       "partial pages submitted = %d\n",
		       lmStat.commit,
		       lmStat.submitted,
		       lmStat.pagedone,
		       lmStat.full_page,
		       lmStat.partial_page);
	return 0;
}

static int jfs_lmstats_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, jfs_lmstats_proc_show, NULL);
}

const struct file_operations jfs_lmstats_proc_fops = {
	.open		= jfs_lmstats_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif /* CONFIG_JFS_STATISTICS */
