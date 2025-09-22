/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_ATOMFIRMWARE_H__
#define __AMDGPU_ATOMFIRMWARE_H__

#define get_index_into_master_table(master_table, table_name) (offsetof(struct master_table, table_name) / sizeof(uint16_t))

uint32_t amdgpu_atomfirmware_query_firmware_capability(struct amdgpu_device *adev);
bool amdgpu_atomfirmware_gpu_virtualization_supported(struct amdgpu_device *adev);
void amdgpu_atomfirmware_scratch_regs_init(struct amdgpu_device *adev);
int amdgpu_atomfirmware_allocate_fb_scratch(struct amdgpu_device *adev);
int amdgpu_atomfirmware_get_vram_info(struct amdgpu_device *adev,
	int *vram_width, int *vram_type, int *vram_vendor);
int amdgpu_atomfirmware_get_clock_info(struct amdgpu_device *adev);
int amdgpu_atomfirmware_get_gfx_info(struct amdgpu_device *adev);
bool amdgpu_atomfirmware_mem_ecc_supported(struct amdgpu_device *adev);
bool amdgpu_atomfirmware_sram_ecc_supported(struct amdgpu_device *adev);
bool amdgpu_atomfirmware_ras_rom_addr(struct amdgpu_device *adev, uint8_t *i2c_address);
bool amdgpu_atomfirmware_mem_training_supported(struct amdgpu_device *adev);
bool amdgpu_atomfirmware_dynamic_boot_config_supported(struct amdgpu_device *adev);
int amdgpu_atomfirmware_get_fw_reserved_fb_size(struct amdgpu_device *adev);
int amdgpu_atomfirmware_asic_init(struct amdgpu_device *adev, bool fb_reset);

#endif
