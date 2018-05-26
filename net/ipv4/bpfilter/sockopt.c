// SPDX-License-Identifier: GPL-2.0
#include <linux/uaccess.h>
#include <linux/bpfilter.h>
#include <uapi/linux/bpf.h>
#include <linux/wait.h>
#include <linux/kmod.h>

int (*bpfilter_process_sockopt)(struct sock *sk, int optname,
				char __user *optval,
				unsigned int optlen, bool is_set);
EXPORT_SYMBOL_GPL(bpfilter_process_sockopt);

static int bpfilter_mbox_request(struct sock *sk, int optname,
				 char __user *optval,
				 unsigned int optlen, bool is_set)
{
	if (!bpfilter_process_sockopt) {
		int err = request_module("bpfilter");

		if (err)
			return err;
		if (!bpfilter_process_sockopt)
			return -ECHILD;
	}
	return bpfilter_process_sockopt(sk, optname, optval, optlen, is_set);
}

int bpfilter_ip_set_sockopt(struct sock *sk, int optname, char __user *optval,
			    unsigned int optlen)
{
	return bpfilter_mbox_request(sk, optname, optval, optlen, true);
}

int bpfilter_ip_get_sockopt(struct sock *sk, int optname, char __user *optval,
			    int __user *optlen)
{
	int len;

	if (get_user(len, optlen))
		return -EFAULT;

	return bpfilter_mbox_request(sk, optname, optval, len, false);
}
