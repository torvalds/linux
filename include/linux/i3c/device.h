/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Cadence Design Systems Inc.
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 */

#ifndef I3C_DEV_H
#define I3C_DEV_H

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/kconfig.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

/**
 * enum i3c_error_code - I3C error codes
 *
 * @I3C_ERROR_UNKNOWN: unknown error, usually means the error is not I3C
 *		       related
 * @I3C_ERROR_M0: M0 error
 * @I3C_ERROR_M1: M1 error
 * @I3C_ERROR_M2: M2 error
 *
 * These are the standard error codes as defined by the I3C specification.
 * When -EIO is returned by the i3c_device_do_priv_xfers() or
 * i3c_device_send_hdr_cmds() one can check the error code in
 * &struct_i3c_priv_xfer.err or &struct i3c_hdr_cmd.err to get a better idea of
 * what went wrong.
 *
 */
enum i3c_error_code {
	I3C_ERROR_UNKNOWN = 0,
	I3C_ERROR_M0 = 1,
	I3C_ERROR_M1,
	I3C_ERROR_M2,
};

/**
 * enum i3c_hdr_mode - HDR mode ids
 * @I3C_HDR_DDR: DDR mode
 * @I3C_HDR_TSP: TSP mode
 * @I3C_HDR_TSL: TSL mode
 */
enum i3c_hdr_mode {
	I3C_HDR_DDR,
	I3C_HDR_TSP,
	I3C_HDR_TSL,
};

/**
 * struct i3c_priv_xfer - I3C SDR private transfer
 * @rnw: encodes the transfer direction. true for a read, false for a write
 * @len: transfer length in bytes of the transfer
 * @actual_len: actual length in bytes are transferred by the controller
 * @data: input/output buffer
 * @data.in: input buffer. Must point to a DMA-able buffer
 * @data.out: output buffer. Must point to a DMA-able buffer
 * @err: I3C error code
 */
struct i3c_priv_xfer {
	u8 rnw;
	u16 len;
	u16 actual_len;
	union {
		void *in;
		const void *out;
	} data;
	enum i3c_error_code err;
};

/**
 * enum i3c_dcr - I3C DCR values
 * @I3C_DCR_GENERIC_DEVICE: generic I3C device
 */
enum i3c_dcr {
	I3C_DCR_GENERIC_DEVICE = 0,
};

#define I3C_PID_MANUF_ID(pid)		(((pid) & GENMASK_ULL(47, 33)) >> 33)
#define I3C_PID_RND_LOWER_32BITS(pid)	(!!((pid) & BIT_ULL(32)))
#define I3C_PID_RND_VAL(pid)		((pid) & GENMASK_ULL(31, 0))
#define I3C_PID_PART_ID(pid)		(((pid) & GENMASK_ULL(31, 16)) >> 16)
#define I3C_PID_INSTANCE_ID(pid)	(((pid) & GENMASK_ULL(15, 12)) >> 12)
#define I3C_PID_EXTRA_INFO(pid)		((pid) & GENMASK_ULL(11, 0))

#define I3C_BCR_DEVICE_ROLE(bcr)	((bcr) & GENMASK(7, 6))
#define I3C_BCR_I3C_SLAVE		(0 << 6)
#define I3C_BCR_I3C_MASTER		(1 << 6)
#define I3C_BCR_HDR_CAP			BIT(5)
#define I3C_BCR_BRIDGE			BIT(4)
#define I3C_BCR_OFFLINE_CAP		BIT(3)
#define I3C_BCR_IBI_PAYLOAD		BIT(2)
#define I3C_BCR_IBI_REQ_CAP		BIT(1)
#define I3C_BCR_MAX_DATA_SPEED_LIM	BIT(0)

/**
 * struct i3c_device_info - I3C device information
 * @pid: Provisioned ID
 * @bcr: Bus Characteristic Register
 * @dcr: Device Characteristic Register
 * @static_addr: static/I2C address
 * @dyn_addr: dynamic address
 * @hdr_cap: supported HDR modes
 * @max_read_ds: max read speed information
 * @max_write_ds: max write speed information
 * @max_ibi_len: max IBI payload length
 * @max_read_turnaround: max read turn-around time in micro-seconds
 * @max_read_len: max private SDR read length in bytes
 * @max_write_len: max private SDR write length in bytes
 *
 * These are all basic information that should be advertised by an I3C device.
 * Some of them are optional depending on the device type and device
 * capabilities.
 * For each I3C slave attached to a master with
 * i3c_master_add_i3c_dev_locked(), the core will send the relevant CCC command
 * to retrieve these data.
 */
