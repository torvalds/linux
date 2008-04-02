#ifndef __SEQ_FILE_NET_H__
#define __SEQ_FILE_NET_H__

#include <linux/seq_file.h>

struct net;
extern struct net init_net;

struct seq_net_private {
#ifdef CONFIG_NET_NS
	struct net *net;
#endif
};

int seq_open_net(struct inode *, struct file *,
		 const struct seq_operations *, int);
int seq_release_net(struct inode *, struct file *);
static inline struct net *seq_file_net(struct seq_file *seq)
{
#ifdef CONFIG_NET_NS
	return ((struct seq_net_private *)seq->private)->net;
#else
	return &init_net;
#endif
}

#endif
