/*	$NetBSD: cd9660.c,v 1.32 2011/08/23 17:09:11 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD AND BSD-4-Clause
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
 */
/*
 * Copyright (c) 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Luke Mewburn for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
  */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>

#include "makefs.h"
#include "cd9660.h"
#include "cd9660/iso9660_rrip.h"
#include "cd9660/cd9660_archimedes.h"

static void cd9660_finalize_PVD(iso9660_disk *);
static cd9660node *cd9660_allocate_cd9660node(void);
static void cd9660_set_defaults(iso9660_disk *);
static int cd9660_arguments_set_string(const char *, const char *, int,
    char, char *);
static void cd9660_populate_iso_dir_record(
    struct _iso_directory_record_cd9660 *, u_char, u_char, u_char,
    const char *);
static void cd9660_setup_root_node(iso9660_disk *);
static int cd9660_setup_volume_descriptors(iso9660_disk *);
#if 0
static int cd9660_fill_extended_attribute_record(cd9660node *);
#endif
static void cd9660_sort_nodes(cd9660node *);
static int cd9660_translate_node_common(iso9660_disk *, cd9660node *);
static int cd9660_translate_node(iso9660_disk *, fsnode *, cd9660node *);
static int cd9660_compare_filename(const char *, const char *);
static void cd9660_sorted_child_insert(cd9660node *, cd9660node *);
static int cd9660_handle_collisions(iso9660_disk *, cd9660node *, int);
static cd9660node *cd9660_rename_filename(iso9660_disk *, cd9660node *, int,
    int);
static void cd9660_copy_filenames(iso9660_disk *, cd9660node *);
static void cd9660_sorting_nodes(cd9660node *);
static int cd9660_count_collisions(cd9660node *);
static cd9660node *cd9660_rrip_move_directory(iso9660_disk *, cd9660node *);
static int cd9660_add_dot_records(iso9660_disk *, cd9660node *);

static void cd9660_convert_structure(iso9660_disk *, fsnode *, cd9660node *, int,
    int *, int *);
static void cd9660_free_structure(cd9660node *);
static int cd9660_generate_path_table(iso9660_disk *);
static int cd9660_level1_convert_filename(iso9660_disk *, const char *, char *,
    int);
static int cd9660_level2_convert_filename(iso9660_disk *, const char *, char *,
    int);
#if 0
static int cd9660_joliet_convert_filename(iso9660_disk *, const char *, char *,
    int);
#endif
static int cd9660_convert_filename(iso9660_disk *, const char *, char *, int);
static void cd9660_populate_dot_records(iso9660_disk *, cd9660node *);
static int64_t cd9660_compute_offsets(iso9660_disk *, cd9660node *, int64_t);
#if 0
static int cd9660_copy_stat_info(cd9660node *, cd9660node *, int);
#endif
static cd9660node *cd9660_create_virtual_entry(iso9660_disk *, const char *,
    cd9660node *, int, int);
static cd9660node *cd9660_create_file(iso9660_disk *, const char *,
    cd9660node *, cd9660node *);
static cd9660node *cd9660_create_directory(iso9660_disk *, const char *,
    cd9660node *, cd9660node *);
static cd9660node *cd9660_create_special_directory(iso9660_disk *, u_char,
    cd9660node *);
static int  cd9660_add_generic_bootimage(iso9660_disk *, const char *);


/*
 * Allocate and initialize a cd9660node
 * @returns struct cd9660node * Pointer to new node, or NULL on error
 */
static cd9660node *
cd9660_allocate_cd9660node(void)
{
	cd9660node *temp = ecalloc(1, sizeof(*temp));

	TAILQ_INIT(&temp->cn_children);
	temp->parent = temp->dot_record = temp->dot_dot_record = NULL;
	temp->ptnext = temp->ptprev = temp->ptlast = NULL;
	temp->node = NULL;
	temp->isoDirRecord = NULL;
	temp->isoExtAttributes = NULL;
	temp->rr_real_parent = temp->rr_relocated = NULL;
	temp->su_tail_data = NULL;
	return temp;
}

/**
* Set default values for cd9660 extension to makefs
*/
static void
cd9660_set_defaults(iso9660_disk *diskStructure)
{
	/*Fix the sector size for now, though the spec allows for other sizes*/
	diskStructure->sectorSize = 2048;

	/* Set up defaults in our own structure */
	diskStructure->verbose_level = 0;
	diskStructure->keep_bad_images = 0;
	diskStructure->follow_sym_links = 0;
	diskStructure->isoLevel = 2;

	diskStructure->rock_ridge_enabled = 0;
	diskStructure->rock_ridge_renamed_dir_name = 0;
	diskStructure->rock_ridge_move_count = 0;
	diskStructure->rr_moved_dir = 0;

	diskStructure->archimedes_enabled = 0;
	diskStructure->chrp_boot = 0;

	diskStructure->include_padding_areas = 1;

	/* Spec breaking functionality */
	diskStructure->allow_deep_trees =
	    diskStructure->allow_start_dot =
	    diskStructure->allow_max_name =
	    diskStructure->allow_illegal_chars =
	    diskStructure->allow_lowercase =
	    diskStructure->allow_multidot =
	    diskStructure->omit_trailing_period = 0;

	/* Make sure the PVD is clear */
	memset(&diskStructure->primaryDescriptor, 0, 2048);

	memset(diskStructure->primaryDescriptor.publisher_id,	0x20,128);
	memset(diskStructure->primaryDescriptor.preparer_id,	0x20,128);
	memset(diskStructure->primaryDescriptor.application_id,	0x20,128);
	memset(diskStructure->primaryDescriptor.copyright_file_id, 0x20,37);
	memset(diskStructure->primaryDescriptor.abstract_file_id, 0x20,37);
	memset(diskStructure->primaryDescriptor.bibliographic_file_id, 0x20,37);

	strcpy(diskStructure->primaryDescriptor.system_id, "FreeBSD");

	/* Boot support: Initially disabled */
	diskStructure->has_generic_bootimage = 0;
	diskStructure->generic_bootimage = NULL;

	diskStructure->boot_image_directory = 0;
	/*memset(diskStructure->boot_descriptor, 0, 2048);*/

	diskStructure->is_bootable = 0;
	TAILQ_INIT(&diskStructure->boot_images);
	LIST_INIT(&diskStructure->boot_entries);
}

void
cd9660_prep_opts(fsinfo_t *fsopts)
{
	iso9660_disk *diskStructure = ecalloc(1, sizeof(*diskStructure));

#define OPT_STR(letter, name, desc) \
	{ letter, name, NULL, OPT_STRBUF, 0, 0, desc }

#define OPT_NUM(letter, name, field, min, max, desc) \
	{ letter, name, &diskStructure->field, \
	  sizeof(diskStructure->field) == 8 ? OPT_INT64 : \
	  (sizeof(diskStructure->field) == 4 ? OPT_INT32 : \
	  (sizeof(diskStructure->field) == 2 ? OPT_INT16 : OPT_INT8)), \
	  min, max, desc }

#define OPT_BOOL(letter, name, field, desc) \
	OPT_NUM(letter, name, field, 0, 1, desc)

	const option_t cd9660_options[] = {
		OPT_NUM('l', "isolevel", isoLevel,
		    1, 2, "ISO Level"),
		OPT_NUM('v', "verbose", verbose_level,
		    0, 2, "Turns on verbose output"),

		OPT_BOOL('h', "help", displayHelp,
		    "Show help message"),
		OPT_BOOL('S', "follow-symlinks", follow_sym_links,
		    "Resolve symlinks in pathnames"),
		OPT_BOOL('R', "rockridge", rock_ridge_enabled,
		    "Enable Rock-Ridge extensions"),
		OPT_BOOL('C', "chrp-boot", chrp_boot,
		    "Enable CHRP boot"),
		OPT_BOOL('K', "keep-bad-images", keep_bad_images,
		    "Keep bad images"),
		OPT_BOOL('D', "allow-deep-trees", allow_deep_trees,
		    "Allow trees more than 8 levels"),
		OPT_BOOL('a', "allow-max-name", allow_max_name,
		    "Allow 37 char filenames (unimplemented)"),
		OPT_BOOL('i', "allow-illegal-chars", allow_illegal_chars,
		    "Allow illegal characters in filenames"),
		OPT_BOOL('m', "allow-multidot", allow_multidot,
		    "Allow multiple periods in filenames"),
		OPT_BOOL('o', "omit-trailing-period", omit_trailing_period,
		    "Omit trailing periods in filenames"),
		OPT_BOOL('\0', "allow-lowercase", allow_lowercase,
		    "Allow lowercase characters in filenames"),
		OPT_BOOL('\0', "archimedes", archimedes_enabled,
		    "Enable Archimedes structure"),
		OPT_BOOL('\0', "no-trailing-padding", include_padding_areas,
		    "Include padding areas"),

		OPT_STR('A', "applicationid", "Application Identifier"),
		OPT_STR('P', "publisher", "Publisher Identifier"),
		OPT_STR('p', "preparer", "Preparer Identifier"),
		OPT_STR('L', "label", "Disk Label"),
		OPT_STR('V', "volumeid", "Volume Set Identifier"),
		OPT_STR('B', "bootimage", "Boot image parameter"),
		OPT_STR('G', "generic-bootimage", "Generic boot image param"),
		OPT_STR('\0', "bootimagedir", "Boot image directory"),
		OPT_STR('\0', "no-emul-boot", "No boot emulation"),
		OPT_STR('\0', "no-boot", "No boot support"),
		OPT_STR('\0', "hard-disk-boot", "Boot from hard disk"),
		OPT_STR('\0', "boot-load-segment", "Boot load segment"),
		OPT_STR('\0', "platformid", "Section Header Platform ID"),

		{ .name = NULL }
	};

	fsopts->fs_specific = diskStructure;
	fsopts->fs_options = copy_opts(cd9660_options);

	cd9660_set_defaults(diskStructure);
}

