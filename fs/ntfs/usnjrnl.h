/*
 * usnjrnl.h - Defines for NTFS kernel transaction log ($UsnJrnl) handling.
 *	       Part of the Linux-NTFS project.
 *
 * Copyright (c) 2005 Anton Altaparmakov
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

#ifndef _LINUX_NTFS_USNJRNL_H
#define _LINUX_NTFS_USNJRNL_H

#ifdef NTFS_RW

#include "types.h"
#include "endian.h"
#include "layout.h"
#include "volume.h"

/*
 * Transaction log ($UsnJrnl) organization:
 *
 * The transaction log records whenever a file is modified in any way.  So for
 * example it will record that file "blah" was written to at a particular time
 * but not what was written.  If will record that a file was deleted or
 * created, that a file was truncated, etc.  See below for all the reason
 * codes used.
 *
 * The transaction log is in the $Extend directory which is in the root
 * directory of each volume.  If it is not present it means transaction
 * logging is disabled.  If it is present it means transaction logging is
 * either enabled or in the process of being disabled in which case we can
 * ignore it as it will go away as soon as Windows gets its hands on it.
 *
 * To determine whether the transaction logging is enabled or in the process
 * of being disabled, need to check the volume flags in the
 * $VOLUME_INFORMATION attribute in the $Volume system file (which is present
 * in the root directory and has a fixed mft record number, see layout.h).
 * If the flag VOLUME_DELETE_USN_UNDERWAY is set it means the transaction log
 * is in the process of being disabled and if this flag is clear it means the
 * transaction log is enabled.
 *
 * The transaction log consists of two parts; the $DATA/$Max attribute as well
 * as the $DATA/$J attribute.  $Max is a header describing the transaction
 * log whilst $J is the transaction log data itself as a sequence of variable
 * sized USN_RECORDs (see below for all the structures).
 *
 * We do not care about transaction logging at this point in time but we still
 * need to let windows know that the transaction log is out of date.  To do
 * this we need to stamp the transaction log.  This involves setting the
 * lowest_valid_usn field in the $DATA/$Max attribute to the usn to be used
 * for the next added USN_RECORD to the $DATA/$J attribute as well as
 * generating a new journal_id in $DATA/$Max.
 *
 * The journal_id is as of the current version (2.0) of the transaction log
 * simply the 64-bit timestamp of when the journal was either created or last
 * stamped.
 *
 * To determine the next usn there are two ways.  The first is to parse
 * $DATA/$J and to find the last USN_RECORD in it and to add its record_length
 * to its usn (which is the byte offset in the $DATA/$J attribute).  The
 * second is simply to take the data size of the attribute.  Since the usns
 * are simply byte offsets into $DATA/$J, this is exactly the next usn.  For
 * obvious reasons we use the second method as it is much simpler and faster.
 *
 * As an aside, note that to actually disable the transaction log, one would
 * need to set the VOLUME_DELETE_USN_UNDERWAY flag (see above), then go
 * through all the mft records on the volume and set the usn field in their
 * $STANDARD_INFORMATION attribute to zero.  Once that is done, one would need
 * to delete the transaction log file, i.e. \$Extent\$UsnJrnl, and finally,
 * one would need to clear the VOLUME_DELETE_USN_UNDERWAY flag.
 *
 * Note that if a volume is unmounted whilst the transaction log is being
 * disabled, the process will continue the next time the volume is mounted.
 * This is why we can safely mount read-write when we see a transaction log
 * in the process of being deleted.
 */

/* Some $UsnJrnl related constants. */
#define UsnJrnlMajorVer		2
#define UsnJrnlMinorVer		0

/*
 * $DATA/$Max attribute.  This is (always?) resident and has a fixed size of
 * 32 bytes.  It contains the header describing the transaction log.
 */
typedef struct {
/*Ofs*/
/*   0*/sle64 maximum_size;	/* The maximum on-disk size of the $DATA/$J
				   attribute. */
/*   8*/sle64 allocation_delta;	/* Number of bytes by which to increase the
				   size of the $DATA/$J attribute. */
/*0x10*/sle64 journal_id;	/* Current id of the transaction log. */
/*0x18*/leUSN lowest_valid_usn;	/* Lowest valid usn in $DATA/$J for the
				   current journal_id. */
/* sizeof() = 32 (0x20) bytes */
} __attribute__ ((__packed__)) USN_HEADER;

/*
 * Reason flags (32-bit).  Cumulative flags describing the change(s) to the
 * file since it was last opened.  I think the names speak for themselves but
 * if you disagree check out the descriptions in the Linux NTFS project NTFS
 * documentation: http://www.linux-ntfs.org/
 */
