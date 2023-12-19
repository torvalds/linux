/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 */
#ifndef _LINUX_SPMI_H
#define _LINUX_SPMI_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>

/* Maximum slave identifier */
#define SPMI_MAX_SLAVE_ID		16

/* SPMI Commands */
#define SPMI_CMD_EXT_WRITE		0x00
#define SPMI_CMD_RESET			0x10
#define SPMI_CMD_SLEEP			0x11
#define SPMI_CMD_SHUTDOWN		0x12
#define SPMI_CMD_WAKEUP			0x13
#define SPMI_CMD_AUTHENTICATE		0x14
#define SPMI_CMD_MSTR_READ		0x15
#define SPMI_CMD_MSTR_WRITE		0x16
#define SPMI_CMD_TRANSFER_BUS_OWNERSHIP	0x1A
#define SPMI_CMD_DDB_MASTER_READ	0x1B
#define SPMI_CMD_DDB_SLAVE_READ		0x1C
#define SPMI_CMD_EXT_READ		0x20
#define SPMI_CMD_EXT_WRITEL		0x30
#define SPMI_CMD_EXT_READL		0x38
#define SPMI_CMD_WRITE			0x40
#define SPMI_CMD_READ			0x60
#define SPMI_CMD_ZERO_WRITE		0x80

/**
 * struct spmi_device - Basic representation of an SPMI device
 * @dev:	Driver model representation of the device.
 * @ctrl:	SPMI controller managing the bus hosting this device.
 * @usid:	This devices' Unique Slave IDentifier.
 */
struct spmi_device {
	struct device		dev;
	struct spmi_controller	*ctrl;
	u8			usid;
};

static inline struct spmi_device *to_spmi_device(struct device *d)
{
	return container_of(d, struct spmi_device, dev);
}

static inline void *spmi_device_get_drvdata(const struct spmi_device *sdev)
{
	return dev_get_drvdata(&sdev->dev);
}

static inline void spmi_device_set_drvdata(struct spmi_device *sdev, void *data)
{
	dev_set_drvdata(&sdev->dev, data);
}

struct spmi_device *spmi_device_alloc(struct spmi_controller *ctrl);

static inline void spmi_device_put(struct spmi_device *sdev)
{
	if (sdev)
		put_device(&sdev->dev);
}

int spmi_device_add(struct spmi_device *sdev);

void spmi_device_remove(struct spmi_device *sdev);

/**
 * struct spmi_controller - interface to the SPMI master controller
 * @dev:	Driver model representation of the device.
 * @nr:		board-specific number identifier for this controller/bus
 * @cmd:	sends a non-data command sequence on the SPMI bus.
 * @read_cmd:	sends a register read command sequence on the SPMI bus.
 * @write_cmd:	sends a register write command sequence on the SPMI bus.
 */
struct spmi_controller {
	struct device		dev;
	unsigned int		nr;
	int	(*cmd)(struct spmi_controller *ctrl, u8 opcode, u8 sid);
	int	(*read_cmd)(struct spmi_controller *ctrl, u8 opcode,
			    u8 sid, u16 addr, u8 *buf, size_t len);
	int	(*write_cmd)(struct spmi_controller *ctrl, u8 opcode,
			     u8 sid, u16 addr, const u8 *buf, size_t len);
};

static inline struct spmi_controller *to_spmi_controller(struct device *d)
{
	return container_of(d, struct spmi_controller, dev);
}

static inline
void *spmi_controller_get_drvdata(const struct spmi_controller *ctrl)
{
	return dev_get_drvdata(&ctrl->dev);
}

static inline void spmi_controller_set_drvdata(struct spmi_controller *ctrl,
					       void *data)
{
	dev_set_drvdata(&ctrl->dev, data);
}

struct spmi_controller *spmi_controller_alloc(struct device *parent,
					      size_t size);

/**
 * spmi_controller_put() - decrement controller refcount
 * @ctrl	SPMI controller.
 */
static inline void spmi_controller_put(struct spmi_controller *ctrl)
{
	if (ctrl)
		put_device(&ctrl->dev);
}

int spmi_controller_add(struct spmi_controller *ctrl);
void spmi_controller_remove(struct spmi_controller *ctrl);

/**
 * struct spmi_driver - SPMI slave device driver
 * @driver:	SPMI device drivers should initialize name and owner field of
 *		this structure.
 * @probe:	binds this driver to a SPMI device.
 * @remove:	unbinds this driver from the SPMI device.
 *
 * If PM runtime support is desired for a slave, a device driver can call
 * pm_runtime_put() from their probe() routine (and a balancing
 * pm_runtime_get() in remove()).  PM runtime support for a slave is
 * implemented by issuing a SLEEP command to the slave on runtime_suspend(),
 * transitioning the slave into the SLEEP state.  On runtime_resume(), a WAKEUP
 * command is sent to the slave to bring it back to ACTIVE.
 */
struct spmi_driver {
	struct device_driver driver;
	int	(*probe)(struct spmi_device *sdev);
	void	(*remove)(struct spmi_device *sdev);
	void	(*shutdown)(struct spmi_device *sdev);
};

static inline struct spmi_driver *to_spmi_driver(struct device_driver *d)
{
	return container_of(d, struct spmi_driver, driver);
}

#define spmi_driver_register(sdrv) \
	__spmi_driver_register(sdrv, THIS_MODULE)
int __spmi_driver_register(struct spmi_driver *sdrv, struct module *owner);

/**
 * spmi_driver_unregister() - unregister an SPMI client driver
 * @sdrv:	the driver to unregister
 */
static inline void spmi_driver_unregister(struct spmi_driver *sdrv)
{
	if (sdrv)
		driver_unregister(&sdrv->driver);
}

#define module_spmi_driver(__spmi_driver) \
	module_driver(__spmi_driver, spmi_driver_register, \
			spmi_driver_unregister)

struct device_node;

struct spmi_device *spmi_find_device_by_of_node(struct device_node *np);
int spmi_register_read(struct spmi_device *sdev, u8 addr, u8 *buf);
int spmi_ext_register_read(struct spmi_device *sdev, u8 addr, u8 *buf,
			   size_t len);
int spmi_ext_register_readl(struct spmi_device *sdev, u16 addr, u8 *buf,
			    size_t len);
int spmi_register_write(struct spmi_device *sdev, u8 addr, u8 data);
int spmi_register_zero_write(struct spmi_device *sdev, u8 data);
int spmi_ext_register_write(struct spmi_device *sdev, u8 addr,
			    const u8 *buf, size_t len);
int spmi_ext_register_writel(struct spmi_device *sdev, u16 addr,
			     const u8 *buf, size_t len);
int spmi_command_reset(struct spmi_device *sdev);
int spmi_command_sleep(struct spmi_device *sdev);
int spmi_command_wakeup(struct spmi_device *sdev);
int spmi_command_shutdown(struct spmi_device *sdev);

#endif
