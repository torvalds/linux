/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_SOF_CLIENT_H
#define __SOC_SOF_CLIENT_H

#include <linux/auxiliary_bus.h>
#include <linux/device.h>
#include <linux/list.h>
#include <sound/sof.h>

struct sof_ipc_fw_version;
struct sof_ipc_cmd_hdr;
struct snd_sof_dev;
struct dentry;

/**
 * struct sof_client_dev - SOF client device
 * @auxdev:	auxiliary device
 * @sdev:	pointer to SOF core device struct
 * @list:	item in SOF core client dev list
 * @data:	device specific data
 */
struct sof_client_dev {
	struct auxiliary_device auxdev;
	struct snd_sof_dev *sdev;
	struct list_head list;
	void *data;
};

#define sof_client_dev_to_sof_dev(cdev)		((cdev)->sdev)

#define auxiliary_dev_to_sof_client_dev(auxiliary_dev) \
	container_of(auxiliary_dev, struct sof_client_dev, auxdev)

#define dev_to_sof_client_dev(dev) \
	container_of(to_auxiliary_dev(dev), struct sof_client_dev, auxdev)

int sof_client_ipc_tx_message(struct sof_client_dev *cdev, void *ipc_msg,
			      void *reply_data, size_t reply_bytes);

struct dentry *sof_client_get_debugfs_root(struct sof_client_dev *cdev);
struct device *sof_client_get_dma_dev(struct sof_client_dev *cdev);
const struct sof_ipc_fw_version *sof_client_get_fw_version(struct sof_client_dev *cdev);

/* module refcount management of SOF core */
int sof_client_core_module_get(struct sof_client_dev *cdev);
void sof_client_core_module_put(struct sof_client_dev *cdev);

/* IPC notification */
typedef void (*sof_client_event_callback)(struct sof_client_dev *cdev, void *msg_buf);

int sof_client_register_ipc_rx_handler(struct sof_client_dev *cdev,
				       u32 ipc_msg_type,
				       sof_client_event_callback callback);
void sof_client_unregister_ipc_rx_handler(struct sof_client_dev *cdev,
					  u32 ipc_msg_type);

/* DSP state notification and query */
typedef void (*sof_client_fw_state_callback)(struct sof_client_dev *cdev,
					     enum sof_fw_state state);

int sof_client_register_fw_state_handler(struct sof_client_dev *cdev,
					 sof_client_fw_state_callback callback);
void sof_client_unregister_fw_state_handler(struct sof_client_dev *cdev);
enum sof_fw_state sof_client_get_fw_state(struct sof_client_dev *cdev);

#endif /* __SOC_SOF_CLIENT_H */
