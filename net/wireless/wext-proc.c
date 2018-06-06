/*
 * This file implement the Wireless Extensions proc API.
 *
 * Authors :	Jean Tourrilhes - HPL - <jt@hpl.hp.com>
 * Copyright (c) 1997-2007 Jean Tourrilhes, All Rights Reserved.
 *
 * (As all part of the Linux kernel, this file is GPL)
 */

/*
 * The /proc/net/wireless file is a human readable user-space interface
 * exporting various wireless specific statistics from the wireless devices.
 * This is the most popular part of the Wireless Extensions ;-)
 *
 * This interface is a pure clone of /proc/net/dev (in net/core/dev.c).
 * The content of the file is basically the content of "struct iw_statistics".
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <net/iw_handler.h>
#include <net/wext.h>


static void wireless_seq_printf_stats(struct seq_file *seq,
				      struct net_device *dev)
{
	/* Get stats from the driver */
	struct iw_statistics *stats = get_wireless_stats(dev);
	static struct iw_statistics nullstats = {};

	/* show device if it's wireless regardless of current stats */
	if (!stats) {
#ifdef CONFIG_WIRELESS_EXT
		if (dev->wireless_handlers)
			stats = &nullstats;
#endif
#ifdef CONFIG_CFG80211
		if (dev->ieee80211_ptr)
			stats = &nullstats;
#endif
	}

	if (stats) {
		seq_printf(seq, "%6s: %04x  %3d%c  %3d%c  %3d%c  %6d %6d %6d "
				"%6d %6d   %6d\n",
			   dev->name, stats->status, stats->qual.qual,
			   stats->qual.updated & IW_QUAL_QUAL_UPDATED
			   ? '.' : ' ',
			   ((__s32) stats->qual.level) -
			   ((stats->qual.updated & IW_QUAL_DBM) ? 0x100 : 0),
			   stats->qual.updated & IW_QUAL_LEVEL_UPDATED
			   ? '.' : ' ',
			   ((__s32) stats->qual.noise) -
			   ((stats->qual.updated & IW_QUAL_DBM) ? 0x100 : 0),
			   stats->qual.updated & IW_QUAL_NOISE_UPDATED
			   ? '.' : ' ',
			   stats->discard.nwid, stats->discard.code,
			   stats->discard.fragment, stats->discard.retries,
			   stats->discard.misc, stats->miss.beacon);

		if (stats != &nullstats)
			stats->qual.updated &= ~IW_QUAL_ALL_UPDATED;
	}
}

/* ---------------------------------------------------------------- */
/*
 * Print info for /proc/net/wireless (print all entries)
 */
static int wireless_dev_seq_show(struct seq_file *seq, void *v)
{
	might_sleep();

	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "Inter-| sta-|   Quality        |   Discarded "
				"packets               | Missed | WE\n"
				" face | tus | link level noise |  nwid  "
				"crypt   frag  retry   misc | beacon | %d\n",
			   WIRELESS_EXT);
	else
		wireless_seq_printf_stats(seq, v);
	return 0;
}

static void *wireless_dev_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct net *net = seq_file_net(seq);
	loff_t off;
	struct net_device *dev;

	rtnl_lock();
	if (!*pos)
		return SEQ_START_TOKEN;

	off = 1;
	for_each_netdev(net, dev)
		if (off++ == *pos)
			return dev;
	return NULL;
}

static void *wireless_dev_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct net *net = seq_file_net(seq);

	++*pos;

	return v == SEQ_START_TOKEN ?
		first_net_device(net) : next_net_device(v);
}

static void wireless_dev_seq_stop(struct seq_file *seq, void *v)
{
	rtnl_unlock();
}

static const struct seq_operations wireless_seq_ops = {
	.start = wireless_dev_seq_start,
	.next  = wireless_dev_seq_next,
	.stop  = wireless_dev_seq_stop,
	.show  = wireless_dev_seq_show,
};

static int seq_open_wireless(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &wireless_seq_ops,
			    sizeof(struct seq_net_private));
}

static const struct file_operations wireless_seq_fops = {
	.open    = seq_open_wireless,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_net,
};

int __net_init wext_proc_init(struct net *net)
{
	/* Create /proc/net/wireless entry */
	if (!proc_create("wireless", 0444, net->proc_net,
			 &wireless_seq_fops))
		return -ENOMEM;

	return 0;
}

void __net_exit wext_proc_exit(struct net *net)
{
	remove_proc_entry("wireless", net->proc_net);
}
