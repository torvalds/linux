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

#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/cdrom.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/mempool.h>
#include <uapi/linux/pktcdvd.h>

/* default bio write queue congestion marks */
#define PKT_WRITE_CONGESTION_ON    10000
#define PKT_WRITE_CONGESTION_OFF   9000


struct packet_settings
{
	__u32			size;		/* packet size in (512 byte) sectors */
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
	struct bio_list		read_queue;
	struct bio_list		write_queue;
	sector_t		last_write;	/* The sector where the last write ended */
	int			successive_reads;
};

/*
 * 32 buffers of 2048 bytes
 */
#if (PAGE_SIZE % CD_FRAMESIZE) != 0
#error "PAGE_SIZE must be a multiple of CD_FRAMESIZE"
#endif
#define PACKET_MAX_SIZE		128
#define FRAMES_PER_PAGE		(PAGE_SIZE / CD_FRAMESIZE)
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

	struct bio_list		orig_bios;	/* Original bios passed to pkt_make_request */
						/* that will be handled by this packet */
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
	struct page		*pages[PACKET_MAX_SIZE / FRAMES_PER_PAGE];

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
	bool			congested;	/* Someone is waiting for bio_queue_size
						 * to drop. */
	sector_t		current_sector;	/* Keep track of where the elevator is */
	atomic_t		scan_queue;	/* Set to non-zero when pkt_handle_queue */
						/* needs to be run. */
	mempool_t		rb_pool;	/* mempool for pkt_rb_node allocations */

	struct packet_iosched   iosched;
	struct gendisk		*disk;

	int			write_congestion_off;
	int			write_congestion_on;

	struct device		*dev;		/* sysfs pktcdvd[0-7] dev */

	struct dentry		*dfs_d_root;	/* debugfs: devname directory */
	struct dentry		*dfs_f_info;	/* debugfs: info file */
};

#endif /* __PKTCDVD_H */
