/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ISOFS_FS_H
#define _ISOFS_FS_H

#include <linux/types.h>
#include <linux/magic.h>

/*
 * The isofs filesystem constants/structures
 */

/* This part borrowed from the bsd386 isofs */
#define ISODCL(from, to) (to - from + 1)

struct iso_volume_descriptor {
	__u8 type[ISODCL(1,1)]; /* 711 */
	char id[ISODCL(2,6)];
	__u8 version[ISODCL(7,7)];
	__u8 data[ISODCL(8,2048)];
};

/* volume descriptor types */
#define ISO_VD_PRIMARY 1
#define ISO_VD_SUPPLEMENTARY 2
#define ISO_VD_END 255

#define ISO_STANDARD_ID "CD001"

struct iso_primary_descriptor {
	__u8 type			[ISODCL (  1,   1)]; /* 711 */
	char id				[ISODCL (  2,   6)];
	__u8 version			[ISODCL (  7,   7)]; /* 711 */
	__u8 unused1			[ISODCL (  8,   8)];
	char system_id			[ISODCL (  9,  40)]; /* achars */
	char volume_id			[ISODCL ( 41,  72)]; /* dchars */
	__u8 unused2			[ISODCL ( 73,  80)];
	__u8 volume_space_size		[ISODCL ( 81,  88)]; /* 733 */
	__u8 unused3			[ISODCL ( 89, 120)];
	__u8 volume_set_size		[ISODCL (121, 124)]; /* 723 */
	__u8 volume_sequence_number	[ISODCL (125, 128)]; /* 723 */
	__u8 logical_block_size		[ISODCL (129, 132)]; /* 723 */
	__u8 path_table_size		[ISODCL (133, 140)]; /* 733 */
	__u8 type_l_path_table		[ISODCL (141, 144)]; /* 731 */
	__u8 opt_type_l_path_table	[ISODCL (145, 148)]; /* 731 */
	__u8 type_m_path_table		[ISODCL (149, 152)]; /* 732 */
	__u8 opt_type_m_path_table	[ISODCL (153, 156)]; /* 732 */
	__u8 root_directory_record	[ISODCL (157, 190)]; /* 9.1 */
	char volume_set_id		[ISODCL (191, 318)]; /* dchars */
	char publisher_id		[ISODCL (319, 446)]; /* achars */
	char preparer_id		[ISODCL (447, 574)]; /* achars */
	char application_id		[ISODCL (575, 702)]; /* achars */
	char copyright_file_id		[ISODCL (703, 739)]; /* 7.5 dchars */
	char abstract_file_id		[ISODCL (740, 776)]; /* 7.5 dchars */
	char bibliographic_file_id	[ISODCL (777, 813)]; /* 7.5 dchars */
	__u8 creation_date		[ISODCL (814, 830)]; /* 8.4.26.1 */
	__u8 modification_date		[ISODCL (831, 847)]; /* 8.4.26.1 */
	__u8 expiration_date		[ISODCL (848, 864)]; /* 8.4.26.1 */
	__u8 effective_date		[ISODCL (865, 881)]; /* 8.4.26.1 */
	__u8 file_structure_version	[ISODCL (882, 882)]; /* 711 */
	__u8 unused4			[ISODCL (883, 883)];
	__u8 application_data		[ISODCL (884, 1395)];
	__u8 unused5			[ISODCL (1396, 2048)];
};

/* Almost the same as the primary descriptor but two fields are specified */
struct iso_supplementary_descriptor {
	__u8 type			[ISODCL (  1,   1)]; /* 711 */
	char id				[ISODCL (  2,   6)];
	__u8 version			[ISODCL (  7,   7)]; /* 711 */
	__u8 flags			[ISODCL (  8,   8)]; /* 853 */
	char system_id			[ISODCL (  9,  40)]; /* achars */
	char volume_id			[ISODCL ( 41,  72)]; /* dchars */
	__u8 unused2			[ISODCL ( 73,  80)];
	__u8 volume_space_size		[ISODCL ( 81,  88)]; /* 733 */
	__u8 escape			[ISODCL ( 89, 120)]; /* 856 */
	__u8 volume_set_size		[ISODCL (121, 124)]; /* 723 */
	__u8 volume_sequence_number	[ISODCL (125, 128)]; /* 723 */
	__u8 logical_block_size		[ISODCL (129, 132)]; /* 723 */
	__u8 path_table_size		[ISODCL (133, 140)]; /* 733 */
	__u8 type_l_path_table		[ISODCL (141, 144)]; /* 731 */
	__u8 opt_type_l_path_table	[ISODCL (145, 148)]; /* 731 */
	__u8 type_m_path_table		[ISODCL (149, 152)]; /* 732 */
	__u8 opt_type_m_path_table	[ISODCL (153, 156)]; /* 732 */
	__u8 root_directory_record	[ISODCL (157, 190)]; /* 9.1 */
	char volume_set_id		[ISODCL (191, 318)]; /* dchars */
	char publisher_id		[ISODCL (319, 446)]; /* achars */
	char preparer_id		[ISODCL (447, 574)]; /* achars */
	char application_id		[ISODCL (575, 702)]; /* achars */
	char copyright_file_id		[ISODCL (703, 739)]; /* 7.5 dchars */
	char abstract_file_id		[ISODCL (740, 776)]; /* 7.5 dchars */
	char bibliographic_file_id	[ISODCL (777, 813)]; /* 7.5 dchars */
	__u8 creation_date		[ISODCL (814, 830)]; /* 8.4.26.1 */
	__u8 modification_date		[ISODCL (831, 847)]; /* 8.4.26.1 */
	__u8 expiration_date		[ISODCL (848, 864)]; /* 8.4.26.1 */
	__u8 effective_date		[ISODCL (865, 881)]; /* 8.4.26.1 */
	__u8 file_structure_version	[ISODCL (882, 882)]; /* 711 */
	__u8 unused4			[ISODCL (883, 883)];
	__u8 application_data		[ISODCL (884, 1395)];
	__u8 unused5			[ISODCL (1396, 2048)];
};


