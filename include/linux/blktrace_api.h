#ifndef BLKTRACE_H
#define BLKTRACE_H

#include <linux/blkdev.h>
#include <linux/relay.h>

/*
 * Trace categories
 */
enum blktrace_cat {
	BLK_TC_READ	= 1 << 0,	/* reads */
	BLK_TC_WRITE	= 1 << 1,	/* writes */
	BLK_TC_BARRIER	= 1 << 2,	/* barrier */
	BLK_TC_SYNC	= 1 << 3,	/* barrier */
	BLK_TC_QUEUE	= 1 << 4,	/* queueing/merging */
	BLK_TC_REQUEUE	= 1 << 5,	/* requeueing */
	BLK_TC_ISSUE	= 1 << 6,	/* issue */
	BLK_TC_COMPLETE	= 1 << 7,	/* completions */
	BLK_TC_FS	= 1 << 8,	/* fs requests */
	BLK_TC_PC	= 1 << 9,	/* pc requests */
	BLK_TC_NOTIFY	= 1 << 10,	/* special message */

	BLK_TC_END	= 1 << 15,	/* only 16-bits, reminder */
};

#define BLK_TC_SHIFT		(16)
#define BLK_TC_ACT(act)		((act) << BLK_TC_SHIFT)

/*
 * Basic trace actions
 */
enum blktrace_act {
	__BLK_TA_QUEUE = 1,		/* queued */
	__BLK_TA_BACKMERGE,		/* back merged to existing rq */
	__BLK_TA_FRONTMERGE,		/* front merge to existing rq */
	__BLK_TA_GETRQ,			/* allocated new request */
	__BLK_TA_SLEEPRQ,		/* sleeping on rq allocation */
	__BLK_TA_REQUEUE,		/* request requeued */
	__BLK_TA_ISSUE,			/* sent to driver */
	__BLK_TA_COMPLETE,		/* completed by driver */
	__BLK_TA_PLUG,			/* queue was plugged */
	__BLK_TA_UNPLUG_IO,		/* queue was unplugged by io */
	__BLK_TA_UNPLUG_TIMER,		/* queue was unplugged by timer */
	__BLK_TA_INSERT,		/* insert request */
	__BLK_TA_SPLIT,			/* bio was split */
	__BLK_TA_BOUNCE,		/* bio was bounced */
	__BLK_TA_REMAP,			/* bio was remapped */
};

/*
 * Trace actions in full. Additionally, read or write is masked
 */
#define BLK_TA_QUEUE		(__BLK_TA_QUEUE | BLK_TC_ACT(BLK_TC_QUEUE))
#define BLK_TA_BACKMERGE	(__BLK_TA_BACKMERGE | BLK_TC_ACT(BLK_TC_QUEUE))
#define BLK_TA_FRONTMERGE	(__BLK_TA_FRONTMERGE | BLK_TC_ACT(BLK_TC_QUEUE))
#define	BLK_TA_GETRQ		(__BLK_TA_GETRQ | BLK_TC_ACT(BLK_TC_QUEUE))
#define	BLK_TA_SLEEPRQ		(__BLK_TA_SLEEPRQ | BLK_TC_ACT(BLK_TC_QUEUE))
#define	BLK_TA_REQUEUE		(__BLK_TA_REQUEUE | BLK_TC_ACT(BLK_TC_REQUEUE))
#define BLK_TA_ISSUE		(__BLK_TA_ISSUE | BLK_TC_ACT(BLK_TC_ISSUE))
#define BLK_TA_COMPLETE		(__BLK_TA_COMPLETE| BLK_TC_ACT(BLK_TC_COMPLETE))
#define BLK_TA_PLUG		(__BLK_TA_PLUG | BLK_TC_ACT(BLK_TC_QUEUE))
#define BLK_TA_UNPLUG_IO	(__BLK_TA_UNPLUG_IO | BLK_TC_ACT(BLK_TC_QUEUE))
#define BLK_TA_UNPLUG_TIMER	(__BLK_TA_UNPLUG_TIMER | BLK_TC_ACT(BLK_TC_QUEUE))
#define BLK_TA_INSERT		(__BLK_TA_INSERT | BLK_TC_ACT(BLK_TC_QUEUE))
#define BLK_TA_SPLIT		(__BLK_TA_SPLIT)
#define BLK_TA_BOUNCE		(__BLK_TA_BOUNCE)
#define BLK_TA_REMAP		(__BLK_TA_REMAP | BLK_TC_ACT(BLK_TC_QUEUE))

#define BLK_IO_TRACE_MAGIC	0x65617400
#define BLK_IO_TRACE_VERSION	0x07

/*
 * The trace itself
 */
struct blk_io_trace {
	u32 magic;		/* MAGIC << 8 | version */
	u32 sequence;		/* event number */
	u64 time;		/* in microseconds */
	u64 sector;		/* disk offset */
	u32 bytes;		/* transfer length */
	u32 action;		/* what happened */
	u32 pid;		/* who did it */
	u32 device;		/* device number */
	u32 cpu;		/* on what cpu did it happen */
	u16 error;		/* completion error */
	u16 pdu_len;		/* length of data after this trace */
};

/*
 * The remap event
 */
struct blk_io_trace_remap {
	u32 device;
	u32 __pad;
	u64 sector;
};

enum {
	Blktrace_setup = 1,
	Blktrace_running,
	Blktrace_stopped,
};

struct blk_trace {
	int trace_state;
	struct rchan *rchan;
	unsigned long *sequence;
	u16 act_mask;
	u64 start_lba;
	u64 end_lba;
	u32 pid;
	u32 dev;
	struct dentry *dir;
	struct dentry *dropped_file;
	atomic_t dropped;
};

