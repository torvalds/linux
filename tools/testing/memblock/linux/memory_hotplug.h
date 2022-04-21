/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MEMORY_HOTPLUG_H
#define _LINUX_MEMORY_HOTPLUG_H

#include <linux/numa.h>
#include <linux/pfn.h>
#include <linux/cache.h>
#include <linux/types.h>

static inline bool movable_node_is_enabled(void)
{
#ifdef MOVABLE_NODE
	return true;
#else
	return false;
#endif
}

#endif
