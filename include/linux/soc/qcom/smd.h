#ifndef __QCOM_SMD_H__
#define __QCOM_SMD_H__

#include <linux/device.h>
#include <linux/mod_devicetable.h>

struct qcom_smd;
struct qcom_smd_channel;
struct qcom_smd_lookup;

/**
 * struct qcom_smd_device - smd device struct
 * @dev:	the device struct
 * @channel:	handle to the smd channel for this device
 */
struct qcom_smd_device {
	struct device dev;
	struct qcom_smd_channel *channel;
};

/**
 * struct qcom_smd_driver - smd driver struct
 * @driver:	underlying device driver
 * @probe:	invoked when the smd channel is found
 * @remove:	invoked when the smd channel is closed
 * @callback:	invoked when an inbound message is received on the channel,
 *		should return 0 on success or -EBUSY if the data cannot be
 *		consumed at this time
 */
struct qcom_smd_driver {
	struct device_driver driver;
	int (*probe)(struct qcom_smd_device *dev);
	void (*remove)(struct qcom_smd_device *dev);
	int (*callback)(struct qcom_smd_device *, const void *, size_t);
};

int qcom_smd_driver_register(struct qcom_smd_driver *drv);
void qcom_smd_driver_unregister(struct qcom_smd_driver *drv);

#define module_qcom_smd_driver(__smd_driver) \
	module_driver(__smd_driver, qcom_smd_driver_register, \
		      qcom_smd_driver_unregister)

int qcom_smd_send(struct qcom_smd_channel *channel, const void *data, int len);

#endif
