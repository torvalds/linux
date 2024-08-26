// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2023 Google Inc
//
// Author: Curtis Malainey <cujomalainey@chromium.org>
//

#include <linux/auxiliary_bus.h>
#include <linux/debugfs.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <sound/sof/header.h>

#include "sof-client.h"

#define SOF_IPC_CLIENT_SUSPEND_DELAY_MS	3000

struct sof_msg_inject_priv {
	struct dentry *kernel_dfs_file;
	size_t max_msg_size;

	void *kernel_buffer;
};

static int sof_msg_inject_dfs_open(struct inode *inode, struct file *file)
{
	int ret = debugfs_file_get(file->f_path.dentry);

	if (unlikely(ret))
		return ret;

	ret = simple_open(inode, file);
	if (ret)
		debugfs_file_put(file->f_path.dentry);

	return ret;
}

static ssize_t sof_kernel_msg_inject_dfs_write(struct file *file, const char __user *buffer,
					       size_t count, loff_t *ppos)
{
	struct sof_client_dev *cdev = file->private_data;
	struct sof_msg_inject_priv *priv = cdev->data;
	struct sof_ipc_cmd_hdr *hdr = priv->kernel_buffer;
	struct device *dev = &cdev->auxdev.dev;
	ssize_t size;
	int ret;

	if (*ppos)
		return 0;

	size = simple_write_to_buffer(priv->kernel_buffer, priv->max_msg_size,
				      ppos, buffer, count);
	if (size < 0)
		return size;
	if (size != count)
		return -EFAULT;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err_ratelimited(dev, "debugfs write failed to resume %d\n", ret);
		return ret;
	}

	sof_client_ipc_rx_message(cdev, hdr, priv->kernel_buffer);

	pm_runtime_mark_last_busy(dev);
	ret = pm_runtime_put_autosuspend(dev);
	if (ret < 0)
		dev_err_ratelimited(dev, "debugfs write failed to idle %d\n", ret);

	return count;
};

static int sof_msg_inject_dfs_release(struct inode *inode, struct file *file)
{
	debugfs_file_put(file->f_path.dentry);

	return 0;
}

static const struct file_operations sof_kernel_msg_inject_fops = {
	.open = sof_msg_inject_dfs_open,
	.write = sof_kernel_msg_inject_dfs_write,
	.release = sof_msg_inject_dfs_release,

	.owner = THIS_MODULE,
};

static int sof_msg_inject_probe(struct auxiliary_device *auxdev,
				const struct auxiliary_device_id *id)
{
	struct sof_client_dev *cdev = auxiliary_dev_to_sof_client_dev(auxdev);
	struct dentry *debugfs_root = sof_client_get_debugfs_root(cdev);
	struct device *dev = &auxdev->dev;
	struct sof_msg_inject_priv *priv;
	size_t alloc_size;

	/* allocate memory for client data */
	priv = devm_kzalloc(&auxdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->max_msg_size = sof_client_get_ipc_max_payload_size(cdev);
	alloc_size = priv->max_msg_size;
	priv->kernel_buffer = devm_kmalloc(dev, alloc_size, GFP_KERNEL);

	if (!priv->kernel_buffer)
		return -ENOMEM;

	cdev->data = priv;

	priv->kernel_dfs_file = debugfs_create_file("kernel_ipc_msg_inject", 0644,
						    debugfs_root, cdev,
						    &sof_kernel_msg_inject_fops);

	/* enable runtime PM */
	pm_runtime_set_autosuspend_delay(dev, SOF_IPC_CLIENT_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_idle(dev);

	return 0;
}

static void sof_msg_inject_remove(struct auxiliary_device *auxdev)
{
	struct sof_client_dev *cdev = auxiliary_dev_to_sof_client_dev(auxdev);
	struct sof_msg_inject_priv *priv = cdev->data;

	pm_runtime_disable(&auxdev->dev);

	debugfs_remove(priv->kernel_dfs_file);
}

static const struct auxiliary_device_id sof_msg_inject_client_id_table[] = {
	{ .name = "snd_sof.kernel_injector" },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, sof_msg_inject_client_id_table);

/*
 * No need for driver pm_ops as the generic pm callbacks in the auxiliary bus
 * type are enough to ensure that the parent SOF device resumes to bring the DSP
 * back to D0.
 * Driver name will be set based on KBUILD_MODNAME.
 */
static struct auxiliary_driver sof_msg_inject_client_drv = {
	.probe = sof_msg_inject_probe,
	.remove = sof_msg_inject_remove,

	.id_table = sof_msg_inject_client_id_table,
};

module_auxiliary_driver(sof_msg_inject_client_drv);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SOF IPC Kernel Injector Client Driver");
MODULE_IMPORT_NS(SND_SOC_SOF_CLIENT);
