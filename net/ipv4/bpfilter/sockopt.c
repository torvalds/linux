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

void bpfilter_umh_cleanup(struct umd_info *info)
{
	fput(info->pipe_to_umh);
	fput(info->pipe_from_umh);
	put_pid(info->tgid);
	info->tgid = NULL;
}
EXPORT_SYMBOL_GPL(bpfilter_umh_cleanup);

static int bpfilter_mbox_request(struct sock *sk, int optname, sockptr_t optval,
				 unsigned int optlen, bool is_set)
{
	int err;
	mutex_lock(&bpfilter_ops.lock);
	if (!bpfilter_ops.sockopt) {
		mutex_unlock(&bpfilter_ops.lock);
		request_module("bpfilter");
		mutex_lock(&bpfilter_ops.lock);

		if (!bpfilter_ops.sockopt) {
			err = -ENOPROTOOPT;
			goto out;
		}
	}
	if (bpfilter_ops.info.tgid &&
	    thread_group_exited(bpfilter_ops.info.tgid))
		bpfilter_umh_cleanup(&bpfilter_ops.info);

	if (!bpfilter_ops.info.tgid) {
		err = bpfilter_ops.start();
		if (err)
			goto out;
	}
	err = bpfilter_ops.sockopt(sk, optname, optval, optlen, is_set);
out:
	mutex_unlock(&bpfilter_ops.lock);
	return err;
}

int bpfilter_ip_set_sockopt(struct sock *sk, int optname, sockptr_t optval,
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

	return bpfilter_mbox_request(sk, optname, USER_SOCKPTR(optval), len,
				     false);
}

static int __init bpfilter_sockopt_init(void)
{
	mutex_init(&bpfilter_ops.lock);
	bpfilter_ops.info.tgid = NULL;
	bpfilter_ops.info.driver_name = "bpfilter_umh";

	return 0;
}
device_initcall(bpfilter_sockopt_init);
