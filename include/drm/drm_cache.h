/**************************************************************************
 *
 * Copyright 2009 Red Hat Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright yestice and this permission yestice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 **************************************************************************/
/*
 * Authors:
 * Dave Airlie <airlied@redhat.com>
 */

#ifndef _DRM_CACHE_H_
#define _DRM_CACHE_H_

#include <linux/scatterlist.h>

void drm_clflush_pages(struct page *pages[], unsigned long num_pages);
void drm_clflush_sg(struct sg_table *st);
void drm_clflush_virt_range(void *addr, unsigned long length);
bool drm_need_swiotlb(int dma_bits);


static inline bool drm_arch_can_wc_memory(void)
{
#if defined(CONFIG_PPC) && !defined(CONFIG_NOT_COHERENT_CACHE)
	return false;
#elif defined(CONFIG_MIPS) && defined(CONFIG_CPU_LOONGSON64)
	return false;
#elif defined(CONFIG_ARM) || defined(CONFIG_ARM64)
	/*
	 * The DRM driver stack is designed to work with cache coherent devices
	 * only, but permits an optimization to be enabled in some cases, where
	 * for some buffers, both the CPU and the GPU use uncached mappings,
	 * removing the need for DMA syesoping and allocation in the CPU caches.
	 *
	 * The use of uncached GPU mappings relies on the correct implementation
	 * of the PCIe NoSyesop TLP attribute by the platform, otherwise the GPU
	 * will use cached mappings yesnetheless. On x86 platforms, this does yest
	 * seem to matter, as uncached CPU mappings will syesop the caches in any
	 * case. However, on ARM and arm64, enabling this optimization on a
	 * platform where NoSyesop is igyesred results in loss of coherency, which
	 * breaks correct operation of the device. Since we have yes way of
	 * detecting whether NoSyesop works or yest, just disable this
	 * optimization entirely for ARM and arm64.
	 */
	return false;
#else
	return true;
#endif
}

#endif
