#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/fsnotify_backend.h>
#include <linux/security.h>
#include <linux/syscalls.h>

#include "fanotify.h"

static int fanotify_release(struct inode *ignored, struct file *file)
{
	struct fsnotify_group *group = file->private_data;

	pr_debug("%s: file=%p group=%p\n", __func__, file, group);

	/* matches the fanotify_init->fsnotify_alloc_group */
	fsnotify_put_group(group);

	return 0;
}

static const struct file_operations fanotify_fops = {
	.poll		= NULL,
	.read		= NULL,
	.fasync		= NULL,
	.release	= fanotify_release,
	.unlocked_ioctl	= NULL,
	.compat_ioctl	= NULL,
};

/* fanotify syscalls */
SYSCALL_DEFINE3(fanotify_init, unsigned int, flags, unsigned int, event_f_flags,
		unsigned int, priority)
{
	struct fsnotify_group *group;
	int f_flags, fd;

	pr_debug("%s: flags=%d event_f_flags=%d priority=%d\n",
		__func__, flags, event_f_flags, priority);

	if (event_f_flags)
		return -EINVAL;
	if (priority)
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (flags & ~FAN_ALL_INIT_FLAGS)
		return -EINVAL;

	f_flags = (O_RDONLY | FMODE_NONOTIFY);
	if (flags & FAN_CLOEXEC)
		f_flags |= O_CLOEXEC;
	if (flags & FAN_NONBLOCK)
		f_flags |= O_NONBLOCK;

	/* fsnotify_alloc_group takes a ref.  Dropped in fanotify_release */
	group = fsnotify_alloc_group(&fanotify_fsnotify_ops);
	if (IS_ERR(group))
		return PTR_ERR(group);

	fd = anon_inode_getfd("[fanotify]", &fanotify_fops, group, f_flags);
	if (fd < 0)
		goto out_put_group;

	return fd;

out_put_group:
	fsnotify_put_group(group);
	return fd;
}
