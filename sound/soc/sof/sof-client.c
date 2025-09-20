// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2022 Intel Corporation
//
// Authors: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//	    Peter Ujfalusi <peter.ujfalusi@linux.intel.com>
//

#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <sound/sof/ipc4/header.h>
#include "ops.h"
#include "sof-client.h"
#include "sof-priv.h"
#include "ipc3-priv.h"
#include "ipc4-priv.h"

/**
 * struct sof_ipc_event_entry - IPC client event description
 * @ipc_msg_type:	IPC msg type of the event the client is interested
 * @cdev:		sof_client_dev of the requesting client
 * @callback:		Callback function of the client
 * @list:		item in SOF core client event list
 */
struct sof_ipc_event_entry {
	u32 ipc_msg_type;
	struct sof_client_dev *cdev;
	sof_client_event_callback callback;
	struct list_head list;
};

/**
 * struct sof_state_event_entry - DSP panic event subscription entry
 * @cdev:		sof_client_dev of the requesting client
 * @callback:		Callback function of the client
 * @list:		item in SOF core client event list
 */
struct sof_state_event_entry {
	struct sof_client_dev *cdev;
	sof_client_fw_state_callback callback;
	struct list_head list;
};

/**
 * struct sof_client_dev_entry - client device entry for internal management use
 * @sdev:	pointer to SOF core device struct
 * @list:	item in SOF core client dev list
 * @client_dev: SOF client device
 */
struct sof_client_dev_entry {
	struct snd_sof_dev *sdev;
	struct list_head list;

	struct sof_client_dev client_dev;
};

#define cdev_to_centry(cdev) \
	container_of(cdev, struct sof_client_dev_entry, client_dev)

static void sof_client_auxdev_release(struct device *dev)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	struct sof_client_dev *cdev = auxiliary_dev_to_sof_client_dev(auxdev);
	struct sof_client_dev_entry *centry = cdev_to_centry(cdev);

	kfree(cdev->auxdev.dev.platform_data);
	kfree(centry);
}

static int sof_client_dev_add_data(struct sof_client_dev *cdev, const void *data,
				   size_t size)
{
	void *d = NULL;

	if (data) {
		d = kmemdup(data, size, GFP_KERNEL);
		if (!d)
			return -ENOMEM;
	}

	cdev->auxdev.dev.platform_data = d;
	return 0;
}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_IPC_FLOOD_TEST)
static int sof_register_ipc_flood_test(struct snd_sof_dev *sdev)
{
	int ret = 0;
	int i;

	if (sdev->pdata->ipc_type != SOF_IPC_TYPE_3)
		return 0;

	for (i = 0; i < CONFIG_SND_SOC_SOF_DEBUG_IPC_FLOOD_TEST_NUM; i++) {
		ret = sof_client_dev_register(sdev, "ipc_flood", i, NULL, 0);
		if (ret < 0)
			break;
	}

	if (ret) {
		for (; i >= 0; --i)
			sof_client_dev_unregister(sdev, "ipc_flood", i);
	}

	return ret;
}

static void sof_unregister_ipc_flood_test(struct snd_sof_dev *sdev)
{
	int i;

	for (i = 0; i < CONFIG_SND_SOC_SOF_DEBUG_IPC_FLOOD_TEST_NUM; i++)
		sof_client_dev_unregister(sdev, "ipc_flood", i);
}
#else
static inline int sof_register_ipc_flood_test(struct snd_sof_dev *sdev)
{
	return 0;
}

static inline void sof_unregister_ipc_flood_test(struct snd_sof_dev *sdev) {}
#endif /* CONFIG_SND_SOC_SOF_DEBUG_IPC_FLOOD_TEST */

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_IPC_MSG_INJECTOR)
static int sof_register_ipc_msg_injector(struct snd_sof_dev *sdev)
{
	return sof_client_dev_register(sdev, "msg_injector", 0, NULL, 0);
}

static void sof_unregister_ipc_msg_injector(struct snd_sof_dev *sdev)
{
	sof_client_dev_unregister(sdev, "msg_injector", 0);
}
#else
static inline int sof_register_ipc_msg_injector(struct snd_sof_dev *sdev)
{
	return 0;
}

