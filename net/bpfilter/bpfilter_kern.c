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

static void shutdown_umh(void)
{
	struct task_struct *tsk;

	if (bpfilter_ops.stop)
		return;

	tsk = get_pid_task(find_vpid(bpfilter_ops.info.pid), PIDTYPE_PID);
	if (tsk) {
		send_sig(SIGKILL, tsk, 1);
		put_task_struct(tsk);
	}
}

static void __stop_umh(void)
{
	if (IS_ENABLED(CONFIG_INET))
		shutdown_umh();
}

static int __bpfilter_process_sockopt(struct sock *sk, int optname,
				      char __user *optval,
				      unsigned int optlen, bool is_set)
{
	struct mbox_request req;
	struct mbox_reply reply;
	loff_t pos = 0;
	ssize_t n;
	int ret = -EFAULT;

	req.is_set = is_set;
	req.pid = current->pid;
	req.cmd = optname;
	req.addr = (long __force __user)optval;
	req.len = optlen;
	if (!bpfilter_ops.info.pid)
		goto out;
	n = kernel_write(bpfilter_ops.info.pipe_to_umh, &req, sizeof(req),
			   &pos);
	if (n != sizeof(req)) {
		pr_err("write fail %zd\n", n);
		__stop_umh();
		ret = -EFAULT;
		goto out;
	}
	pos = 0;
	n = kernel_read(bpfilter_ops.info.pipe_from_umh, &reply, sizeof(reply),
			&pos);
	if (n != sizeof(reply)) {
		pr_err("read fail %zd\n", n);
		__stop_umh();
		ret = -EFAULT;
		goto out;
	}
	ret = reply.status;
out:
	return ret;
}

static int start_umh(void)
{
	int err;

	/* fork usermode process */
	err = fork_usermode_blob(&bpfilter_umh_start,
				 &bpfilter_umh_end - &bpfilter_umh_start,
				 &bpfilter_ops.info);
	if (err)
		return err;
	bpfilter_ops.stop = false;
	pr_info("Loaded bpfilter_umh pid %d\n", bpfilter_ops.info.pid);

	/* health check that usermode process started correctly */
	if (__bpfilter_process_sockopt(NULL, 0, NULL, 0, 0) != 0) {
		shutdown_umh();
		return -EFAULT;
	}

	return 0;
}

static int __init load_umh(void)
{
	int err;

	mutex_lock(&bpfilter_ops.lock);
	if (!bpfilter_ops.stop) {
		err = -EFAULT;
		goto out;
	}
	err = start_umh();
	if (!err && IS_ENABLED(CONFIG_INET)) {
		bpfilter_ops.sockopt = &__bpfilter_process_sockopt;
		bpfilter_ops.start = &start_umh;
	}
out:
	mutex_unlock(&bpfilter_ops.lock);
	return err;
}

static void __exit fini_umh(void)
{
	mutex_lock(&bpfilter_ops.lock);
	if (IS_ENABLED(CONFIG_INET)) {
		shutdown_umh();
		bpfilter_ops.start = NULL;
		bpfilter_ops.sockopt = NULL;
	}
	mutex_unlock(&bpfilter_ops.lock);
}
module_init(load_umh);
module_exit(fini_umh);
MODULE_LICENSE("GPL");
