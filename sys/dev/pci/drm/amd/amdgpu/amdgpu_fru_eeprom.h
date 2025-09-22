/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_FRU_EEPROM_H__
#define __AMDGPU_FRU_EEPROM_H__

#define AMDGPU_PRODUCT_NAME_LEN 64

/* FRU product information */
struct amdgpu_fru_info {
	char				product_number[20];
	char				product_name[AMDGPU_PRODUCT_NAME_LEN];
	char				serial[20];
	char				manufacturer_name[32];
	char				fru_id[32];
};

int amdgpu_fru_get_product_info(struct amdgpu_device *adev);
int amdgpu_fru_sysfs_init(struct amdgpu_device *adev);
void amdgpu_fru_sysfs_fini(struct amdgpu_device *adev);

#endif  // __AMDGPU_FRU_EEPROM_H__
