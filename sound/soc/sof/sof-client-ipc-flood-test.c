// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2022 Intel Corporation
//
// Authors: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//	    Peter Ujfalusi <peter.ujfalusi@linux.intel.com>
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

#define MAX_IPC_FLOOD_DURATION_MS	1000
#define MAX_IPC_FLOOD_COUNT		10000
#define IPC_FLOOD_TEST_RESULT_LEN	512
#define SOF_IPC_CLIENT_SUSPEND_DELAY_MS	3000

#define DEBUGFS_IPC_FLOOD_COUNT		"ipc_flood_count"
#define DEBUGFS_IPC_FLOOD_DURATION	"ipc_flood_duration_ms"

struct sof_ipc_flood_priv {
	struct dentry *dfs_root;
	struct dentry *dfs_link[2];
	char *buf;
};

static int sof_ipc_flood_dfs_open(struct inode *inode, struct file *file)
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

/*
 * helper function to perform the flood test. Only one of the two params, ipc_duration_ms
 * or ipc_count, will be non-zero and will determine the type of test
 */
static int sof_debug_ipc_flood_test(struct sof_client_dev *cdev,
				    bool flood_duration_test,
				    unsigned long ipc_duration_ms,
				    unsigned long ipc_count)
{
	struct sof_ipc_flood_priv *priv = cdev->data;
	struct device *dev = &cdev->auxdev.dev;
	struct sof_ipc_cmd_hdr hdr;
	u64 min_response_time = U64_MAX;
	ktime_t start, end, test_end;
	u64 avg_response_time = 0;
	u64 max_response_time = 0;
	u64 ipc_response_time;
	int i = 0;
	int ret;

	/* configure test IPC */
	hdr.cmd = SOF_IPC_GLB_TEST_MSG | SOF_IPC_TEST_IPC_FLOOD;
	hdr.size = sizeof(hdr);

	/* set test end time for duration flood test */
	if (flood_duration_test)
		test_end = ktime_get_ns() + ipc_duration_ms * NSEC_PER_MSEC;

	/* send test IPC's */
	while (1) {
		start = ktime_get();
		ret = sof_client_ipc_tx_message_no_reply(cdev, &hdr);
		end = ktime_get();

		if (ret < 0)
			break;

		/* compute min and max response times */
		ipc_response_time = ktime_to_ns(ktime_sub(end, start));
		min_response_time = min(min_response_time, ipc_response_time);
		max_response_time = max(max_response_time, ipc_response_time);

		/* sum up response times */
		avg_response_time += ipc_response_time;
		i++;

		/* test complete? */
		if (flood_duration_test) {
			if (ktime_to_ns(end) >= test_end)
				break;
		} else {
			if (i == ipc_count)
				break;
		}
	}

	if (ret < 0)
		dev_err(dev, "ipc flood test failed at %d iterations\n", i);

	/* return if the first IPC fails */
	if (!i)
		return ret;

	/* compute average response time */
	do_div(avg_response_time, i);

	/* clear previous test output */
	memset(priv->buf, 0, IPC_FLOOD_TEST_RESULT_LEN);

	if (!ipc_count) {
		dev_dbg(dev, "IPC Flood test duration: %lums\n", ipc_duration_ms);
		snprintf(priv->buf, IPC_FLOOD_TEST_RESULT_LEN,
			 "IPC Flood test duration: %lums\n", ipc_duration_ms);
	}

	dev_dbg(dev, "IPC Flood count: %d, Avg response time: %lluns\n",
		i, avg_response_time);
	dev_dbg(dev, "Max response time: %lluns\n", max_response_time);
	dev_dbg(dev, "Min response time: %lluns\n", min_response_time);

	/* format output string and save test results */
	snprintf(priv->buf + strlen(priv->buf),
		 IPC_FLOOD_TEST_RESULT_LEN - strlen(priv->buf),
		 "IPC Flood count: %d\nAvg response time: %lluns\n",
		 i, avg_response_time);

	snprintf(priv->buf + strlen(priv->buf),
		 IPC_FLOOD_TEST_RESULT_LEN - strlen(priv->buf),
		 "Max response time: %lluns\nMin response time: %lluns\n",
		 max_response_time, min_response_time);

	return ret;
}