struct i3c_device_info {
	u64 pid;
	u8 bcr;
	u8 dcr;
	u8 static_addr;
	u8 dyn_addr;
	u8 hdr_cap;
	u8 max_read_ds;
	u8 max_write_ds;
	u8 max_ibi_len;
	u32 max_read_turnaround;
	u16 max_read_len;
	u16 max_write_len;
};

/*
 * I3C device internals are kept hidden from I3C device users. It's just
 * simpler to refactor things when everything goes through getter/setters, and
 * I3C device drivers should not have to worry about internal representation
 * anyway.
 */
struct i3c_device;

/* These macros should be used to i3c_device_id entries. */
#define I3C_MATCH_MANUF_AND_PART (I3C_MATCH_MANUF | I3C_MATCH_PART)

#define I3C_DEVICE(_manufid, _partid, _drvdata)				\
	{								\
		.match_flags = I3C_MATCH_MANUF_AND_PART,		\
		.manuf_id = _manufid,					\
		.part_id = _partid,					\
		.data = _drvdata,					\
	}

#define I3C_DEVICE_EXTRA_INFO(_manufid, _partid, _info, _drvdata)	\
	{								\
		.match_flags = I3C_MATCH_MANUF_AND_PART |		\
			       I3C_MATCH_EXTRA_INFO,			\
		.manuf_id = _manufid,					\
		.part_id = _partid,					\
		.extra_info = _info,					\
		.data = _drvdata,					\
	}

#define I3C_CLASS(_dcr, _drvdata)					\
	{								\
		.match_flags = I3C_MATCH_DCR,				\
		.dcr = _dcr,						\
	}

/**
 * struct i3c_driver - I3C device driver
 * @driver: inherit from device_driver
 * @probe: I3C device probe method
 * @remove: I3C device remove method
 * @id_table: I3C device match table. Will be used by the framework to decide
 *	      which device to bind to this driver
 */
struct i3c_driver {
	struct device_driver driver;
	int (*probe)(struct i3c_device *dev);
	void (*remove)(struct i3c_device *dev);
	const struct i3c_device_id *id_table;
};

#define drv_to_i3cdrv(__drv)	container_of_const(__drv, struct i3c_driver, driver)

struct device *i3cdev_to_dev(struct i3c_device *i3cdev);

/**
 * dev_to_i3cdev() - Returns the I3C device containing @dev
 * @__dev: device object
 *
 * Return: a pointer to an I3C device object.
 */
#define dev_to_i3cdev(__dev)	container_of_const(__dev, struct i3c_device, dev)

const struct i3c_device_id *
i3c_device_match_id(struct i3c_device *i3cdev,
		    const struct i3c_device_id *id_table);

static inline void i3cdev_set_drvdata(struct i3c_device *i3cdev,
				      void *data)
{
	struct device *dev = i3cdev_to_dev(i3cdev);

	dev_set_drvdata(dev, data);
}

static inline void *i3cdev_get_drvdata(struct i3c_device *i3cdev)
{
	struct device *dev = i3cdev_to_dev(i3cdev);

	return dev_get_drvdata(dev);
}

int i3c_driver_register_with_owner(struct i3c_driver *drv,
				   struct module *owner);
void i3c_driver_unregister(struct i3c_driver *drv);

#define i3c_driver_register(__drv)		\
	i3c_driver_register_with_owner(__drv, THIS_MODULE)

/**
 * module_i3c_driver() - Register a module providing an I3C driver
 * @__drv: the I3C driver to register
 *
 * Provide generic init/exit functions that simply register/unregister an I3C
 * driver.
 * Should be used by any driver that does not require extra init/cleanup steps.
 */
#define module_i3c_driver(__drv)		\
	module_driver(__drv, i3c_driver_register, i3c_driver_unregister)

