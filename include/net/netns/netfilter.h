#ifndef __NETNS_NETFILTER_H
#define __NETNS_NETFILTER_H

#include <linux/proc_fs.h>

struct netns_nf {
#if defined CONFIG_PROC_FS
	struct proc_dir_entry *proc_netfilter;
#endif
};
#endif