static inline void sof_unregister_ipc_msg_injector(struct snd_sof_dev *sdev) {}
#endif /* CONFIG_SND_SOC_SOF_DEBUG_IPC_MSG_INJECTOR */

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_IPC_KERNEL_INJECTOR)
static int sof_register_ipc_kernel_injector(struct snd_sof_dev *sdev)
{
	/* Only IPC3 supported right now */
	if (sdev->pdata->ipc_type != SOF_IPC_TYPE_3)
		return 0;

	return sof_client_dev_register(sdev, "kernel_injector", 0, NULL, 0);
}

static void sof_unregister_ipc_kernel_injector(struct snd_sof_dev *sdev)
{
	sof_client_dev_unregister(sdev, "kernel_injector", 0);
}
#else
static inline int sof_register_ipc_kernel_injector(struct snd_sof_dev *sdev)
{
	return 0;
}

static inline void sof_unregister_ipc_kernel_injector(struct snd_sof_dev *sdev) {}
#endif /* CONFIG_SND_SOC_SOF_DEBUG_IPC_KERNEL_INJECTOR */

int sof_register_clients(struct snd_sof_dev *sdev)
{
	int ret;

	if (sdev->dspless_mode_selected)
		return 0;

	/* Register platform independent client devices */
	ret = sof_register_ipc_flood_test(sdev);
	if (ret) {
		dev_err(sdev->dev, "IPC flood test client registration failed\n");
		return ret;
	}

	ret = sof_register_ipc_msg_injector(sdev);
	if (ret) {
		dev_err(sdev->dev, "IPC message injector client registration failed\n");
		goto err_msg_injector;
	}

	ret = sof_register_ipc_kernel_injector(sdev);
	if (ret) {
		dev_err(sdev->dev, "IPC kernel injector client registration failed\n");
		goto err_kernel_injector;
	}

	/* Platform dependent client device registration */

	if (sof_ops(sdev) && sof_ops(sdev)->register_ipc_clients)
		ret = sof_ops(sdev)->register_ipc_clients(sdev);

	if (!ret)
		return 0;

	sof_unregister_ipc_kernel_injector(sdev);

err_kernel_injector:
	sof_unregister_ipc_msg_injector(sdev);

err_msg_injector:
	sof_unregister_ipc_flood_test(sdev);

	return ret;
}

void sof_unregister_clients(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev) && sof_ops(sdev)->unregister_ipc_clients)
		sof_ops(sdev)->unregister_ipc_clients(sdev);

	sof_unregister_ipc_kernel_injector(sdev);
	sof_unregister_ipc_msg_injector(sdev);
	sof_unregister_ipc_flood_test(sdev);
}

int sof_client_dev_register(struct snd_sof_dev *sdev, const char *name, u32 id,
			    const void *data, size_t size)
{
	struct sof_client_dev_entry *centry;
	struct auxiliary_device *auxdev;
	struct sof_client_dev *cdev;
	int ret;

	centry = kzalloc(sizeof(*centry), GFP_KERNEL);
	if (!centry)
		return -ENOMEM;

	cdev = &centry->client_dev;

	centry->sdev = sdev;
	auxdev = &cdev->auxdev;
	auxdev->name = name;
	auxdev->dev.parent = sdev->dev;
	auxdev->dev.release = sof_client_auxdev_release;
	auxdev->id = id;

	ret = sof_client_dev_add_data(cdev, data, size);
	if (ret < 0)
		goto err_dev_add_data;

	ret = auxiliary_device_init(auxdev);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to initialize client dev %s.%d\n", name, id);
		goto err_dev_init;
	}

	ret = auxiliary_device_add(&cdev->auxdev);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to add client dev %s.%d\n", name, id);
		/*
		 * sof_client_auxdev_release() will be invoked to free up memory
		 * allocations through put_device()
		 */
		auxiliary_device_uninit(&cdev->auxdev);
		return ret;
	}

	/* add to list of SOF client devices */
	mutex_lock(&sdev->ipc_client_mutex);
	list_add(&centry->list, &sdev->ipc_client_list);
	mutex_unlock(&sdev->ipc_client_mutex);

	return 0;

err_dev_init:
	kfree(cdev->auxdev.dev.platform_data);

err_dev_add_data:
	kfree(centry);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(sof_client_dev_register, "SND_SOC_SOF_CLIENT");

