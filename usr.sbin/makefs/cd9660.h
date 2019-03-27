/*	$NetBSD: cd9660.h,v 1.17 2011/06/23 02:35:56 enami Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2005 Daniel Watt, Walter Deignan, Ryan Gabrys, Alan
 * Perez-Rathke and Ram Vedam.  All rights reserved.
 *
 * This code was written by Daniel Watt, Walter Deignan, Ryan Gabrys,
 * Alan Perez-Rathke and Ram Vedam.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY DANIEL WATT, WALTER DEIGNAN, RYAN
 * GABRYS, ALAN PEREZ-RATHKE AND RAM VEDAM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL DANIEL WATT, WALTER DEIGNAN, RYAN
 * GABRYS, ALAN PEREZ-RATHKE AND RAM VEDAM BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE,DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MAKEFS_CD9660_H
#define _MAKEFS_CD9660_H

#include <inttypes.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <sys/queue.h>
#include <sys/param.h>
#include <sys/endian.h>

#include "makefs.h"
#include "iso.h"
#include "iso_rrip.h"
#include "cd9660/cd9660_eltorito.h"

#ifdef DEBUG
#define	INODE_WARNX(__x)	warnx __x
#else /* DEBUG */
#define	INODE_WARNX(__x)
#endif /* DEBUG */

#define CD9660MAXPATH 4096

#define ISO_STRING_FILTER_NONE = 0x00
#define ISO_STRING_FILTER_DCHARS = 0x01
#define ISO_STRING_FILTER_ACHARS = 0x02

/*
Extended preferences type, in the spirit of what makefs gives us (only ints)
*/
typedef struct {
	const char  *shortName;		/* Short option */
	const char	*name;		/* option name */
	char		*value;		/* where to stuff the value */
	int		minLength;	/* minimum for value */
	int		maxLength;	/* maximum for value */
	const char	*desc;		/* option description */
	int		filterFlags;
} string_option_t;

/******** STRUCTURES **********/

/*Defaults*/
#define ISO_DEFAULT_VOLUMEID "MAKEFS_CD9660_IMAGE"
#define ISO_DEFAULT_APPID "MAKEFS"
#define ISO_DEFAULT_PUBLISHER "MAKEFS"
#define ISO_DEFAULT_PREPARER "MAKEFS"

#define ISO_VOLUME_DESCRIPTOR_STANDARD_ID "CD001"
#define ISO_VOLUME_DESCRIPTOR_BOOT 0
#define ISO_VOLUME_DESCRIPTOR_PVD 1
#define ISO_VOLUME_DESCRIPTOR_TERMINATOR 255

/*30 for name and extension, as well as version number and padding bit*/
#define ISO_FILENAME_MAXLENGTH_BEFORE_VERSION 30
#define ISO_FILENAME_MAXLENGTH	36
#define ISO_FILENAME_MAXLENGTH_WITH_PADDING 37

#define ISO_FLAG_CLEAR 0x00
#define ISO_FLAG_HIDDEN 0x01
#define ISO_FLAG_DIRECTORY 0x02
#define ISO_FLAG_ASSOCIATED 0x04
#define ISO_FLAG_PERMISSIONS 0x08
#define ISO_FLAG_RESERVED5 0x10
#define ISO_FLAG_RESERVED6 0x20
#define ISO_FLAG_FINAL_RECORD 0x40

#define ISO_PATHTABLE_ENTRY_BASESIZE 8

#define ISO_RRIP_DEFAULT_MOVE_DIR_NAME "RR_MOVED"
#define RRIP_DEFAULT_MOVE_DIR_NAME ".rr_moved"

#define	CD9660_BLOCKS(__sector_size, __bytes)	\
	howmany((__bytes), (__sector_size))

#define CD9660_MEM_ALLOC_ERROR(_F)	\
    err(EXIT_FAILURE, "%s, %s l. %d", _F, __FILE__, __LINE__)

#define CD9660_TYPE_FILE	0x01
#define CD9660_TYPE_DIR		0x02
#define CD9660_TYPE_DOT		0x04
#define CD9660_TYPE_DOTDOT	0x08
#define CD9660_TYPE_VIRTUAL	0x80

#define CD9660_INODE_HASH_SIZE	1024
#define CD9660_SECTOR_SIZE	2048

#define CD9660_END_PADDING	150

