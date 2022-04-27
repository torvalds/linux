// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.
//
// Author: Peter Ujfalusi <peter.ujfalusi@linux.intel.com>
//

#include <linux/auxiliary_bus.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/ktime.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <sound/sof/header.h>

#include "sof-client.h"

#define SOF_IPC_CLIENT_SUSPEND_DELAY_MS	3000

struct sof_msg_inject_priv {
	struct dentry *dfs_file;

	void *tx_buffer;
	void *rx_buffer;
};

static int sof_msg_inject_dfs_open(struct inode *inode, struct file *file)
{
	struct sof_client_dev *cdev = inode->i_private;
	int ret;

	if (sof_client_get_fw_state(cdev) == SOF_FW_CRASHED)
		return -ENODEV;

	ret = debugfs_file_get(file->f_path.dentry);
	if (unlikely(ret))
		return ret;

	ret = simple_open(inode, file);
	if (ret)
		debugfs_file_put(file->f_path.dentry);

	return ret;
}

static ssize_t sof_msg_inject_dfs_read(struct file *file, char __user *buffer,
				       size_t count, loff_t *ppos)
{
	struct sof_client_dev *cdev = file->private_data;
	struct sof_msg_inject_priv *priv = cdev->data;
	struct sof_ipc_reply *rhdr = priv->rx_buffer;

	if (!rhdr->hdr.size || !count || *ppos)
		return 0;

	if (count > rhdr->hdr.size)
		count = rhdr->hdr.size;

	if (copy_to_user(buffer, priv->rx_buffer, count))
		return -EFAULT;

	*ppos += count;
	return count;
}

static ssize_t sof_msg_inject_dfs_write(struct file *file, const char __user *buffer,
					size_t count, loff_t *ppos)
{
	struct sof_client_dev *cdev = file->private_data;
	struct sof_msg_inject_priv *priv = cdev->data;
	struct device *dev = &cdev->auxdev.dev;
	int ret, err;
	size_t size;

	if (*ppos)
		return 0;

	size = simple_write_to_buffer(priv->tx_buffer, SOF_IPC_MSG_MAX_SIZE,
				      ppos, buffer, count);
	if (size != count)
		return size > 0 ? -EFAULT : size;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err_ratelimited(dev, "debugfs write failed to resume %d\n", ret);
		return ret;
	}

	/* send the message */
	memset(priv->rx_buffer, 0, SOF_IPC_MSG_MAX_SIZE);
	ret = sof_client_ipc_tx_message(cdev, priv->tx_buffer, priv->rx_buffer,
					SOF_IPC_MSG_MAX_SIZE);
	pm_runtime_mark_last_busy(dev);
	err = pm_runtime_put_autosuspend(dev);
	if (err < 0)
		dev_err_ratelimited(dev, "debugfs write failed to idle %d\n", err);

	/* return size if test is successful */
	if (ret >= 0)
		ret = size;

	return ret;
};

static int sof_msg_inject_dfs_release(struct inode *inode, struct file *file)
{
	debugfs_file_put(file->f_path.dentry);

	return 0;
}

static const struct file_operations sof_msg_inject_fops = {
	.open = sof_msg_inject_dfs_open,
	.read = sof_msg_inject_dfs_read,
	.write = sof_msg_inject_dfs_write,
	.llseek = default_llseek,
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

	/* allocate memory for client data */
	priv = devm_kzalloc(&auxdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->tx_buffer = devm_kmalloc(dev, SOF_IPC_MSG_MAX_SIZE, GFP_KERNEL);
	priv->rx_buffer = devm_kzalloc(dev, SOF_IPC_MSG_MAX_SIZE, GFP_KERNEL);
	if (!priv->tx_buffer || !priv->rx_buffer)
		return -ENOMEM;

	cdev->data = priv;

	priv->dfs_file = debugfs_create_file("ipc_msg_inject", 0644, debugfs_root,
					     cdev, &sof_msg_inject_fops);

	/* enable runtime PM */
	pm_runtime_set_autosuspend_delay(dev, SOF_IPC_CLIENT_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
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

	debugfs_remove(priv->dfs_file);
}

static const struct auxiliary_device_id sof_msg_inject_client_id_table[] = {
	{ .name = "snd_sof.msg_injector" },
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

MODULE_DESCRIPTION("SOF IPC Message Injector Client Driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_CLIENT);