void sof_client_dev_unregister(struct snd_sof_dev *sdev, const char *name, u32 id)
{
	struct sof_client_dev_entry *centry;

	mutex_lock(&sdev->ipc_client_mutex);

	/*
	 * sof_client_auxdev_release() will be invoked to free up memory
	 * allocations through put_device()
	 */
	list_for_each_entry(centry, &sdev->ipc_client_list, list) {
		struct sof_client_dev *cdev = &centry->client_dev;

		if (!strcmp(cdev->auxdev.name, name) && cdev->auxdev.id == id) {
			list_del(&centry->list);
			auxiliary_device_delete(&cdev->auxdev);
			auxiliary_device_uninit(&cdev->auxdev);
			break;
		}
	}

	mutex_unlock(&sdev->ipc_client_mutex);
}
EXPORT_SYMBOL_NS_GPL(sof_client_dev_unregister, "SND_SOC_SOF_CLIENT");

int sof_client_ipc_tx_message(struct sof_client_dev *cdev, void *ipc_msg,
			      void *reply_data, size_t reply_bytes)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);

	if (sdev->pdata->ipc_type == SOF_IPC_TYPE_3) {
		struct sof_ipc_cmd_hdr *hdr = ipc_msg;

		return sof_ipc_tx_message(sdev->ipc, ipc_msg, hdr->size,
					  reply_data, reply_bytes);
	} else if (sdev->pdata->ipc_type == SOF_IPC_TYPE_4) {
		struct sof_ipc4_msg *msg = ipc_msg;

		return sof_ipc_tx_message(sdev->ipc, ipc_msg, msg->data_size,
					  reply_data, reply_bytes);
	}

	return -EINVAL;
}
EXPORT_SYMBOL_NS_GPL(sof_client_ipc_tx_message, "SND_SOC_SOF_CLIENT");

int sof_client_ipc_rx_message(struct sof_client_dev *cdev, void *ipc_msg, void *msg_buf)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);

	if (IS_ENABLED(CONFIG_SND_SOC_SOF_IPC3) &&
	    sdev->pdata->ipc_type == SOF_IPC_TYPE_3) {
		struct sof_ipc_cmd_hdr *hdr = ipc_msg;

		if (hdr->size < sizeof(hdr)) {
			dev_err(sdev->dev, "The received message size is invalid\n");
			return -EINVAL;
		}

		sof_ipc3_do_rx_work(sdev, ipc_msg, msg_buf);
		return 0;
	}

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_NS_GPL(sof_client_ipc_rx_message, "SND_SOC_SOF_CLIENT");

int sof_client_ipc_set_get_data(struct sof_client_dev *cdev, void *ipc_msg,
				bool set)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);

	if (sdev->pdata->ipc_type == SOF_IPC_TYPE_3) {
		struct sof_ipc_cmd_hdr *hdr = ipc_msg;

		return sof_ipc_set_get_data(sdev->ipc, ipc_msg, hdr->size, set);
	} else if (sdev->pdata->ipc_type == SOF_IPC_TYPE_4) {
		struct sof_ipc4_msg *msg = ipc_msg;

		return sof_ipc_set_get_data(sdev->ipc, ipc_msg, msg->data_size,
					    set);
	}

	return -EINVAL;
}
EXPORT_SYMBOL_NS_GPL(sof_client_ipc_set_get_data, "SND_SOC_SOF_CLIENT");

#ifdef CONFIG_SND_SOC_SOF_IPC4
struct sof_ipc4_fw_module *sof_client_ipc4_find_module(struct sof_client_dev *c, const guid_t *uuid)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(c);

	if (sdev->pdata->ipc_type == SOF_IPC_TYPE_4)
		return sof_ipc4_find_module_by_uuid(sdev, uuid);
	dev_err(sdev->dev, "Only supported with IPC4\n");

	return NULL;
}
EXPORT_SYMBOL_NS_GPL(sof_client_ipc4_find_module, "SND_SOC_SOF_CLIENT");

struct snd_sof_widget *sof_client_ipc4_find_swidget_by_id(struct sof_client_dev *cdev,
							  u32 module_id, int instance_id)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);

	if (sdev->pdata->ipc_type == SOF_IPC_TYPE_4)
		return sof_ipc4_find_swidget_by_ids(sdev, module_id, instance_id);
	dev_err(sdev->dev, "Only supported with IPC4\n");

	return NULL;
}
EXPORT_SYMBOL_NS_GPL(sof_client_ipc4_find_swidget_by_id, "SND_SOC_SOF_CLIENT");
#endif

