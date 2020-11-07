/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Linaro Ltd
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 */
#ifndef __IDLE_INJECT_H__
#define __IDLE_INJECT_H__

/* private idle injection device structure */
struct idle_inject_device;

struct idle_inject_device *idle_inject_register(struct cpumask *cpumask);

void idle_inject_unregister(struct idle_inject_device *ii_dev);

int idle_inject_start(struct idle_inject_device *ii_dev);

void idle_inject_stop(struct idle_inject_device *ii_dev);

void idle_inject_set_duration(struct idle_inject_device *ii_dev,
				 unsigned int run_duration_us,
				 unsigned int idle_duration_us);

void idle_inject_get_duration(struct idle_inject_device *ii_dev,
				 unsigned int *run_duration_us,
				 unsigned int *idle_duration_us);

void idle_inject_set_latency(struct idle_inject_device *ii_dev,
			     unsigned int latency_us);

#endif /* __IDLE_INJECT_H__ */
