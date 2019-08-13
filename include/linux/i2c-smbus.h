/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * i2c-smbus.h - SMBus extensions to the I2C protocol
 *
 * Copyright (C) 2010 Jean Delvare <jdelvare@suse.de>
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
	int			irq;
};

struct i2c_client *i2c_setup_smbus_alert(struct i2c_adapter *adapter,
					 struct i2c_smbus_alert_setup *setup);
int i2c_handle_smbus_alert(struct i2c_client *ara);

#if IS_ENABLED(CONFIG_I2C_SMBUS) && IS_ENABLED(CONFIG_OF)
int of_i2c_setup_smbus_alert(struct i2c_adapter *adap);
#else
static inline int of_i2c_setup_smbus_alert(struct i2c_adapter *adap)
{
	return 0;
}
#endif

#endif /* _LINUX_I2C_SMBUS_H */