int sof_suspend_clients(struct snd_sof_dev *sdev, pm_message_t state)
{
	const struct auxiliary_driver *adrv;
	struct sof_client_dev_entry *centry;

	mutex_lock(&sdev->ipc_client_mutex);

	list_for_each_entry(centry, &sdev->ipc_client_list, list) {
		struct sof_client_dev *cdev = &centry->client_dev;

		/* Skip devices without loaded driver */
		if (!cdev->auxdev.dev.driver)
			continue;

		adrv = to_auxiliary_drv(cdev->auxdev.dev.driver);
		if (adrv->suspend)
			adrv->suspend(&cdev->auxdev, state);
	}

	mutex_unlock(&sdev->ipc_client_mutex);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sof_suspend_clients, "SND_SOC_SOF_CLIENT");

int sof_resume_clients(struct snd_sof_dev *sdev)
{
	const struct auxiliary_driver *adrv;
	struct sof_client_dev_entry *centry;

	mutex_lock(&sdev->ipc_client_mutex);

	list_for_each_entry(centry, &sdev->ipc_client_list, list) {
		struct sof_client_dev *cdev = &centry->client_dev;

		/* Skip devices without loaded driver */
		if (!cdev->auxdev.dev.driver)
			continue;

		adrv = to_auxiliary_drv(cdev->auxdev.dev.driver);
		if (adrv->resume)
			adrv->resume(&cdev->auxdev);
	}

	mutex_unlock(&sdev->ipc_client_mutex);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sof_resume_clients, "SND_SOC_SOF_CLIENT");

struct dentry *sof_client_get_debugfs_root(struct sof_client_dev *cdev)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);

	return sdev->debugfs_root;
}
EXPORT_SYMBOL_NS_GPL(sof_client_get_debugfs_root, "SND_SOC_SOF_CLIENT");

/* DMA buffer allocation in client drivers must use the core SOF device */
struct device *sof_client_get_dma_dev(struct sof_client_dev *cdev)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);

	return sdev->dev;
}
EXPORT_SYMBOL_NS_GPL(sof_client_get_dma_dev, "SND_SOC_SOF_CLIENT");

const struct sof_ipc_fw_version *sof_client_get_fw_version(struct sof_client_dev *cdev)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);

	return &sdev->fw_ready.version;
}
EXPORT_SYMBOL_NS_GPL(sof_client_get_fw_version, "SND_SOC_SOF_CLIENT");

size_t sof_client_get_ipc_max_payload_size(struct sof_client_dev *cdev)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);

	return sdev->ipc->max_payload_size;
}
EXPORT_SYMBOL_NS_GPL(sof_client_get_ipc_max_payload_size, "SND_SOC_SOF_CLIENT");

enum sof_ipc_type sof_client_get_ipc_type(struct sof_client_dev *cdev)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);

	return sdev->pdata->ipc_type;
}
EXPORT_SYMBOL_NS_GPL(sof_client_get_ipc_type, "SND_SOC_SOF_CLIENT");

/* module refcount management of SOF core */
int sof_client_core_module_get(struct sof_client_dev *cdev)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);

	if (!try_module_get(sdev->dev->driver->owner))
		return -ENODEV;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sof_client_core_module_get, "SND_SOC_SOF_CLIENT");

void sof_client_core_module_put(struct sof_client_dev *cdev)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);

	module_put(sdev->dev->driver->owner);
}
EXPORT_SYMBOL_NS_GPL(sof_client_core_module_put, "SND_SOC_SOF_CLIENT");

/* IPC event handling */
void sof_client_ipc_rx_dispatcher(struct snd_sof_dev *sdev, void *msg_buf)
{
	struct sof_ipc_event_entry *event;
	u32 msg_type;

	if (sdev->pdata->ipc_type == SOF_IPC_TYPE_3) {
		struct sof_ipc_cmd_hdr *hdr = msg_buf;

		msg_type = hdr->cmd & SOF_GLB_TYPE_MASK;
	} else if (sdev->pdata->ipc_type == SOF_IPC_TYPE_4) {
		struct sof_ipc4_msg *msg = msg_buf;

		msg_type = SOF_IPC4_NOTIFICATION_TYPE_GET(msg->primary);
	} else {
		dev_dbg_once(sdev->dev, "Not supported IPC version: %d\n",
			     sdev->pdata->ipc_type);
		return;
	}

	mutex_lock(&sdev->client_event_handler_mutex);

	list_for_each_entry(event, &sdev->ipc_rx_handler_list, list) {
		if (event->ipc_msg_type == msg_type)
			event->callback(event->cdev, msg_buf);
	}

	mutex_unlock(&sdev->client_event_handler_mutex);
}

