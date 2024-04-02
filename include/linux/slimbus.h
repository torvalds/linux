// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2011-2017, The Linux Foundation
 */

#ifndef _LINUX_SLIMBUS_H
#define _LINUX_SLIMBUS_H
#include <linux/device.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/mod_devicetable.h>

extern const struct bus_type slimbus_bus;

/**
 * struct slim_eaddr - Enumeration address for a SLIMbus device
 * @instance: Instance value
 * @dev_index: Device index
 * @prod_code: Product code
 * @manf_id: Manufacturer Id for the device
 */
struct slim_eaddr {
	u8 instance;
	u8 dev_index;
	u16 prod_code;
	u16 manf_id;
} __packed;

/**
 * enum slim_device_status - slim device status
 * @SLIM_DEVICE_STATUS_DOWN: Slim device is absent or not reported yet.
 * @SLIM_DEVICE_STATUS_UP: Slim device is announced on the bus.
 * @SLIM_DEVICE_STATUS_RESERVED: Reserved for future use.
 */
enum slim_device_status {
	SLIM_DEVICE_STATUS_DOWN = 0,
	SLIM_DEVICE_STATUS_UP,
	SLIM_DEVICE_STATUS_RESERVED,
};

struct slim_controller;

/**
 * struct slim_device - Slim device handle.
 * @dev: Driver model representation of the device.
 * @e_addr: Enumeration address of this device.
 * @status: slim device status
 * @ctrl: slim controller instance.
 * @laddr: 1-byte Logical address of this device.
 * @is_laddr_valid: indicates if the laddr is valid or not
 * @stream_list: List of streams on this device
 * @stream_list_lock: lock to protect the stream list
 *
 * This is the client/device handle returned when a SLIMbus
 * device is registered with a controller.
 * Pointer to this structure is used by client-driver as a handle.
 */
struct slim_device {
	struct device		dev;
	struct slim_eaddr	e_addr;
	struct slim_controller	*ctrl;
	enum slim_device_status	status;
	u8			laddr;
	bool			is_laddr_valid;
	struct list_head	stream_list;
	spinlock_t stream_list_lock;
};

#define to_slim_device(d) container_of(d, struct slim_device, dev)

/**
 * struct slim_driver - SLIMbus 'generic device' (slave) device driver
 *				(similar to 'spi_device' on SPI)
 * @probe: Binds this driver to a SLIMbus device.
 * @remove: Unbinds this driver from the SLIMbus device.
 * @shutdown: Standard shutdown callback used during powerdown/halt.
 * @device_status: This callback is called when
 *	- The device reports present and gets a laddr assigned
 *	- The device reports absent, or the bus goes down.
 * @driver: SLIMbus device drivers should initialize name and owner field of
 *	    this structure
 * @id_table: List of SLIMbus devices supported by this driver
 */

struct slim_driver {
	int	(*probe)(struct slim_device *sl);
	void	(*remove)(struct slim_device *sl);
	void	(*shutdown)(struct slim_device *sl);
	int	(*device_status)(struct slim_device *sl,
				 enum slim_device_status s);
	struct device_driver		driver;
	const struct slim_device_id	*id_table;
};
#define to_slim_driver(d) container_of(d, struct slim_driver, driver)

/**
 * struct slim_val_inf - Slimbus value or information element
 * @start_offset: Specifies starting offset in information/value element map
 * @rbuf: buffer to read the values
 * @wbuf: buffer to write
 * @num_bytes: upto 16. This ensures that the message will fit the slicesize
 *		per SLIMbus spec
 * @comp: completion for asynchronous operations, valid only if TID is
 *	  required for transaction, like REQUEST operations.
 *	  Rest of the transactions are synchronous anyway.
 */
struct slim_val_inf {
	u16			start_offset;
	u8			num_bytes;
	u8			*rbuf;
	const u8		*wbuf;
	struct	completion	*comp;
};

