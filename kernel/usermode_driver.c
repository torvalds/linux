// SPDX-License-Identifier: GPL-2.0-only
/*
 * umd - User mode driver support
 */
#include <linux/shmem_fs.h>
#include <linux/pipe_fs_i.h>
#include <linux/mount.h>
#include <linux/fs_struct.h>
#include <linux/task_work.h>
#include <linux/usermode_driver.h>

static struct vfsmount *blob_to_mnt(const void *data, size_t len, const char *name)
{
	struct file_system_type *type;
	struct vfsmount *mnt;
	struct file *file;
	ssize_t written;
	loff_t pos = 0;

	type = get_fs_type("tmpfs");
	if (!type)
		return ERR_PTR(-ENODEV);

	mnt = kern_mount(type);
	put_filesystem(type);
	if (IS_ERR(mnt))
		return mnt;

	file = file_open_root(mnt->mnt_root, mnt, name, O_CREAT | O_WRONLY, 0700);
	if (IS_ERR(file)) {
		mntput(mnt);
		return ERR_CAST(file);
	}

	written = kernel_write(file, data, len, &pos);
	if (written != len) {
		int err = written;
		if (err >= 0)
			err = -ENOMEM;
		filp_close(file, NULL);
		mntput(mnt);
		return ERR_PTR(err);
	}

	fput(file);

	/* Flush delayed fput so exec can open the file read-only */
	flush_delayed_fput();
	task_work_run();
	return mnt;
}

/**
 * umd_load_blob - Remember a blob of bytes for fork_usermode_driver
 * @info: information about usermode driver
 * @data: a blob of bytes that can be executed as a file
 * @len:  The lentgh of the blob
 *
 */
int umd_load_blob(struct umd_info *info, const void *data, size_t len)
{
	struct vfsmount *mnt;

	if (WARN_ON_ONCE(info->wd.dentry || info->wd.mnt))
		return -EBUSY;

	mnt = blob_to_mnt(data, len, info->driver_name);
	if (IS_ERR(mnt))
		return PTR_ERR(mnt);

	info->wd.mnt = mnt;
	info->wd.dentry = mnt->mnt_root;
	return 0;
}
EXPORT_SYMBOL_GPL(umd_load_blob);

/**
 * umd_unload_blob - Disassociate @info from a previously loaded blob
 * @info: information about usermode driver
 *
 */
int umd_unload_blob(struct umd_info *info)
{
	if (WARN_ON_ONCE(!info->wd.mnt ||
			 !info->wd.dentry ||
			 info->wd.mnt->mnt_root != info->wd.dentry))
		return -EINVAL;

	kern_unmount(info->wd.mnt);
	info->wd.mnt = NULL;
	info->wd.dentry = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(umd_unload_blob);

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

	set_fs_pwd(current->fs, &umd_info->wd);
	umd_info->pipe_to_umh = to_umh[1];
	umd_info->pipe_from_umh = from_umh[0];
	umd_info->tgid = get_pid(task_tgid(current));
	return 0;
}

static void umd_cleanup(struct subprocess_info *info)
{
	struct umd_info *umd_info = info->data;

	/* cleanup if umh_setup() was successful but exec failed */
	if (info->retval)
		umd_cleanup_helper(umd_info);
}

/**
 * umd_cleanup_helper - release the resources which were allocated in umd_setup
 * @info: information about usermode driver
 */
void umd_cleanup_helper(struct umd_info *info)
{
	fput(info->pipe_to_umh);
	fput(info->pipe_from_umh);
	put_pid(info->tgid);
	info->tgid = NULL;
}
EXPORT_SYMBOL_GPL(umd_cleanup_helper);

/**
 * fork_usermode_driver - fork a usermode driver
 * @info: information about usermode driver (shouldn't be NULL)
 *
 * Returns either negative error or zero which indicates success in
 * executing a usermode driver. In such case 'struct umd_info *info'
 * is populated with two pipes and a tgid of the process. The caller is
 * responsible for health check of the user process, killing it via
 * tgid, and closing the pipes when user process is no longer needed.
 */
int fork_usermode_driver(struct umd_info *info)
{
	struct subprocess_info *sub_info;
	const char *argv[] = { info->driver_name, NULL };
	int err;

	if (WARN_ON_ONCE(info->tgid))
		return -EBUSY;

	err = -ENOMEM;
	sub_info = call_usermodehelper_setup(info->driver_name,
					     (char **)argv, NULL, GFP_KERNEL,
					     umd_setup, umd_cleanup, info);
	if (!sub_info)
		goto out;

	err = call_usermodehelper_exec(sub_info, UMH_WAIT_EXEC);
out:
	return err;
}
EXPORT_SYMBOL_GPL(fork_usermode_driver);


