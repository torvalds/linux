// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Google LLC
 *
 * A kernel module for testing the revocable API.
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/revocable.h>
#include <linux/slab.h>

#define TEST_CMD_RESOURCE_GONE "resource_gone"
#define TEST_MAGIC_OFFSET 0x1234
#define TEST_MAGIC_OFFSET2 0x5678

static struct dentry *debugfs_dir;

struct revocable_test_provider_priv {
	struct revocable_provider *rp;
	struct dentry *dentry;
	char res[16];
};

static int revocable_test_consumer_open(struct inode *inode, struct file *filp)
{
	struct revocable *rev;
	struct revocable_provider *rp = inode->i_private;

	rev = revocable_alloc(rp);
	if (!rev)
		return -ENOMEM;
	filp->private_data = rev;

	return 0;
}

static int revocable_test_consumer_release(struct inode *inode,
					   struct file *filp)
{
	struct revocable *rev = filp->private_data;

	revocable_free(rev);
	return 0;
}

static ssize_t revocable_test_consumer_read(struct file *filp,
					    char __user *buf,
					    size_t count, loff_t *offset)
{
	char *res;
	char data[16];
	size_t len;
	struct revocable *rev = filp->private_data;

	switch (*offset) {
	case 0:
		res = revocable_try_access(rev);
		snprintf(data, sizeof(data), "%s", res ?: "(null)");
		revocable_withdraw_access(rev);
		break;
	case TEST_MAGIC_OFFSET:
		{
			REVOCABLE_TRY_ACCESS_WITH(rev, res);
			snprintf(data, sizeof(data), "%s", res ?: "(null)");
		}
		break;
	case TEST_MAGIC_OFFSET2:
		REVOCABLE_TRY_ACCESS_SCOPED(rev, res)
			snprintf(data, sizeof(data), "%s", res ?: "(null)");
		break;
	default:
		return 0;
	}

	len = min_t(size_t, strlen(data), count);
	if (copy_to_user(buf, data, len))
		return -EFAULT;

	*offset = len;
	return len;
}

static const struct file_operations revocable_test_consumer_fops = {
	.open = revocable_test_consumer_open,
	.release = revocable_test_consumer_release,
	.read = revocable_test_consumer_read,
	.llseek = default_llseek,
};

static int revocable_test_provider_open(struct inode *inode, struct file *filp)
{
	struct revocable_test_provider_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	filp->private_data = priv;

	return 0;
}

static int revocable_test_provider_release(struct inode *inode,
					   struct file *filp)
{
	struct revocable_test_provider_priv *priv = filp->private_data;

	debugfs_remove(priv->dentry);
	if (priv->rp)
		revocable_provider_revoke(priv->rp);
	kfree(priv);

	return 0;
}

static ssize_t revocable_test_provider_write(struct file *filp,
					     const char __user *buf,
					     size_t count, loff_t *offset)
{
	size_t copied;
	char data[64];
	struct revocable_test_provider_priv *priv = filp->private_data;

	copied = strncpy_from_user(data, buf, sizeof(data));
	if (copied < 0)
		return copied;
	if (copied == sizeof(data))
		data[sizeof(data) - 1] = '\0';

	/*
	 * Note: The test can't just close the FD for signaling the
	 * resource gone.  Subsequent file operations on the opening
	 * FD of debugfs return -EIO after calling debugfs_remove().
	 * See also debugfs_file_get().
	 *
	 * Here is a side command channel for signaling the resource
	 * gone.
	 */
	if (!strcmp(data, TEST_CMD_RESOURCE_GONE)) {
		revocable_provider_revoke(priv->rp);
		priv->rp = NULL;
	} else {
		if (priv->res[0] != '\0')
			return 0;

		strscpy(priv->res, data);

		priv->rp = revocable_provider_alloc(&priv->res);
		if (!priv->rp)
			return -ENOMEM;

		priv->dentry = debugfs_create_file("consumer", 0400,
						   debugfs_dir, priv->rp,
						   &revocable_test_consumer_fops);
		if (!priv->dentry) {
			revocable_provider_revoke(priv->rp);
			return -ENOMEM;
		}
	}

	return copied;
}

static const struct file_operations revocable_test_provider_fops = {
	.open = revocable_test_provider_open,
	.release = revocable_test_provider_release,
	.write = revocable_test_provider_write,
};

static int __init revocable_test_init(void)
{
	debugfs_dir = debugfs_create_dir("revocable_test", NULL);
	if (!debugfs_dir)
		return -ENOMEM;

	if (!debugfs_create_file("provider", 0200, debugfs_dir, NULL,
				 &revocable_test_provider_fops)) {
		debugfs_remove_recursive(debugfs_dir);
		return -ENOMEM;
	}

	return 0;
}

static void __exit revocable_test_exit(void)
{
	debugfs_remove_recursive(debugfs_dir);
}

module_init(revocable_test_init);
module_exit(revocable_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tzung-Bi Shih <tzungbi@kernel.org>");
MODULE_DESCRIPTION("Revocable Kselftest");
