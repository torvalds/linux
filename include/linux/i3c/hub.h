/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2026 NXP
 * Generic hub definitions and helper interfaces.
 */
#ifndef _LINUX_I3C_HUB_H
#define _LINUX_I3C_HUB_H

#include <linux/i3c/master.h>
#include <linux/mutex.h>

/**
 * struct i3c_hub - Generic I3C hub context
 * @ops: Vendor callbacks for port connection control
 * @hub_dev: I3C device representing the hub on the parent bus
 * @lock: Serializes hub port routing/forwarding; its lockdep class is keyed
 *        per hub nesting depth in i3c_hub_init().
 */
struct i3c_hub {
	const struct i3c_hub_ops *ops;
	struct i3c_device *hub_dev;
	struct mutex lock; /* Serializes hub port routing. */
};

struct i3c_hub_controller {
	struct i3c_master_controller *parent;
	struct i3c_master_controller controller;
	struct i3c_hub *hub;
};

struct i3c_hub_ops {
	void (*enable_port)(struct i3c_master_controller *controller);
	void (*disable_port)(struct i3c_master_controller *controller);
};

/**
 * i3c_hub_enable_port() - Enable hub connection for a controller
 * @controller: Virtual controller representing a hub port
 *
 * Retrieves hub context from controller drvdata and invokes the vendor
 * callback to enable the associated port connection.
 */
static inline void i3c_hub_enable_port(struct i3c_master_controller *controller)
{
	struct i3c_hub_controller *hub_controller;
	struct i3c_hub *hub;

	hub_controller = dev_get_drvdata(&controller->dev);
	if (!hub_controller || !hub_controller->hub)
		return;

	hub = hub_controller->hub;

	if (hub && hub->ops && hub->ops->enable_port)
		hub->ops->enable_port(controller);
}

/**
 * i3c_hub_disable_port() - Disable hub connection for a controller
 * @controller: Virtual controller representing a hub port
 *
 * Retrieves hub context from controller drvdata and invokes the vendor
 * callback to disable the associated port connection.
 */
static inline void i3c_hub_disable_port(struct i3c_master_controller *controller)
{
	struct i3c_hub_controller *hub_controller;
	struct i3c_hub *hub;

	hub_controller = dev_get_drvdata(&controller->dev);
	if (!hub_controller || !hub_controller->hub)
		return;

	hub = hub_controller->hub;

	if (hub && hub->ops && hub->ops->disable_port)
		hub->ops->disable_port(controller);
}

/*
 * Controller operations used by the virtual controllers created for hub
 * target ports. Hub drivers pass this to i3c_master_register_fwnode().
 */
extern const struct i3c_master_controller_ops i3c_hub_master_ops;

void i3c_hub_init(struct i3c_hub *hub,
		  const struct i3c_hub_ops *ops,
		  struct i3c_device *hub_dev);

int i3c_hub_reserve_parent_addrslots_from_dt(struct i3c_hub_controller *hubc,
					     struct device_node *node);
#endif
