/*
 * fs/partitions/cmdline.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*#define DEBUG 1*/

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/slab.h>

#include "check.h"
#include "cmdline.h"

static char *cmdline;
static int cmdline_parsed;
static struct part_device *cmdline_device;

struct part {
	char *name;
	unsigned long from;
	unsigned long size;
	unsigned long sector_size;
	struct part *next_part;
};

struct part_device {
	char *name;
	struct part *first_part;
	struct part_device *next_device;
};


/* Passed a string like:
 * system:3600:10000:800
 */
static struct part *parse_partition(char *s, int alloc_size, void **alloc)
{
	char *p;
	struct part *this_part;
	pr_debug("%s: '%s', %d, %p\n", __func__, s, alloc_size, alloc);

	if (*alloc == NULL)
		*alloc = kzalloc(alloc_size, GFP_KERNEL);

	this_part = *alloc;
	*alloc += sizeof(*this_part);

	/* Name */
	p = strchr(s, ':');
	if (!p)
		return this_part;
	*p = 0;
	this_part->name = s;

	/* From */
	s = p+1;
	p = strchr(s, ':');
	if (!p)
		return this_part;
	*p = 0;
	this_part->from = simple_strtoul(s, NULL, 16);

	/* Size */
	s = p+1;
	p = strchr(s, ':');
	if (!p)
		return this_part;
	*p = 0;
	this_part->size = simple_strtoul(s, NULL, 16);

	/* Sector size */
	s = p+1;
	this_part->sector_size = simple_strtoul(s, NULL, 16);
	pr_debug("%s: Found %s %lu %lu %lu\n", __func__, this_part->name,
		this_part->from, this_part->size, this_part->sector_size);
	return this_part;
}

/* Passed a string like:
 * system:3600:10000:800,cache:13600:4000:800,userdata:17600:80000:800
 * Could be an empty string
 */
static struct part *parse_partition_list(char *s, int alloc_size, void **alloc)
{
	char *p;
	struct part *this_part;
	struct part *next_part = NULL;
	pr_debug("%s: '%s', %d, %p\n", __func__, s, alloc_size, alloc);

	alloc_size += sizeof(struct part);
	p = strchr(s, ',');
	if (p) {
		*p = 0;
		next_part = parse_partition_list(p+1, alloc_size, alloc);
		if (!next_part)
			BUG();
	}
	this_part = parse_partition(s, alloc_size, alloc);
	this_part->next_part = next_part;
	return this_part;
}
/* Passed a string like:
 * sdhci.0=system:3600:10000:800,cache:13600:4000:800,userdata:17600:80000:800
 */
static struct part_device *parse_device(char *s, int alloc_size, void **alloc) {
	char *p;
	struct part *part;
	struct part_device *device;

	pr_debug("%s: '%s', %d, %p\n", __func__, s, alloc_size, alloc);
	p = strchr(s, '=');
	if (!p)
		return NULL;
	*p = 0;
	alloc_size += sizeof(struct part_device);
	part = parse_partition_list(p+1, alloc_size, alloc);
	if (part) {
		device = *alloc;
		*alloc += sizeof(struct part_device);
		device->name = s;
		device->first_part = part;
	}
	return device;
}


static void parse_cmdline(void) {
	char *s = cmdline;
	void *alloc = 0;
	if (cmdline_parsed)
		return;
	cmdline_device = parse_device(s, 0, &alloc);
	cmdline_parsed = 1;
}


int copy_partitions_to_state(struct part_device *device,
	struct parsed_partitions *state, unsigned int ssz)
{
	int i = 0;
	struct part *part = device->first_part;
	while (part) {
		sector_t from = part->from * (part->sector_size / ssz);
		sector_t size = part->size * (part->sector_size / ssz);
		put_named_partition(state, i+1, from, size, part->name,
			strlen(part->name));
		i++;
		part = part->next_part;
	}
	return i;
}

int cmdline_partition(struct parsed_partitions *state)
{
	struct block_device *bdev = state->bdev;
	unsigned int ssz = bdev_logical_block_size(bdev);
	parse_cmdline();
	pr_debug("%s: %s\n", __func__, dev_name(disk_to_dev(bdev->bd_disk)));

	if (!cmdline_device)
		return 0;

	if (strcmp(cmdline_device->name, dev_name(disk_to_dev(bdev->bd_disk))))
		return 0;

	/* We have a command line partition that matches this device */
	copy_partitions_to_state(cmdline_device, state, ssz);
	return 1;
}


__init static int cmdline_partition_setup(char *s)
{
	cmdline = s;
	return 1;
}

__setup("tegrapart=", cmdline_partition_setup);


