/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BPFILTER_H
#define _LINUX_BPFILTER_H

#include <uapi/linux/bpfilter.h>

struct sock;
int bpfilter_ip_set_sockopt(struct sock *sk, int optname, char __user *optval,
			    unsigned int optlen);
int bpfilter_ip_get_sockopt(struct sock *sk, int optname, char __user *optval,
			    int __user *optlen);
extern int (*bpfilter_process_sockopt)(struct sock *sk, int optname,
				       char __user *optval,
				       unsigned int optlen, bool is_set);
#endif