void
cd9660_cleanup_opts(fsinfo_t *fsopts)
{
	free(fsopts->fs_specific);
	free(fsopts->fs_options);
}

static int
cd9660_arguments_set_string(const char *val, const char *fieldtitle, int length,
			    char testmode, char * dest)
{
	int len, test;

	if (val == NULL)
		warnx("error: The %s requires a string argument", fieldtitle);
	else if ((len = strlen(val)) <= length) {
		if (testmode == 'd')
			test = cd9660_valid_d_chars(val);
		else
			test = cd9660_valid_a_chars(val);
		if (test) {
			memcpy(dest, val, len);
			if (test == 2)
				cd9660_uppercase_characters(dest, len);
			return 1;
		} else
			warnx("error: The %s must be composed of "
			      "%c-characters", fieldtitle, testmode);
	} else
		warnx("error: The %s must be at most 32 characters long",
		    fieldtitle);
	return 0;
}

/*
 * Command-line parsing function
 */

int
cd9660_parse_opts(const char *option, fsinfo_t *fsopts)
{
	int	rv, i;
	iso9660_disk *diskStructure = fsopts->fs_specific;
	option_t *cd9660_options = fsopts->fs_options;
	char buf[1024];
	const char *name, *desc;

	assert(option != NULL);

	if (debug & DEBUG_FS_PARSE_OPTS)
		printf("%s: got `%s'\n", __func__, option);

	i = set_option(cd9660_options, option, buf, sizeof(buf));
	if (i == -1)
		return 0;

	if (cd9660_options[i].name == NULL)
		abort();

	name = cd9660_options[i].name;
	desc = cd9660_options[i].desc;
	switch (cd9660_options[i].letter) {
	case 'h':
	case 'S':
		rv = 0; /* this is not handled yet */
		break;
	case 'L':
		rv = cd9660_arguments_set_string(buf, desc, 32, 'd',
		    diskStructure->primaryDescriptor.volume_id);
		break;
	case 'A':
		rv = cd9660_arguments_set_string(buf, desc, 128, 'a',
		    diskStructure->primaryDescriptor.application_id);
		break;
	case 'P':
		rv = cd9660_arguments_set_string(buf, desc, 128, 'a',
		    diskStructure->primaryDescriptor.publisher_id);
		break;
	case 'p':
		rv = cd9660_arguments_set_string(buf, desc, 128, 'a',
		    diskStructure->primaryDescriptor.preparer_id);
		break;
	case 'V':
		rv = cd9660_arguments_set_string(buf, desc, 128, 'a',
		    diskStructure->primaryDescriptor.volume_set_id);
		break;
	/* Boot options */
	case 'B':
		if (buf[0] == '\0') {
			warnx("The Boot Image parameter requires a valid boot"
			    "information string");
			rv = 0;
		} else
			rv = cd9660_add_boot_disk(diskStructure, buf);
		break;
	case 'G':
		if (buf[0] == '\0') {
			warnx("The Generic Boot Image parameter requires a"
			    " valid boot information string");
			rv = 0;
		} else
			rv = cd9660_add_generic_bootimage(diskStructure, buf);
		break;
	default:
		if (strcmp(name, "bootimagedir") == 0) {
			/*
			 * XXXfvdl this is unused.
			 */
			if (buf[0] == '\0') {
				warnx("The Boot Image Directory parameter"
				    " requires a directory name");
				rv = 0;
			} else {
				diskStructure->boot_image_directory =
				    emalloc(strlen(buf) + 1);
				/* BIG TODO: Add the max length function here */
				rv = cd9660_arguments_set_string(buf, desc, 12,
				    'd', diskStructure->boot_image_directory);
			}
		} else if (strcmp(name, "no-emul-boot") == 0 ||
		    strcmp(name, "no-boot") == 0 ||
		    strcmp(name, "hard-disk-boot") == 0) {
			/* RRIP */
			cd9660_eltorito_add_boot_option(diskStructure, name, 0);
			rv = 1;
		} else if (strcmp(name, "boot-load-segment") == 0 ||
		    strcmp(name, "platformid") == 0) {
			if (buf[0] == '\0') {
				warnx("Option `%s' doesn't contain a value",
				    name);
				rv = 0;
			} else {
				cd9660_eltorito_add_boot_option(diskStructure,
				    name, buf);
				rv = 1;
			}
		} else
			rv = 1;
	}
	return rv;
}

/*
 * Main function for cd9660_makefs
 * Builds the ISO image file
 * @param const char *image The image filename to create
 * @param const char *dir The directory that is being read
 * @param struct fsnode *root The root node of the filesystem tree
 * @param struct fsinfo_t *fsopts Any options
 */
