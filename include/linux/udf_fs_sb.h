/*
 * udf_fs_sb.h
 * 
 * This include file is for the Linux kernel/module.
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hpesjro.fc.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 */

#ifndef _UDF_FS_SB_H
#define _UDF_FS_SB_H 1

#include <asm/semaphore.h>

#pragma pack(1)

#define UDF_MAX_BLOCK_LOADED	8

#define UDF_TYPE1_MAP15			0x1511U
#define UDF_VIRTUAL_MAP15		0x1512U
#define UDF_VIRTUAL_MAP20		0x2012U
#define UDF_SPARABLE_MAP15		0x1522U

struct udf_sparing_data
{
	__u16	s_packet_len;
	struct buffer_head *s_spar_map[4];
};

struct udf_virtual_data
{
	__u32	s_num_entries;
	__u16	s_start_offset;
};

struct udf_bitmap
{
	__u32			s_extLength;
	__u32			s_extPosition;
	__u16			s_nr_groups;
	struct buffer_head 	**s_block_bitmap;
};

struct udf_part_map
{
	union
	{
		struct udf_bitmap	*s_bitmap;
		struct inode		*s_table;
	} s_uspace;
	union
	{
		struct udf_bitmap	*s_bitmap;
		struct inode		*s_table;
	} s_fspace;
	__u32	s_partition_root;
	__u32	s_partition_len;
	__u16	s_partition_type;
	__u16	s_partition_num;
	union
	{
		struct udf_sparing_data s_sparing;
		struct udf_virtual_data s_virtual;
	} s_type_specific;
	__u32	(*s_partition_func)(struct super_block *, __u32, __u16, __u32);
	__u16	s_volumeseqnum;
	__u16	s_partition_flags;
};

#pragma pack()

struct udf_sb_info
{
	struct udf_part_map	*s_partmaps;
	__u8			s_volident[32];

	/* Overall info */
	__u16			s_partitions;
	__u16			s_partition;

	/* Sector headers */
	__s32			s_session;
	__u32			s_anchor[4];
	__u32			s_lastblock;

	struct buffer_head	*s_lvidbh;

	/* Default permissions */
	mode_t			s_umask;
	gid_t			s_gid;
	uid_t			s_uid;

	/* Root Info */
	struct timespec		s_recordtime;

	/* Fileset Info */
	__u16			s_serialnum;

	/* highest UDF revision we have recorded to this media */
	__u16			s_udfrev;

	/* Miscellaneous flags */
	__u32			s_flags;

	/* Encoding info */
	struct nls_table	*s_nls_map;

	/* VAT inode */
	struct inode		*s_vat;

	struct semaphore	s_alloc_sem;
};

#endif /* _UDF_FS_SB_H */
