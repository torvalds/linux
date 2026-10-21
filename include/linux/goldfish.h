/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_GOLDFISH_H
#define __LINUX_GOLDFISH_H

#include <linux/io.h>

/* Helpers for Goldfish virtual platform */

#ifndef gf_ioread32
#define gf_ioread32 ioread32
#endif
#ifndef gf_iowrite32
#define gf_iowrite32 iowrite32
#endif

#endif /* __LINUX_GOLDFISH_H */
