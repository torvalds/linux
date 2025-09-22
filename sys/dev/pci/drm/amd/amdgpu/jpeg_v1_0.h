/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef __JPEG_V1_0_H__
#define __JPEG_V1_0_H__

int jpeg_v1_0_early_init(void *handle);
int jpeg_v1_0_sw_init(void *handle);
void jpeg_v1_0_sw_fini(void *handle);
void jpeg_v1_0_start(struct amdgpu_device *adev, int mode);

#define JPEG_V1_REG_RANGE_START	0x8000
#define JPEG_V1_REG_RANGE_END	0x803f

#define JPEG_V1_LMI_JPEG_WRITE_64BIT_BAR_HIGH	0x8238
#define JPEG_V1_LMI_JPEG_WRITE_64BIT_BAR_LOW	0x8239
#define JPEG_V1_LMI_JPEG_READ_64BIT_BAR_HIGH	0x825a
#define JPEG_V1_LMI_JPEG_READ_64BIT_BAR_LOW	0x825b
#define JPEG_V1_REG_CTX_INDEX			0x8328
#define JPEG_V1_REG_CTX_DATA			0x8329
#define JPEG_V1_REG_SOFT_RESET			0x83a0

#endif /*__JPEG_V1_0_H__*/
