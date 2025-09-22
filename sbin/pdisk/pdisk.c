/*	$OpenBSD: pdisk.c,v 1.87 2016/05/28 22:26:13 tb Exp $	*/

/*
 * pdisk - an editor for Apple format partition tables
 *
 * Written by Eryk Vershen
 *
 * Still under development (as of 15 January 1998)
 */

/*
 * Copyright 1996,1997,1998 by Apple Computer, Inc.
 *              All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies and
 * that both the copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * APPLE COMPUTER DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL APPLE COMPUTER BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>		/* DEV_BSIZE */
#include <sys/dkio.h>
#include <sys/disklabel.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "partition_map.h"
#include "io.h"
#include "dump.h"

int	lflag;	/* list the device */
int	rflag;	/* open device read Only */

static int	first_get = 1;

void	do_dump_map(struct partition_map *, int);
void	do_change_map_size(struct partition_map *);
void	do_create_partition(struct partition_map *, int);
void	do_delete_partition(struct partition_map *);
void	do_display_entry(struct partition_map *);
void	do_rename_partition(struct partition_map *);
void	do_change_type(struct partition_map *);
void	do_reorder(struct partition_map *);
void	do_write_partition_map(struct partition_map *);
void	edit(struct partition_map **);
int	get_base_argument(long *, struct partition_map *);
int	get_size_argument(long *, struct partition_map *);

__dead static void usage(void);