void
cd9660_makefs(const char *image, const char *dir, fsnode *root,
    fsinfo_t *fsopts)
{
	int64_t startoffset;
	int numDirectories;
	uint64_t pathTableSectors;
	int64_t firstAvailableSector;
	int64_t totalSpace;
	int error;
	cd9660node *real_root;
	iso9660_disk *diskStructure = fsopts->fs_specific;

	if (diskStructure->verbose_level > 0)
		printf("%s: ISO level is %i\n", __func__,
		    diskStructure->isoLevel);
	if (diskStructure->isoLevel < 2 &&
	    diskStructure->allow_multidot)
		errx(EXIT_FAILURE, "allow-multidot requires iso level of 2");

	assert(image != NULL);
	assert(dir != NULL);
	assert(root != NULL);

	if (diskStructure->displayHelp) {
		/*
		 * Display help here - probably want to put it in
		 * a separate function
		 */
		return;
	}

	if (diskStructure->verbose_level > 0)
		printf("%s: image %s directory %s root %p\n", __func__,
		    image, dir, root);

	/* Set up some constants. Later, these will be defined with options */

	/* Counter needed for path tables */
	numDirectories = 0;

	/* Convert tree to our own format */
	/* Actually, we now need to add the REAL root node, at level 0 */

	real_root = cd9660_allocate_cd9660node();
	real_root->isoDirRecord = emalloc(sizeof(*real_root->isoDirRecord));
	/* Leave filename blank for root */
	memset(real_root->isoDirRecord->name, 0,
	    ISO_FILENAME_MAXLENGTH_WITH_PADDING);

	real_root->level = 0;
	diskStructure->rootNode = real_root;
	real_root->type = CD9660_TYPE_DIR;
	error = 0;
	real_root->node = root;
	cd9660_convert_structure(diskStructure, root, real_root, 1,
	    &numDirectories, &error);

	if (TAILQ_EMPTY(&real_root->cn_children)) {
		errx(EXIT_FAILURE, "%s: converted directory is empty. "
		    "Tree conversion failed", __func__);
	} else if (error != 0) {
		errx(EXIT_FAILURE, "%s: tree conversion failed", __func__);
	} else {
		if (diskStructure->verbose_level > 0)
			printf("%s: tree converted\n", __func__);
	}

	/* Add the dot and dot dot records */
	cd9660_add_dot_records(diskStructure, real_root);

	cd9660_setup_root_node(diskStructure);

	if (diskStructure->verbose_level > 0)
		printf("%s: done converting tree\n", __func__);

	/* non-SUSP extensions */
	if (diskStructure->archimedes_enabled)
		archimedes_convert_tree(diskStructure->rootNode);

	/* Rock ridge / SUSP init pass */
	if (diskStructure->rock_ridge_enabled) {
		cd9660_susp_initialize(diskStructure, diskStructure->rootNode,
		    diskStructure->rootNode, NULL);
	}

	/* Build path table structure */
	diskStructure->pathTableLength = cd9660_generate_path_table(
	    diskStructure);

	pathTableSectors = CD9660_BLOCKS(diskStructure->sectorSize,
		diskStructure->pathTableLength);

	firstAvailableSector = cd9660_setup_volume_descriptors(diskStructure);
	if (diskStructure->is_bootable) {
		firstAvailableSector = cd9660_setup_boot(diskStructure,
		    firstAvailableSector);
		if (firstAvailableSector < 0)
			errx(EXIT_FAILURE, "setup_boot failed");
	}
	/* LE first, then BE */
	diskStructure->primaryLittleEndianTableSector = firstAvailableSector;
	diskStructure->primaryBigEndianTableSector =
		diskStructure->primaryLittleEndianTableSector + pathTableSectors;

	/* Set the secondary ones to -1, not going to use them for now */
	diskStructure->secondaryBigEndianTableSector = -1;
	diskStructure->secondaryLittleEndianTableSector = -1;

	diskStructure->dataFirstSector =
	    diskStructure->primaryBigEndianTableSector + pathTableSectors;
	if (diskStructure->verbose_level > 0)
		printf("%s: Path table conversion complete. "
		    "Each table is %i bytes, or %" PRIu64 " sectors.\n",
		    __func__,
		    diskStructure->pathTableLength, pathTableSectors);

	startoffset = diskStructure->sectorSize*diskStructure->dataFirstSector;

	totalSpace = cd9660_compute_offsets(diskStructure, real_root, startoffset);

	diskStructure->totalSectors = diskStructure->dataFirstSector +
		CD9660_BLOCKS(diskStructure->sectorSize, totalSpace);

	/* Disabled until pass 1 is done */
	if (diskStructure->rock_ridge_enabled) {
		diskStructure->susp_continuation_area_start_sector =
		    diskStructure->totalSectors;
		diskStructure->totalSectors +=
		    CD9660_BLOCKS(diskStructure->sectorSize,
			diskStructure->susp_continuation_area_size);
		cd9660_susp_finalize(diskStructure, diskStructure->rootNode);
	}


	cd9660_finalize_PVD(diskStructure);

	/* Add padding sectors, just for testing purposes right now */
	/* diskStructure->totalSectors+=150; */

	/* Debugging output */
	if (diskStructure->verbose_level > 0) {
		printf("%s: Sectors 0-15 reserved\n", __func__);
		printf("%s: Primary path tables starts in sector %"
		    PRId64 "\n", __func__,
		    diskStructure->primaryLittleEndianTableSector);
		printf("%s: File data starts in sector %"
		    PRId64 "\n", __func__, diskStructure->dataFirstSector);
		printf("%s: Total sectors: %"
		    PRId64 "\n", __func__, diskStructure->totalSectors);
	}

	/*
	 * Add padding sectors at the end
	 * TODO: Clean this up and separate padding
	 */
	if (diskStructure->include_padding_areas)
		diskStructure->totalSectors += 150;

	cd9660_write_image(diskStructure, image);

	if (diskStructure->verbose_level > 1) {
		debug_print_volume_descriptor_information(diskStructure);
		debug_print_tree(diskStructure, real_root, 0);
		debug_print_path_tree(real_root);
	}

	/* Clean up data structures */
	cd9660_free_structure(real_root);

	if (diskStructure->verbose_level > 0)
		printf("%s: done\n", __func__);
}

/* Generic function pointer - implement later */
typedef int (*cd9660node_func)(cd9660node *);

static void
cd9660_finalize_PVD(iso9660_disk *diskStructure)
{
	time_t tstamp = stampst.st_ino ? stampst.st_mtime : time(NULL);

	/* root should be a fixed size of 34 bytes since it has no name */
	memcpy(diskStructure->primaryDescriptor.root_directory_record,
		diskStructure->rootNode->dot_record->isoDirRecord, 34);

	/* In RRIP, this might be longer than 34 */
	diskStructure->primaryDescriptor.root_directory_record[0] = 34;

	/* Set up all the important numbers in the PVD */
	cd9660_bothendian_dword(diskStructure->totalSectors,
	    (unsigned char *)diskStructure->primaryDescriptor.volume_space_size);
	cd9660_bothendian_word(1,
	    (unsigned char *)diskStructure->primaryDescriptor.volume_set_size);
	cd9660_bothendian_word(1,
	    (unsigned char *)
		diskStructure->primaryDescriptor.volume_sequence_number);
	cd9660_bothendian_word(diskStructure->sectorSize,
	    (unsigned char *)
		diskStructure->primaryDescriptor.logical_block_size);
	cd9660_bothendian_dword(diskStructure->pathTableLength,
	    (unsigned char *)diskStructure->primaryDescriptor.path_table_size);

	cd9660_731(diskStructure->primaryLittleEndianTableSector,
		(u_char *)diskStructure->primaryDescriptor.type_l_path_table);
	cd9660_732(diskStructure->primaryBigEndianTableSector,
		(u_char *)diskStructure->primaryDescriptor.type_m_path_table);

	diskStructure->primaryDescriptor.file_structure_version[0] = 1;

	/* Pad all strings with spaces instead of nulls */
	cd9660_pad_string_spaces(diskStructure->primaryDescriptor.volume_id, 32);
	cd9660_pad_string_spaces(diskStructure->primaryDescriptor.system_id, 32);
	cd9660_pad_string_spaces(diskStructure->primaryDescriptor.volume_set_id,
	    128);
	cd9660_pad_string_spaces(diskStructure->primaryDescriptor.publisher_id,
	    128);
	cd9660_pad_string_spaces(diskStructure->primaryDescriptor.preparer_id,
	    128);
	cd9660_pad_string_spaces(diskStructure->primaryDescriptor.application_id,
	    128);
	cd9660_pad_string_spaces(
	    diskStructure->primaryDescriptor.copyright_file_id, 37);
	cd9660_pad_string_spaces(
		diskStructure->primaryDescriptor.abstract_file_id, 37);
	cd9660_pad_string_spaces(
		diskStructure->primaryDescriptor.bibliographic_file_id, 37);

	/* Setup dates */
	cd9660_time_8426(
	    (unsigned char *)diskStructure->primaryDescriptor.creation_date,
	    tstamp);
	cd9660_time_8426(
	    (unsigned char *)diskStructure->primaryDescriptor.modification_date,
	    tstamp);

#if 0
	cd9660_set_date(diskStructure->primaryDescriptor.expiration_date,
	    tstamp);
#endif

	memset(diskStructure->primaryDescriptor.expiration_date, '0', 16);
	diskStructure->primaryDescriptor.expiration_date[16] = 0;

	cd9660_time_8426(
	    (unsigned char *)diskStructure->primaryDescriptor.effective_date,
	    tstamp);
	/* make this sane */
	cd9660_time_915(diskStructure->rootNode->dot_record->isoDirRecord->date,
	    tstamp);
}

static void
cd9660_populate_iso_dir_record(struct _iso_directory_record_cd9660 *record,
			       u_char ext_attr_length, u_char flags,
			       u_char name_len, const char * name)
{
	record->ext_attr_length[0] = ext_attr_length;
	record->flags[0] = ISO_FLAG_CLEAR | flags;
	record->file_unit_size[0] = 0;
	record->interleave[0] = 0;
	cd9660_bothendian_word(1, record->volume_sequence_number);
	record->name_len[0] = name_len;
	memset(record->name, '\0', sizeof (record->name));
	memcpy(record->name, name, name_len);
	record->length[0] = 33 + name_len;

	/* Todo : better rounding */
	record->length[0] += (record->length[0] & 1) ? 1 : 0;
}

static void
cd9660_setup_root_node(iso9660_disk *diskStructure)
{
	cd9660_populate_iso_dir_record(diskStructure->rootNode->isoDirRecord,
	    0, ISO_FLAG_DIRECTORY, 1, "\0");

}

