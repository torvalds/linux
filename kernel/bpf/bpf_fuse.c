// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021 Google LLC

#include <linux/filter.h>
#include <linux/android_fuse.h>

static const struct bpf_func_proto *
fuse_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_trace_printk:
			return bpf_get_trace_printk_proto();

	case BPF_FUNC_get_current_uid_gid:
			return &bpf_get_current_uid_gid_proto;

	case BPF_FUNC_get_current_pid_tgid:
			return &bpf_get_current_pid_tgid_proto;

	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;

	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;

	default:
		pr_debug("Invalid fuse bpf func %d\n", func_id);
		return NULL;
	}
}

static bool fuse_prog_is_valid_access(int off, int size,
				enum bpf_access_type type,
				const struct bpf_prog *prog,
				struct bpf_insn_access_aux *info)
{
	int i;

	if (off < 0 || off > offsetofend(struct fuse_bpf_args, out_args))
		return false;

	/* TODO This is garbage. Do it properly */
	for (i = 0; i < 5; i++) {
		if (off == offsetof(struct fuse_bpf_args, in_args[i].value)) {
			info->reg_type = PTR_TO_BUF;
			info->ctx_field_size = 256;
			if (type != BPF_READ)
				return false;
			return true;
		}
	}
	for (i = 0; i < 3; i++) {
		if (off == offsetof(struct fuse_bpf_args, out_args[i].value)) {
			info->reg_type = PTR_TO_BUF;
			info->ctx_field_size = 256;
			return true;
		}
	}
	if (type != BPF_READ)
		return false;

	return true;
}

const struct bpf_verifier_ops fuse_verifier_ops = {
	.get_func_proto  = fuse_prog_func_proto,
	.is_valid_access = fuse_prog_is_valid_access,
};

const struct bpf_prog_ops fuse_prog_ops = {
};

struct bpf_prog *fuse_get_bpf_prog(struct file *file)
{
	struct bpf_prog *bpf_prog = ERR_PTR(-EINVAL);

	if (!file || IS_ERR(file))
		return bpf_prog;
	/**
	 * Two ways of getting a bpf prog from another task's fd, since
	 * bpf_prog_get_type_dev only works with an fd
	 *
	 * 1) Duplicate a little of the needed code. Requires access to
	 *    bpf_prog_fops for validation, which is not exported for modules
	 * 2) Insert the bpf_file object into a fd from the current task
	 *    Stupidly complex, but I think OK, as security checks are not run
	 *    during the existence of the handle
	 *
	 * Best would be to upstream 1) into kernel/bpf/syscall.c and export it
	 * for use here. Failing that, we have to use 2, since fuse must be
	 * compilable as a module.
	 */
#if 1
	if (file->f_op != &bpf_prog_fops)
		goto out;

	bpf_prog = file->private_data;
	if (bpf_prog->type == BPF_PROG_TYPE_FUSE)
		bpf_prog_inc(bpf_prog);
	else
		bpf_prog = ERR_PTR(-EINVAL);

#else
	{
		int task_fd = get_unused_fd_flags(file->f_flags);

		if (task_fd < 0)
			goto out;

		fd_install(task_fd, file);

		bpf_prog = bpf_prog_get_type_dev(task_fd, BPF_PROG_TYPE_FUSE,
						 false);

		/* Close the fd, which also closes the file */
		__close_fd(current->files, task_fd);
		file = NULL;
	}
#endif

out:
	if (file)
		fput(file);
	return bpf_prog;
}
EXPORT_SYMBOL(fuse_get_bpf_prog);