/*
 * Writing to the debugfs entry initiates the IPC flood test based on
 * the IPC count or the duration specified by the user.
 */
static ssize_t sof_ipc_flood_dfs_write(struct file *file, const char __user *buffer,
				       size_t count, loff_t *ppos)
{
	struct sof_client_dev *cdev = file->private_data;
	struct device *dev = &cdev->auxdev.dev;
	unsigned long ipc_duration_ms = 0;
	bool flood_duration_test = false;
	unsigned long ipc_count = 0;
	int err;
	char *string;
	int ret;

	if (*ppos != 0)
		return -EINVAL;

	string = kzalloc(count + 1, GFP_KERNEL);
	if (!string)
		return -ENOMEM;

	if (copy_from_user(string, buffer, count)) {
		ret = -EFAULT;
		goto out;
	}

	/*
	 * write op is only supported for ipc_flood_count or
	 * ipc_flood_duration_ms debugfs entries atm.
	 * ipc_flood_count floods the DSP with the number of IPC's specified.
	 * ipc_duration_ms test floods the DSP for the time specified
	 * in the debugfs entry.
	 */
	if (debugfs_get_aux_num(file))
		flood_duration_test = true;

	/* test completion criterion */
	if (flood_duration_test)
		ret = kstrtoul(string, 0, &ipc_duration_ms);
	else
		ret = kstrtoul(string, 0, &ipc_count);
	if (ret < 0)
		goto out;

	/* limit max duration/ipc count for flood test */
	if (flood_duration_test) {
		if (!ipc_duration_ms) {
			ret = count;
			goto out;
		}

		/* find the minimum. min() is not used to avoid warnings */
		if (ipc_duration_ms > MAX_IPC_FLOOD_DURATION_MS)
			ipc_duration_ms = MAX_IPC_FLOOD_DURATION_MS;
	} else {
		if (!ipc_count) {
			ret = count;
			goto out;
		}

		/* find the minimum. min() is not used to avoid warnings */
		if (ipc_count > MAX_IPC_FLOOD_COUNT)
			ipc_count = MAX_IPC_FLOOD_COUNT;
	}

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err_ratelimited(dev, "debugfs write failed to resume %d\n", ret);
		goto out;
	}

	/* flood test */
	ret = sof_debug_ipc_flood_test(cdev, flood_duration_test,
				       ipc_duration_ms, ipc_count);

	err = pm_runtime_put_autosuspend(dev);
	if (err < 0)
		dev_err_ratelimited(dev, "debugfs write failed to idle %d\n", err);

	/* return count if test is successful */
	if (ret >= 0)
		ret = count;
out:
	kfree(string);
	return ret;
}

/* return the result of the last IPC flood test */
static ssize_t sof_ipc_flood_dfs_read(struct file *file, char __user *buffer,
				      size_t count, loff_t *ppos)
{
	struct sof_client_dev *cdev = file->private_data;
	struct sof_ipc_flood_priv *priv = cdev->data;
	size_t size_ret;

	if (*ppos)
		return 0;

	count = min_t(size_t, count, strlen(priv->buf));
	size_ret = copy_to_user(buffer, priv->buf, count);
	if (size_ret)
		return -EFAULT;

	*ppos += count;
	return count;
}

static int sof_ipc_flood_dfs_release(struct inode *inode, struct file *file)
{
	debugfs_file_put(file->f_path.dentry);

	return 0;
}

static const struct file_operations sof_ipc_flood_fops = {
	.open = sof_ipc_flood_dfs_open,
	.read = sof_ipc_flood_dfs_read,
	.llseek = default_llseek,
	.write = sof_ipc_flood_dfs_write,
	.release = sof_ipc_flood_dfs_release,

	.owner = THIS_MODULE,
};