/*********** SUPPORT FUNCTIONS ***********/
static int
cd9660_setup_volume_descriptors(iso9660_disk *diskStructure)
{
	/* Boot volume descriptor should come second */
	int sector = 16;
	/* For now, a fixed 2 : PVD and terminator */
	volume_descriptor *temp, *t;

	/* Set up the PVD */
	temp = emalloc(sizeof(*temp));
	temp->volumeDescriptorData =
	   (unsigned char *)&diskStructure->primaryDescriptor;
	temp->volumeDescriptorData[0] = ISO_VOLUME_DESCRIPTOR_PVD;
	temp->volumeDescriptorData[6] = 1;
	temp->sector = sector;
	memcpy(temp->volumeDescriptorData + 1,
	    ISO_VOLUME_DESCRIPTOR_STANDARD_ID, 5);
	diskStructure->firstVolumeDescriptor = temp;

	sector++;
	/* Set up boot support if enabled. BVD must reside in sector 17 */
	if (diskStructure->is_bootable) {
		t = emalloc(sizeof(*t));
		t->volumeDescriptorData = ecalloc(1, 2048);
		temp->next = t;
		temp = t;
		t->sector = 17;
		if (diskStructure->verbose_level > 0)
			printf("Setting up boot volume descriptor\n");
		cd9660_setup_boot_volume_descriptor(diskStructure, t);
		sector++;
	}

	/* Set up the terminator */
	t = emalloc(sizeof(*t));
	t->volumeDescriptorData = ecalloc(1, 2048);
	temp->next = t;
	t->volumeDescriptorData[0] = ISO_VOLUME_DESCRIPTOR_TERMINATOR;
	t->next = NULL;
	t->volumeDescriptorData[6] = 1;
	t->sector = sector;
	memcpy(t->volumeDescriptorData + 1,
	    ISO_VOLUME_DESCRIPTOR_STANDARD_ID, 5);

	sector++;
	return sector;
}

#if 0
/*
 * Populate EAR at some point. Not required, but is used by NetBSD's
 * cd9660 support
 */
static int
cd9660_fill_extended_attribute_record(cd9660node *node)
{
	node->isoExtAttributes = emalloc(sizeof(*node->isoExtAttributes));
	return 1;
}
#endif

static int
cd9660_translate_node_common(iso9660_disk *diskStructure, cd9660node *newnode)
{
	time_t tstamp = stampst.st_ino ? stampst.st_mtime : time(NULL);
	u_char flag;
	char temp[ISO_FILENAME_MAXLENGTH_WITH_PADDING];

	/* Now populate the isoDirRecord structure */
	memset(temp, 0, ISO_FILENAME_MAXLENGTH_WITH_PADDING);

	(void)cd9660_convert_filename(diskStructure, newnode->node->name,
	    temp, !(S_ISDIR(newnode->node->type)));

	flag = ISO_FLAG_CLEAR;
	if (S_ISDIR(newnode->node->type))
		flag |= ISO_FLAG_DIRECTORY;

	cd9660_populate_iso_dir_record(newnode->isoDirRecord, 0,
	    flag, strlen(temp), temp);

	/* Set the various dates */

	/* If we want to use the current date and time */

	cd9660_time_915(newnode->isoDirRecord->date, tstamp);

	cd9660_bothendian_dword(newnode->fileDataLength,
	    newnode->isoDirRecord->size);
	/* If the file is a link, we want to set the size to 0 */
	if (S_ISLNK(newnode->node->type))
		newnode->fileDataLength = 0;

	return 1;
}

/*
 * Translate fsnode to cd9660node
 * Translate filenames and other metadata, including dates, sizes,
 * permissions, etc
 * @param struct fsnode * The node generated by makefs
 * @param struct cd9660node * The intermediate node to be written to
 * @returns int 0 on failure, 1 on success
 */
static int
cd9660_translate_node(iso9660_disk *diskStructure, fsnode *node,
    cd9660node *newnode)
{
	if (node == NULL) {
		if (diskStructure->verbose_level > 0)
			printf("%s: NULL node passed, returning\n", __func__);
		return 0;
	}
	newnode->isoDirRecord = emalloc(sizeof(*newnode->isoDirRecord));
	/* Set the node pointer */
	newnode->node = node;

	/* Set the size */
	if (!(S_ISDIR(node->type)))
		newnode->fileDataLength = node->inode->st.st_size;

	if (cd9660_translate_node_common(diskStructure, newnode) == 0)
		return 0;

	/* Finally, overwrite some of the values that are set by default */
	cd9660_time_915(newnode->isoDirRecord->date,
	    stampst.st_ino ? stampst.st_mtime : node->inode->st.st_mtime);

	return 1;
}

/*
 * Compares two ISO filenames
 * @param const char * The first file name
 * @param const char * The second file name
 * @returns : -1 if first is less than second, 0 if they are the same, 1 if
 * 	the second is greater than the first
 */
static int
cd9660_compare_filename(const char *first, const char *second)
{
	/*
	 * This can be made more optimal once it has been tested
	 * (the extra character, for example, is for testing)
	 */

	int p1 = 0;
	int p2 = 0;
	char c1, c2;
	/* First, on the filename */

	while (p1 < ISO_FILENAME_MAXLENGTH_BEFORE_VERSION-1
		&& p2 < ISO_FILENAME_MAXLENGTH_BEFORE_VERSION-1) {
		c1 = first[p1];
		c2 = second[p2];
		if (c1 == '.' && c2 =='.')
			break;
		else if (c1 == '.') {
			p2++;
			c1 = ' ';
		} else if (c2 == '.') {
			p1++;
			c2 = ' ';
		} else {
			p1++;
			p2++;
		}

		if (c1 < c2)
			return -1;
		else if (c1 > c2) {
			return 1;
		}
	}

	if (first[p1] == '.' && second[p2] == '.') {
		p1++;
		p2++;
		while (p1 < ISO_FILENAME_MAXLENGTH_BEFORE_VERSION - 1
			&& p2 < ISO_FILENAME_MAXLENGTH_BEFORE_VERSION - 1) {
			c1 = first[p1];
			c2 = second[p2];
			if (c1 == ';' && c2 == ';')
				break;
			else if (c1 == ';') {
				p2++;
				c1 = ' ';
			} else if (c2 == ';') {
				p1++;
				c2 = ' ';
			} else {
				p1++;
				p2++;
			}

			if (c1 < c2)
				return -1;
			else if (c1 > c2)
				return 1;
		}
	}
	return 0;
}

/*
 * Insert a node into list with ISO sorting rules
 * @param cd9660node * The head node of the list
 * @param cd9660node * The node to be inserted
 */
static void
cd9660_sorted_child_insert(cd9660node *parent, cd9660node *cn_new)
{
	int compare;
	cd9660node *cn;
	struct cd9660_children_head *head = &parent->cn_children;

	/* TODO: Optimize? */
	cn_new->parent = parent;

	/*
	 * first will either be 0, the . or the ..
	 * if . or .., this means no other entry may be written before first
	 * if 0, the new node may be inserted at the head
	 */

	TAILQ_FOREACH(cn, head, cn_next_child) {
		/*
		 * Dont insert a node twice -
		 * that would cause an infinite loop
		 */
		if (cn_new == cn)
			return;

		compare = cd9660_compare_filename(cn_new->isoDirRecord->name,
			cn->isoDirRecord->name);

		if (compare == 0)
			compare = cd9660_compare_filename(cn_new->node->name,
				cn->node->name);

		if (compare < 0)
			break;
	}
	if (cn == NULL)
		TAILQ_INSERT_TAIL(head, cn_new, cn_next_child);
	else
		TAILQ_INSERT_BEFORE(cn, cn_new, cn_next_child);
}

/*
 * Called After cd9660_sorted_child_insert
 * handles file collisions by suffixing each filname with ~n
 * where n represents the files respective place in the ordering
 */
