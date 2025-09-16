/* SPDX-License-Identifier: GPL-2.0 */
/*
 * I2C Address Translator
 *
 * Copyright (c) 2019,2022 Luca Ceresoli <luca@lucaceresoli.net>
 * Copyright (c) 2022,2023 Tomi Valkeinen <tomi.valkeinen@ideasonboard.com>
 *
 * Based on i2c-mux.h
 */

#ifndef _LINUX_I2C_ATR_H
#define _LINUX_I2C_ATR_H

#include <linux/i2c.h>
#include <linux/types.h>

struct device;
struct fwnode_handle;
struct i2c_atr;

/**
 * enum i2c_atr_flags - Flags for an I2C ATR driver
 *
 * @I2C_ATR_F_STATIC: ATR does not support dynamic mapping, use static mapping.
 *                    Mappings will only be added or removed as a result of
 *                    devices being added or removed from a child bus.
 *                    The ATR pool will have to be big enough to accomodate all
 *                    devices expected to be added to the child buses.
 * @I2C_ATR_F_PASSTHROUGH: Allow unmapped incoming addresses to pass through
 */
enum i2c_atr_flags {
	I2C_ATR_F_STATIC = BIT(0),
	I2C_ATR_F_PASSTHROUGH = BIT(1),
};

/**
 * struct i2c_atr_ops - Callbacks from ATR to the device driver.
 * @attach_addr: Notify the driver of a new device connected on a child
 *               bus, with the alias assigned to it. The driver must
 *               configure the hardware to use the alias.
 * @detach_addr: Notify the driver of a device getting disconnected. The
 *               driver must configure the hardware to stop using the
 *               alias.
 *
 * All these functions return 0 on success, a negative error code otherwise.
 */
struct i2c_atr_ops {
	int (*attach_addr)(struct i2c_atr *atr, u32 chan_id,
			   u16 addr, u16 alias);
	void (*detach_addr)(struct i2c_atr *atr, u32 chan_id,
			    u16 addr);
};

/**
 * struct i2c_atr_adap_desc - An ATR downstream bus descriptor
 * @chan_id:        Index of the new adapter (0 .. max_adapters-1).  This value is
 *                  passed to the callbacks in `struct i2c_atr_ops`.
 * @parent:         The device used as the parent of the new i2c adapter, or NULL
 *                  to use the i2c-atr device as the parent.
 * @bus_handle:     The fwnode handle that points to the adapter's i2c
 *                  peripherals, or NULL.
 * @num_aliases:    The number of aliases in this adapter's private alias pool. Set
 *                  to zero if this adapter uses the ATR's global alias pool.
 * @aliases:        An optional array of private aliases used by the adapter
 *                  instead of the ATR's global pool of aliases. Must contain
 *                  exactly num_aliases entries if num_aliases > 0, is ignored
 *                  otherwise.
 */
struct i2c_atr_adap_desc {
	u32 chan_id;
	struct device *parent;
	struct fwnode_handle *bus_handle;
	size_t num_aliases;
	u16 *aliases;
};

/**
 * i2c_atr_new() - Allocate and initialize an I2C ATR helper.
 * @parent:       The parent (upstream) adapter
 * @dev:          The device acting as an ATR
 * @ops:          Driver-specific callbacks
 * @max_adapters: Maximum number of child adapters
 * @flags:        Flags for ATR
 *
 * The new ATR helper is connected to the parent adapter but has no child
 * adapters. Call i2c_atr_add_adapter() to add some.
 *
 * Call i2c_atr_delete() to remove.
 *
 * Return: pointer to the new ATR helper object, or ERR_PTR
 */
struct i2c_atr *i2c_atr_new(struct i2c_adapter *parent, struct device *dev,
			    const struct i2c_atr_ops *ops, int max_adapters,
			    u32 flags);

/**
 * i2c_atr_delete - Delete an I2C ATR helper.
 * @atr: I2C ATR helper to be deleted.
 *
 * Precondition: all the adapters added with i2c_atr_add_adapter() must be
 * removed by calling i2c_atr_del_adapter().
 */
void i2c_atr_delete(struct i2c_atr *atr);

/**
 * i2c_atr_add_adapter - Create a child ("downstream") I2C bus.
 * @atr:        The I2C ATR
 * @desc:       An ATR adapter descriptor
 *
 * After calling this function a new i2c bus will appear. Adding and removing
 * devices on the downstream bus will result in calls to the
 * &i2c_atr_ops->attach_client and &i2c_atr_ops->detach_client callbacks for the
 * driver to assign an alias to the device.
 *
 * The adapter's fwnode is set to @bus_handle, or if @bus_handle is NULL the
 * function looks for a child node whose 'reg' property matches the chan_id
 * under the i2c-atr device's 'i2c-atr' node.
 *
 * Call i2c_atr_del_adapter() to remove the adapter.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int i2c_atr_add_adapter(struct i2c_atr *atr, struct i2c_atr_adap_desc *desc);

/**
 * i2c_atr_del_adapter - Remove a child ("downstream") I2C bus added by
 *                       i2c_atr_add_adapter(). If no I2C bus has been added
 *                       this function is a no-op.
 * @atr:     The I2C ATR
 * @chan_id: Index of the adapter to be removed (0 .. max_adapters-1)
 */
void i2c_atr_del_adapter(struct i2c_atr *atr, u32 chan_id);

/**
 * i2c_atr_set_driver_data - Set private driver data to the i2c-atr instance.
 * @atr:  The I2C ATR
 * @data: Pointer to the data to store
 */
void i2c_atr_set_driver_data(struct i2c_atr *atr, void *data);

/**
 * i2c_atr_get_driver_data - Get the stored drive data.
 * @atr:     The I2C ATR
 *
 * Return: Pointer to the stored data
 */
void *i2c_atr_get_driver_data(struct i2c_atr *atr);

#endif /* _LINUX_I2C_ATR_H */
