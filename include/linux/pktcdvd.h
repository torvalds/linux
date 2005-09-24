/*
 * Copyright (C) 2000 Jens Axboe <axboe@suse.de>
 * Copyright (C) 2001-2004 Peter Osterlund <petero2@telia.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Packet writing layer for ATAPI and SCSI CD-R, CD-RW, DVD-R, and
 * DVD-RW devices.
 *
 */
#ifndef __PKTCDVD_H
#define __PKTCDVD_H

#include <linux/types.h>

/*
 * 1 for normal debug messages, 2 is very verbose. 0 to turn it off.
 */
#define PACKET_DEBUG		1

#define	MAX_WRITERS		8

#define PKT_RB_POOL_SIZE	512

/*
 * How long we should hold a non-full packet before starting data gathering.
 */
#define PACKET_WAIT_TIME	(HZ * 5 / 1000)

/*
 * use drive write caching -- we need deferred error handling to be
 * able to sucessfully recover with this option (drive will return good
 * status as soon as the cdb is validated).
 */
#if defined(CONFIG_CDROM_PKTCDVD_WCACHE)
#define USE_WCACHING		1
#else
#define USE_WCACHING		0
#endif

/*
 * No user-servicable parts beyond this point ->
 */

/*
 * device types
 */
#define PACKET_CDR		1
#define	PACKET_CDRW		2
#define PACKET_DVDR		3
#define PACKET_DVDRW		4

/*
 * flags
 */
#define PACKET_WRITABLE		1	/* pd is writable */
#define PACKET_NWA_VALID	2	/* next writable address valid */
#define PACKET_LRA_VALID	3	/* last recorded address valid */
#define PACKET_MERGE_SEGS	4	/* perform segment merging to keep */
					/* underlying cdrom device happy */

/*
 * Disc status -- from READ_DISC_INFO
 */
#define PACKET_DISC_EMPTY	0
#define PACKET_DISC_INCOMPLETE	1
#define PACKET_DISC_COMPLETE	2
#define PACKET_DISC_OTHER	3

/*
 * write type, and corresponding data block type
 */
#define PACKET_MODE1		1
#define PACKET_MODE2		2
#define PACKET_BLOCK_MODE1	8
#define PACKET_BLOCK_MODE2	10

/*
 * Last session/border status
 */
#define PACKET_SESSION_EMPTY		0
#define PACKET_SESSION_INCOMPLETE	1
#define PACKET_SESSION_RESERVED		2
#define PACKET_SESSION_COMPLETE		3

#define PACKET_MCN			"4a656e734178626f65323030300000"

#undef PACKET_USE_LS

#define PKT_CTRL_CMD_SETUP	0
#define PKT_CTRL_CMD_TEARDOWN	1
#define PKT_CTRL_CMD_STATUS	2

struct pkt_ctrl_command {
	__u32 command;				/* in: Setup, teardown, status */
	__u32 dev_index;			/* in/out: Device index */
	__u32 dev;				/* in/out: Device nr for cdrw device */
	__u32 pkt_dev;				/* in/out: Device nr for packet device */
	__u32 num_devices;			/* out: Largest device index + 1 */
	__u32 padding;				/* Not used */
};

/*
 * packet ioctls
 */
#define PACKET_IOCTL_MAGIC	('X')
#define PACKET_CTRL_CMD		_IOWR(PACKET_IOCTL_MAGIC, 1, struct pkt_ctrl_command)

#ifdef __KERNEL__
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/cdrom.h>

struct packet_settings
{
	__u8			size;		/* packet size in (512 byte) sectors */
	__u8			fp;		/* fixed packets */
	__u8			link_loss;	/* the rest is specified
						 * as per Mt Fuji */
	__u8			write_type;
	__u8			track_mode;
	__u8			block_mode;
};

/*
 * Very crude stats for now
 */
struct packet_stats
{
	unsigned long		pkt_started;
	unsigned long		pkt_ended;
	unsigned long		secs_w;
	unsigned long		secs_rg;
	unsigned long		secs_r;
};

struct packet_cdrw
{
	struct list_head	pkt_free_list;
	struct list_head	pkt_active_list;
	spinlock_t		active_list_lock; /* Serialize access to pkt_active_list */
	struct task_struct	*thread;
	atomic_t		pending_bios;
};

/*
 * Switch to high speed reading after reading this many kilobytes
 * with no interspersed writes.
 */
#define HI_SPEED_SWITCH 512

