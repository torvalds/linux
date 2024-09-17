/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SEQ_FILE_NET_H__
#define __SEQ_FILE_NET_H__

#include <linux/seq_file.h>
#include <net/net_trackers.h>

struct net;
extern struct net init_net;

struct seq_net_private {
#ifdef CONFIG_NET_NS
	struct net	*net;
	netns_tracker	ns_tracker;
#endif
};

static inline struct net *seq_file_net(struct seq_file *seq)
{
#ifdef CONFIG_NET_NS
	return ((struct seq_net_private *)seq->private)->net;
#else
	return &init_net;
#endif
}

/*
 * This one is needed for proc_create_net_single since net is stored directly
 * in private not as a struct i.e. seq_file_net can't be used.
 */
static inline struct net *seq_file_single_net(struct seq_file *seq)
{
#ifdef CONFIG_NET_NS
	return (struct net *)seq->private;
#else
	return &init_net;
#endif
}

#endif
