// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/bpfilter.h>
#include <uapi/linux/bpf.h>
#include <linux/wait.h>
#include <linux/kmod.h>
#include <linux/fs.h>
#include <linux/file.h>

struct bpfilter_umh_ops bpfilter_ops;
EXPORT_SYMBOL_GPL(bpfilter_ops);

static void bpfilter_umh_cleanup(struct umh_info *info)
{
	bpfilter_ops.stop = true;
	fput(info->pipe_to_umh);
	fput(info->pipe_from_umh);
	info->pid = 0;
}

static int bpfilter_mbox_request(struct sock *sk, int optname,
				 char __user *optval,
				 unsigned int optlen, bool is_set)
{
	int err;

	if (!bpfilter_ops.sockopt) {
		err = request_module("bpfilter");

		if (err)
			return err;
		if (!bpfilter_ops.sockopt)
			return -ECHILD;
	}
	if (bpfilter_ops.stop) {
		err = bpfilter_ops.start();
		if (err)
			return err;
	}
	return bpfilter_ops.sockopt(sk, optname, optval, optlen, is_set);
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

static int __init bpfilter_sockopt_init(void)
{
	bpfilter_ops.stop = true;
	bpfilter_ops.info.cmdline = "bpfilter_umh";
	bpfilter_ops.info.cleanup = &bpfilter_umh_cleanup;

	return 0;
}

module_init(bpfilter_sockopt_init);
