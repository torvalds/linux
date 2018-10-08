// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/init.h>
#include <linux/module.h>
#include <linux/umh.h>
#include <linux/bpfilter.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/file.h>
#include "msgfmt.h"

extern char bpfilter_umh_start;
extern char bpfilter_umh_end;

static struct umh_info info;
/* since ip_getsockopt() can run in parallel, serialize access to umh */
static DEFINE_MUTEX(bpfilter_lock);

static void shutdown_umh(struct umh_info *info)
{
	struct task_struct *tsk;

	if (!info->pid)
		return;
	tsk = pid_task(find_vpid(info->pid), PIDTYPE_PID);
	if (tsk)
		force_sig(SIGKILL, tsk);
	fput(info->pipe_to_umh);
	fput(info->pipe_from_umh);
	info->pid = 0;
}

static void __stop_umh(void)
{
	if (IS_ENABLED(CONFIG_INET)) {
		bpfilter_process_sockopt = NULL;
		shutdown_umh(&info);
	}
}

static void stop_umh(void)
{
	mutex_lock(&bpfilter_lock);
	__stop_umh();
	mutex_unlock(&bpfilter_lock);
}

static int __bpfilter_process_sockopt(struct sock *sk, int optname,
				      char __user *optval,
				      unsigned int optlen, bool is_set)
{
	struct mbox_request req;
	struct mbox_reply reply;
	loff_t pos;
	ssize_t n;
	int ret = -EFAULT;

	req.is_set = is_set;
	req.pid = current->pid;
	req.cmd = optname;
	req.addr = (long __force __user)optval;
	req.len = optlen;
	mutex_lock(&bpfilter_lock);
	if (!info.pid)
		goto out;
	n = __kernel_write(info.pipe_to_umh, &req, sizeof(req), &pos);
	if (n != sizeof(req)) {
		pr_err("write fail %zd\n", n);
		__stop_umh();
		ret = -EFAULT;
		goto out;
	}
	pos = 0;
	n = kernel_read(info.pipe_from_umh, &reply, sizeof(reply), &pos);
	if (n != sizeof(reply)) {
		pr_err("read fail %zd\n", n);
		__stop_umh();
		ret = -EFAULT;
		goto out;
	}
	ret = reply.status;
out:
	mutex_unlock(&bpfilter_lock);
	return ret;
}

static int __init load_umh(void)
{
	int err;

	/* fork usermode process */
	err = fork_usermode_blob(&bpfilter_umh_start,
				 &bpfilter_umh_end - &bpfilter_umh_start,
				 &info);
	if (err)
		return err;
	pr_info("Loaded bpfilter_umh pid %d\n", info.pid);

	/* health check that usermode process started correctly */
	if (__bpfilter_process_sockopt(NULL, 0, NULL, 0, 0) != 0) {
		stop_umh();
		return -EFAULT;
	}
	if (IS_ENABLED(CONFIG_INET))
		bpfilter_process_sockopt = &__bpfilter_process_sockopt;

	return 0;
}

static void __exit fini_umh(void)
{
	stop_umh();
}
module_init(load_umh);
module_exit(fini_umh);
MODULE_LICENSE("GPL");
