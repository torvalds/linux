/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPIBLKTRACE_H
#define _UAPIBLKTRACE_H

#include <linux/types.h>

/*
 * Trace categories
 */
enum blktrace_cat {
	BLK_TC_READ	= 1 << 0,	/* reads */
	BLK_TC_WRITE	= 1 << 1,	/* writes */
	BLK_TC_FLUSH	= 1 << 2,	/* flush */
	BLK_TC_SYNC	= 1 << 3,	/* sync IO */
	BLK_TC_SYNCIO	= BLK_TC_SYNC,
	BLK_TC_QUEUE	= 1 << 4,	/* queueing/merging */
	BLK_TC_REQUEUE	= 1 << 5,	/* requeueing */
	BLK_TC_ISSUE	= 1 << 6,	/* issue */
	BLK_TC_COMPLETE	= 1 << 7,	/* completions */
	BLK_TC_FS	= 1 << 8,	/* fs requests */
	BLK_TC_PC	= 1 << 9,	/* pc requests */
	BLK_TC_NOTIFY	= 1 << 10,	/* special message */
	BLK_TC_AHEAD	= 1 << 11,	/* readahead */
	BLK_TC_META	= 1 << 12,	/* metadata */
	BLK_TC_DISCARD	= 1 << 13,	/* discard requests */
	BLK_TC_DRV_DATA	= 1 << 14,	/* binary per-driver data */
	BLK_TC_FUA	= 1 << 15,	/* fua requests */

	BLK_TC_END_V1	= 1 << 15,	/* we've run out of bits! */

	BLK_TC_ZONE_APPEND	= 1ull << 16,  	/* zone append */
	BLK_TC_ZONE_RESET	= 1ull << 17,	/* zone reset */
	BLK_TC_ZONE_RESET_ALL	= 1ull << 18,	/* zone reset all */
	BLK_TC_ZONE_FINISH	= 1ull << 19,	/* zone finish */
	BLK_TC_ZONE_OPEN	= 1ull << 20,	/* zone open */
	BLK_TC_ZONE_CLOSE	= 1ull << 21,	/* zone close */

	BLK_TC_WRITE_ZEROES	= 1ull << 22,	/* write-zeroes */

	BLK_TC_END_V2		= 1ull << 22,
};

#define BLK_TC_SHIFT		(16)
#define BLK_TC_ACT(act)		((u64)(act) << BLK_TC_SHIFT)

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
	__BLK_TA_BOUNCE,		/* unused, was: bio was bounced */
	__BLK_TA_REMAP,			/* bio was remapped */
	__BLK_TA_ABORT,			/* request aborted */
	__BLK_TA_DRV_DATA,		/* driver-specific binary data */
	__BLK_TA_ZONE_PLUG,		/* zone write plug was plugged */
	__BLK_TA_ZONE_UNPLUG,		/* zone write plug was unplugged */
	__BLK_TA_CGROUP = 1 << 8,	/* from a cgroup*/
};

/*
 * Notify events.
 */
enum blktrace_notify {
	__BLK_TN_PROCESS = 0,		/* establish pid/name mapping */
	__BLK_TN_TIMESTAMP,		/* include system clock */
	__BLK_TN_MESSAGE,		/* Character string message */
	__BLK_TN_CGROUP = __BLK_TA_CGROUP, /* from a cgroup */
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
#define BLK_TA_ABORT		(__BLK_TA_ABORT | BLK_TC_ACT(BLK_TC_QUEUE))
#define BLK_TA_DRV_DATA	(__BLK_TA_DRV_DATA | BLK_TC_ACT(BLK_TC_DRV_DATA))

#define BLK_TA_ZONE_APPEND	(__BLK_TA_COMPLETE |\
				 BLK_TC_ACT(BLK_TC_ZONE_APPEND))
#define BLK_TA_ZONE_PLUG	(__BLK_TA_ZONE_PLUG | BLK_TC_ACT(BLK_TC_QUEUE))
#define BLK_TA_ZONE_UNPLUG	(__BLK_TA_ZONE_UNPLUG |\
				 BLK_TC_ACT(BLK_TC_QUEUE))

#define BLK_TN_PROCESS		(__BLK_TN_PROCESS | BLK_TC_ACT(BLK_TC_NOTIFY))
#define BLK_TN_TIMESTAMP	(__BLK_TN_TIMESTAMP | BLK_TC_ACT(BLK_TC_NOTIFY))
#define BLK_TN_MESSAGE		(__BLK_TN_MESSAGE | BLK_TC_ACT(BLK_TC_NOTIFY))

#define BLK_IO_TRACE_MAGIC	0x65617400
#define BLK_IO_TRACE_VERSION	0x07
#define BLK_IO_TRACE2_VERSION	0x08

/*
 * The trace itself
 */
struct blk_io_trace {
	__u32 magic;		/* MAGIC << 8 | version */
	__u32 sequence;		/* event number */
	__u64 time;		/* in nanoseconds */
	__u64 sector;		/* disk offset */
	__u32 bytes;		/* transfer length */
	__u32 action;		/* what happened */
	__u32 pid;		/* who did it */
	__u32 device;		/* device number */
	__u32 cpu;		/* on what cpu did it happen */
	__u16 error;		/* completion error */
	__u16 pdu_len;		/* length of data after this trace */
	/* cgroup id will be stored here if exists */
};

struct blk_io_trace2 {
	__u32 magic;		/* MAGIC << 8 | BLK_IO_TRACE2_VERSION */
	__u32 sequence;		/* event number */
	__u64 time;		/* in nanoseconds */
	__u64 sector;		/* disk offset */
	__u32 bytes;		/* transfer length */
	__u32 pid;		/* who did it */
	__u64 action;		/* what happened */
	__u32 device;		/* device number */
	__u32 cpu;		/* on what cpu did it happen */
	__u16 error;		/* completion error */
	__u16 pdu_len;		/* length of data after this trace */
	__u8 pad[12];
	/* cgroup id will be stored here if it exists */
};
/*
 * The remap event
 */
struct blk_io_trace_remap {
	__be32 device_from;
	__be32 device_to;
	__be64 sector_from;
};

enum {
	Blktrace_setup = 1,
	Blktrace_running,
	Blktrace_stopped,
};

#define BLKTRACE_BDEV_SIZE	32
#define BLKTRACE_BDEV_SIZE2	64

/*
 * User setup structure passed with BLKTRACESETUP
 */
struct blk_user_trace_setup {
	char name[BLKTRACE_BDEV_SIZE];	/* output */
	__u16 act_mask;			/* input */
	__u32 buf_size;			/* input */
	__u32 buf_nr;			/* input */
	__u64 start_lba;
	__u64 end_lba;
	__u32 pid;
};

/*
 * User setup structure passed with BLKTRACESETUP2
 */
struct blk_user_trace_setup2 {
	char name[BLKTRACE_BDEV_SIZE2];		/* output */
	__u64 act_mask;				/* input */
	__u32 buf_size;				/* input */
	__u32 buf_nr;				/* input */
	__u64 start_lba;
	__u64 end_lba;
	__u32 pid;
	__u32 flags;		/* currently unused */
	__u64 reserved[11];
};

#endif /* _UAPIBLKTRACE_H */
