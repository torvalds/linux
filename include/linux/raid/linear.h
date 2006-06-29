#ifndef _LINEAR_H
#define _LINEAR_H

#include <linux/raid/md.h>

struct dev_info {
	mdk_rdev_t	*rdev;
	sector_t	size;
	sector_t	offset;
};

typedef struct dev_info dev_info_t;

struct linear_private_data
{
	struct linear_private_data *prev;	/* earlier version */
	dev_info_t		**hash_table;
	sector_t		hash_spacing;
	sector_t		array_size;
	int			preshift; /* shift before dividing by hash_spacing */
	dev_info_t		disks[0];
};


typedef struct linear_private_data linear_conf_t;

#define mddev_to_conf(mddev) ((linear_conf_t *) mddev->private)

#endif
