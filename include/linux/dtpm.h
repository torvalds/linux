/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Linaro Ltd
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 */
#ifndef ___DTPM_H__
#define ___DTPM_H__

#include <linux/powercap.h>

#define MAX_DTPM_DESCR 8
#define MAX_DTPM_CONSTRAINTS 1

struct dtpm {
	struct powercap_zone zone;
	struct dtpm *parent;
	struct list_head sibling;
	struct list_head children;
	struct dtpm_ops *ops;
	unsigned long flags;
	u64 power_limit;
	u64 power_max;
	u64 power_min;
	int weight;
};

struct dtpm_ops {
	u64 (*set_power_uw)(struct dtpm *, u64);
	u64 (*get_power_uw)(struct dtpm *);
	int (*update_power_uw)(struct dtpm *);
	void (*release)(struct dtpm *);
};

struct device_node;

struct dtpm_subsys_ops {
	const char *name;
	int (*init)(void);
	void (*exit)(void);
	int (*setup)(struct dtpm *, struct device_node *);
};

enum DTPM_NODE_TYPE {
	DTPM_NODE_VIRTUAL = 0,
	DTPM_NODE_DT,
};

struct dtpm_node {
	enum DTPM_NODE_TYPE type;
	const char *name;
	struct dtpm_node *parent;
};

static inline struct dtpm *to_dtpm(struct powercap_zone *zone)
{
	return container_of(zone, struct dtpm, zone);
}

int dtpm_update_power(struct dtpm *dtpm);

int dtpm_release_zone(struct powercap_zone *pcz);

void dtpm_init(struct dtpm *dtpm, struct dtpm_ops *ops);

void dtpm_unregister(struct dtpm *dtpm);

int dtpm_register(const char *name, struct dtpm *dtpm, struct dtpm *parent);

int dtpm_create_hierarchy(struct of_device_id *dtpm_match_table);

void dtpm_destroy_hierarchy(void);
#endif