/*
 * User setup structure passed with BLKTRACESTART
 */
struct blk_user_trace_setup {
	char name[BDEVNAME_SIZE];	/* output */
	u16 act_mask;			/* input */
	u32 buf_size;			/* input */
	u32 buf_nr;			/* input */
	u64 start_lba;
	u64 end_lba;
	u32 pid;
};

#if defined(CONFIG_BLK_DEV_IO_TRACE)
extern int blk_trace_ioctl(struct block_device *, unsigned, char __user *);
extern void blk_trace_shutdown(request_queue_t *);
extern void __blk_add_trace(struct blk_trace *, sector_t, int, int, u32, int, int, void *);

/**
 * blk_add_trace_rq - Add a trace for a request oriented action
 * @q:		queue the io is for
 * @rq:		the source request
 * @what:	the action
 *
 * Description:
 *     Records an action against a request. Will log the bio offset + size.
 *
 **/
static inline void blk_add_trace_rq(struct request_queue *q, struct request *rq,
				    u32 what)
{
	struct blk_trace *bt = q->blk_trace;
	int rw = rq->flags & 0x07;

	if (likely(!bt))
		return;

	if (blk_pc_request(rq)) {
		what |= BLK_TC_ACT(BLK_TC_PC);
		__blk_add_trace(bt, 0, rq->data_len, rw, what, rq->errors, sizeof(rq->cmd), rq->cmd);
	} else  {
		what |= BLK_TC_ACT(BLK_TC_FS);
		__blk_add_trace(bt, rq->hard_sector, rq->hard_nr_sectors << 9, rw, what, rq->errors, 0, NULL);
	}
}

/**
 * blk_add_trace_bio - Add a trace for a bio oriented action
 * @q:		queue the io is for
 * @bio:	the source bio
 * @what:	the action
 *
 * Description:
 *     Records an action against a bio. Will log the bio offset + size.
 *
 **/
static inline void blk_add_trace_bio(struct request_queue *q, struct bio *bio,
				     u32 what)
{
	struct blk_trace *bt = q->blk_trace;

	if (likely(!bt))
		return;

	__blk_add_trace(bt, bio->bi_sector, bio->bi_size, bio->bi_rw, what, !bio_flagged(bio, BIO_UPTODATE), 0, NULL);
}

/**
 * blk_add_trace_generic - Add a trace for a generic action
 * @q:		queue the io is for
 * @bio:	the source bio
 * @rw:		the data direction
 * @what:	the action
 *
 * Description:
 *     Records a simple trace
 *
 **/
static inline void blk_add_trace_generic(struct request_queue *q,
					 struct bio *bio, int rw, u32 what)
{
	struct blk_trace *bt = q->blk_trace;

	if (likely(!bt))
		return;

	if (bio)
		blk_add_trace_bio(q, bio, what);
	else
		__blk_add_trace(bt, 0, 0, rw, what, 0, 0, NULL);
}

/**
 * blk_add_trace_pdu_int - Add a trace for a bio with an integer payload
 * @q:		queue the io is for
 * @what:	the action
 * @bio:	the source bio
 * @pdu:	the integer payload
 *
 * Description:
 *     Adds a trace with some integer payload. This might be an unplug
 *     option given as the action, with the depth at unplug time given
 *     as the payload
 *
 **/
static inline void blk_add_trace_pdu_int(struct request_queue *q, u32 what,
					 struct bio *bio, unsigned int pdu)
{
	struct blk_trace *bt = q->blk_trace;
	u64 rpdu = cpu_to_be64(pdu);

	if (likely(!bt))
		return;

	if (bio)
		__blk_add_trace(bt, bio->bi_sector, bio->bi_size, bio->bi_rw, what, !bio_flagged(bio, BIO_UPTODATE), sizeof(rpdu), &rpdu);
	else
		__blk_add_trace(bt, 0, 0, 0, what, 0, sizeof(rpdu), &rpdu);
}

/**
 * blk_add_trace_remap - Add a trace for a remap operation
 * @q:		queue the io is for
 * @bio:	the source bio
 * @dev:	target device
 * @from:	source sector
 * @to:		target sector
 *
 * Description:
 *     Device mapper or raid target sometimes need to split a bio because
 *     it spans a stripe (or similar). Add a trace for that action.
 *
 **/
static inline void blk_add_trace_remap(struct request_queue *q, struct bio *bio,
				       dev_t dev, sector_t from, sector_t to)
{
	struct blk_trace *bt = q->blk_trace;
	struct blk_io_trace_remap r;

	if (likely(!bt))
		return;

	r.device = cpu_to_be32(dev);
	r.sector = cpu_to_be64(to);

	__blk_add_trace(bt, from, bio->bi_size, bio->bi_rw, BLK_TA_REMAP, !bio_flagged(bio, BIO_UPTODATE), sizeof(r), &r);
}

#else /* !CONFIG_BLK_DEV_IO_TRACE */
#define blk_trace_ioctl(bdev, cmd, arg)		(-ENOTTY)
#define blk_trace_shutdown(q)			do { } while (0)
#define blk_add_trace_rq(q, rq, what)		do { } while (0)
#define blk_add_trace_bio(q, rq, what)		do { } while (0)
#define blk_add_trace_generic(q, rq, rw, what)	do { } while (0)
#define blk_add_trace_pdu_int(q, what, bio, pdu)	do { } while (0)
#define blk_add_trace_remap(q, bio, dev, f, t)	do {} while (0)
#endif /* CONFIG_BLK_DEV_IO_TRACE */

#endif
