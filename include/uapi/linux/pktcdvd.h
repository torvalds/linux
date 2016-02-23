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
#ifndef _UAPI__PKTCDVD_H
#define _UAPI__PKTCDVD_H

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
 * able to successfully recover with this option (drive will return good
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


#endif /* _UAPI__PKTCDVD_H */