int sof_client_register_ipc_rx_handler(struct sof_client_dev *cdev,
				       u32 ipc_msg_type,
				       sof_client_event_callback callback)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);
	struct sof_ipc_event_entry *event;

	if (!callback)
		return -EINVAL;

	if (sdev->pdata->ipc_type == SOF_IPC_TYPE_3) {
		if (!(ipc_msg_type & SOF_GLB_TYPE_MASK))
			return -EINVAL;
	} else if (sdev->pdata->ipc_type == SOF_IPC_TYPE_4) {
		if (!(ipc_msg_type & SOF_IPC4_NOTIFICATION_TYPE_MASK))
			return -EINVAL;
	} else {
		dev_warn(sdev->dev, "%s: Not supported IPC version: %d\n",
			 __func__, sdev->pdata->ipc_type);
		return -EINVAL;
	}

	event = kmalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	event->ipc_msg_type = ipc_msg_type;
	event->cdev = cdev;
	event->callback = callback;

	/* add to list of SOF client devices */
	mutex_lock(&sdev->client_event_handler_mutex);
	list_add(&event->list, &sdev->ipc_rx_handler_list);
	mutex_unlock(&sdev->client_event_handler_mutex);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sof_client_register_ipc_rx_handler, "SND_SOC_SOF_CLIENT");

void sof_client_unregister_ipc_rx_handler(struct sof_client_dev *cdev,
					  u32 ipc_msg_type)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);
	struct sof_ipc_event_entry *event;

	mutex_lock(&sdev->client_event_handler_mutex);

	list_for_each_entry(event, &sdev->ipc_rx_handler_list, list) {
		if (event->cdev == cdev && event->ipc_msg_type == ipc_msg_type) {
			list_del(&event->list);
			kfree(event);
			break;
		}
	}

	mutex_unlock(&sdev->client_event_handler_mutex);
}
EXPORT_SYMBOL_NS_GPL(sof_client_unregister_ipc_rx_handler, "SND_SOC_SOF_CLIENT");

/*DSP state notification and query */
void sof_client_fw_state_dispatcher(struct snd_sof_dev *sdev)
{
	struct sof_state_event_entry *event;

	mutex_lock(&sdev->client_event_handler_mutex);

	list_for_each_entry(event, &sdev->fw_state_handler_list, list)
		event->callback(event->cdev, sdev->fw_state);

	mutex_unlock(&sdev->client_event_handler_mutex);
}

int sof_client_register_fw_state_handler(struct sof_client_dev *cdev,
					 sof_client_fw_state_callback callback)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);
	struct sof_state_event_entry *event;

	if (!callback)
		return -EINVAL;

	event = kmalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	event->cdev = cdev;
	event->callback = callback;

	/* add to list of SOF client devices */
	mutex_lock(&sdev->client_event_handler_mutex);
	list_add(&event->list, &sdev->fw_state_handler_list);
	mutex_unlock(&sdev->client_event_handler_mutex);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sof_client_register_fw_state_handler, "SND_SOC_SOF_CLIENT");

void sof_client_unregister_fw_state_handler(struct sof_client_dev *cdev)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);
	struct sof_state_event_entry *event;

	mutex_lock(&sdev->client_event_handler_mutex);

	list_for_each_entry(event, &sdev->fw_state_handler_list, list) {
		if (event->cdev == cdev) {
			list_del(&event->list);
			kfree(event);
			break;
		}
	}

	mutex_unlock(&sdev->client_event_handler_mutex);
}
EXPORT_SYMBOL_NS_GPL(sof_client_unregister_fw_state_handler, "SND_SOC_SOF_CLIENT");

enum sof_fw_state sof_client_get_fw_state(struct sof_client_dev *cdev)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);

	return sdev->fw_state;
}
EXPORT_SYMBOL_NS_GPL(sof_client_get_fw_state, "SND_SOC_SOF_CLIENT");

struct snd_sof_dev *sof_client_dev_to_sof_dev(struct sof_client_dev *cdev)
{
	struct sof_client_dev_entry *centry = cdev_to_centry(cdev);

	return centry->sdev;
}
EXPORT_SYMBOL_NS_GPL(sof_client_dev_to_sof_dev, "SND_SOC_SOF_CLIENT");