int
main(int argc, char **argv)
{
	struct disklabel dl;
	struct stat st;
	struct partition_map *map;
	int c, fd, oflags;

	oflags = O_RDWR;
	while ((c = getopt(argc, argv, "lr")) != -1) {
		switch (c) {
		case 'l':
			lflag = 1;
			oflags = O_RDONLY;
			break;
		case 'r':
			rflag = 1;
			oflags = O_RDONLY;
			break;
		default:
			usage();
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	fd = opendev(*argv, oflags, OPENDEV_PART, NULL);
	if (fd == -1)
		err(1, "can't open file '%s'", *argv);

	if (fstat(fd, &st) == -1)
		err(1, "can't fstat %s", *argv);
	if (!S_ISCHR(st.st_mode))
		errx(1, "%s is not a character device", *argv);

	if (ioctl(fd, DIOCGPDINFO, &dl) == -1)
		err(1, "can't get disklabel for %s", *argv);
	if (dl.d_secsize != DEV_BSIZE)
		errx(1, "disk sector size (%d) != 512\n", dl.d_secsize);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	map = open_partition_map(fd, *argv, DL_GETDSIZE(&dl), dl.d_secsize);
	if (map != NULL) {
		if (lflag)
			dump_partition_map(map);
		else
			edit(&map);
	}

	free_partition_map(map);
	close(fd);

	return 0;
}

/*
 * Edit the file
 */
void
edit(struct partition_map **mapp)
{
	struct partition_map *map = *mapp;
	struct partition_map *oldmap;
	int command;

	printf("Edit %s -\n", map->name);

	while (get_command("Command (? for help): ", first_get, &command)) {
		first_get = 0;

		switch (command) {
		case '?':
			printf("Notes:\n"
			    "  Base and length fields are blocks, which "
			    "vary in size between media.\n"
			    "  The base field can be <nth>p; i.e. the "
			    "base of the nth partition.\n"
			    "  The length field can be a length followed "
			    "by k, m, g or t to indicate\n"
			    "    kilo, mega, giga, or tera bytes.\n"
			    "  The length field can also be <nth>p; i.e. "
			    "the length of the nth partition.\n"
			    "  The name of a partition is descriptive "
			    "text.\n\n");

			/* fall through */
		case 'h':
			printf("Commands are:\n"
			    "  ?    verbose command help\n"
			    "  C    create a partition of a specified type\n"
			    "  c    create an OpenBSD partition\n"
			    "  d    delete a partition\n"
			    "  f    full display of a partition\n"
			    "  h    command help\n"
			    "  i    (re)initialize the partition map\n"
			    "  n    (re)name a partition\n"
			    "  P    show the partition map's data structures\n"
			    "  p    print the partition map\n"
			    "  q    quit editing\n"
			    "  r    reorder (swap) disk positions of two "
			        "entries in the partition map\n"
			    "  s    change the size of the partition map\n"
			    "  t    change the type of a partition\n"
			    "  w    write the partition map to disk\n");
			break;
		case 'P':
			do_dump_map(map, 1);
			break;
		case 'p':
			do_dump_map(map, 0);
			break;
		case 'q':
			if (map->changed) {
				if (get_okay("Discard changes? [n/y]: ", 0) !=
				    1) {
					break;
				}
			}
			flush_to_newline(1);
			return;
		case 'i':
			if (get_okay("Discard current map? [n/y]: ", 0) == 1) {
				oldmap = map;
				map = create_partition_map(oldmap->fd,
				    oldmap->name, oldmap->media_size,
				    oldmap->sbBlkSize);
				if (map == NULL)
					break;
				*mapp = map;
				free_partition_map(oldmap);
			}
			break;
		case 'C':
			do_create_partition(map, 1);
			break;
		case 'c':
			do_create_partition(map, 0);
			break;
		case 'n':
			do_rename_partition(map);
			break;
		case 'd':
			do_delete_partition(map);
			break;
		case 'r':
			do_reorder(map);
			break;
		case 's':
			do_change_map_size(map);
			break;
		case 't':
			do_change_type(map);
			break;
		case 'w':
			do_write_partition_map(map);
			break;
		case 'f':
			do_display_entry(map);
			break;
		default:
			bad_input("No such command (%c)", command);
			break;
		}
	}
}

void
do_create_partition(struct partition_map *map, int get_type)
{
	long base, length;
	char *name = NULL;
	char *type = NULL;

	if (get_base_argument(&base, map) == 0)
		return;
	if (get_size_argument(&length, map) == 0)
		return;

	name = get_dpistr_argument("Name of partition: ");
	if (name == NULL) {
		bad_input("Bad name");
		goto out;
	}

	if (get_type == 0)
		type = strdup(kUnixType);
	else
		type = get_dpistr_argument("Type of partition: ");
	if (type == NULL) {
		bad_input("Bad type");
		goto out;
	}

	if (strncasecmp(type, kFreeType, DPISTRLEN) == 0) {
		bad_input("Can't create a partition with the Free type");
		goto out;
	}
	if (strncasecmp(type, kMapType, DPISTRLEN) == 0) {
		bad_input("Can't create a partition with the Map type");
		goto out;
	}

	add_partition_to_map(name, type, base, length, map);

out:
	free(type);
	free(name);

	return;
}

int
get_base_argument(long *number, struct partition_map *map)
{
	struct entry *entry;
	int result = 0;

	if (get_number_argument("First block: ", number) == 0) {
		bad_input("Bad block number");
	} else {
		result = 1;
		if (get_partition_modifier()) {
			entry = find_entry_by_disk_address(*number, map);
			if (entry == NULL) {
				bad_input("Bad partition number");
				result = 0;
			} else {
				*number = entry->dpme_pblock_start;
			}
		}
	}
	return result;
}


int
get_size_argument(long *number, struct partition_map *map)
{
	struct entry *entry;
	unsigned long multiple;
	int result = 0;

	if (get_number_argument("Length in blocks: ", number) == 0) {
		bad_input("Bad length");
	} else {
		multiple = get_multiplier(map->sbBlkSize);
		if (multiple == 0) {
			bad_input("Bad multiplier");
		} else if (multiple != 1) {
			*number *= multiple;
			result = 1;
		} else if (get_partition_modifier()) {
			entry = find_entry_by_disk_address(*number, map);
			if (entry == NULL) {
				bad_input("Bad partition number");
			} else {
				*number = entry->dpme_pblocks;
				result = 1;
			}
		} else {
			result = 1;
		}
	}
	return result;
}


void
do_rename_partition(struct partition_map *map)
{
	struct entry *entry;
	char *name;
	long ix;

	if (get_number_argument("Partition number: ", &ix) == 0) {
		bad_input("Bad partition number");
		return;
	}
	entry = find_entry_by_disk_address(ix, map);
	if (entry == NULL) {
		printf("No such partition\n");
		return;
	}

	printf("Existing partition name ``%s''.\n", entry->dpme_name);
	name = get_dpistr_argument("New name of partition: ");
	if (name == NULL) {
		bad_input("Bad name");
		return;
	}

	/*
	 * Since dpme_name is supposed to be NUL-filled, make sure
	 * current contents are zapped before copying in new name!
	 */
	memset(entry->dpme_name, 0, sizeof(entry->dpme_name));
	strlcpy(entry->dpme_name, name, sizeof(entry->dpme_name));
	map->changed = 1;

	free(name);
	return;
}

void
do_change_type(struct partition_map *map)
{
	struct entry *entry;
	char *type;
	long ix;

	if (get_number_argument("Partition number: ", &ix) == 0) {
		bad_input("Bad partition number");
		return;
	}
	entry = find_entry_by_disk_address(ix, map);
	if (entry == NULL) {
		printf("No such partition\n");
		return;
	}

	printf("Existing partition type ``%s''.\n", entry->dpme_type);
	type = get_dpistr_argument("New type of partition: ");
	if (type == NULL) {
		bad_input("Bad type");
		return;
	}

        /*
	 * Since dpme_type is supposed to be NUL-filled, make sure
         * current contents are zapped before copying in new type!
	 */
	memset(entry->dpme_type, 0, sizeof(entry->dpme_type));
	strncpy(entry->dpme_type, type, sizeof(entry->dpme_type));
	map->changed = 1;

	free(type);
	return;
}


void
do_delete_partition(struct partition_map *map)
{
	struct entry *cur;
	long ix;

	if (get_number_argument("Partition number: ", &ix) == 0) {
		bad_input("Bad partition number");
		return;
	}

	cur = find_entry_by_disk_address(ix, map);
	if (cur == NULL)
		printf("No such partition\n");
	else
		delete_partition_from_map(cur);
}


void
do_reorder(struct partition_map *map)
{
	long ix, old_index;

	if (get_number_argument("Partition number: ", &old_index) == 0) {
		bad_input("Bad partition number");
		return;
	}
	if (get_number_argument("New number: ", &ix) == 0) {
		bad_input("Bad partition number");
		return;
	}
	move_entry_in_map(old_index, ix, map);
}


void
do_write_partition_map(struct partition_map *map)
{
	if (map->changed == 0) {
		bad_input("The map has not been changed.");
		return;
	}
	if (rflag) {
		bad_input("The map is not writable.");
		return;
	}
	printf("Writing the map destroys what was there before. ");
	if (get_okay("Is that okay? [n/y]: ", 0) != 1) {
		return;
	}
	write_partition_map(map);

	map->changed = 0;
}


void
do_change_map_size(struct partition_map *map)
{
	long size;

	if (get_number_argument("New size: ", &size) == 0) {
		bad_input("Bad size");
		return;
	}
	resize_map(size, map);
}


void
do_display_entry(struct partition_map *map)
{
	long number;

	if (get_number_argument("Partition number: ", &number) == 0) {
		bad_input("Bad partition number");
		return;
	}
	if (number == 0)
		full_dump_block_zero(map);
	else
		full_dump_partition_entry(map, number);
}

void
do_dump_map(struct partition_map *map, int verbose)
{
	if (verbose)
		show_data_structures(map);
	else
		dump_partition_map(map);
}

__dead static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-lr] disk\n", __progname);

	exit(1);
}
