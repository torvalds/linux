/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#ifndef __ISP_H__
#define __ISP_H__

#include <linux/types.h>

struct device;

struct isp_platform_data {
	void *adev;
	u32 asic_type;
	resource_size_t base_rmmio_size;
};

int isp_user_buffer_alloc(struct device *dev, void *dmabuf,
			  void **buf_obj, u64 *buf_addr);

void isp_user_buffer_free(void *buf_obj);

int isp_kernel_buffer_alloc(struct device *dev, u64 size,
			    void **buf_obj, u64 *gpu_addr, void **cpu_addr);

void isp_kernel_buffer_free(void **buf_obj, u64 *gpu_addr, void **cpu_addr);

#endif
