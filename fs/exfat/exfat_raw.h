/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#ifndef _EXFAT_RAW_H
#define _EXFAT_RAW_H

#include <linux/types.h>

#define PBR_SIGNATURE		0xAA55

#define EXFAT_MAX_FILE_LEN	255

#define VOL_CLEAN		0x0000
#define VOL_DIRTY		0x0002

#define EXFAT_EOF_CLUSTER	0xFFFFFFFFu
#define EXFAT_BAD_CLUSTER	0xFFFFFFF7u
#define EXFAT_FREE_CLUSTER	0
/* Cluster 0, 1 are reserved, the first cluster is 2 in the cluster heap. */
#define EXFAT_RESERVED_CLUSTERS	2
#define EXFAT_FIRST_CLUSTER	2
#define EXFAT_DATA_CLUSTER_COUNT(sbi)	\
	((sbi)->num_clusters - EXFAT_RESERVED_CLUSTERS)

/* AllocationPossible and NoFatChain field in GeneralSecondaryFlags Field */
#define ALLOC_FAT_CHAIN		0x01
#define ALLOC_NO_FAT_CHAIN	0x03

#define DENTRY_SIZE		32 /* directory entry size */
#define DENTRY_SIZE_BITS	5
/* exFAT allows 8388608(256MB) directory entries */
#define MAX_EXFAT_DENTRIES	8388608

/* dentry types */
#define EXFAT_UNUSED		0x00	/* end of directory */
#define EXFAT_DELETE		(~0x80)
#define IS_EXFAT_DELETED(x)	((x) < 0x80) /* deleted file (0x01~0x7F) */
#define EXFAT_INVAL		0x80	/* invalid value */
#define EXFAT_BITMAP		0x81	/* allocation bitmap */
#define EXFAT_UPCASE		0x82	/* upcase table */
#define EXFAT_VOLUME		0x83	/* volume label */
#define EXFAT_FILE		0x85	/* file or dir */
#define EXFAT_GUID		0xA0
#define EXFAT_PADDING		0xA1
#define EXFAT_ACLTAB		0xA2
#define EXFAT_STREAM		0xC0	/* stream entry */
#define EXFAT_NAME		0xC1	/* file name entry */
#define EXFAT_ACL		0xC2	/* stream entry */

#define IS_EXFAT_CRITICAL_PRI(x)	(x < 0xA0)
#define IS_EXFAT_BENIGN_PRI(x)		(x < 0xC0)
#define IS_EXFAT_CRITICAL_SEC(x)	(x < 0xE0)

/* checksum types */
#define CS_DIR_ENTRY		0
#define CS_PBR_SECTOR		1
#define CS_DEFAULT		2

/* file attributes */
#define ATTR_READONLY		0x0001
#define ATTR_HIDDEN		0x0002
#define ATTR_SYSTEM		0x0004
#define ATTR_VOLUME		0x0008
#define ATTR_SUBDIR		0x0010
#define ATTR_ARCHIVE		0x0020

#define ATTR_RWMASK		(ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME | \
				 ATTR_SUBDIR | ATTR_ARCHIVE)

#define PBR64_JUMP_BOOT_LEN		3
#define PBR64_OEM_NAME_LEN		8
#define PBR64_RESERVED_LEN		53

#define EXFAT_FILE_NAME_LEN		15

/* EXFAT BIOS parameter block (64 bytes) */
struct bpb64 {
	__u8 jmp_boot[PBR64_JUMP_BOOT_LEN];
	__u8 oem_name[PBR64_OEM_NAME_LEN];
	__u8 res_zero[PBR64_RESERVED_LEN];
} __packed;

/* EXFAT EXTEND BIOS parameter block (56 bytes) */
struct bsx64 {
	__le64 vol_offset;
	__le64 vol_length;
	__le32 fat_offset;
	__le32 fat_length;
	__le32 clu_offset;
	__le32 clu_count;
	__le32 root_cluster;
	__le32 vol_serial;
	__u8 fs_version[2];
	__le16 vol_flags;
	__u8 sect_size_bits;
	__u8 sect_per_clus_bits;
	__u8 num_fats;
	__u8 phy_drv_no;
	__u8 perc_in_use;
	__u8 reserved2[7];
} __packed;

/* EXFAT PBR[BPB+BSX] (120 bytes) */
struct pbr64 {
	struct bpb64 bpb;
	struct bsx64 bsx;
} __packed;

/* Common PBR[Partition Boot Record] (512 bytes) */
struct pbr {
	union {
		__u8 raw[64];
		struct bpb64 f64;
	} bpb;
	union {
		__u8 raw[56];
		struct bsx64 f64;
	} bsx;
	__u8 boot_code[390];
	__le16 signature;
} __packed;

struct exfat_dentry {
	__u8 type;
	union {
		struct {
			__u8 num_ext;
			__le16 checksum;
			__le16 attr;
			__le16 reserved1;
			__le16 create_time;
			__le16 create_date;
			__le16 modify_time;
			__le16 modify_date;
			__le16 access_time;
			__le16 access_date;
			__u8 create_time_ms;
			__u8 modify_time_ms;
			__u8 create_tz;
			__u8 modify_tz;
			__u8 access_tz;
			__u8 reserved2[7];
		} __packed file; /* file directory entry */
		struct {
			__u8 flags;
			__u8 reserved1;
			__u8 name_len;
			__le16 name_hash;
			__le16 reserved2;
			__le64 valid_size;
			__le32 reserved3;
			__le32 start_clu;
			__le64 size;
		} __packed stream; /* stream extension directory entry */
		struct {
			__u8 flags;
			__le16 unicode_0_14[EXFAT_FILE_NAME_LEN];
		} __packed name; /* file name directory entry */
		struct {
			__u8 flags;
			__u8 reserved[18];
			__le32 start_clu;
			__le64 size;
		} __packed bitmap; /* allocation bitmap directory entry */
		struct {
			__u8 reserved1[3];
			__le32 checksum;
			__u8 reserved2[12];
			__le32 start_clu;
			__le64 size;
		} __packed upcase; /* up-case table directory entry */
	} __packed dentry;
} __packed;

#define EXFAT_TZ_VALID		(1 << 7)

/* Jan 1 GMT 00:00:00 1980 */
#define EXFAT_MIN_TIMESTAMP_SECS    315532800LL
/* Dec 31 GMT 23:59:59 2107 */
#define EXFAT_MAX_TIMESTAMP_SECS    4354819199LL

#endif /* !_EXFAT_RAW_H */
