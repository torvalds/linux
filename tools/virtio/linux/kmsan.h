/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KMSAN_H
#define _LINUX_KMSAN_H

#include <linux/gfp.h>

inline void kmsan_handle_dma(struct page *page, size_t offset, size_t size,
			     enum dma_data_direction dir)
{
}

#endif /* _LINUX_KMSAN_H */