/*
 * The IPC test client creates a couple of debugfs entries that will be used
 * flood tests. Users can write to these entries to execute the IPC flood test
 * by specifying either the number of IPCs to flood the DSP with or the duration
 * (in ms) for which the DSP should be flooded with test IPCs. At the
 * end of each test, the average, min and max response times are reported back.
 * The results of the last flood test can be accessed by reading the debugfs
 * entries.
 */
static int sof_ipc_flood_probe(struct auxiliary_device *auxdev,
			       const struct auxiliary_device_id *id)
{
	struct sof_client_dev *cdev = auxiliary_dev_to_sof_client_dev(auxdev);
	struct dentry *debugfs_root = sof_client_get_debugfs_root(cdev);
	struct device *dev = &auxdev->dev;
	struct sof_ipc_flood_priv *priv;

	/* allocate memory for client data */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->buf = devm_kmalloc(dev, IPC_FLOOD_TEST_RESULT_LEN, GFP_KERNEL);
	if (!priv->buf)
		return -ENOMEM;

	cdev->data = priv;

	/* create debugfs root folder with device name under parent SOF dir */
	priv->dfs_root = debugfs_create_dir(dev_name(dev), debugfs_root);
	if (!IS_ERR_OR_NULL(priv->dfs_root)) {
		/* create read-write ipc_flood_count debugfs entry */
		debugfs_create_file_aux_num(DEBUGFS_IPC_FLOOD_COUNT, 0644,
				    priv->dfs_root, cdev, 0, &sof_ipc_flood_fops);

		/* create read-write ipc_flood_duration_ms debugfs entry */
		debugfs_create_file_aux_num(DEBUGFS_IPC_FLOOD_DURATION, 0644,
				    priv->dfs_root, cdev, 1, &sof_ipc_flood_fops);

		if (auxdev->id == 0) {
			/*
			 * Create symlinks for backwards compatibility to the
			 * first IPC flood test instance
			 */
			char target[100];

			snprintf(target, 100, "%s/" DEBUGFS_IPC_FLOOD_COUNT,
				 dev_name(dev));
			priv->dfs_link[0] =
				debugfs_create_symlink(DEBUGFS_IPC_FLOOD_COUNT,
						       debugfs_root, target);

			snprintf(target, 100, "%s/" DEBUGFS_IPC_FLOOD_DURATION,
				 dev_name(dev));
			priv->dfs_link[1] =
				debugfs_create_symlink(DEBUGFS_IPC_FLOOD_DURATION,
						       debugfs_root, target);
		}
	}

	/* enable runtime PM */
	pm_runtime_set_autosuspend_delay(dev, SOF_IPC_CLIENT_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_idle(dev);

	return 0;
}

static void sof_ipc_flood_remove(struct auxiliary_device *auxdev)
{
	struct sof_client_dev *cdev = auxiliary_dev_to_sof_client_dev(auxdev);
	struct sof_ipc_flood_priv *priv = cdev->data;

	pm_runtime_disable(&auxdev->dev);

	if (auxdev->id == 0) {
		debugfs_remove(priv->dfs_link[0]);
		debugfs_remove(priv->dfs_link[1]);
	}

	debugfs_remove_recursive(priv->dfs_root);
}

static const struct auxiliary_device_id sof_ipc_flood_client_id_table[] = {
	{ .name = "snd_sof.ipc_flood" },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, sof_ipc_flood_client_id_table);

/*
 * No need for driver pm_ops as the generic pm callbacks in the auxiliary bus
 * type are enough to ensure that the parent SOF device resumes to bring the DSP
 * back to D0.
 * Driver name will be set based on KBUILD_MODNAME.
 */
static struct auxiliary_driver sof_ipc_flood_client_drv = {
	.probe = sof_ipc_flood_probe,
	.remove = sof_ipc_flood_remove,

	.id_table = sof_ipc_flood_client_id_table,
};

module_auxiliary_driver(sof_ipc_flood_client_drv);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SOF IPC Flood Test Client Driver");
MODULE_IMPORT_NS("SND_SOC_SOF_CLIENT");
