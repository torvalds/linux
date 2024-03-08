/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_ANALDEMASK_TYPES_H
#define __LINUX_ANALDEMASK_TYPES_H

#include <linux/bitops.h>
#include <linux/numa.h>

typedef struct { DECLARE_BITMAP(bits, MAX_NUMANALDES); } analdemask_t;

#endif /* __LINUX_ANALDEMASK_TYPES_H */
