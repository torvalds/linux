/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The On Chip Memory (OCMEM) allocator allows various clients to allocate
 * memory from OCMEM based on performance, latency and power requirements.
 * This is typically used by the GPU, camera/video, and audio components on
 * some Snapdragon SoCs.
 *
 * Copyright (C) 2019 Brian Masney <masneyb@onstation.org>
 * Copyright (C) 2015 Red Hat. Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/device.h>
#include <linux/err.h>

#ifndef __OCMEM_H__
#define __OCMEM_H__

enum ocmem_client {
	/* GMEM clients */
	OCMEM_GRAPHICS = 0x0,
	/*
	 * TODO add more once ocmem_allocate() is clever enough to
	 * deal with multiple clients.
	 */
	OCMEM_CLIENT_MAX,
};

struct ocmem;

struct ocmem_buf {
	unsigned long offset;
	unsigned long addr;
	unsigned long len;
};

#if IS_ENABLED(CONFIG_QCOM_OCMEM)

struct ocmem *of_get_ocmem(struct device *dev);
struct ocmem_buf *ocmem_allocate(struct ocmem *ocmem, enum ocmem_client client,
				 unsigned long size);
void ocmem_free(struct ocmem *ocmem, enum ocmem_client client,
		struct ocmem_buf *buf);

#else /* IS_ENABLED(CONFIG_QCOM_OCMEM) */

static inline struct ocmem *of_get_ocmem(struct device *dev)
{
	return ERR_PTR(-ENODEV);
}

static inline struct ocmem_buf *ocmem_allocate(struct ocmem *ocmem,
					       enum ocmem_client client,
					       unsigned long size)
{
	return ERR_PTR(-ENODEV);
}

static inline void ocmem_free(struct ocmem *ocmem, enum ocmem_client client,
			      struct ocmem_buf *buf)
{
}

#endif /* IS_ENABLED(CONFIG_QCOM_OCMEM) */

#endif /* __OCMEM_H__ */