/* Slight modification of the ISO structure in iso.h */
typedef struct _iso_directory_record_cd9660 {
	u_char length			[ISODCL (1, 1)];	/* 711 */
	u_char ext_attr_length		[ISODCL (2, 2)];	/* 711 */
	u_char extent			[ISODCL (3, 10)];	/* 733 */
	u_char size			[ISODCL (11, 18)];	/* 733 */
	u_char date			[ISODCL (19, 25)];	/* 7 by 711 */
	u_char flags			[ISODCL (26, 26)];
	u_char file_unit_size		[ISODCL (27, 27)];	/* 711 */
	u_char interleave		[ISODCL (28, 28)];	/* 711 */
	u_char volume_sequence_number	[ISODCL (29, 32)];	/* 723 */
	u_char name_len			[ISODCL (33, 33)];	/* 711 */
	char name			[ISO_FILENAME_MAXLENGTH_WITH_PADDING];
} iso_directory_record_cd9660;

/* TODO: Lots of optimization of this structure */
typedef struct _cd9660node {
	u_char	type;/* Used internally */
	/* Tree structure */
	struct _cd9660node	*parent;	/* parent (NULL if root) */
	TAILQ_HEAD(cd9660_children_head, _cd9660node)	cn_children;
	TAILQ_ENTRY(_cd9660node)		cn_next_child;

	struct _cd9660node *dot_record; /* For directories, used mainly in RRIP */
	struct _cd9660node *dot_dot_record;

	fsnode		*node;		/* pointer to fsnode */
	struct _iso_directory_record_cd9660	*isoDirRecord;
	struct iso_extended_attributes	*isoExtAttributes;

	/***** SIZE CALCULATION *****/
	/*already stored in isoDirRecord, but this is an int version, and will be
		copied to isoDirRecord on writing*/
	uint32_t fileDataSector;

	/*
	 * same thing, though some notes:
	 * If a file, this is the file size
	 * If a directory, this is the size of all its children's
	 *	directory records
	 * plus necessary padding
	 */
	int64_t fileDataLength;

	int64_t fileSectorsUsed;
	int fileRecordSize;/*copy of a variable, int for quicker calculations*/

	/* Old name, used for renaming - needs to be optimized but low priority */
	char o_name [ISO_FILENAME_MAXLENGTH_WITH_PADDING];

	/***** SPACE RESERVED FOR EXTENSIONS *****/
	/* For memory efficiency's sake - we should move this to a separate struct
		and point to null if not needed */
	/* For Rock Ridge */
	struct _cd9660node *rr_real_parent, *rr_relocated;

	int64_t susp_entry_size;
	int64_t susp_dot_entry_size;
	int64_t susp_dot_dot_entry_size;

	/* Continuation area stuff */
	int64_t susp_entry_ce_start;
	int64_t susp_dot_ce_start;
	int64_t susp_dot_dot_ce_start;

	int64_t susp_entry_ce_length;
	int64_t susp_dot_ce_length;
	int64_t susp_dot_dot_ce_length;

	/* Data to put at the end of the System Use field */
	int64_t su_tail_size;
	char *su_tail_data;

	/*** PATH TABLE STUFF ***/
	int level;			/*depth*/
	int ptnumber;
	struct _cd9660node *ptnext, *ptprev, *ptlast;

	/* SUSP entries */
	TAILQ_HEAD(susp_linked_list, ISO_SUSP_ATTRIBUTES) head;
} cd9660node;

typedef struct _path_table_entry
{
	u_char length[ISODCL (1, 1)];
	u_char extended_attribute_length[ISODCL (2, 2)];
	u_char first_sector[ISODCL (3, 6)];
	u_char parent_number[ISODCL (7, 8)];
	u_char name[ISO_FILENAME_MAXLENGTH_WITH_PADDING];
} path_table_entry;

typedef struct _volume_descriptor
{
	u_char *volumeDescriptorData; /*ALWAYS 2048 bytes long*/
	int64_t sector;
	struct _volume_descriptor *next;
} volume_descriptor;

