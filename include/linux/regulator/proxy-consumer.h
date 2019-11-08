/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#ifndef _LINUX_REGULATOR_PROXY_CONSUMER_H_
#define _LINUX_REGULATOR_PROXY_CONSUMER_H_

#include <linux/device.h>
#include <linux/of.h>

#if IS_ENABLED(CONFIG_REGULATOR_PROXY_CONSUMER)

int regulator_proxy_consumer_register(struct device *dev,
				      struct device_node *node);
void regulator_proxy_consumer_unregister(struct device *dev);
int devm_regulator_proxy_consumer_register(struct device *dev,
				      struct device_node *node);
void devm_regulator_proxy_consumer_unregister(struct device *dev);
void regulator_proxy_consumer_sync_state(struct device *dev);

#else

static inline int regulator_proxy_consumer_register(struct device *dev,
						    struct device_node *node)
{ return 0; }
static inline void regulator_proxy_consumer_unregister(struct device *dev)
{ }
static inline int devm_regulator_proxy_consumer_register(struct device *dev,
						    struct device_node *node)
{ return 0; }
static inline void devm_regulator_proxy_consumer_unregister(struct device *dev)
{ }
void regulator_proxy_consumer_sync_state(struct device *dev)
{ }

#endif
#endif