static int
cd9660_handle_collisions(iso9660_disk *diskStructure, cd9660node *colliding,
    int past)
{
	cd9660node *iter, *next, *prev;
	int skip;
	int delete_chars = 0;
	int temp_past = past;
	int temp_skip;
	int flag = 0;
	cd9660node *end_of_range;

	for (iter = TAILQ_FIRST(&colliding->cn_children);
	     iter != NULL && (next = TAILQ_NEXT(iter, cn_next_child)) != NULL;) {
		if (strcmp(iter->isoDirRecord->name,
		           next->isoDirRecord->name) != 0) {
			iter = TAILQ_NEXT(iter, cn_next_child);
			continue;
		}
		flag = 1;
		temp_skip = skip = cd9660_count_collisions(iter);
		end_of_range = iter;
		while (temp_skip > 0) {
			temp_skip--;
			end_of_range = TAILQ_NEXT(end_of_range, cn_next_child);
		}
		temp_past = past;
		while (temp_past > 0) {
			if ((next = TAILQ_NEXT(end_of_range, cn_next_child)) != NULL)
				end_of_range = next;
			else if ((prev = TAILQ_PREV(iter, cd9660_children_head, cn_next_child)) != NULL)
				iter = prev;
			else
				delete_chars++;
			temp_past--;
		}
		skip += past;
		iter = cd9660_rename_filename(diskStructure, iter, skip,
		    delete_chars);
	}
	return flag;
}


static cd9660node *
cd9660_rename_filename(iso9660_disk *diskStructure, cd9660node *iter, int num,
    int delete_chars)
{
	int i = 0;
	int numbts, digit, digits, temp, powers, count;
	char *naming;
	int maxlength;
        char *tmp;

	if (diskStructure->verbose_level > 0)
		printf("Rename_filename called\n");

	assert(1 <= diskStructure->isoLevel && diskStructure->isoLevel <= 2);
	/* TODO : A LOT of chanes regarding 8.3 filenames */
	if (diskStructure->isoLevel == 1)
		maxlength = 8;
	else if (diskStructure->isoLevel == 2)
		maxlength = 31;
	else
		maxlength = ISO_FILENAME_MAXLENGTH_BEFORE_VERSION;

	tmp = emalloc(ISO_FILENAME_MAXLENGTH_WITH_PADDING);

	while (i < num && iter) {
		powers = 1;
		count = 0;
		digits = 1;
		while (((int)(i / powers) ) >= 10) {
			digits++;
			powers = powers * 10;
		}

		naming = iter->o_name;

		/*
		while ((*naming != '.') && (*naming != ';')) {
			naming++;
			count++;
		}
		*/

		while (count < maxlength) {
			if (*naming == ';')
				break;
			naming++;
			count++;
		}

		if ((count + digits) < maxlength)
			numbts = count;
		else
			numbts = maxlength - (digits);
		numbts -= delete_chars;

		/* 8.3 rules - keep the extension, add before the dot */

		/*
		 * This code makes a bunch of assumptions.
		 * See if you can spot them all :)
		 */

#if 0
		if (diskStructure->isoLevel == 1) {
			numbts = 8 - digits - delete_chars;
			if (dot < 0) {

			} else {
				if (dot < 8) {
					memmove(&tmp[numbts],&tmp[dot],4);
				}
			}
		}
#endif

		/* (copying just the filename before the '.' */
		memcpy(tmp, (iter->o_name), numbts);

		/* adding the appropriate number following the name */
		temp = i;
		while (digits > 0) {
			digit = (int)(temp / powers);
			temp = temp - digit * powers;
			sprintf(&tmp[numbts] , "%d", digit);
			digits--;
			numbts++;
			powers = powers / 10;
		}

		while ((*naming != ';')  && (numbts < maxlength)) {
			tmp[numbts] = (*naming);
			naming++;
			numbts++;
		}

		tmp[numbts] = ';';
		tmp[numbts+1] = '1';
		tmp[numbts+2] = '\0';

		/*
		 * now tmp has exactly the identifier
		 * we want so we'll copy it back to record
		 */
		memcpy((iter->isoDirRecord->name), tmp, numbts + 3);

		iter = TAILQ_NEXT(iter, cn_next_child);
		i++;
	}

	free(tmp);
	return iter;
}

/* Todo: Figure out why these functions are nec. */
static void
cd9660_copy_filenames(iso9660_disk *diskStructure, cd9660node *node)
{
	cd9660node *cn;

	if (TAILQ_EMPTY(&node->cn_children))
		return;

	if (TAILQ_FIRST(&node->cn_children)->isoDirRecord == NULL) {
		debug_print_tree(diskStructure, diskStructure->rootNode, 0);
		exit(1);
	}

	TAILQ_FOREACH(cn, &node->cn_children, cn_next_child) {
		cd9660_copy_filenames(diskStructure, cn);
		memcpy(cn->o_name, cn->isoDirRecord->name,
		    ISO_FILENAME_MAXLENGTH_WITH_PADDING);
	}
}

static void
cd9660_sorting_nodes(cd9660node *node)
{
	cd9660node *cn;

	TAILQ_FOREACH(cn, &node->cn_children, cn_next_child)
		cd9660_sorting_nodes(cn);
	cd9660_sort_nodes(node);
}

/* XXX Bubble sort. */
static void
cd9660_sort_nodes(cd9660node *node)
{
	cd9660node *cn, *next;

	do {
		TAILQ_FOREACH(cn, &node->cn_children, cn_next_child) {
			if ((next = TAILQ_NEXT(cn, cn_next_child)) == NULL)
				return;
			else if (strcmp(next->isoDirRecord->name,
				        cn->isoDirRecord->name) >= 0)
				continue;
			TAILQ_REMOVE(&node->cn_children, next, cn_next_child);
			TAILQ_INSERT_BEFORE(cn, next, cn_next_child);
			break;
		}
	} while (cn != NULL);
}

static int
cd9660_count_collisions(cd9660node *copy)
{
	int count = 0;
	cd9660node *iter, *next;

	for (iter = copy;
	     (next = TAILQ_NEXT(iter, cn_next_child)) != NULL;
	     iter = next) {
		if (cd9660_compare_filename(iter->isoDirRecord->name,
			next->isoDirRecord->name) == 0)
			count++;
		else
			return count;
	}
#if 0
	if ((next = TAILQ_NEXT(iter, cn_next_child)) != NULL) {
		printf("%s: count is %i\n", __func__, count);
		compare = cd9660_compare_filename(iter->isoDirRecord->name,
			next->isoDirRecord->name);
		if (compare == 0) {
			count++;
			return cd9660_recurse_on_collision(next, count);
		} else
			return count;
	}
#endif
	return count;
}

static cd9660node *
cd9660_rrip_move_directory(iso9660_disk *diskStructure, cd9660node *dir)
{
	char newname[9];
	cd9660node *tfile;

	/*
	 * This function needs to:
	 * 1) Create an empty virtual file in place of the old directory
	 * 2) Point the virtual file to the new directory
	 * 3) Point the relocated directory to its old parent
	 * 4) Move the directory specified by dir into rr_moved_dir,
	 * and rename it to "diskStructure->rock_ridge_move_count" (as a string)
	 */

	/* First see if the moved directory even exists */
	if (diskStructure->rr_moved_dir == NULL) {
		diskStructure->rr_moved_dir = cd9660_create_directory(
		    diskStructure, ISO_RRIP_DEFAULT_MOVE_DIR_NAME,
		    diskStructure->rootNode, dir);
		if (diskStructure->rr_moved_dir == NULL)
			return 0;
		cd9660_time_915(diskStructure->rr_moved_dir->isoDirRecord->date,
		    stampst.st_ino ? stampst.st_mtime : start_time.tv_sec);
	}

	/* Create a file with the same ORIGINAL name */
	tfile = cd9660_create_file(diskStructure, dir->node->name, dir->parent,
	    dir);
	if (tfile == NULL)
		return NULL;

	diskStructure->rock_ridge_move_count++;
	snprintf(newname, sizeof(newname), "%08i",
	    diskStructure->rock_ridge_move_count);

	/* Point to old parent */
	dir->rr_real_parent = dir->parent;

	/* Place the placeholder file */
	if (TAILQ_EMPTY(&dir->rr_real_parent->cn_children)) {
		TAILQ_INSERT_HEAD(&dir->rr_real_parent->cn_children, tfile,
		    cn_next_child);
	} else {
		cd9660_sorted_child_insert(dir->rr_real_parent, tfile);
	}

	/* Point to new parent */
	dir->parent = diskStructure->rr_moved_dir;

	/* Point the file to the moved directory */
	tfile->rr_relocated = dir;

	/* Actually move the directory */
	cd9660_sorted_child_insert(diskStructure->rr_moved_dir, dir);

	/* TODO: Inherit permissions / ownership (basically the entire inode) */

	/* Set the new name */
	memset(dir->isoDirRecord->name, 0, ISO_FILENAME_MAXLENGTH_WITH_PADDING);
	strncpy(dir->isoDirRecord->name, newname, 8);
	dir->isoDirRecord->length[0] = 34 + 8;
	dir->isoDirRecord->name_len[0] = 8;

	return dir;
}

