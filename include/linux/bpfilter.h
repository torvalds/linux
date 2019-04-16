/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BPFILTER_H
#define _LINUX_BPFILTER_H

#include <uapi/linux/bpfilter.h>
#include <linux/umh.h>

struct sock;
int bpfilter_ip_set_sockopt(struct sock *sk, int optname, char __user *optval,
			    unsigned int optlen);
int bpfilter_ip_get_sockopt(struct sock *sk, int optname, char __user *optval,
			    int __user *optlen);
struct bpfilter_umh_ops {
	struct umh_info info;
	/* since ip_getsockopt() can run in parallel, serialize access to umh */
	struct mutex lock;
	int (*sockopt)(struct sock *sk, int optname,
		       char __user *optval,
		       unsigned int optlen, bool is_set);
	int (*start)(void);
	bool stop;
};
extern struct bpfilter_umh_ops bpfilter_ops;
#endif
