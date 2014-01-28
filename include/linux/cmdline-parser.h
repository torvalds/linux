/*
 * Parsing command line, get the partitions information.
 *
 * Written by Cai Zhiyong <caizhiyong@huawei.com>
 *
 */
#ifndef CMDLINEPARSEH
#define CMDLINEPARSEH

#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/slab.h>

/* partition flags */
#define PF_RDONLY                   0x01 /* Device is read only */
#define PF_POWERUP_LOCK             0x02 /* Always locked after reset */

struct cmdline_subpart {
	char name[BDEVNAME_SIZE]; /* partition name, such as 'rootfs' */
	sector_t from;
	sector_t size;
	int flags;
	struct cmdline_subpart *next_subpart;
};

struct cmdline_parts {
	char name[BDEVNAME_SIZE]; /* block device, such as 'mmcblk0' */
	unsigned int nr_subparts;
	struct cmdline_subpart *subpart;
	struct cmdline_parts *next_parts;
};

void cmdline_parts_free(struct cmdline_parts **parts);

int cmdline_parts_parse(struct cmdline_parts **parts, const char *cmdline);

struct cmdline_parts *cmdline_parts_find(struct cmdline_parts *parts,
					 const char *bdev);

void cmdline_parts_set(struct cmdline_parts *parts, sector_t disk_size,
		       int slot,
		       int (*add_part)(int, struct cmdline_subpart *, void *),
		       void *param);

#endif /* CMDLINEPARSEH */
