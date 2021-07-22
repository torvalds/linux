// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/fs.h>
#include <linux/sched/signal.h>
#include "bpf_preload.h"

extern char bpf_preload_umd_start;
extern char bpf_preload_umd_end;

static int preload(struct bpf_preload_info *obj);
static int finish(void);

static struct bpf_preload_ops umd_ops = {
	.info.driver_name = "bpf_preload",
	.preload = preload,
	.finish = finish,
	.owner = THIS_MODULE,
};

static int preload(struct bpf_preload_info *obj)
{
	int magic = BPF_PRELOAD_START;
	loff_t pos = 0;
	int i, err;
	ssize_t n;

	err = fork_usermode_driver(&umd_ops.info);
	if (err)
		return err;

	/* send the start magic to let UMD proceed with loading BPF progs */
	n = kernel_write(umd_ops.info.pipe_to_umh,
			 &magic, sizeof(magic), &pos);
	if (n != sizeof(magic))
		return -EPIPE;

	/* receive bpf_link IDs and names from UMD */
	pos = 0;
	for (i = 0; i < BPF_PRELOAD_LINKS; i++) {
		n = kernel_read(umd_ops.info.pipe_from_umh,
				&obj[i], sizeof(*obj), &pos);
		if (n != sizeof(*obj))
			return -EPIPE;
	}
	return 0;
}

static int finish(void)
{
	int magic = BPF_PRELOAD_END;
	struct pid *tgid;
	loff_t pos = 0;
	ssize_t n;

	/* send the last magic to UMD. It will do a normal exit. */
	n = kernel_write(umd_ops.info.pipe_to_umh,
			 &magic, sizeof(magic), &pos);
	if (n != sizeof(magic))
		return -EPIPE;

	tgid = umd_ops.info.tgid;
	if (tgid) {
		wait_event(tgid->wait_pidfd, thread_group_exited(tgid));
		umd_cleanup_helper(&umd_ops.info);
	}
	return 0;
}

static int __init load_umd(void)
{
	int err;

	err = umd_load_blob(&umd_ops.info, &bpf_preload_umd_start,
			    &bpf_preload_umd_end - &bpf_preload_umd_start);
	if (err)
		return err;
	bpf_preload_ops = &umd_ops;
	return err;
}

static void __exit fini_umd(void)
{
	struct pid *tgid;

	bpf_preload_ops = NULL;

	/* kill UMD in case it's still there due to earlier error */
	tgid = umd_ops.info.tgid;
	if (tgid) {
		kill_pid(tgid, SIGKILL, 1);

		wait_event(tgid->wait_pidfd, thread_group_exited(tgid));
		umd_cleanup_helper(&umd_ops.info);
	}
	umd_unload_blob(&umd_ops.info);
}
late_initcall(load_umd);
module_exit(fini_umd);
MODULE_LICENSE("GPL");