static int
cd9660_add_dot_records(iso9660_disk *diskStructure, cd9660node *root)
{
	struct cd9660_children_head *head = &root->cn_children;
	cd9660node *cn;

	TAILQ_FOREACH(cn, head, cn_next_child) {
		if ((cn->type & CD9660_TYPE_DIR) == 0)
			continue;
		/* Recursion first */
		cd9660_add_dot_records(diskStructure, cn);
	}
	cd9660_create_special_directory(diskStructure, CD9660_TYPE_DOT, root);
	cd9660_create_special_directory(diskStructure, CD9660_TYPE_DOTDOT,
	    root);
	return 1;
}

/*
 * Convert node to cd9660 structure
 * This function is designed to be called recursively on the root node of
 * the filesystem
 * Lots of recursion going on here, want to make sure it is efficient
 * @param struct fsnode * The root node to be converted
 * @param struct cd9660* The parent node (should not be NULL)
 * @param int Current directory depth
 * @param int* Running count of the number of directories that are being created
 */
static void
cd9660_convert_structure(iso9660_disk *diskStructure, fsnode *root,
    cd9660node *parent_node, int level, int *numDirectories, int *error)
{
	fsnode *iterator = root;
	cd9660node *this_node;
	int working_level;
	int add;
	int flag = 0;
	int counter = 0;

	/*
	 * Newer, more efficient method, reduces recursion depth
	 */
	if (root == NULL) {
		warnx("%s: root is null", __func__);
		return;
	}

	/* Test for an empty directory - makefs still gives us the . record */
	if ((S_ISDIR(root->type)) && (root->name[0] == '.')
		&& (root->name[1] == '\0')) {
		root = root->next;
		if (root == NULL)
			return;
	}
	if ((this_node = cd9660_allocate_cd9660node()) == NULL) {
		CD9660_MEM_ALLOC_ERROR(__func__);
	}

	/*
	 * To reduce the number of recursive calls, we will iterate over
	 * the next pointers to the right.
	 */
	while (iterator != NULL) {
		add = 1;
		/*
		 * Increment the directory count if this is a directory
		 * Ignore "." entries. We will generate them later
		 */
		if (!S_ISDIR(iterator->type) ||
		    strcmp(iterator->name, ".") != 0) {

			/* Translate the node, including its filename */
			this_node->parent = parent_node;
			cd9660_translate_node(diskStructure, iterator,
			    this_node);
			this_node->level = level;

			if (S_ISDIR(iterator->type)) {
				(*numDirectories)++;
				this_node->type = CD9660_TYPE_DIR;
				working_level = level + 1;

				/*
				 * If at level 8, directory would be at 8
				 * and have children at 9 which is not
				 * allowed as per ISO spec
				 */
				if (level == 8) {
					if ((!diskStructure->allow_deep_trees) &&
					  (!diskStructure->rock_ridge_enabled)) {
						warnx("error: found entry "
						     "with depth greater "
						     "than 8.");
						(*error) = 1;
						return;
					} else if (diskStructure->
						   rock_ridge_enabled) {
						working_level = 3;
						/*
						 * Moved directory is actually
						 * at level 2.
						 */
						this_node->level =
						    working_level - 1;
						if (cd9660_rrip_move_directory(
							diskStructure,
							this_node) == NULL) {
							warnx("Failure in "
							      "cd9660_rrip_"
							      "move_directory"
							);
							(*error) = 1;
							return;
						}
						add = 0;
					}
				}

				/* Do the recursive call on the children */
				if (iterator->child != NULL) {
					cd9660_convert_structure(diskStructure,
						iterator->child, this_node,
						working_level,
						numDirectories, error);

					if ((*error) == 1) {
						warnx("%s: Error on recursive "
						    "call", __func__);
						return;
					}
				}

			} else {
				/* Only directories should have children */
				assert(iterator->child == NULL);

				this_node->type = CD9660_TYPE_FILE;
			}

			/*
			 * Finally, do a sorted insert
			 */
			if (add) {
				cd9660_sorted_child_insert(
				    parent_node, this_node);
			}

			/*Allocate new temp_node */
			if (iterator->next != NULL) {
				this_node = cd9660_allocate_cd9660node();
				if (this_node == NULL)
					CD9660_MEM_ALLOC_ERROR(__func__);
			}
		}
		iterator = iterator->next;
	}

	/* cd9660_handle_collisions(first_node); */

	/* TODO: need cleanup */
	cd9660_copy_filenames(diskStructure, parent_node);

	do {
		flag = cd9660_handle_collisions(diskStructure, parent_node,
		    counter);
		counter++;
		cd9660_sorting_nodes(parent_node);
	} while ((flag == 1) && (counter < 100));
}

/*
 * Clean up the cd9660node tree
 * This is designed to be called recursively on the root node
 * @param struct cd9660node *root The node to free
 * @returns void
 */
static void
cd9660_free_structure(cd9660node *root)
{
	cd9660node *cn;

	while ((cn = TAILQ_FIRST(&root->cn_children)) != NULL) {
		TAILQ_REMOVE(&root->cn_children, cn, cn_next_child);
		cd9660_free_structure(cn);
	}
	free(root);
}

/*
 * Be a little more memory conservative:
 * instead of having the TAILQ_ENTRY as part of the cd9660node,
 * just create a temporary structure
 */
static struct ptq_entry
{
	TAILQ_ENTRY(ptq_entry) ptq;
	cd9660node *node;
} *n;

#define PTQUEUE_NEW(n,s,r,t){\
	n = emalloc(sizeof(struct s));	\
	n->node = t;\
}

/*
 * Generate the path tables
 * The specific implementation of this function is left as an exercise to the
 * programmer. It could be done recursively. Make sure you read how the path
 * table has to be laid out, it has levels.
 * @param struct iso9660_disk *disk The disk image
 * @returns int The number of built path tables (between 1 and 4), 0 on failure
 */
static int
cd9660_generate_path_table(iso9660_disk *diskStructure)
{
	cd9660node *cn, *dirNode = diskStructure->rootNode;
	cd9660node *last = dirNode;
	int pathTableSize = 0;	/* computed as we go */
	int counter = 1;	/* root gets a count of 0 */

	TAILQ_HEAD(cd9660_pt_head, ptq_entry) pt_head;
	TAILQ_INIT(&pt_head);

	PTQUEUE_NEW(n, ptq_entry, -1, diskStructure->rootNode);

	/* Push the root node */
	TAILQ_INSERT_HEAD(&pt_head, n, ptq);

	/* Breadth-first traversal of file structure */
	while (pt_head.tqh_first != 0) {
		n = pt_head.tqh_first;
		dirNode = n->node;
		TAILQ_REMOVE(&pt_head, pt_head.tqh_first, ptq);
		free(n);

		/* Update the size */
		pathTableSize += ISO_PATHTABLE_ENTRY_BASESIZE
		    + dirNode->isoDirRecord->name_len[0]+
			(dirNode->isoDirRecord->name_len[0] % 2 == 0 ? 0 : 1);
			/* includes the padding bit */

		dirNode->ptnumber=counter;
		if (dirNode != last) {
			last->ptnext = dirNode;
			dirNode->ptprev = last;
		}
		last = dirNode;

		/* Push children onto queue */
		TAILQ_FOREACH(cn, &dirNode->cn_children, cn_next_child) {
			/*
			 * Dont add the DOT and DOTDOT types to the path
			 * table.
			 */
			if ((cn->type != CD9660_TYPE_DOT)
				&& (cn->type != CD9660_TYPE_DOTDOT)) {

				if (S_ISDIR(cn->node->type)) {
					PTQUEUE_NEW(n, ptq_entry, -1, cn);
					TAILQ_INSERT_TAIL(&pt_head, n, ptq);
				}
			}
		}
		counter++;
	}
	return pathTableSize;
}

void
cd9660_compute_full_filename(cd9660node *node, char *buf)
{
	int len;

	len = CD9660MAXPATH + 1;
	len = snprintf(buf, len, "%s/%s/%s", node->node->root,
	    node->node->path, node->node->name);
	if (len > CD9660MAXPATH)
		errx(EXIT_FAILURE, "Pathname too long.");
}

/* NEW filename conversion method */
typedef int(*cd9660_filename_conversion_functor)(iso9660_disk *, const char *,
    char *, int);


/*
 * TODO: These two functions are almost identical.
 * Some code cleanup is possible here
 *
 * XXX bounds checking!
 */
