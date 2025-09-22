/*
 * Header file iso9660.h - assorted structure definitions and typecasts.
 * specific to iso9660 filesystem.

   Written by Eric Youngdale (1993).

   Copyright 1993 Yggdrasil Computing, Incorporated

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/*
 * 	$Id: iso9660.h,v 1.2 2023/11/21 08:46:06 jmatthew Exp $
 */

#ifndef _ISOFS_FS_H
#define _ISOFS_FS_H

/*
 * The isofs filesystem constants/structures
 */

/* This part borrowed from the bsd386 isofs */
#define ISODCL(from, to) (to - from + 1)

struct iso_volume_descriptor {
	char type[ISODCL(1,1)]; /* 711 */
	char id[ISODCL(2,6)];
	char version[ISODCL(7,7)];
	char data[ISODCL(8,2048)];
};

/* volume descriptor types */
#define ISO_VD_PRIMARY       1
#define ISO_VD_SUPPLEMENTARY 2     /* Used by Joliet */
#define ISO_VD_END           255

#define ISO_STANDARD_ID "CD001"

#define EL_TORITO_ID "EL TORITO SPECIFICATION"
#define EL_TORITO_ARCH_x86 0
#define EL_TORITO_ARCH_PPC 1
#define EL_TORITO_ARCH_MAC 2
#define EL_TORITO_ARCH_EFI 0xEF
#define EL_TORITO_BOOTABLE 0x88
#define EL_TORITO_MEDIA_NOEMUL 0
#define EL_TORITO_MEDIA_12FLOP  1
#define EL_TORITO_MEDIA_144FLOP 2
#define EL_TORITO_MEDIA_288FLOP 3
#define EL_TORITO_MEDIA_HD      4

struct iso_primary_descriptor {
	char type			[ISODCL (  1,   1)]; /* 711 */
	char id				[ISODCL (  2,   6)];
	char version			[ISODCL (  7,   7)]; /* 711 */
	char unused1			[ISODCL (  8,   8)];
	char system_id			[ISODCL (  9,  40)]; /* achars */
	char volume_id			[ISODCL ( 41,  72)]; /* dchars */
	char unused2			[ISODCL ( 73,  80)];
	char volume_space_size		[ISODCL ( 81,  88)]; /* 733 */
	char escape_sequences		[ISODCL ( 89, 120)];
	char volume_set_size		[ISODCL (121, 124)]; /* 723 */
	char volume_sequence_number	[ISODCL (125, 128)]; /* 723 */
	char logical_block_size		[ISODCL (129, 132)]; /* 723 */
	char path_table_size		[ISODCL (133, 140)]; /* 733 */
	char type_l_path_table		[ISODCL (141, 144)]; /* 731 */
	char opt_type_l_path_table	[ISODCL (145, 148)]; /* 731 */
	char type_m_path_table		[ISODCL (149, 152)]; /* 732 */
	char opt_type_m_path_table	[ISODCL (153, 156)]; /* 732 */
	char root_directory_record	[ISODCL (157, 190)]; /* 9.1 */
	char volume_set_id		[ISODCL (191, 318)]; /* dchars */
	char publisher_id		[ISODCL (319, 446)]; /* achars */
	char preparer_id		[ISODCL (447, 574)]; /* achars */
	char application_id		[ISODCL (575, 702)]; /* achars */
	char copyright_file_id		[ISODCL (703, 739)]; /* 7.5 dchars */
	char abstract_file_id		[ISODCL (740, 776)]; /* 7.5 dchars */
	char bibliographic_file_id	[ISODCL (777, 813)]; /* 7.5 dchars */
	char creation_date		[ISODCL (814, 830)]; /* 8.4.26.1 */
	char modification_date		[ISODCL (831, 847)]; /* 8.4.26.1 */
	char expiration_date		[ISODCL (848, 864)]; /* 8.4.26.1 */
	char effective_date		[ISODCL (865, 881)]; /* 8.4.26.1 */
	char file_structure_version	[ISODCL (882, 882)]; /* 711 */
	char unused4			[ISODCL (883, 883)];
	char application_data		[ISODCL (884, 1395)];
	char unused5			[ISODCL (1396, 2048)];
};

/* El Torito Boot Record Volume Descriptor */
struct eltorito_boot_descriptor {
        char id	           		[ISODCL (  1,    1)]; /* 711 */
	char id2			[ISODCL (  2,    6)];
	char version			[ISODCL (  7,    7)]; /* 711 */
	char system_id			[ISODCL (  8,   39)];
	char unused2			[ISODCL ( 40,   71)];
        char bootcat_ptr                [ISODCL ( 72 ,  75)];
	char unused5			[ISODCL ( 76, 2048)];
};

/* Validation entry for El Torito */
struct eltorito_validation_entry {
        char headerid          		[ISODCL (  1,    1)]; /* 711 */
	char arch			[ISODCL (  2,    2)];
	char pad1			[ISODCL (  3,    4)]; /* 711 */
	char id  			[ISODCL (  5,   28)];
	char cksum			[ISODCL ( 29,   30)];
        char key1                       [ISODCL ( 31,   31)];
	char key2			[ISODCL ( 32,   32)];
};

/* El Torito initial/default entry in boot catalog */
struct eltorito_defaultboot_entry {
        char boot_id           		[ISODCL (  1,    1)]; /* 711 */
	char boot_media			[ISODCL (  2,    2)];
	char loadseg			[ISODCL (  3,    4)]; /* 711 */
	char arch  			[ISODCL (  5,    5)];
	char pad1			[ISODCL (  6,    6)];
        char nsect                      [ISODCL (  7,    8)];
	char bootoff			[ISODCL (  9,   12)];
        char pad2                       [ISODCL ( 13,   32)];
};

/* El Torito section header entry in boot catalog */
struct eltorito_sectionheader_entry {
#define EL_TORITO_SHDR_ID_SHDR		0x90
#define EL_TORITO_SHDR_ID_LAST_SHDR	0x91
	char header_id			[ISODCL (  1,    1)]; /* 711 */
	char platform_id		[ISODCL (  2,    2)];
	char entry_count		[ISODCL (  3,    4)]; /* 721 */
	char id				[ISODCL (  5,   32)];
};


/* We use this to help us look up the parent inode numbers. */

struct iso_path_table{
	unsigned char  name_len[2];	/* 721 */
	char extent[4];		/* 731 */
	char  parent[2];	/* 721 */
	char name[1];
};

struct iso_directory_record {
	unsigned char length			[ISODCL (1, 1)]; /* 711 */
	char ext_attr_length		[ISODCL (2, 2)]; /* 711 */
	char extent			[ISODCL (3, 10)]; /* 733 */
	char size			[ISODCL (11, 18)]; /* 733 */
	char date			[ISODCL (19, 25)]; /* 7 by 711 */
	char flags			[ISODCL (26, 26)];
	char file_unit_size		[ISODCL (27, 27)]; /* 711 */
	char interleave			[ISODCL (28, 28)]; /* 711 */
	char volume_sequence_number	[ISODCL (29, 32)]; /* 723 */
	unsigned char name_len		[ISODCL (33, 33)]; /* 711 */
	char name			[34]; /* Not really, but we need something here */
};
#endif



