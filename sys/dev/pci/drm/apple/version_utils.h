// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright The Asahi Linux Contributors */

#ifndef __APPLE_VERSION_UTILS_H__
#define __APPLE_VERSION_UTILS_H__

#include <linux/kernel.h>
#include <linux/args.h>

#define DCP_FW_UNION(u) (u).DCP_FW
#define DCP_FW_SUFFIX CONCATENATE(_, DCP_FW)
#define DCP_FW_NAME(name) CONCATENATE(name, DCP_FW_SUFFIX)
#define DCP_FW_VERSION(x, y, z) ( ((x) << 16) | ((y) << 8) | (z) )

#endif /*__APPLE_VERSION_UTILS_H__*/