typedef struct _iso9660_disk {
	int sectorSize;
	struct iso_primary_descriptor		primaryDescriptor;
	struct iso_supplementary_descriptor	supplementaryDescriptor;

	volume_descriptor *firstVolumeDescriptor;

	cd9660node *rootNode;

	/* Important sector numbers here */
	/* primaryDescriptor.type_l_path_table*/
	int64_t primaryBigEndianTableSector;

	/* primaryDescriptor.type_m_path_table*/
	int64_t primaryLittleEndianTableSector;

	/* primaryDescriptor.opt_type_l_path_table*/
	int64_t secondaryBigEndianTableSector;

	/* primaryDescriptor.opt_type_m_path_table*/
	int64_t secondaryLittleEndianTableSector;

	/* primaryDescriptor.path_table_size*/
	int pathTableLength;
	int64_t dataFirstSector;

	int64_t totalSectors;
	/* OPTIONS GO HERE */
	int	isoLevel;

	int include_padding_areas;

	int follow_sym_links;
	int verbose_level;
	int displayHelp;
	int keep_bad_images;

	/* SUSP options and variables */
	int64_t susp_continuation_area_start_sector;
	int64_t susp_continuation_area_size;
	int64_t susp_continuation_area_current_free;

	int rock_ridge_enabled;
	/* Other Rock Ridge Variables */
	char *rock_ridge_renamed_dir_name;
	int rock_ridge_move_count;
	cd9660node *rr_moved_dir;

	int archimedes_enabled;
	int chrp_boot;

	/* Spec breaking options */
	u_char allow_deep_trees;
	u_char allow_start_dot;
	u_char allow_max_name; /* Allow 37 char filenames*/
	u_char allow_illegal_chars; /* ~, !, # */
	u_char allow_lowercase;
	u_char allow_multidot;
	u_char omit_trailing_period;

	/* BOOT INFORMATION HERE */
	int has_generic_bootimage; /* Default to 0 */
	char *generic_bootimage;

	int is_bootable;/* Default to 0 */
	int64_t boot_catalog_sector;
	boot_volume_descriptor *boot_descriptor;
	char * boot_image_directory;

	TAILQ_HEAD(boot_image_list,cd9660_boot_image) boot_images;
	int image_serialno;
	LIST_HEAD(boot_catalog_entries,boot_catalog_entry) boot_entries;

} iso9660_disk;

/************ FUNCTIONS **************/
int			cd9660_valid_a_chars(const char *);
int			cd9660_valid_d_chars(const char *);
void			cd9660_uppercase_characters(char *, int);

/* ISO Data Types */
void			cd9660_721(uint16_t, unsigned char *);
void			cd9660_731(uint32_t, unsigned char *);
void			cd9660_722(uint16_t, unsigned char *);
void			cd9660_732(uint32_t, unsigned char *);
void 			cd9660_bothendian_dword(uint32_t dw, unsigned char *);
void 			cd9660_bothendian_word(uint16_t dw, unsigned char *);
void			cd9660_set_date(char *, time_t);
void			cd9660_time_8426(unsigned char *, time_t);
void			cd9660_time_915(unsigned char *, time_t);

/*** Boot Functions ***/
int	cd9660_write_generic_bootimage(FILE *);
int	cd9660_write_boot(iso9660_disk *, FILE *);
int	cd9660_add_boot_disk(iso9660_disk *, const char *);
int	cd9660_eltorito_add_boot_option(iso9660_disk *, const char *,
    const char *);
int	cd9660_setup_boot(iso9660_disk *, int);
int	cd9660_setup_boot_volume_descriptor(iso9660_disk *,
    volume_descriptor *);


/*** Write Functions ***/
int	cd9660_write_image(iso9660_disk *, const char *image);
int	cd9660_copy_file(iso9660_disk *, FILE *, off_t, const char *);

void	cd9660_compute_full_filename(cd9660node *, char *);
int	cd9660_compute_record_size(iso9660_disk *, cd9660node *);

/* Debugging functions */
void	debug_print_tree(iso9660_disk *, cd9660node *,int);
void	debug_print_path_tree(cd9660node *);
void	debug_print_volume_descriptor_information(iso9660_disk *);
void	debug_dump_to_xml_ptentry(path_table_entry *,int, int);
void	debug_dump_to_xml_path_table(FILE *, off_t, int, int);
void	debug_dump_to_xml(FILE *);
int	debug_get_encoded_number(unsigned char *, int);
void	debug_dump_integer(const char *, char *,int);
void	debug_dump_string(const char *,unsigned char *,int);
void	debug_dump_directory_record_9_1(unsigned char *);
void	debug_dump_to_xml_volume_descriptor(unsigned char *,int);

void	cd9660_pad_string_spaces(char *, int);

#endif