enum {
	USN_REASON_DATA_OVERWRITE	= cpu_to_le32(0x00000001),
	USN_REASON_DATA_EXTEND		= cpu_to_le32(0x00000002),
	USN_REASON_DATA_TRUNCATION	= cpu_to_le32(0x00000004),
	USN_REASON_NAMED_DATA_OVERWRITE	= cpu_to_le32(0x00000010),
	USN_REASON_NAMED_DATA_EXTEND	= cpu_to_le32(0x00000020),
	USN_REASON_NAMED_DATA_TRUNCATION= cpu_to_le32(0x00000040),
	USN_REASON_FILE_CREATE		= cpu_to_le32(0x00000100),
	USN_REASON_FILE_DELETE		= cpu_to_le32(0x00000200),
	USN_REASON_EA_CHANGE		= cpu_to_le32(0x00000400),
	USN_REASON_SECURITY_CHANGE	= cpu_to_le32(0x00000800),
	USN_REASON_RENAME_OLD_NAME	= cpu_to_le32(0x00001000),
	USN_REASON_RENAME_NEW_NAME	= cpu_to_le32(0x00002000),
	USN_REASON_INDEXABLE_CHANGE	= cpu_to_le32(0x00004000),
	USN_REASON_BASIC_INFO_CHANGE	= cpu_to_le32(0x00008000),
	USN_REASON_HARD_LINK_CHANGE	= cpu_to_le32(0x00010000),
	USN_REASON_COMPRESSION_CHANGE	= cpu_to_le32(0x00020000),
	USN_REASON_ENCRYPTION_CHANGE	= cpu_to_le32(0x00040000),
	USN_REASON_OBJECT_ID_CHANGE	= cpu_to_le32(0x00080000),
	USN_REASON_REPARSE_POINT_CHANGE	= cpu_to_le32(0x00100000),
	USN_REASON_STREAM_CHANGE	= cpu_to_le32(0x00200000),
	USN_REASON_CLOSE		= cpu_to_le32(0x80000000),
};

typedef le32 USN_REASON_FLAGS;

/*
 * Source info flags (32-bit).  Information about the source of the change(s)
 * to the file.  For detailed descriptions of what these mean, see the Linux
 * NTFS project NTFS documentation:
 *	http://www.linux-ntfs.org/
 */
enum {
	USN_SOURCE_DATA_MANAGEMENT	  = cpu_to_le32(0x00000001),
	USN_SOURCE_AUXILIARY_DATA	  = cpu_to_le32(0x00000002),
	USN_SOURCE_REPLICATION_MANAGEMENT = cpu_to_le32(0x00000004),
};

typedef le32 USN_SOURCE_INFO_FLAGS;

/*
 * $DATA/$J attribute.  This is always non-resident, is marked as sparse, and
 * is of variabled size.  It consists of a sequence of variable size
 * USN_RECORDS.  The minimum allocated_size is allocation_delta as
 * specified in $DATA/$Max.  When the maximum_size specified in $DATA/$Max is
 * exceeded by more than allocation_delta bytes, allocation_delta bytes are
 * allocated and appended to the $DATA/$J attribute and an equal number of
 * bytes at the beginning of the attribute are freed and made sparse.  Note the
 * making sparse only happens at volume checkpoints and hence the actual
 * $DATA/$J size can exceed maximum_size + allocation_delta temporarily.
 */
typedef struct {
/*Ofs*/
/*   0*/le32 length;		/* Byte size of this record (8-byte
				   aligned). */
/*   4*/le16 major_ver;		/* Major version of the transaction log used
				   for this record. */
/*   6*/le16 minor_ver;		/* Minor version of the transaction log used
				   for this record. */
/*   8*/leMFT_REF mft_reference;/* The mft reference of the file (or
				   directory) described by this record. */
/*0x10*/leMFT_REF parent_directory;/* The mft reference of the parent
				   directory of the file described by this
				   record. */
/*0x18*/leUSN usn;		/* The usn of this record.  Equals the offset
				   within the $DATA/$J attribute. */
/*0x20*/sle64 time;		/* Time when this record was created. */
/*0x28*/USN_REASON_FLAGS reason;/* Reason flags (see above). */
/*0x2c*/USN_SOURCE_INFO_FLAGS source_info;/* Source info flags (see above). */
/*0x30*/le32 security_id;	/* File security_id copied from
				   $STANDARD_INFORMATION. */
/*0x34*/FILE_ATTR_FLAGS file_attributes;	/* File attributes copied from
				   $STANDARD_INFORMATION or $FILE_NAME (not
				   sure which). */
/*0x38*/le16 file_name_size;	/* Size of the file name in bytes. */
/*0x3a*/le16 file_name_offset;	/* Offset to the file name in bytes from the
				   start of this record. */
/*0x3c*/ntfschar file_name[0];	/* Use when creating only.  When reading use
				   file_name_offset to determine the location
				   of the name. */
/* sizeof() = 60 (0x3c) bytes */
} __attribute__ ((__packed__)) USN_RECORD;

extern bool ntfs_stamp_usnjrnl(ntfs_volume *vol);

#endif /* NTFS_RW */

#endif /* _LINUX_NTFS_USNJRNL_H */
