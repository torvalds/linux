/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_DMA_H
#define _TOOLS_DMA_H

#include <linux/types.h>

phys_addr_t dummy_physical_memory_low_limit(void);

#define ARCH_LOW_ADDRESS_LIMIT dummy_physical_memory_low_limit()

#endif
