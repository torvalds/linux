/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * Authors: Ken Merry           (Spectra Logic Corporation)
 *
 * $FreeBSD$
 */

#ifndef _SYS_DISK_ZONE_H_
#define _SYS_DISK_ZONE_H_

/*
 * Interface for Zone-based disks.  This allows managing devices that
 * conform to the SCSI Zoned Block Commands (ZBC) and ATA Zoned ATA Command
 * Set (ZAC) specifications.  Devices using these command sets are
 * currently (October 2015) hard drives using Shingled Magnetic Recording
 * (SMR).
 */

/*
 * There are currently three types of zoned devices:
 * 
 * Drive Managed:
 * Drive Managed drives look and act just like a standard random access 
 * block device, but underneath, the drive reads and writes the bulk of
 * its capacity using SMR zones.  Sequential writes will yield better
 * performance, but writing sequentially is not required.
 *
 * Host Aware:
 * Host Aware drives expose the underlying zone layout via SCSI or ATA
 * commands and allow the host to manage the zone conditions.  The host
 * is not required to manage the zones on the drive, though.  Sequential
 * writes will yield better performance in Sequential Write Preferred
 * zones, but the host can write randomly in those zones.
 * 
 * Host Managed:
 * Host Managed drives expose the underlying zone layout via SCSI or ATA
 * commands.  The host is required to access the zones according to the
 * rules described by the zone layout.  Any commands that violate the
 * rules will be returned with an error.
 */
struct disk_zone_disk_params {
	uint32_t zone_mode;
#define	DISK_ZONE_MODE_NONE		0x00
#define	DISK_ZONE_MODE_HOST_AWARE	0x01
#define	DISK_ZONE_MODE_DRIVE_MANAGED	0x02
#define	DISK_ZONE_MODE_HOST_MANAGED	0x04
	uint64_t flags;
#define	DISK_ZONE_DISK_URSWRZ		0x001
#define	DISK_ZONE_OPT_SEQ_SET		0x002
#define	DISK_ZONE_OPT_NONSEQ_SET	0x004
#define	DISK_ZONE_MAX_SEQ_SET		0x008
#define	DISK_ZONE_RZ_SUP		0x010
#define	DISK_ZONE_OPEN_SUP		0x020
#define	DISK_ZONE_CLOSE_SUP		0x040
#define	DISK_ZONE_FINISH_SUP		0x080
#define	DISK_ZONE_RWP_SUP		0x100
#define	DISK_ZONE_CMD_SUP_MASK		0x1f0
	uint64_t optimal_seq_zones;
	uint64_t optimal_nonseq_zones;
	uint64_t max_seq_zones;
};

/*
 * Used for reset write pointer, open, close and finish.
 */
struct disk_zone_rwp {
	uint64_t	id;
	uint8_t		flags;
#define	DISK_ZONE_RWP_FLAG_NONE	0x00
#define	DISK_ZONE_RWP_FLAG_ALL	0x01
};

/*
 * Report Zones header.  All of these values are passed out.
 */
struct disk_zone_rep_header {
	uint8_t		same;
#define	DISK_ZONE_SAME_ALL_DIFFERENT	0x0 /* Lengths and types vary */
#define	DISK_ZONE_SAME_ALL_SAME		0x1 /* Lengths and types the same */
#define	DISK_ZONE_SAME_LAST_DIFFERENT	0x2 /* Types same, last len varies */
#define	DISK_ZONE_SAME_TYPES_DIFFERENT	0x3 /* Types vary, length the same */
	uint64_t	maximum_lba;
	/*
	 * XXX KDM padding space may not be a good idea inside the bio.
	 */
	uint8_t		reserved[64];
};

/*
 * Report Zones entry.  Note that the zone types, conditions, and flags
 * are mapped directly from the SCSI/ATA flag values.  Any additional
 * SCSI/ATA zone types or conditions or flags that are defined in the
 * future could result in additional values that are not yet defined here.
 */
struct disk_zone_rep_entry {
	uint8_t		zone_type;
#define	DISK_ZONE_TYPE_CONVENTIONAL	0x01
#define	DISK_ZONE_TYPE_SEQ_REQUIRED	0x02 /* Host Managed */
#define	DISK_ZONE_TYPE_SEQ_PREFERRED	0x03 /* Host Aware */
	uint8_t		zone_condition;
#define	DISK_ZONE_COND_NOT_WP		0x00
#define	DISK_ZONE_COND_EMPTY		0x01
#define	DISK_ZONE_COND_IMPLICIT_OPEN	0x02
#define	DISK_ZONE_COND_EXPLICIT_OPEN	0x03
#define	DISK_ZONE_COND_CLOSED		0x04
#define	DISK_ZONE_COND_READONLY		0x0D
#define	DISK_ZONE_COND_FULL		0x0E
#define	DISK_ZONE_COND_OFFLINE		0x0F
	uint8_t		zone_flags;
#define	DISK_ZONE_FLAG_RESET		0x01 /* Zone needs RWP */
#define	DISK_ZONE_FLAG_NON_SEQ		0x02 /* Zone accssessed nonseq */
	uint64_t	zone_length;
	uint64_t	zone_start_lba;
	uint64_t	write_pointer_lba;
	/* XXX KDM padding space may not be a good idea inside the bio */
	uint8_t		reserved[32];
};

struct disk_zone_report {
	uint64_t 			starting_id;      /* Passed In */
	uint8_t				rep_options;      /* Passed In */
#define	DISK_ZONE_REP_ALL	0x00
#define	DISK_ZONE_REP_EMPTY	0x01
#define	DISK_ZONE_REP_IMP_OPEN	0x02
#define	DISK_ZONE_REP_EXP_OPEN	0x03
#define	DISK_ZONE_REP_CLOSED	0x04
#define	DISK_ZONE_REP_FULL	0x05
#define	DISK_ZONE_REP_READONLY	0x06
#define	DISK_ZONE_REP_OFFLINE	0x07
#define	DISK_ZONE_REP_RWP	0x10
#define	DISK_ZONE_REP_NON_SEQ	0x11
#define	DISK_ZONE_REP_NON_WP	0x3F
	struct disk_zone_rep_header	header;
	uint32_t			entries_allocated; /* Passed In */
	uint32_t			entries_filled;    /* Passed Out */
	uint32_t			entries_available; /* Passed Out */
	struct disk_zone_rep_entry	*entries;
};

union disk_zone_params {
	struct disk_zone_disk_params	disk_params;
	struct disk_zone_rwp		rwp;
	struct disk_zone_report		report;
};

struct disk_zone_args {
	uint8_t 		zone_cmd;
#define	DISK_ZONE_OPEN		0x00
#define	DISK_ZONE_CLOSE		0x01
#define	DISK_ZONE_FINISH	0x02
#define	DISK_ZONE_REPORT_ZONES	0x03
#define	DISK_ZONE_RWP		0x04
#define	DISK_ZONE_GET_PARAMS	0x05
	union disk_zone_params	zone_params;
};

#endif /* _SYS_DISK_ZONE_H_ */
