#ifndef __QCOM_SMD_H__
#define __QCOM_SMD_H__

#include <linux/device.h>
#include <linux/mod_devicetable.h>

struct qcom_smd;
struct qcom_smd_channel;
struct qcom_smd_lookup;

/**
 * struct qcom_smd_id - struct used for matching a smd device
 * @name:	name of the channel
 */
struct qcom_smd_id {
	char name[20];
};

/**
 * struct qcom_ipc_device - ipc device struct
 * @dev:	the device struct
 * @channel:	handle to the smd channel for this device
 */
struct qcom_ipc_device {
	struct device dev;
	struct qcom_smd_channel *channel;
};

/**
 * struct qcom_ipc_driver - ipc driver struct
 * @driver:	underlying device driver
 * @smd_match_table: static channel match table
 * @probe:	invoked when the ipc channel is found
 * @remove:	invoked when the ipc channel is closed
 * @callback:	invoked when an inbound message is received on the channel,
 *		should return 0 on success or -EBUSY if the data cannot be
 *		consumed at this time
 */
struct qcom_ipc_driver {
	struct device_driver driver;
	const struct qcom_smd_id *smd_match_table;

	int (*probe)(struct qcom_ipc_device *dev);
	void (*remove)(struct qcom_ipc_device *dev);
	int (*callback)(void *, const void *, size_t);
};

int qcom_ipc_driver_register(struct qcom_ipc_driver *drv);
void qcom_ipc_driver_unregister(struct qcom_ipc_driver *drv);
void qcom_ipc_bus_register(struct bus_type *bus);

#define module_qcom_ipc_driver(__ipc_driver) \
	module_driver(__ipc_driver, qcom_ipc_driver_register, \
		      qcom_ipc_driver_unregister)

int qcom_smd_send(struct qcom_smd_channel *channel, const void *data, int len);

#endif
