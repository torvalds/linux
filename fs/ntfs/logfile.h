/*
 * logfile.h - Defines for NTFS kernel journal ($LogFile) handling.  Part of
 *	       the Linux-NTFS project.
 *
 * Copyright (c) 2000-2005 Anton Altaparmakov
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_NTFS_LOGFILE_H
#define _LINUX_NTFS_LOGFILE_H

#ifdef NTFS_RW

#include <linux/fs.h>

#include "types.h"
#include "endian.h"
#include "layout.h"

/*
 * Journal ($LogFile) organization:
 *
 * Two restart areas present in the first two pages (restart pages, one restart
 * area in each page).  When the volume is dismounted they should be identical,
 * except for the update sequence array which usually has a different update
 * sequence number.
 *
 * These are followed by log records organized in pages headed by a log record
 * header going up to log file size.  Not all pages contain log records when a
 * volume is first formatted, but as the volume ages, all records will be used.
 * When the log file fills up, the records at the beginning are purged (by
 * modifying the oldest_lsn to a higher value presumably) and writing begins
 * at the beginning of the file.  Effectively, the log file is viewed as a
 * circular entity.
 *
 * NOTE: Windows NT, 2000, and XP all use log file version 1.1 but they accept
 * versions <= 1.x, including 0.-1.  (Yes, that is a minus one in there!)  We
 * probably only want to support 1.1 as this seems to be the current version
 * and we don't know how that differs from the older versions.  The only
 * exception is if the journal is clean as marked by the two restart pages
 * then it doesn't matter whether we are on an earlier version.  We can just
 * reinitialize the logfile and start again with version 1.1.
 */

/* Some $LogFile related constants. */
#define MaxLogFileSize		0x100000000ULL
#define DefaultLogPageSize	4096
#define MinLogRecordPages	48

/*
 * Log file restart page header (begins the restart area).
 */
typedef struct {
/*Ofs*/
/*  0	NTFS_RECORD; -- Unfolded here as gcc doesn't like unnamed structs. */
/*  0*/	NTFS_RECORD_TYPE magic;	/* The magic is "RSTR". */
/*  4*/	le16 usa_ofs;		/* See NTFS_RECORD definition in layout.h.
				   When creating, set this to be immediately
				   after this header structure (without any
				   alignment). */
/*  6*/	le16 usa_count;		/* See NTFS_RECORD definition in layout.h. */

/*  8*/	leLSN chkdsk_lsn;	/* The last log file sequence number found by
				   chkdsk.  Only used when the magic is changed
				   to "CHKD".  Otherwise this is zero. */
/* 16*/	le32 system_page_size;	/* Byte size of system pages when the log file
				   was created, has to be >= 512 and a power of
				   2.  Use this to calculate the required size
				   of the usa (usa_count) and add it to usa_ofs.
				   Then verify that the result is less than the
				   value of the restart_area_offset. */
/* 20*/	le32 log_page_size;	/* Byte size of log file pages, has to be >=
				   512 and a power of 2.  The default is 4096
				   and is used when the system page size is
				   between 4096 and 8192.  Otherwise this is
				   set to the system page size instead. */
/* 24*/	le16 restart_area_offset;/* Byte offset from the start of this header to
				   the RESTART_AREA.  Value has to be aligned
				   to 8-byte boundary.  When creating, set this
				   to be after the usa. */
/* 26*/	sle16 minor_ver;	/* Log file minor version.  Only check if major
				   version is 1. */
/* 28*/	sle16 major_ver;	/* Log file major version.  We only support
				   version 1.1. */
/* sizeof() = 30 (0x1e) bytes */
} __attribute__ ((__packed__)) RESTART_PAGE_HEADER;

/*
 * Constant for the log client indices meaning that there are no client records
 * in this particular client array.  Also inside the client records themselves,
 * this means that there are no client records preceding or following this one.
 */
#define LOGFILE_NO_CLIENT	cpu_to_le16(0xffff)
#define LOGFILE_NO_CLIENT_CPU	0xffff

/*
 * These are the so far known RESTART_AREA_* flags (16-bit) which contain
 * information about the log file in which they are present.
 */
enum {
	RESTART_VOLUME_IS_CLEAN	= cpu_to_le16(0x0002),
	RESTART_SPACE_FILLER	= cpu_to_le16(0xffff), /* gcc: Force enum bit width to 16. */
} __attribute__ ((__packed__));