static int
cd9660_level1_convert_filename(iso9660_disk *diskStructure, const char *oldname,
    char *newname, int is_file)
{
	/*
	 * ISO 9660 : 10.1
	 * File Name shall not contain more than 8 d or d1 characters
	 * File Name Extension shall not contain more than 3 d or d1 characters
	 * Directory Identifier shall not contain more than 8 d or d1 characters
	 */
	int namelen = 0;
	int extlen = 0;
	int found_ext = 0;

	while (*oldname != '\0' && extlen < 3) {
		/* Handle period first, as it is special */
		if (*oldname == '.') {
			if (found_ext) {
				*newname++ = '_';
				extlen ++;
			}
			else {
				*newname++ = '.';
				found_ext = 1;
			}
		} else {
			/* cut RISC OS file type off ISO name */
			if (diskStructure->archimedes_enabled &&
			    *oldname == ',' && strlen(oldname) == 4)
				break;

			/* Enforce 12.3 / 8 */
			if (namelen == 8 && !found_ext)
				break;

			if (islower((unsigned char)*oldname))
				*newname++ = toupper((unsigned char)*oldname);
			else if (isupper((unsigned char)*oldname)
			    || isdigit((unsigned char)*oldname))
				*newname++ = *oldname;
			else
				*newname++ = '_';

			if (found_ext)
				extlen++;
			else
				namelen++;
		}
		oldname++;
	}
	if (is_file) {
		if (!found_ext && !diskStructure->omit_trailing_period)
			*newname++ = '.';
		/* Add version */
		sprintf(newname, ";%i", 1);
	}
	return namelen + extlen + found_ext;
}

/* XXX bounds checking! */
static int
cd9660_level2_convert_filename(iso9660_disk *diskStructure, const char *oldname,
    char *newname, int is_file)
{
	/*
	 * ISO 9660 : 7.5.1
	 * File name : 0+ d or d1 characters
	 * separator 1 (.)
	 * File name extension : 0+ d or d1 characters
	 * separator 2 (;)
	 * File version number (5 characters, 1-32767)
	 * 1 <= Sum of File name and File name extension <= 30
	 */
	int namelen = 0;
	int extlen = 0;
	int found_ext = 0;

	while (*oldname != '\0' && namelen + extlen < 30) {
		/* Handle period first, as it is special */
		if (*oldname == '.') {
			if (found_ext) {
				if (diskStructure->allow_multidot) {
					*newname++ = '.';
				} else {
					*newname++ = '_';
				}
				extlen ++;
			}
			else {
				*newname++ = '.';
				found_ext = 1;
			}
		} else {
			/* cut RISC OS file type off ISO name */
			if (diskStructure->archimedes_enabled &&
			    *oldname == ',' && strlen(oldname) == 4)
				break;

			 if (islower((unsigned char)*oldname))
				*newname++ = toupper((unsigned char)*oldname);
			else if (isupper((unsigned char)*oldname) ||
			    isdigit((unsigned char)*oldname))
				*newname++ = *oldname;
			else if (diskStructure->allow_multidot &&
			    *oldname == '.') {
			    	*newname++ = '.';
			} else {
				*newname++ = '_';
			}

			if (found_ext)
				extlen++;
			else
				namelen++;
		}
		oldname ++;
	}
	if (is_file) {
		if (!found_ext && !diskStructure->omit_trailing_period)
			*newname++ = '.';
		/* Add version */
		sprintf(newname, ";%i", 1);
	}
	return namelen + extlen + found_ext;
}

#if 0
static int
cd9660_joliet_convert_filename(iso9660_disk *diskStructure, const char *oldname,
    char *newname, int is_file)
{
	/* TODO: implement later, move to cd9660_joliet.c ?? */
}
#endif


/*
 * Convert a file name to ISO compliant file name
 * @param char * oldname The original filename
 * @param char ** newname The new file name, in the appropriate character
 *                        set and of appropriate length
 * @param int 1 if file, 0 if directory
 * @returns int The length of the new string
 */
static int
cd9660_convert_filename(iso9660_disk *diskStructure, const char *oldname,
    char *newname, int is_file)
{
	assert(1 <= diskStructure->isoLevel && diskStructure->isoLevel <= 2);
	/* NEW */
	cd9660_filename_conversion_functor conversion_function = NULL;
	if (diskStructure->isoLevel == 1)
		conversion_function = &cd9660_level1_convert_filename;
	else if (diskStructure->isoLevel == 2)
		conversion_function = &cd9660_level2_convert_filename;
	return (*conversion_function)(diskStructure, oldname, newname, is_file);
}

int
cd9660_compute_record_size(iso9660_disk *diskStructure, cd9660node *node)
{
	int size = node->isoDirRecord->length[0];

	if (diskStructure->rock_ridge_enabled)
		size += node->susp_entry_size;
	size += node->su_tail_size;
	size += size & 1; /* Ensure length of record is even. */
	assert(size <= 254);
	return size;
}

static void
cd9660_populate_dot_records(iso9660_disk *diskStructure, cd9660node *node)
{
	node->dot_record->fileDataSector = node->fileDataSector;
	memcpy(node->dot_record->isoDirRecord,node->isoDirRecord, 34);
	node->dot_record->isoDirRecord->name_len[0] = 1;
	node->dot_record->isoDirRecord->name[0] = 0;
	node->dot_record->isoDirRecord->name[1] = 0;
	node->dot_record->isoDirRecord->length[0] = 34;
	node->dot_record->fileRecordSize =
	    cd9660_compute_record_size(diskStructure, node->dot_record);

	if (node == diskStructure->rootNode) {
		node->dot_dot_record->fileDataSector = node->fileDataSector;
		memcpy(node->dot_dot_record->isoDirRecord,node->isoDirRecord,
		    34);
	} else {
		node->dot_dot_record->fileDataSector =
		    node->parent->fileDataSector;
		memcpy(node->dot_dot_record->isoDirRecord,
		    node->parent->isoDirRecord,34);
	}
	node->dot_dot_record->isoDirRecord->name_len[0] = 1;
	node->dot_dot_record->isoDirRecord->name[0] = 1;
	node->dot_dot_record->isoDirRecord->name[1] = 0;
	node->dot_dot_record->isoDirRecord->length[0] = 34;
	node->dot_dot_record->fileRecordSize =
	    cd9660_compute_record_size(diskStructure, node->dot_dot_record);
}

/*
 * @param struct cd9660node *node The node
 * @param int The offset (in bytes) - SHOULD align to the beginning of a sector
 * @returns int The total size of files and directory entries (should be
 *              a multiple of sector size)
*/
static int64_t
cd9660_compute_offsets(iso9660_disk *diskStructure, cd9660node *node,
    int64_t startOffset)
{
	/*
	 * This function needs to compute the size of directory records and
	 * runs, file lengths, and set the appropriate variables both in
	 * cd9660node and isoDirEntry
	 */
	int64_t used_bytes = 0;
	int64_t current_sector_usage = 0;
	cd9660node *child;
	fsinode *inode;
	int64_t r;

	assert(node != NULL);


	/*
	 * NOTE : There needs to be some special case detection for
	 * the "real root" node, since for it, node->node is undefined
	 */

	node->fileDataSector = -1;

	if (node->type & CD9660_TYPE_DIR) {
		node->fileRecordSize = cd9660_compute_record_size(
		    diskStructure, node);
		/*Set what sector this directory starts in*/
		node->fileDataSector =
		    CD9660_BLOCKS(diskStructure->sectorSize,startOffset);

		cd9660_bothendian_dword(node->fileDataSector,
		    node->isoDirRecord->extent);

		/*
		 * First loop over children, need to know the size of
		 * their directory records
		 */
		node->fileSectorsUsed = 1;
		TAILQ_FOREACH(child, &node->cn_children, cn_next_child) {
			node->fileDataLength +=
			    cd9660_compute_record_size(diskStructure, child);
			if ((cd9660_compute_record_size(diskStructure, child) +
			    current_sector_usage) >=
		 	    diskStructure->sectorSize) {
				current_sector_usage = 0;
				node->fileSectorsUsed++;
			}

			current_sector_usage +=
			    cd9660_compute_record_size(diskStructure, child);
		}

		cd9660_bothendian_dword(node->fileSectorsUsed *
			diskStructure->sectorSize,node->isoDirRecord->size);

		/*
		 * This should point to the sector after the directory
		 * record (or, the first byte in that sector)
		 */
		used_bytes += node->fileSectorsUsed * diskStructure->sectorSize;

		for (child = TAILQ_NEXT(node->dot_dot_record, cn_next_child);
		     child != NULL; child = TAILQ_NEXT(child, cn_next_child)) {
			/* Directories need recursive call */
			if (S_ISDIR(child->node->type)) {
				r = cd9660_compute_offsets(diskStructure, child,
				    used_bytes + startOffset);

				if (r != -1)
					used_bytes += r;
				else
					return -1;
			}
		}

		/* Explicitly set the . and .. records */
		cd9660_populate_dot_records(diskStructure, node);

		/* Finally, do another iteration to write the file data*/
		for (child = TAILQ_NEXT(node->dot_dot_record, cn_next_child);
		     child != NULL;
		     child = TAILQ_NEXT(child, cn_next_child)) {
			/* Files need extent set */
			if (S_ISDIR(child->node->type))
				continue;
			child->fileRecordSize =
			    cd9660_compute_record_size(diskStructure, child);

			child->fileSectorsUsed =
			    CD9660_BLOCKS(diskStructure->sectorSize,
				child->fileDataLength);

			inode = child->node->inode;
			if ((inode->flags & FI_ALLOCATED) == 0) {
				inode->ino =
				    CD9660_BLOCKS(diskStructure->sectorSize,
				        used_bytes + startOffset);
				inode->flags |= FI_ALLOCATED;
				used_bytes += child->fileSectorsUsed *
				    diskStructure->sectorSize;
			} else {
				INODE_WARNX(("%s: already allocated inode %d "
				      "data sectors at %" PRIu32, __func__,
				      (int)inode->st.st_ino, inode->ino));
			}
			child->fileDataSector = inode->ino;
			cd9660_bothendian_dword(child->fileDataSector,
				child->isoDirRecord->extent);
		}
	}

	return used_bytes;
}

