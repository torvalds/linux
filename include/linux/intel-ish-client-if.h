/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel ISH client Interface definitions
 *
 * Copyright (c) 2019, Intel Corporation.
 */

#ifndef _INTEL_ISH_CLIENT_IF_H_
#define _INTEL_ISH_CLIENT_IF_H_

#include <linux/device.h>
#include <linux/mod_devicetable.h>

struct ishtp_cl_device;
struct ishtp_device;
struct ishtp_cl;
struct ishtp_fw_client;

typedef __printf(2, 3) void (*ishtp_print_log)(struct ishtp_device *dev,
					       const char *format, ...);

/* Client state */
enum cl_state {
	ISHTP_CL_INITIALIZING = 0,
	ISHTP_CL_CONNECTING,
	ISHTP_CL_CONNECTED,
	ISHTP_CL_DISCONNECTING,
	ISHTP_CL_DISCONNECTED
};

/**
 * struct ishtp_cl_device - ISHTP device handle
 * @driver:	driver instance on a bus
 * @name:	Name of the device for probe
 * @probe:	driver callback for device probe
 * @remove:	driver callback on device removal
 *
 * Client drivers defines to get probed/removed for ISHTP client device.
 */
struct ishtp_cl_driver {
	struct device_driver driver;
	const char *name;
	const struct ishtp_device_id *id;
	int (*probe)(struct ishtp_cl_device *dev);
	void (*remove)(struct ishtp_cl_device *dev);
	int (*reset)(struct ishtp_cl_device *dev);
	const struct dev_pm_ops *pm;
};

/**
 * struct ishtp_msg_data - ISHTP message data struct
 * @size:	Size of data in the *data
 * @data:	Pointer to data
 */
struct ishtp_msg_data {
	uint32_t size;
	unsigned char *data;
};

/*
 * struct ishtp_cl_rb - request block structure
 * @list:	Link to list members
 * @cl:		ISHTP client instance
 * @buffer:	message header
 * @buf_idx:	Index into buffer
 * @read_time:	 unused at this time
 */
struct ishtp_cl_rb {
	struct list_head list;
	struct ishtp_cl *cl;
	struct ishtp_msg_data buffer;
	unsigned long buf_idx;
	unsigned long read_time;
};

int ishtp_cl_driver_register(struct ishtp_cl_driver *driver,
			     struct module *owner);
void ishtp_cl_driver_unregister(struct ishtp_cl_driver *driver);
int ishtp_register_event_cb(struct ishtp_cl_device *device,
			    void (*read_cb)(struct ishtp_cl_device *));

/* Get the device * from ishtp device instance */
struct device *ishtp_device(struct ishtp_cl_device *cl_device);
/* wait for IPC resume */
bool ishtp_wait_resume(struct ishtp_device *dev);
/* Trace interface for clients */
ishtp_print_log ishtp_trace_callback(struct ishtp_cl_device *cl_device);
/* Get device pointer of PCI device for DMA acces */
struct device *ishtp_get_pci_device(struct ishtp_cl_device *cl_device);

struct ishtp_cl *ishtp_cl_allocate(struct ishtp_cl_device *cl_device);
void ishtp_cl_free(struct ishtp_cl *cl);
int ishtp_cl_link(struct ishtp_cl *cl);
void ishtp_cl_unlink(struct ishtp_cl *cl);
int ishtp_cl_disconnect(struct ishtp_cl *cl);
int ishtp_cl_connect(struct ishtp_cl *cl);
int ishtp_cl_send(struct ishtp_cl *cl, uint8_t *buf, size_t length);
int ishtp_cl_flush_queues(struct ishtp_cl *cl);
int ishtp_cl_io_rb_recycle(struct ishtp_cl_rb *rb);
bool ishtp_cl_tx_empty(struct ishtp_cl *cl);
struct ishtp_cl_rb *ishtp_cl_rx_get_rb(struct ishtp_cl *cl);
void *ishtp_get_client_data(struct ishtp_cl *cl);
void ishtp_set_client_data(struct ishtp_cl *cl, void *data);
struct ishtp_device *ishtp_get_ishtp_device(struct ishtp_cl *cl);
void ishtp_set_tx_ring_size(struct ishtp_cl *cl, int size);
void ishtp_set_rx_ring_size(struct ishtp_cl *cl, int size);
void ishtp_set_connection_state(struct ishtp_cl *cl, int state);
void ishtp_cl_set_fw_client_id(struct ishtp_cl *cl, int fw_client_id);

void ishtp_put_device(struct ishtp_cl_device *cl_dev);
void ishtp_get_device(struct ishtp_cl_device *cl_dev);
void ishtp_set_drvdata(struct ishtp_cl_device *cl_device, void *data);
void *ishtp_get_drvdata(struct ishtp_cl_device *cl_device);
struct ishtp_cl_device *ishtp_dev_to_cl_device(struct device *dev);
int ishtp_register_event_cb(struct ishtp_cl_device *device,
				void (*read_cb)(struct ishtp_cl_device *));
struct	ishtp_fw_client *ishtp_fw_cl_get_client(struct ishtp_device *dev,
						const guid_t *uuid);
int ishtp_get_fw_client_id(struct ishtp_fw_client *fw_client);
int ish_hw_reset(struct ishtp_device *dev);
#endif /* _INTEL_ISH_CLIENT_IF_H_ */
