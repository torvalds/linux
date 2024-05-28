/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_NODEMASK_TYPES_H
#define __LINUX_NODEMASK_TYPES_H

#include <linux/bitops.h>
#include <linux/numa.h>

typedef struct { DECLARE_BITMAP(bits, MAX_NUMNODES); } nodemask_t;

#endif /* __LINUX_NODEMASK_TYPES_H */
