/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * Copyright (C) 2017 Hari Bathini, IBM Corporation
 */

#ifndef __PERF_NAMESPACES_H
#define __PERF_NAMESPACES_H

#include "../perf.h"
#include <linux/list.h>

struct namespaces_event;

struct namespaces {
	struct list_head list;
	u64 end_time;
	struct perf_ns_link_info link_info[];
};

struct namespaces *namespaces__new(struct namespaces_event *event);
void namespaces__free(struct namespaces *namespaces);

#endif  /* __PERF_NAMESPACES_H */