struct packet_iosched
{
	atomic_t		attention;	/* Set to non-zero when queue processing is needed */
	int			writing;	/* Non-zero when writing, zero when reading */
	spinlock_t		lock;		/* Protecting read/write queue manipulations */
	struct bio		*read_queue;
	struct bio		*read_queue_tail;
	struct bio		*write_queue;
	struct bio		*write_queue_tail;
	sector_t		last_write;	/* The sector where the last write ended */
	int			successive_reads;
};

/*
 * 32 buffers of 2048 bytes
 */
#if (PAGE_SIZE % CD_FRAMESIZE) != 0
#error "PAGE_SIZE must be a multiple of CD_FRAMESIZE"
#endif
#define PACKET_MAX_SIZE		32
#define PAGES_PER_PACKET	(PACKET_MAX_SIZE * CD_FRAMESIZE / PAGE_SIZE)
#define PACKET_MAX_SECTORS	(PACKET_MAX_SIZE * CD_FRAMESIZE >> 9)

enum packet_data_state {
	PACKET_IDLE_STATE,			/* Not used at the moment */
	PACKET_WAITING_STATE,			/* Waiting for more bios to arrive, so */
						/* we don't have to do as much */
						/* data gathering */
	PACKET_READ_WAIT_STATE,			/* Waiting for reads to fill in holes */
	PACKET_WRITE_WAIT_STATE,		/* Waiting for the write to complete */
	PACKET_RECOVERY_STATE,			/* Recover after read/write errors */
	PACKET_FINISHED_STATE,			/* After write has finished */

	PACKET_NUM_STATES			/* Number of possible states */
};

/*
 * Information needed for writing a single packet
 */
struct pktcdvd_device;

struct packet_data
{
	struct list_head	list;

	spinlock_t		lock;		/* Lock protecting state transitions and */
						/* orig_bios list */

	struct bio		*orig_bios;	/* Original bios passed to pkt_make_request */
	struct bio		*orig_bios_tail;/* that will be handled by this packet */
	int			write_size;	/* Total size of all bios in the orig_bios */
						/* list, measured in number of frames */

	struct bio		*w_bio;		/* The bio we will send to the real CD */
						/* device once we have all data for the */
						/* packet we are going to write */
	sector_t		sector;		/* First sector in this packet */
	int			frames;		/* Number of frames in this packet */

	enum packet_data_state	state;		/* Current state */
	atomic_t		run_sm;		/* Incremented whenever the state */
						/* machine needs to be run */
	long			sleep_time;	/* Set this to non-zero to make the state */
						/* machine run after this many jiffies. */

	atomic_t		io_wait;	/* Number of pending IO operations */
	atomic_t		io_errors;	/* Number of read/write errors during IO */

	struct bio		*r_bios[PACKET_MAX_SIZE]; /* bios to use during data gathering */
	struct page		*pages[PAGES_PER_PACKET];

	int			cache_valid;	/* If non-zero, the data for the zone defined */
						/* by the sector variable is completely cached */
						/* in the pages[] vector. */

	int			id;		/* ID number for debugging */
	struct pktcdvd_device	*pd;
};

struct pkt_rb_node {
	struct rb_node		rb_node;
	struct bio		*bio;
};

struct packet_stacked_data
{
	struct bio		*bio;		/* Original read request bio */
	struct pktcdvd_device	*pd;
};
#define PSD_POOL_SIZE		64

struct pktcdvd_device
{
	struct block_device	*bdev;		/* dev attached */
	dev_t			pkt_dev;	/* our dev */
	char			name[20];
	struct packet_settings	settings;
	struct packet_stats	stats;
	int			refcnt;		/* Open count */
	int			write_speed;	/* current write speed, kB/s */
	int			read_speed;	/* current read speed, kB/s */
	unsigned long		offset;		/* start offset */
	__u8			mode_offset;	/* 0 / 8 */
	__u8			type;
	unsigned long		flags;
	__u16			mmc3_profile;
	__u32			nwa;		/* next writable address */
	__u32			lra;		/* last recorded address */
	struct packet_cdrw	cdrw;
	wait_queue_head_t	wqueue;

	spinlock_t		lock;		/* Serialize access to bio_queue */
	struct rb_root		bio_queue;	/* Work queue of bios we need to handle */
	int			bio_queue_size;	/* Number of nodes in bio_queue */
	sector_t		current_sector;	/* Keep track of where the elevator is */
	atomic_t		scan_queue;	/* Set to non-zero when pkt_handle_queue */
						/* needs to be run. */
	mempool_t		*rb_pool;	/* mempool for pkt_rb_node allocations */

	struct packet_iosched   iosched;
	struct gendisk		*disk;
};

#endif /* __KERNEL__ */

#endif /* __PKTCDVD_H */
