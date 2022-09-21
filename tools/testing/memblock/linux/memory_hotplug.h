/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MEMORY_HOTPLUG_H
#define _LINUX_MEMORY_HOTPLUG_H

#include <linux/numa.h>
#include <linux/pfn.h>
#include <linux/cache.h>
#include <linux/types.h>

extern bool movable_node_enabled;

static inline bool movable_node_is_enabled(void)
{
	return movable_node_enabled;
}

#endif
