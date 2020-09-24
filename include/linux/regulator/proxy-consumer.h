/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 */

#ifndef _LINUX_REGULATOR_PROXY_CONSUMER_H_
#define _LINUX_REGULATOR_PROXY_CONSUMER_H_

#include <linux/device.h>
#include <linux/of.h>

struct proxy_consumer;

#ifdef CONFIG_REGULATOR_PROXY_CONSUMER

struct proxy_consumer *regulator_proxy_consumer_register(struct device *reg_dev,
			struct device_node *reg_node);

int regulator_proxy_consumer_unregister(struct proxy_consumer *consumer);

#else

static inline struct proxy_consumer *regulator_proxy_consumer_register(
			struct device *reg_dev, struct device_node *reg_node)
{ return NULL; }

static inline int regulator_proxy_consumer_unregister(
			struct proxy_consumer *consumer)
{ return 0; }

#endif

#endif
