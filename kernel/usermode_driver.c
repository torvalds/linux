// SPDX-License-Identifier: GPL-2.0-only
/*
 * umd - User mode driver support
 */
#include <linux/shmem_fs.h>
#include <linux/pipe_fs_i.h>
#include <linux/usermode_driver.h>

static LIST_HEAD(umh_list);
static DEFINE_MUTEX(umh_list_lock);

static int umd_setup(struct subprocess_info *info, struct cred *new)
{
	struct umd_info *umd_info = info->data;
	struct file *from_umh[2];
	struct file *to_umh[2];
	int err;

	/* create pipe to send data to umh */
	err = create_pipe_files(to_umh, 0);
	if (err)
		return err;
	err = replace_fd(0, to_umh[0], 0);
	fput(to_umh[0]);
	if (err < 0) {
		fput(to_umh[1]);
		return err;
	}

	/* create pipe to receive data from umh */
	err = create_pipe_files(from_umh, 0);
	if (err) {
		fput(to_umh[1]);
		replace_fd(0, NULL, 0);
		return err;
	}
	err = replace_fd(1, from_umh[1], 0);
	fput(from_umh[1]);
	if (err < 0) {
		fput(to_umh[1]);
		replace_fd(0, NULL, 0);
		fput(from_umh[0]);
		return err;
	}

	umd_info->pipe_to_umh = to_umh[1];
	umd_info->pipe_from_umh = from_umh[0];
	umd_info->pid = task_pid_nr(current);
	current->flags |= PF_UMH;
	return 0;
}

static void umd_cleanup(struct subprocess_info *info)
{
	struct umd_info *umd_info = info->data;

	/* cleanup if umh_setup() was successful but exec failed */
	if (info->retval) {
		fput(umd_info->pipe_to_umh);
		fput(umd_info->pipe_from_umh);
	}
}

/**
 * fork_usermode_blob - fork a blob of bytes as a usermode process
 * @data: a blob of bytes that can be do_execv-ed as a file
 * @len: length of the blob
 * @info: information about usermode process (shouldn't be NULL)
 *
 * Returns either negative error or zero which indicates success
 * in executing a blob of bytes as a usermode process. In such
 * case 'struct umd_info *info' is populated with two pipes
 * and a pid of the process. The caller is responsible for health
 * check of the user process, killing it via pid, and closing the
 * pipes when user process is no longer needed.
 */
int fork_usermode_blob(void *data, size_t len, struct umd_info *info)
{
	struct subprocess_info *sub_info;
	char **argv = NULL;
	struct file *file;
	ssize_t written;
	loff_t pos = 0;
	int err;

	file = shmem_kernel_file_setup(info->driver_name, len, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);

	written = kernel_write(file, data, len, &pos);
	if (written != len) {
		err = written;
		if (err >= 0)
			err = -ENOMEM;
		goto out;
	}

	err = -ENOMEM;
	argv = argv_split(GFP_KERNEL, info->driver_name, NULL);
	if (!argv)
		goto out;

	sub_info = call_usermodehelper_setup(info->driver_name, argv, NULL,
					     GFP_KERNEL,
					     umd_setup, umd_cleanup, info);
	if (!sub_info)
		goto out;

	sub_info->file = file;
	err = call_usermodehelper_exec(sub_info, UMH_WAIT_EXEC);
	if (!err) {
		mutex_lock(&umh_list_lock);
		list_add(&info->list, &umh_list);
		mutex_unlock(&umh_list_lock);
	}
out:
	if (argv)
		argv_free(argv);
	fput(file);
	return err;
}
EXPORT_SYMBOL_GPL(fork_usermode_blob);

void __exit_umh(struct task_struct *tsk)
{
	struct umd_info *info;
	pid_t pid = tsk->pid;

	mutex_lock(&umh_list_lock);
	list_for_each_entry(info, &umh_list, list) {
		if (info->pid == pid) {
			list_del(&info->list);
			mutex_unlock(&umh_list_lock);
			goto out;
		}
	}
	mutex_unlock(&umh_list_lock);
	return;
out:
	if (info->cleanup)
		info->cleanup(info);
}

