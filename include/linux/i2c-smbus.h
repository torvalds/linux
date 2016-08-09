/*
 * i2c-smbus.h - SMBus extensions to the I2C protocol
 *
 * Copyright (C) 2010 Jean Delvare <jdelvare@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA.
 */

#ifndef _LINUX_I2C_SMBUS_H
#define _LINUX_I2C_SMBUS_H

#include <linux/i2c.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>


/**
 * i2c_smbus_alert_setup - platform data for the smbus_alert i2c client
 * @alert_edge_triggered: whether the alert interrupt is edge (1) or level (0)
 *		triggered
 * @irq: IRQ number, if the smbus_alert driver should take care of interrupt
 *		handling
 *
 * If irq is not specified, the smbus_alert driver doesn't take care of
 * interrupt handling. In that case it is up to the I2C bus driver to either
 * handle the interrupts or to poll for alerts.
 *
 * If irq is specified then it it crucial that alert_edge_triggered is
 * properly set.
 */
struct i2c_smbus_alert_setup {
	unsigned int		alert_edge_triggered:1;
	int			irq;
};

struct i2c_client *i2c_setup_smbus_alert(struct i2c_adapter *adapter,
					 struct i2c_smbus_alert_setup *setup);
int i2c_handle_smbus_alert(struct i2c_client *ara);

/**
 * smbus_host_notify - internal structure used by the Host Notify mechanism.
 * @adapter: the I2C adapter associated with this struct
 * @work: worker used to schedule the IRQ in the slave device
 * @lock: spinlock to check if a notification is already pending
 * @pending: flag set when a notification is pending (any new notification will
 *		be rejected if pending is true)
 * @payload: the actual payload of the Host Notify event
 * @addr: the address of the slave device which raised the notification
 *
 * This struct needs to be allocated by i2c_setup_smbus_host_notify() and does
 * not need to be freed. Internally, i2c_setup_smbus_host_notify() uses a
 * managed resource to clean this up when the adapter get released.
 */
struct smbus_host_notify {
	struct i2c_adapter	*adapter;
	struct work_struct	work;
	spinlock_t		lock;
	bool			pending;
	u16			payload;
	u8			addr;
};

struct smbus_host_notify *i2c_setup_smbus_host_notify(struct i2c_adapter *adap);
int i2c_handle_smbus_host_notify(struct smbus_host_notify *host_notify,
				 unsigned short addr, unsigned int data);

#endif /* _LINUX_I2C_SMBUS_H */
