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
#include <sound/sof/ipc4/header.h>

#include "sof-client.h"

#define SOF_IPC_CLIENT_SUSPEND_DELAY_MS	3000

struct sof_msg_inject_priv {
	struct dentry *dfs_file;
	size_t max_msg_size;
	enum sof_ipc_type ipc_type;

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

static ssize_t sof_msg_inject_ipc4_dfs_read(struct file *file,
					    char __user *buffer,
					    size_t count, loff_t *ppos)
{
	struct sof_client_dev *cdev = file->private_data;
	struct sof_msg_inject_priv *priv = cdev->data;
	struct sof_ipc4_msg *ipc4_msg = priv->rx_buffer;
	size_t header_size = sizeof(ipc4_msg->header_u64);
	size_t remaining;

	if (!ipc4_msg->header_u64 || !count || *ppos)
		return 0;

	/* we need space for the header at minimum (u64) */
	if (count < header_size)
		return -ENOSPC;

	remaining = header_size;

	/* Only get large config have payload */
	if (SOF_IPC4_MSG_IS_MODULE_MSG(ipc4_msg->primary) &&
	    (SOF_IPC4_MSG_TYPE_GET(ipc4_msg->primary) == SOF_IPC4_MOD_LARGE_CONFIG_GET))
		remaining += ipc4_msg->data_size;

	if (count > remaining)
		count = remaining;
	else if (count < remaining)
		remaining = count;

	/* copy the header first */
	if (copy_to_user(buffer, &ipc4_msg->header_u64, header_size))
		return -EFAULT;

	*ppos += header_size;
	remaining -= header_size;

	if (!remaining)
		return count;

	if (remaining > ipc4_msg->data_size)
		remaining = ipc4_msg->data_size;

	/* Copy the payload */
	if (copy_to_user(buffer + *ppos, ipc4_msg->data_ptr, remaining))
		return -EFAULT;

	*ppos += remaining;
	return count;
}

static int sof_msg_inject_send_message(struct sof_client_dev *cdev)
{
	struct sof_msg_inject_priv *priv = cdev->data;
	struct device *dev = &cdev->auxdev.dev;
	int ret, err;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err_ratelimited(dev, "debugfs write failed to resume %d\n", ret);
		return ret;
	}

	/* send the message */
	ret = sof_client_ipc_tx_message(cdev, priv->tx_buffer, priv->rx_buffer,
					priv->max_msg_size);
	if (ret)
		dev_err(dev, "IPC message send failed: %d\n", ret);

	pm_runtime_mark_last_busy(dev);
	err = pm_runtime_put_autosuspend(dev);
	if (err < 0)
		dev_err_ratelimited(dev, "debugfs write failed to idle %d\n", err);

	return ret;
}

static ssize_t sof_msg_inject_dfs_write(struct file *file, const char __user *buffer,
					size_t count, loff_t *ppos)
{
	struct sof_client_dev *cdev = file->private_data;
	struct sof_msg_inject_priv *priv = cdev->data;
	ssize_t size;
	int ret;

	if (*ppos)
		return 0;

	size = simple_write_to_buffer(priv->tx_buffer, priv->max_msg_size,
				      ppos, buffer, count);
	if (size < 0)
		return size;
	if (size != count)
		return -EFAULT;

	memset(priv->rx_buffer, 0, priv->max_msg_size);

	ret = sof_msg_inject_send_message(cdev);

	/* return the error code if test failed */
	if (ret < 0)
		size = ret;

	return size;
};

static ssize_t sof_msg_inject_ipc4_dfs_write(struct file *file,
					     const char __user *buffer,
					     size_t count, loff_t *ppos)
{
	struct sof_client_dev *cdev = file->private_data;
	struct sof_msg_inject_priv *priv = cdev->data;
	struct sof_ipc4_msg *ipc4_msg = priv->tx_buffer;
	ssize_t size;
	int ret;

	if (*ppos)
		return 0;

	if (count < sizeof(ipc4_msg->header_u64))
		return -EINVAL;

	/* copy the header first */
	size = simple_write_to_buffer(&ipc4_msg->header_u64,
				      sizeof(ipc4_msg->header_u64),
				      ppos, buffer, count);
	if (size < 0)
		return size;
	if (size != sizeof(ipc4_msg->header_u64))
		return -EFAULT;

	count -= size;
	/* Copy the payload */
	size = simple_write_to_buffer(ipc4_msg->data_ptr,
				      priv->max_msg_size, ppos, buffer,
				      count);
	if (size < 0)
		return size;
	if (size != count)
		return -EFAULT;

	ipc4_msg->data_size = count;

	/* Initialize the reply storage */
	ipc4_msg = priv->rx_buffer;
	ipc4_msg->header_u64 = 0;
	ipc4_msg->data_size = priv->max_msg_size;
	memset(ipc4_msg->data_ptr, 0, priv->max_msg_size);

	ret = sof_msg_inject_send_message(cdev);

	/* return the error code if test failed */
	if (ret < 0)
		size = ret;

	return size;
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

static const struct file_operations sof_msg_inject_ipc4_fops = {
	.open = sof_msg_inject_dfs_open,
	.read = sof_msg_inject_ipc4_dfs_read,
	.write = sof_msg_inject_ipc4_dfs_write,
	.llseek = default_llseek,
	.release = sof_msg_inject_dfs_release,

	.owner = THIS_MODULE,
};

static int sof_msg_inject_probe(struct auxiliary_device *auxdev,
				const struct auxiliary_device_id *id)
{
	struct sof_client_dev *cdev = auxiliary_dev_to_sof_client_dev(auxdev);
	struct dentry *debugfs_root = sof_client_get_debugfs_root(cdev);
	static const struct file_operations *fops;
	struct device *dev = &auxdev->dev;
	struct sof_msg_inject_priv *priv;
	size_t alloc_size;

	/* allocate memory for client data */
	priv = devm_kzalloc(&auxdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ipc_type = sof_client_get_ipc_type(cdev);
	priv->max_msg_size = sof_client_get_ipc_max_payload_size(cdev);
	alloc_size = priv->max_msg_size;

	if (priv->ipc_type == SOF_INTEL_IPC4)
		alloc_size += sizeof(struct sof_ipc4_msg);

	priv->tx_buffer = devm_kmalloc(dev, alloc_size, GFP_KERNEL);
	priv->rx_buffer = devm_kzalloc(dev, alloc_size, GFP_KERNEL);
	if (!priv->tx_buffer || !priv->rx_buffer)
		return -ENOMEM;

	if (priv->ipc_type == SOF_INTEL_IPC4) {
		struct sof_ipc4_msg *ipc4_msg;

		ipc4_msg = priv->tx_buffer;
		ipc4_msg->data_ptr = priv->tx_buffer + sizeof(struct sof_ipc4_msg);

		ipc4_msg = priv->rx_buffer;
		ipc4_msg->data_ptr = priv->rx_buffer + sizeof(struct sof_ipc4_msg);

		fops = &sof_msg_inject_ipc4_fops;
	} else {
		fops = &sof_msg_inject_fops;
	}

	cdev->data = priv;

	priv->dfs_file = debugfs_create_file("ipc_msg_inject", 0644, debugfs_root,
					     cdev, fops);

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
