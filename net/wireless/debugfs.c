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

#include <linux/slab.h>
#include "core.h"
#include "debugfs.h"

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
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};

DEBUGFS_READONLY_FILE(rts_threshold, 20, "%d",
		      wiphy->rts_threshold)
DEBUGFS_READONLY_FILE(fragmentation_threshold, 20, "%d",
		      wiphy->frag_threshold);
DEBUGFS_READONLY_FILE(short_retry_limit, 20, "%d",
		      wiphy->retry_short)
DEBUGFS_READONLY_FILE(long_retry_limit, 20, "%d",
		      wiphy->retry_long);

static int ht_print_chan(struct ieee80211_channel *chan,
			 char *buf, int buf_size, int offset)
{
	if (WARN_ON(offset > buf_size))
		return 0;

	if (chan->flags & IEEE80211_CHAN_DISABLED)
		return scnprintf(buf + offset,
				 buf_size - offset,
				 "%d Disabled\n",
				 chan->center_freq);

	return scnprintf(buf + offset,
			 buf_size - offset,
			 "%d HT40 %c%c\n",
			 chan->center_freq,
			 (chan->flags & IEEE80211_CHAN_NO_HT40MINUS) ?
				' ' : '-',
			 (chan->flags & IEEE80211_CHAN_NO_HT40PLUS) ?
				' ' : '+');
}

static ssize_t ht40allow_map_read(struct file *file,
				  char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct wiphy *wiphy = file->private_data;
	char *buf;
	unsigned int offset = 0, buf_size = PAGE_SIZE, i, r;
	enum ieee80211_band band;
	struct ieee80211_supported_band *sband;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rtnl_lock();

	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		sband = wiphy->bands[band];
		if (!sband)
			continue;
		for (i = 0; i < sband->n_channels; i++)
			offset += ht_print_chan(&sband->channels[i],
						buf, buf_size, offset);
	}

	rtnl_unlock();

	r = simple_read_from_buffer(user_buf, count, ppos, buf, offset);

	kfree(buf);

	return r;
}

static const struct file_operations ht40allow_map_ops = {
	.read = ht40allow_map_read,
	.open = simple_open,
	.llseek = default_llseek,
};

#define DEBUGFS_ADD(name)						\
	debugfs_create_file(#name, S_IRUGO, phyd, &rdev->wiphy, &name## _ops);

void cfg80211_debugfs_rdev_add(struct cfg80211_registered_device *rdev)
{
	struct dentry *phyd = rdev->wiphy.debugfsdir;

	DEBUGFS_ADD(rts_threshold);
	DEBUGFS_ADD(fragmentation_threshold);
	DEBUGFS_ADD(short_retry_limit);
	DEBUGFS_ADD(long_retry_limit);
	DEBUGFS_ADD(ht40allow_map);
}