#define HS_STANDARD_ID "CDROM"

struct  hs_volume_descriptor {
	__u8 foo			[ISODCL (  1,   8)]; /* 733 */
	__u8 type			[ISODCL (  9,   9)]; /* 711 */
	char id				[ISODCL ( 10,  14)];
	__u8 version			[ISODCL ( 15,  15)]; /* 711 */
	__u8 data[ISODCL(16,2048)];
};


struct hs_primary_descriptor {
	__u8 foo			[ISODCL (  1,   8)]; /* 733 */
	__u8 type			[ISODCL (  9,   9)]; /* 711 */
	__u8 id				[ISODCL ( 10,  14)];
	__u8 version			[ISODCL ( 15,  15)]; /* 711 */
	__u8 unused1			[ISODCL ( 16,  16)]; /* 711 */
	char system_id			[ISODCL ( 17,  48)]; /* achars */
	char volume_id			[ISODCL ( 49,  80)]; /* dchars */
	__u8 unused2			[ISODCL ( 81,  88)]; /* 733 */
	__u8 volume_space_size		[ISODCL ( 89,  96)]; /* 733 */
	__u8 unused3			[ISODCL ( 97, 128)]; /* 733 */
	__u8 volume_set_size		[ISODCL (129, 132)]; /* 723 */
	__u8 volume_sequence_number	[ISODCL (133, 136)]; /* 723 */
	__u8 logical_block_size		[ISODCL (137, 140)]; /* 723 */
	__u8 path_table_size		[ISODCL (141, 148)]; /* 733 */
	__u8 type_l_path_table		[ISODCL (149, 152)]; /* 731 */
	__u8 unused4			[ISODCL (153, 180)]; /* 733 */
	__u8 root_directory_record	[ISODCL (181, 214)]; /* 9.1 */
};

/* We use this to help us look up the parent inode numbers. */

struct iso_path_table{
	__u8  name_len[2];	/* 721 */
	__u8  extent[4];	/* 731 */
	__u8  parent[2];	/* 721 */
	char name[];
} __attribute__((packed));

/* high sierra is identical to iso, except that the date is only 6 bytes, and
   there is an extra reserved byte after the flags */

struct iso_directory_record {
	__u8 length			[ISODCL (1, 1)]; /* 711 */
	__u8 ext_attr_length		[ISODCL (2, 2)]; /* 711 */
	__u8 extent			[ISODCL (3, 10)]; /* 733 */
	__u8 size			[ISODCL (11, 18)]; /* 733 */
	__u8 date			[ISODCL (19, 25)]; /* 7 by 711 */
	__u8 flags			[ISODCL (26, 26)];
	__u8 file_unit_size		[ISODCL (27, 27)]; /* 711 */
	__u8 interleave			[ISODCL (28, 28)]; /* 711 */
	__u8 volume_sequence_number	[ISODCL (29, 32)]; /* 723 */
	__u8 name_len			[ISODCL (33, 33)]; /* 711 */
	char name			[];
} __attribute__((packed));

#define ISOFS_BLOCK_BITS 11
#define ISOFS_BLOCK_SIZE 2048

#define ISOFS_BUFFER_SIZE(INODE) ((INODE)->i_sb->s_blocksize)
#define ISOFS_BUFFER_BITS(INODE) ((INODE)->i_sb->s_blocksize_bits)

#endif /* _ISOFS_FS_H */
