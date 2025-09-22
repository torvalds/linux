/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
 */
#include "amdgpu_ras.h"
#include "amdgpu.h"
#include "amdgpu_mca.h"

#define smnMCMP0_STATUST0 	0x03830408
#define smnMCMP1_STATUST0 	0x03b30408
#define smnMCMPIO_STATUST0 	0x0c930408


static void mca_v3_0_mp0_query_ras_error_count(struct amdgpu_device *adev,
					       void *ras_error_status)
{
	amdgpu_mca_query_ras_error_count(adev,
				         smnMCMP0_STATUST0,
				         ras_error_status);
}

static int mca_v3_0_ras_block_match(struct amdgpu_ras_block_object *block_obj,
				enum amdgpu_ras_block block, uint32_t sub_block_index)
{
	if (!block_obj)
		return -EINVAL;

	if ((block_obj->ras_comm.block == block) &&
		(block_obj->ras_comm.sub_block_index == sub_block_index)) {
		return 0;
	}

	return -EINVAL;
}

static const struct amdgpu_ras_block_hw_ops mca_v3_0_mp0_hw_ops = {
	.query_ras_error_count = mca_v3_0_mp0_query_ras_error_count,
	.query_ras_error_address = NULL,
};

struct amdgpu_mca_ras_block mca_v3_0_mp0_ras = {
	.ras_block = {
		.hw_ops = &mca_v3_0_mp0_hw_ops,
		.ras_block_match = mca_v3_0_ras_block_match,
	},
};

static void mca_v3_0_mp1_query_ras_error_count(struct amdgpu_device *adev,
					       void *ras_error_status)
{
	amdgpu_mca_query_ras_error_count(adev,
				         smnMCMP1_STATUST0,
				         ras_error_status);
}

static const struct amdgpu_ras_block_hw_ops mca_v3_0_mp1_hw_ops = {
	.query_ras_error_count = mca_v3_0_mp1_query_ras_error_count,
	.query_ras_error_address = NULL,
};

struct amdgpu_mca_ras_block mca_v3_0_mp1_ras = {
	.ras_block = {
		.hw_ops = &mca_v3_0_mp1_hw_ops,
		.ras_block_match = mca_v3_0_ras_block_match,
	},
};

static void mca_v3_0_mpio_query_ras_error_count(struct amdgpu_device *adev,
					       void *ras_error_status)
{
	amdgpu_mca_query_ras_error_count(adev,
				         smnMCMPIO_STATUST0,
				         ras_error_status);
}

static const struct amdgpu_ras_block_hw_ops mca_v3_0_mpio_hw_ops = {
	.query_ras_error_count = mca_v3_0_mpio_query_ras_error_count,
	.query_ras_error_address = NULL,
};

struct amdgpu_mca_ras_block mca_v3_0_mpio_ras = {
	.ras_block = {
		.hw_ops = &mca_v3_0_mpio_hw_ops,
		.ras_block_match = mca_v3_0_ras_block_match,
	},
};