typedef le16 RESTART_AREA_FLAGS;

/*
 * Log file restart area record.  The offset of this record is found by adding
 * the offset of the RESTART_PAGE_HEADER to the restart_area_offset value found
 * in it.  See notes at restart_area_offset above.
 */
typedef struct {
/*Ofs*/
/*  0*/	leLSN current_lsn;	/* The current, i.e. last LSN inside the log
				   when the restart area was last written.
				   This happens often but what is the interval?
				   Is it just fixed time or is it every time a
				   check point is written or somethine else?
				   On create set to 0. */
/*  8*/	le16 log_clients;	/* Number of log client records in the array of
				   log client records which follows this
				   restart area.  Must be 1.  */
/* 10*/	le16 client_free_list;	/* The index of the first free log client record
				   in the array of log client records.
				   LOGFILE_NO_CLIENT means that there are no
				   free log client records in the array.
				   If != LOGFILE_NO_CLIENT, check that
				   log_clients > client_free_list.  On Win2k
				   and presumably earlier, on a clean volume
				   this is != LOGFILE_NO_CLIENT, and it should
				   be 0, i.e. the first (and only) client
				   record is free and thus the logfile is
				   closed and hence clean.  A dirty volume
				   would have left the logfile open and hence
				   this would be LOGFILE_NO_CLIENT.  On WinXP
				   and presumably later, the logfile is always
				   open, even on clean shutdown so this should
				   always be LOGFILE_NO_CLIENT. */
/* 12*/	le16 client_in_use_list;/* The index of the first in-use log client
				   record in the array of log client records.
				   LOGFILE_NO_CLIENT means that there are no
				   in-use log client records in the array.  If
				   != LOGFILE_NO_CLIENT check that log_clients
				   > client_in_use_list.  On Win2k and
				   presumably earlier, on a clean volume this
				   is LOGFILE_NO_CLIENT, i.e. there are no
				   client records in use and thus the logfile
				   is closed and hence clean.  A dirty volume
				   would have left the logfile open and hence
				   this would be != LOGFILE_NO_CLIENT, and it
				   should be 0, i.e. the first (and only)
				   client record is in use.  On WinXP and
				   presumably later, the logfile is always
				   open, even on clean shutdown so this should
				   always be 0. */
/* 14*/	RESTART_AREA_FLAGS flags;/* Flags modifying LFS behaviour.  On Win2k
				   and presumably earlier this is always 0.  On
				   WinXP and presumably later, if the logfile
				   was shutdown cleanly, the second bit,
				   RESTART_VOLUME_IS_CLEAN, is set.  This bit
				   is cleared when the volume is mounted by
				   WinXP and set when the volume is dismounted,
				   thus if the logfile is dirty, this bit is
				   clear.  Thus we don't need to check the
				   Windows version to determine if the logfile
				   is clean.  Instead if the logfile is closed,
				   we know it must be clean.  If it is open and
				   this bit is set, we also know it must be
				   clean.  If on the other hand the logfile is
				   open and this bit is clear, we can be almost
				   certain that the logfile is dirty. */
/* 16*/	le32 seq_number_bits;	/* How many bits to use for the sequence
				   number.  This is calculated as 67 - the
				   number of bits required to store the logfile
				   size in bytes and this can be used in with
				   the specified file_size as a consistency
				   check. */
/* 20*/	le16 restart_area_length;/* Length of the restart area including the
				   client array.  Following checks required if
				   version matches.  Otherwise, skip them.
				   restart_area_offset + restart_area_length
				   has to be <= system_page_size.  Also,
				   restart_area_length has to be >=
				   client_array_offset + (log_clients *
				   sizeof(log client record)). */
/* 22*/	le16 client_array_offset;/* Offset from the start of this record to
				   the first log client record if versions are
				   matched.  When creating, set this to be
				   after this restart area structure, aligned
				   to 8-bytes boundary.  If the versions do not
				   match, this is ignored and the offset is
				   assumed to be (sizeof(RESTART_AREA) + 7) &
				   ~7, i.e. rounded up to first 8-byte
				   boundary.  Either way, client_array_offset
				   has to be aligned to an 8-byte boundary.
				   Also, restart_area_offset +
				   client_array_offset has to be <= 510.
				   Finally, client_array_offset + (log_clients
				   * sizeof(log client record)) has to be <=
				   system_page_size.  On Win2k and presumably
				   earlier, this is 0x30, i.e. immediately
				   following this record.  On WinXP and
				   presumably later, this is 0x40, i.e. there
				   are 16 extra bytes between this record and
				   the client array.  This probably means that
				   the RESTART_AREA record is actually bigger
				   in WinXP and later. */
/* 24*/	sle64 file_size;	/* Usable byte size of the log file.  If the
				   restart_area_offset + the offset of the
				   file_size are > 510 then corruption has
				   occured.  This is the very first check when
				   starting with the restart_area as if it
				   fails it means that some of the above values
				   will be corrupted by the multi sector
				   transfer protection.  The file_size has to
				   be rounded down to be a multiple of the
				   log_page_size in the RESTART_PAGE_HEADER and
				   then it has to be at least big enough to
				   store the two restart pages and 48 (0x30)
				   log record pages. */
/* 32*/	le32 last_lsn_data_length;/* Length of data of last LSN, not including
				   the log record header.  On create set to
				   0. */
/* 36*/	le16 log_record_header_length;/* Byte size of the log record header.
				   If the version matches then check that the
				   value of log_record_header_length is a
				   multiple of 8, i.e.
				   (log_record_header_length + 7) & ~7 ==
				   log_record_header_length.  When creating set
				   it to sizeof(LOG_RECORD_HEADER), aligned to
				   8 bytes. */
/* 38*/	le16 log_page_data_offset;/* Offset to the start of data in a log record
				   page.  Must be a multiple of 8.  On create
				   set it to immediately after the update
				   sequence array of the log record page. */
/* 40*/	le32 restart_log_open_count;/* A counter that gets incremented every
				   time the logfile is restarted which happens
				   at mount time when the logfile is opened.
				   When creating set to a random value.  Win2k
				   sets it to the low 32 bits of the current
				   system time in NTFS format (see time.h). */
/* 44*/	le32 reserved;		/* Reserved/alignment to 8-byte boundary. */
/* sizeof() = 48 (0x30) bytes */
} __attribute__ ((__packed__)) RESTART_AREA;

