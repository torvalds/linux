/*
 * Copyright 2013 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König <christian.koenig@amd.com>
 */

#include <linux/firmware.h>

#include "radeon.h"
#include "radeon_asic.h"
#include "cikd.h"

/**
 * uvd_v4_2_resume - memory controller programming
 *
 * @rdev: radeon_device pointer
 *
 * Let the UVD memory controller know it's offsets
 */
int uvd_v4_2_resume(struct radeon_device *rdev)
{
	uint64_t addr;
	uint32_t size;

	/* program the VCPU memory controller bits 0-27 */

	/* skip over the header of the new firmware format */
	if (rdev->uvd.fw_header_present)
		addr = (rdev->uvd.gpu_addr + 0x200) >> 3;
	else
		addr = rdev->uvd.gpu_addr >> 3;

	size = RADEON_GPU_PAGE_ALIGN(rdev->uvd_fw->size + 4) >> 3;
	WREG32(UVD_VCPU_CACHE_OFFSET0, addr);
	WREG32(UVD_VCPU_CACHE_SIZE0, size);

	addr += size;
	size = RADEON_UVD_HEAP_SIZE >> 3;
	WREG32(UVD_VCPU_CACHE_OFFSET1, addr);
	WREG32(UVD_VCPU_CACHE_SIZE1, size);

	addr += size;
	size = (RADEON_UVD_STACK_SIZE +
	       (RADEON_UVD_SESSION_SIZE * rdev->uvd.max_handles)) >> 3;
	WREG32(UVD_VCPU_CACHE_OFFSET2, addr);
	WREG32(UVD_VCPU_CACHE_SIZE2, size);

	/* bits 28-31 */
	addr = (rdev->uvd.gpu_addr >> 28) & 0xF;
	WREG32(UVD_LMI_ADDR_EXT, (addr << 12) | (addr << 0));

	/* bits 32-39 */
	addr = (rdev->uvd.gpu_addr >> 32) & 0xFF;
	WREG32(UVD_LMI_EXT40_ADDR, addr | (0x9 << 16) | (0x1 << 31));

	if (rdev->uvd.fw_header_present)
		WREG32(UVD_GP_SCRATCH4, rdev->uvd.max_handles);

	return 0;
}
