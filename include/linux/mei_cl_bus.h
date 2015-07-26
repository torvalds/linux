#ifndef _LINUX_MEI_CL_BUS_H
#define _LINUX_MEI_CL_BUS_H

#include <linux/device.h>
#include <linux/uuid.h>
#include <linux/mod_devicetable.h>

struct mei_cl_device;
struct mei_device;

typedef void (*mei_cl_event_cb_t)(struct mei_cl_device *device,
			       u32 events, void *context);

/**
 * struct mei_cl_device - MEI device handle
 * An mei_cl_device pointer is returned from mei_add_device()
 * and links MEI bus clients to their actual ME host client pointer.
 * Drivers for MEI devices will get an mei_cl_device pointer
 * when being probed and shall use it for doing ME bus I/O.
 *
 * @bus_list: device on the bus list
 * @bus: parent mei device
 * @dev: linux driver model device pointer
 * @me_cl: me client
 * @cl: mei client
 * @name: device name
 * @event_work: async work to execute event callback
 * @event_cb: Drivers register this callback to get asynchronous ME
 *	events (e.g. Rx buffer pending) notifications.
 * @event_context: event callback run context
 * @events_mask: Events bit mask requested by driver.
 * @events: Events bitmask sent to the driver.
 *
 * @do_match: wheather device can be matched with a driver
 * @is_added: device is already scanned
 * @priv_data: client private data
 */
struct mei_cl_device {
	struct list_head bus_list;
	struct mei_device *bus;
	struct device dev;

	struct mei_me_client *me_cl;
	struct mei_cl *cl;
	char name[MEI_CL_NAME_SIZE];

	struct work_struct event_work;
	mei_cl_event_cb_t event_cb;
	void *event_context;
	unsigned long events_mask;
	unsigned long events;

	unsigned int do_match:1;
	unsigned int is_added:1;

	void *priv_data;
};

struct mei_cl_driver {
	struct device_driver driver;
	const char *name;

	const struct mei_cl_device_id *id_table;

	int (*probe)(struct mei_cl_device *dev,
		     const struct mei_cl_device_id *id);
	int (*remove)(struct mei_cl_device *dev);
};

int __mei_cl_driver_register(struct mei_cl_driver *driver,
				struct module *owner);
#define mei_cl_driver_register(driver)             \
	__mei_cl_driver_register(driver, THIS_MODULE)

void mei_cl_driver_unregister(struct mei_cl_driver *driver);

ssize_t mei_cl_send(struct mei_cl_device *device, u8 *buf, size_t length);
ssize_t  mei_cl_recv(struct mei_cl_device *device, u8 *buf, size_t length);

int mei_cl_register_event_cb(struct mei_cl_device *device,
			  unsigned long event_mask,
			  mei_cl_event_cb_t read_cb, void *context);

#define MEI_CL_EVENT_RX 0
#define MEI_CL_EVENT_TX 1
#define MEI_CL_EVENT_NOTIF 2

void *mei_cl_get_drvdata(const struct mei_cl_device *device);
void mei_cl_set_drvdata(struct mei_cl_device *device, void *data);

int mei_cl_enable_device(struct mei_cl_device *device);
int mei_cl_disable_device(struct mei_cl_device *device);

#endif /* _LINUX_MEI_CL_BUS_H */
