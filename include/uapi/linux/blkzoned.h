/*
 * Zoned block devices handling.
 *
 * Copyright (C) 2015 Seagate Technology PLC
 *
 * Written by: Shaun Tancheff <shaun.tancheff@seagate.com>
 *
 * Modified by: Damien Le Moal <damien.lemoal@hgst.com>
 * Copyright (C) 2016 Western Digital
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef _UAPI_BLKZONED_H
#define _UAPI_BLKZONED_H

#include <linux/types.h>
#include <linux/ioctl.h>

/**
 * enum blk_zone_type - Types of zones allowed in a zoned device.
 *
 * @BLK_ZONE_TYPE_CONVENTIONAL: The zone has no write pointer and can be writen
 *                              randomly. Zone reset has no effect on the zone.
 * @BLK_ZONE_TYPE_SEQWRITE_REQ: The zone must be written sequentially
 * @BLK_ZONE_TYPE_SEQWRITE_PREF: The zone can be written non-sequentially
 *
 * Any other value not defined is reserved and must be considered as invalid.
 */
enum blk_zone_type {
	BLK_ZONE_TYPE_CONVENTIONAL	= 0x1,
	BLK_ZONE_TYPE_SEQWRITE_REQ	= 0x2,
	BLK_ZONE_TYPE_SEQWRITE_PREF	= 0x3,
};

/**
 * enum blk_zone_cond - Condition [state] of a zone in a zoned device.
 *
 * @BLK_ZONE_COND_NOT_WP: The zone has no write pointer, it is conventional.
 * @BLK_ZONE_COND_EMPTY: The zone is empty.
 * @BLK_ZONE_COND_IMP_OPEN: The zone is open, but not explicitly opened.
 * @BLK_ZONE_COND_EXP_OPEN: The zones was explicitly opened by an
 *                          OPEN ZONE command.
 * @BLK_ZONE_COND_CLOSED: The zone was [explicitly] closed after writing.
 * @BLK_ZONE_COND_FULL: The zone is marked as full, possibly by a zone
 *                      FINISH ZONE command.
 * @BLK_ZONE_COND_READONLY: The zone is read-only.
 * @BLK_ZONE_COND_OFFLINE: The zone is offline (sectors cannot be read/written).
 *
 * The Zone Condition state machine in the ZBC/ZAC standards maps the above
 * deinitions as:
 *   - ZC1: Empty         | BLK_ZONE_EMPTY
 *   - ZC2: Implicit Open | BLK_ZONE_COND_IMP_OPEN
 *   - ZC3: Explicit Open | BLK_ZONE_COND_EXP_OPEN
 *   - ZC4: Closed        | BLK_ZONE_CLOSED
 *   - ZC5: Full          | BLK_ZONE_FULL
 *   - ZC6: Read Only     | BLK_ZONE_READONLY
 *   - ZC7: Offline       | BLK_ZONE_OFFLINE
 *
 * Conditions 0x5 to 0xC are reserved by the current ZBC/ZAC spec and should
 * be considered invalid.
 */
enum blk_zone_cond {
	BLK_ZONE_COND_NOT_WP	= 0x0,
	BLK_ZONE_COND_EMPTY	= 0x1,
	BLK_ZONE_COND_IMP_OPEN	= 0x2,
	BLK_ZONE_COND_EXP_OPEN	= 0x3,
	BLK_ZONE_COND_CLOSED	= 0x4,
	BLK_ZONE_COND_READONLY	= 0xD,
	BLK_ZONE_COND_FULL	= 0xE,
	BLK_ZONE_COND_OFFLINE	= 0xF,
};

/**
 * struct blk_zone - Zone descriptor for BLKREPORTZONE ioctl.
 *
 * @start: Zone start in 512 B sector units
 * @len: Zone length in 512 B sector units
 * @wp: Zone write pointer location in 512 B sector units
 * @type: see enum blk_zone_type for possible values
 * @cond: see enum blk_zone_cond for possible values
 * @non_seq: Flag indicating that the zone is using non-sequential resources
 *           (for host-aware zoned block devices only).
 * @reset: Flag indicating that a zone reset is recommended.
 * @reserved: Padding to 64 B to match the ZBC/ZAC defined zone descriptor size.
 *
 * start, len and wp use the regular 512 B sector unit, regardless of the
 * device logical block size. The overall structure size is 64 B to match the
 * ZBC/ZAC defined zone descriptor and allow support for future additional
 * zone information.
 */
struct blk_zone {
	__u64	start;		/* Zone start sector */
	__u64	len;		/* Zone length in number of sectors */
	__u64	wp;		/* Zone write pointer position */
	__u8	type;		/* Zone type */
	__u8	cond;		/* Zone condition */
	__u8	non_seq;	/* Non-sequential write resources active */
	__u8	reset;		/* Reset write pointer recommended */
	__u8	reserved[36];
};

/**
 * struct blk_zone_report - BLKREPORTZONE ioctl request/reply
 *
 * @sector: starting sector of report
 * @nr_zones: IN maximum / OUT actual
 * @reserved: padding to 16 byte alignment
 * @zones: Space to hold @nr_zones @zones entries on reply.
 *
 * The array of at most @nr_zones must follow this structure in memory.
 */
struct blk_zone_report {
	__u64		sector;
	__u32		nr_zones;
	__u8		reserved[4];
	struct blk_zone zones[0];
} __packed;

/**
 * struct blk_zone_range - BLKRESETZONE ioctl request
 * @sector: starting sector of the first zone to issue reset write pointer
 * @nr_sectors: Total number of sectors of 1 or more zones to reset
 */
struct blk_zone_range {
	__u64		sector;
	__u64		nr_sectors;
};

/**
 * Zoned block device ioctl's:
 *
 * @BLKREPORTZONE: Get zone information. Takes a zone report as argument.
 *                 The zone report will start from the zone containing the
 *                 sector specified in the report request structure.
 * @BLKRESETZONE: Reset the write pointer of the zones in the specified
 *                sector range. The sector range must be zone aligned.
 */
#define BLKREPORTZONE	_IOWR(0x12, 130, struct blk_zone_report)
#define BLKRESETZONE	_IOW(0x12, 131, struct blk_zone_range)

#endif /* _UAPI_BLKZONED_H */
