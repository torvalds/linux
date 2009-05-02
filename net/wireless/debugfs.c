/*
 * cfg80211 debugfs
 *
 * Copyright 2009	Luis R. Rodriguez <lrodriguez@atheros.com>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "core.h"
#include "debugfs.h"

static int cfg80211_open_file_generic(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

#define DEBUGFS_READONLY_FILE(name, buflen, fmt, value...)		\
static ssize_t name## _read(struct file *file, char __user *userbuf,	\
			    size_t count, loff_t *ppos)			\
{									\
	struct wiphy *wiphy= file->private_data;		\
	char buf[buflen];						\
	int res;							\
									\
	res = scnprintf(buf, buflen, fmt "\n", ##value);		\
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);	\
}									\
									\
static const struct file_operations name## _ops = {			\
	.read = name## _read,						\
	.open = cfg80211_open_file_generic,				\
};

DEBUGFS_READONLY_FILE(rts_threshold, 20, "%d",
		      wiphy->rts_threshold)
DEBUGFS_READONLY_FILE(fragmentation_threshold, 20, "%d",
		      wiphy->frag_threshold);
DEBUGFS_READONLY_FILE(short_retry_limit, 20, "%d",
		      wiphy->retry_short)
DEBUGFS_READONLY_FILE(long_retry_limit, 20, "%d",
		      wiphy->retry_long);

#define DEBUGFS_ADD(name)						\
	drv->debugfs.name = debugfs_create_file(#name, S_IRUGO, phyd,	\
						  &drv->wiphy, &name## _ops);
#define DEBUGFS_DEL(name)						\
	debugfs_remove(drv->debugfs.name);				\
	drv->debugfs.name = NULL;

void cfg80211_debugfs_drv_add(struct cfg80211_registered_device *drv)
{
	struct dentry *phyd = drv->wiphy.debugfsdir;

	DEBUGFS_ADD(rts_threshold);
	DEBUGFS_ADD(fragmentation_threshold);
	DEBUGFS_ADD(short_retry_limit);
	DEBUGFS_ADD(long_retry_limit);
}

void cfg80211_debugfs_drv_del(struct cfg80211_registered_device *drv)
{
	DEBUGFS_DEL(rts_threshold);
	DEBUGFS_DEL(fragmentation_threshold);
	DEBUGFS_DEL(short_retry_limit);
	DEBUGFS_DEL(long_retry_limit);
}