#define SLIM_DEVICE_MAX_CHANNELS	256
/* A SLIMBus Device may have frmo 0 to 31 Ports (inclusive) */
#define SLIM_DEVICE_MAX_PORTS		32

/**
 * struct slim_stream_config - SLIMbus stream configuration
 *	Configuring a stream is done at hw_params or prepare call
 *	from audio drivers where they have all the required information
 *	regarding rate, number of channels and so on.
 *	There is a 1:1 mapping of channel and ports.
 *
 * @rate: data rate
 * @bps: bits per data sample
 * @ch_count: number of channels
 * @chs: pointer to list of channel numbers
 * @port_mask: port mask of ports to use for this stream
 * @direction: direction of the stream, SNDRV_PCM_STREAM_PLAYBACK
 *	or SNDRV_PCM_STREAM_CAPTURE.
 */
struct slim_stream_config {
	unsigned int rate;
	unsigned int bps;
	/* MAX 256 channels */
	unsigned int ch_count;
	unsigned int *chs;
	/* Max 32 ports per device */
	unsigned long port_mask;
	int direction;
};

/*
 * use a macro to avoid include chaining to get THIS_MODULE
 */
#define slim_driver_register(drv) \
	__slim_driver_register(drv, THIS_MODULE)
int __slim_driver_register(struct slim_driver *drv, struct module *owner);
void slim_driver_unregister(struct slim_driver *drv);

/**
 * module_slim_driver() - Helper macro for registering a SLIMbus driver
 * @__slim_driver: slimbus_driver struct
 *
 * Helper macro for SLIMbus drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_slim_driver(__slim_driver) \
	module_driver(__slim_driver, slim_driver_register, \
			slim_driver_unregister)

static inline void *slim_get_devicedata(const struct slim_device *dev)
{
	return dev_get_drvdata(&dev->dev);
}

static inline void slim_set_devicedata(struct slim_device *dev, void *data)
{
	dev_set_drvdata(&dev->dev, data);
}

struct slim_device *of_slim_get_device(struct slim_controller *ctrl,
				       struct device_node *np);
struct slim_device *slim_get_device(struct slim_controller *ctrl,
				    struct slim_eaddr *e_addr);
int slim_get_logical_addr(struct slim_device *sbdev);

/* Information Element management messages */
#define SLIM_MSG_MC_REQUEST_INFORMATION          0x20
#define SLIM_MSG_MC_REQUEST_CLEAR_INFORMATION    0x21
#define SLIM_MSG_MC_REPLY_INFORMATION            0x24
#define SLIM_MSG_MC_CLEAR_INFORMATION            0x28
#define SLIM_MSG_MC_REPORT_INFORMATION           0x29

/* Value Element management messages */
#define SLIM_MSG_MC_REQUEST_VALUE                0x60
#define SLIM_MSG_MC_REQUEST_CHANGE_VALUE         0x61
#define SLIM_MSG_MC_REPLY_VALUE                  0x64
#define SLIM_MSG_MC_CHANGE_VALUE                 0x68

int slim_xfer_msg(struct slim_device *sbdev, struct slim_val_inf *msg,
		  u8 mc);
int slim_readb(struct slim_device *sdev, u32 addr);
int slim_writeb(struct slim_device *sdev, u32 addr, u8 value);
int slim_read(struct slim_device *sdev, u32 addr, size_t count, u8 *val);
int slim_write(struct slim_device *sdev, u32 addr, size_t count, u8 *val);

/* SLIMbus Stream apis */
struct slim_stream_runtime;
struct slim_stream_runtime *slim_stream_allocate(struct slim_device *dev,
						 const char *sname);
int slim_stream_prepare(struct slim_stream_runtime *stream,
			struct slim_stream_config *c);
int slim_stream_enable(struct slim_stream_runtime *stream);
int slim_stream_disable(struct slim_stream_runtime *stream);
int slim_stream_unprepare(struct slim_stream_runtime *stream);
int slim_stream_free(struct slim_stream_runtime *stream);

#endif /* _LINUX_SLIMBUS_H */