/*
 * Log client record.  The offset of this record is found by adding the offset
 * of the RESTART_AREA to the client_array_offset value found in it.
 */
typedef struct {
/*Ofs*/
/*  0*/	leLSN oldest_lsn;	/* Oldest LSN needed by this client.  On create
				   set to 0. */
/*  8*/	leLSN client_restart_lsn;/* LSN at which this client needs to restart
				   the volume, i.e. the current position within
				   the log file.  At present, if clean this
				   should = current_lsn in restart area but it
				   probably also = current_lsn when dirty most
				   of the time.  At create set to 0. */
/* 16*/	le16 prev_client;	/* The offset to the previous log client record
				   in the array of log client records.
				   LOGFILE_NO_CLIENT means there is no previous
				   client record, i.e. this is the first one.
				   This is always LOGFILE_NO_CLIENT. */
/* 18*/	le16 next_client;	/* The offset to the next log client record in
				   the array of log client records.
				   LOGFILE_NO_CLIENT means there are no next
				   client records, i.e. this is the last one.
				   This is always LOGFILE_NO_CLIENT. */
/* 20*/	le16 seq_number;	/* On Win2k and presumably earlier, this is set
				   to zero every time the logfile is restarted
				   and it is incremented when the logfile is
				   closed at dismount time.  Thus it is 0 when
				   dirty and 1 when clean.  On WinXP and
				   presumably later, this is always 0. */
/* 22*/	u8 reserved[6];		/* Reserved/alignment. */
/* 28*/	le32 client_name_length;/* Length of client name in bytes.  Should
				   always be 8. */
/* 32*/	ntfschar client_name[64];/* Name of the client in Unicode.  Should
				   always be "NTFS" with the remaining bytes
				   set to 0. */
/* sizeof() = 160 (0xa0) bytes */
} __attribute__ ((__packed__)) LOG_CLIENT_RECORD;

extern bool ntfs_check_logfile(struct inode *log_vi,
		RESTART_PAGE_HEADER **rp);

extern bool ntfs_is_logfile_clean(struct inode *log_vi,
		const RESTART_PAGE_HEADER *rp);

extern bool ntfs_empty_logfile(struct inode *log_vi);

#endif /* NTFS_RW */

#endif /* _LINUX_NTFS_LOGFILE_H */
