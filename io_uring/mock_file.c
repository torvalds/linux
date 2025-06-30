#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/anon_inodes.h>

#include <linux/io_uring/cmd.h>
#include <linux/io_uring_types.h>
#include <uapi/linux/io_uring/mock_file.h>

static int io_mock_cmd(struct io_uring_cmd *cmd, unsigned int issue_flags)
{
	return -ENOTSUPP;
}

static const struct file_operations io_mock_fops = {
	.owner		= THIS_MODULE,
	.uring_cmd	= io_mock_cmd,
};

static int io_create_mock_file(struct io_uring_cmd *cmd, unsigned int issue_flags)
{
	const struct io_uring_sqe *sqe = cmd->sqe;
	struct io_uring_mock_create mc, __user *uarg;
	struct file *file = NULL;
	size_t uarg_size;
	int fd, ret;

	/*
	 * It's a testing only driver that allows exercising edge cases
	 * that wouldn't be possible to hit otherwise.
	 */
	add_taint(TAINT_TEST, LOCKDEP_STILL_OK);

	uarg = u64_to_user_ptr(READ_ONCE(sqe->addr));
	uarg_size = READ_ONCE(sqe->len);

	if (sqe->ioprio || sqe->__pad1 || sqe->addr3 || sqe->file_index)
		return -EINVAL;
	if (uarg_size != sizeof(mc))
		return -EINVAL;

	memset(&mc, 0, sizeof(mc));
	if (copy_from_user(&mc, uarg, uarg_size))
		return -EFAULT;
	if (!mem_is_zero(mc.__resv, sizeof(mc.__resv)) || mc.flags)
		return -EINVAL;

	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return fd;

	file = anon_inode_create_getfile("[io_uring_mock]", &io_mock_fops,
					 NULL, O_RDWR | O_CLOEXEC, NULL);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto fail;
	}

	mc.out_fd = fd;
	if (copy_to_user(uarg, &mc, uarg_size)) {
		fput(file);
		ret = -EFAULT;
		goto fail;
	}

	fd_install(fd, file);
	return 0;
fail:
	put_unused_fd(fd);
	return ret;
}

static int io_probe_mock(struct io_uring_cmd *cmd)
{
	const struct io_uring_sqe *sqe = cmd->sqe;
	struct io_uring_mock_probe mp, __user *uarg;
	size_t uarg_size;

	uarg = u64_to_user_ptr(READ_ONCE(sqe->addr));
	uarg_size = READ_ONCE(sqe->len);

	if (sqe->ioprio || sqe->__pad1 || sqe->addr3 || sqe->file_index ||
	    uarg_size != sizeof(mp))
		return -EINVAL;

	memset(&mp, 0, sizeof(mp));
	if (copy_from_user(&mp, uarg, uarg_size))
		return -EFAULT;
	if (!mem_is_zero(&mp, sizeof(mp)))
		return -EINVAL;

	mp.features = 0;

	if (copy_to_user(uarg, &mp, uarg_size))
		return -EFAULT;
	return 0;
}

static int iou_mock_mgr_cmd(struct io_uring_cmd *cmd, unsigned int issue_flags)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (cmd->cmd_op) {
	case IORING_MOCK_MGR_CMD_PROBE:
		return io_probe_mock(cmd);
	case IORING_MOCK_MGR_CMD_CREATE:
		return io_create_mock_file(cmd, issue_flags);
	}
	return -EOPNOTSUPP;
}

static const struct file_operations iou_mock_dev_fops = {
	.owner		= THIS_MODULE,
	.uring_cmd	= iou_mock_mgr_cmd,
};

static struct miscdevice iou_mock_miscdev = {
	.minor			= MISC_DYNAMIC_MINOR,
	.name			= "io_uring_mock",
	.fops			= &iou_mock_dev_fops,
};

static int __init io_mock_init(void)
{
	int ret;

	ret = misc_register(&iou_mock_miscdev);
	if (ret < 0) {
		pr_err("Could not initialize io_uring mock device\n");
		return ret;
	}
	return 0;
}

static void __exit io_mock_exit(void)
{
	misc_deregister(&iou_mock_miscdev);
}

module_init(io_mock_init)
module_exit(io_mock_exit)

MODULE_AUTHOR("Pavel Begunkov <asml.silence@gmail.com>");
MODULE_DESCRIPTION("io_uring mock file");
MODULE_LICENSE("GPL");