#if 0
/* Might get rid of this func */
static int
cd9660_copy_stat_info(cd9660node *from, cd9660node *to, int file)
{
	to->node->inode->st.st_dev = 0;
	to->node->inode->st.st_ino = 0;
	to->node->inode->st.st_size = 0;
	to->node->inode->st.st_blksize = from->node->inode->st.st_blksize;
	to->node->inode->st.st_atime = from->node->inode->st.st_atime;
	to->node->inode->st.st_mtime = from->node->inode->st.st_mtime;
	to->node->inode->st.st_ctime = from->node->inode->st.st_ctime;
	to->node->inode->st.st_uid = from->node->inode->st.st_uid;
	to->node->inode->st.st_gid = from->node->inode->st.st_gid;
	to->node->inode->st.st_mode = from->node->inode->st.st_mode;
	/* Clear out type */
	to->node->inode->st.st_mode = to->node->inode->st.st_mode & ~(S_IFMT);
	if (file)
		to->node->inode->st.st_mode |= S_IFREG;
	else
		to->node->inode->st.st_mode |= S_IFDIR;
	return 1;
}
#endif

static cd9660node *
cd9660_create_virtual_entry(iso9660_disk *diskStructure, const char *name,
    cd9660node *parent, int file, int insert)
{
	cd9660node *temp;
	fsnode * tfsnode;

	assert(parent != NULL);

	temp = cd9660_allocate_cd9660node();
	if (temp == NULL)
		return NULL;

	tfsnode = emalloc(sizeof(*tfsnode));
	tfsnode->name = estrdup(name);
	temp->isoDirRecord = emalloc(sizeof(*temp->isoDirRecord));

	cd9660_convert_filename(diskStructure, tfsnode->name,
	    temp->isoDirRecord->name, file);

	temp->node = tfsnode;
	temp->parent = parent;

	if (insert) {
		if (temp->parent != NULL) {
			temp->level = temp->parent->level + 1;
			if (!TAILQ_EMPTY(&temp->parent->cn_children))
				cd9660_sorted_child_insert(temp->parent, temp);
			else
				TAILQ_INSERT_HEAD(&temp->parent->cn_children,
				    temp, cn_next_child);
		}
	}

	if (parent->node != NULL) {
		tfsnode->type = parent->node->type;
	}

	/* Clear out file type bits */
	tfsnode->type &= ~(S_IFMT);
	if (file)
		tfsnode->type |= S_IFREG;
	else
		tfsnode->type |= S_IFDIR;

	/* Indicate that there is no spec entry (inode) */
	tfsnode->flags &= ~(FSNODE_F_HASSPEC);
#if 0
	cd9660_copy_stat_info(parent, temp, file);
#endif
	return temp;
}

static cd9660node *
cd9660_create_file(iso9660_disk *diskStructure, const char *name,
    cd9660node *parent, cd9660node *me)
{
	cd9660node *temp;

	temp = cd9660_create_virtual_entry(diskStructure, name, parent, 1, 1);
	if (temp == NULL)
		return NULL;

	temp->fileDataLength = 0;

	temp->type = CD9660_TYPE_FILE | CD9660_TYPE_VIRTUAL;

	temp->node->inode = ecalloc(1, sizeof(*temp->node->inode));
	*temp->node->inode = *me->node->inode;

	if (cd9660_translate_node_common(diskStructure, temp) == 0)
		return NULL;
	return temp;
}

/*
 * Create a new directory which does not exist on disk
 * @param const char * name The name to assign to the directory
 * @param const char * parent Pointer to the parent directory
 * @returns cd9660node * Pointer to the new directory
 */
static cd9660node *
cd9660_create_directory(iso9660_disk *diskStructure, const char *name,
    cd9660node *parent, cd9660node *me)
{
	cd9660node *temp;

	temp = cd9660_create_virtual_entry(diskStructure, name, parent, 0, 1);
	if (temp == NULL)
		return NULL;
	temp->node->type |= S_IFDIR;

	temp->type = CD9660_TYPE_DIR | CD9660_TYPE_VIRTUAL;

	temp->node->inode = ecalloc(1, sizeof(*temp->node->inode));
	*temp->node->inode = *me->node->inode;

	if (cd9660_translate_node_common(diskStructure, temp) == 0)
		return NULL;
	return temp;
}

static cd9660node *
cd9660_create_special_directory(iso9660_disk *diskStructure, u_char type,
    cd9660node *parent)
{
	cd9660node *temp, *first;
	char na[2];

	assert(parent != NULL);

	if (type == CD9660_TYPE_DOT)
		na[0] = 0;
	else if (type == CD9660_TYPE_DOTDOT)
		na[0] = 1;
	else
		return 0;

	na[1] = 0;
	if ((temp = cd9660_create_virtual_entry(diskStructure, na, parent,
	    0, 0)) == NULL)
		return NULL;

	temp->parent = parent;
	temp->type = type;
	temp->isoDirRecord->length[0] = 34;
	/* Dot record is always first */
	if (type == CD9660_TYPE_DOT) {
		parent->dot_record = temp;
		TAILQ_INSERT_HEAD(&parent->cn_children, temp, cn_next_child);
	/* DotDot should be second */
	} else if (type == CD9660_TYPE_DOTDOT) {
		parent->dot_dot_record = temp;
		/*
                 * If the first child is the dot record, insert
                 * this second.  Otherwise, insert it at the head.
		 */
		if ((first = TAILQ_FIRST(&parent->cn_children)) == NULL ||
		    (first->type & CD9660_TYPE_DOT) == 0) {
			TAILQ_INSERT_HEAD(&parent->cn_children, temp,
			    cn_next_child);
		} else {
			TAILQ_INSERT_AFTER(&parent->cn_children, first, temp,
			    cn_next_child);
		}
	}

	return temp;
}

static int
cd9660_add_generic_bootimage(iso9660_disk *diskStructure, const char *bootimage)
{
	struct stat stbuf;

	assert(bootimage != NULL);

	if (*bootimage == '\0') {
		warnx("Error: Boot image must be a filename");
		return 0;
	}

	diskStructure->generic_bootimage = estrdup(bootimage);

	/* Get information about the file */
	if (lstat(diskStructure->generic_bootimage, &stbuf) == -1)
		err(EXIT_FAILURE, "%s: lstat(\"%s\")", __func__,
		    diskStructure->generic_bootimage);

	if (stbuf.st_size > 32768) {
		warnx("Error: Boot image must be no greater than 32768 bytes");
		return 0;
	}

	if (diskStructure->verbose_level > 0) {
		printf("Generic boot image has size %lld\n",
		    (long long)stbuf.st_size);
	}

	diskStructure->has_generic_bootimage = 1;

	return 1;
}