/**
 * i3c_i2c_driver_register() - Register an i2c and an i3c driver
 * @i3cdrv: the I3C driver to register
 * @i2cdrv: the I2C driver to register
 *
 * This function registers both @i2cdev and @i3cdev, and fails if one of these
 * registrations fails. This is mainly useful for devices that support both I2C
 * and I3C modes.
 * Note that when CONFIG_I3C is not enabled, this function only registers the
 * I2C driver.
 *
 * Return: 0 if both registrations succeeds, a negative error code otherwise.
 */
static inline int i3c_i2c_driver_register(struct i3c_driver *i3cdrv,
					  struct i2c_driver *i2cdrv)
{
	int ret;

	ret = i2c_add_driver(i2cdrv);
	if (ret || !IS_ENABLED(CONFIG_I3C))
		return ret;

	ret = i3c_driver_register(i3cdrv);
	if (ret)
		i2c_del_driver(i2cdrv);

	return ret;
}

/**
 * i3c_i2c_driver_unregister() - Unregister an i2c and an i3c driver
 * @i3cdrv: the I3C driver to register
 * @i2cdrv: the I2C driver to register
 *
 * This function unregisters both @i3cdrv and @i2cdrv.
 * Note that when CONFIG_I3C is not enabled, this function only unregisters the
 * @i2cdrv.
 */
static inline void i3c_i2c_driver_unregister(struct i3c_driver *i3cdrv,
					     struct i2c_driver *i2cdrv)
{
	if (IS_ENABLED(CONFIG_I3C))
		i3c_driver_unregister(i3cdrv);

	i2c_del_driver(i2cdrv);
}

/**
 * module_i3c_i2c_driver() - Register a module providing an I3C and an I2C
 *			     driver
 * @__i3cdrv: the I3C driver to register
 * @__i2cdrv: the I3C driver to register
 *
 * Provide generic init/exit functions that simply register/unregister an I3C
 * and an I2C driver.
 * This macro can be used even if CONFIG_I3C is disabled, in this case, only
 * the I2C driver will be registered.
 * Should be used by any driver that does not require extra init/cleanup steps.
 */
#define module_i3c_i2c_driver(__i3cdrv, __i2cdrv)	\
	module_driver(__i3cdrv,				\
		      i3c_i2c_driver_register,		\
		      i3c_i2c_driver_unregister,	\
		      __i2cdrv)

int i3c_device_do_priv_xfers(struct i3c_device *dev,
			     struct i3c_priv_xfer *xfers,
			     int nxfers);

int i3c_device_do_setdasa(struct i3c_device *dev);

void i3c_device_get_info(const struct i3c_device *dev, struct i3c_device_info *info);

struct i3c_ibi_payload {
	unsigned int len;
	const void *data;
};

/**
 * struct i3c_ibi_setup - IBI setup object
 * @max_payload_len: maximum length of the payload associated to an IBI. If one
 *		     IBI appears to have a payload that is bigger than this
 *		     number, the IBI will be rejected.
 * @num_slots: number of pre-allocated IBI slots. This should be chosen so that
 *	       the system never runs out of IBI slots, otherwise you'll lose
 *	       IBIs.
 * @handler: IBI handler, every time an IBI is received. This handler is called
 *	     in a workqueue context. It is allowed to sleep and send new
 *	     messages on the bus, though it's recommended to keep the
 *	     processing done there as fast as possible to avoid delaying
 *	     processing of other queued on the same workqueue.
 *
 * Temporary structure used to pass information to i3c_device_request_ibi().
 * This object can be allocated on the stack since i3c_device_request_ibi()
 * copies every bit of information and do not use it after
 * i3c_device_request_ibi() has returned.
 */
struct i3c_ibi_setup {
	unsigned int max_payload_len;
	unsigned int num_slots;
	void (*handler)(struct i3c_device *dev,
			const struct i3c_ibi_payload *payload);
};

int i3c_device_request_ibi(struct i3c_device *dev,
			   const struct i3c_ibi_setup *setup);
void i3c_device_free_ibi(struct i3c_device *dev);
int i3c_device_enable_ibi(struct i3c_device *dev);
int i3c_device_disable_ibi(struct i3c_device *dev);

#endif /* I3C_DEV_H */
