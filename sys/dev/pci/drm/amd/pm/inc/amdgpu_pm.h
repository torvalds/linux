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

#ifndef __AMDGPU_PM_H__
#define __AMDGPU_PM_H__

struct cg_flag_name {
	u64 flag;
	const char *name;
};

enum amdgpu_device_attr_flags {
	ATTR_FLAG_BASIC = (1 << 0),
	ATTR_FLAG_ONEVF = (1 << 16),
};

#define ATTR_FLAG_TYPE_MASK	(0x0000ffff)
#define ATTR_FLAG_MODE_MASK	(0xffff0000)
#define ATTR_FLAG_MASK_ALL	(0xffffffff)

enum amdgpu_device_attr_states {
	ATTR_STATE_UNSUPPORTED = 0,
	ATTR_STATE_SUPPORTED,
};

enum amdgpu_device_attr_id {
	device_attr_id__unknown = -1,
	device_attr_id__power_dpm_state = 0,
	device_attr_id__power_dpm_force_performance_level,
	device_attr_id__pp_num_states,
	device_attr_id__pp_cur_state,
	device_attr_id__pp_force_state,
	device_attr_id__pp_table,
	device_attr_id__pp_dpm_sclk,
	device_attr_id__pp_dpm_mclk,
	device_attr_id__pp_dpm_socclk,
	device_attr_id__pp_dpm_fclk,
	device_attr_id__pp_dpm_vclk,
	device_attr_id__pp_dpm_vclk1,
	device_attr_id__pp_dpm_dclk,
	device_attr_id__pp_dpm_dclk1,
	device_attr_id__pp_dpm_dcefclk,
	device_attr_id__pp_dpm_pcie,
	device_attr_id__pp_sclk_od,
	device_attr_id__pp_mclk_od,
	device_attr_id__pp_power_profile_mode,
	device_attr_id__pp_od_clk_voltage,
	device_attr_id__gpu_busy_percent,
	device_attr_id__mem_busy_percent,
	device_attr_id__vcn_busy_percent,
	device_attr_id__pcie_bw,
	device_attr_id__pp_features,
	device_attr_id__unique_id,
	device_attr_id__thermal_throttling_logging,
	device_attr_id__apu_thermal_cap,
	device_attr_id__gpu_metrics,
	device_attr_id__smartshift_apu_power,
	device_attr_id__smartshift_dgpu_power,
	device_attr_id__smartshift_bias,
	device_attr_id__pm_metrics,
	device_attr_id__count,
};

struct amdgpu_device_attr {
	struct device_attribute dev_attr;
	enum amdgpu_device_attr_id attr_id;
	enum amdgpu_device_attr_flags flags;
	int (*attr_update)(struct amdgpu_device *adev, struct amdgpu_device_attr *attr,
			   uint32_t mask, enum amdgpu_device_attr_states *states);

};

struct amdgpu_device_attr_entry {
	struct list_head entry;
	struct amdgpu_device_attr *attr;
};

#define to_amdgpu_device_attr(_dev_attr) \
	container_of(_dev_attr, struct amdgpu_device_attr, dev_attr)

#define __AMDGPU_DEVICE_ATTR(_name, _mode, _show, _store, _flags, ...)	\
	{ .dev_attr = __ATTR(_name, _mode, _show, _store),		\
	  .attr_id = device_attr_id__##_name,				\
	  .flags = _flags,						\
	  ##__VA_ARGS__, }

#define AMDGPU_DEVICE_ATTR(_name, _mode, _flags, ...)			\
	__AMDGPU_DEVICE_ATTR(_name, _mode,				\
			     amdgpu_get_##_name, amdgpu_set_##_name,	\
			     _flags, ##__VA_ARGS__)

#define AMDGPU_DEVICE_ATTR_RW(_name, _flags, ...)			\
	AMDGPU_DEVICE_ATTR(_name, S_IRUGO | S_IWUSR,			\
			   _flags, ##__VA_ARGS__)

#define AMDGPU_DEVICE_ATTR_RO(_name, _flags, ...)			\
	__AMDGPU_DEVICE_ATTR(_name, S_IRUGO,				\
			     amdgpu_get_##_name, NULL,			\
			     _flags, ##__VA_ARGS__)

int amdgpu_pm_sysfs_init(struct amdgpu_device *adev);
int amdgpu_pm_virt_sysfs_init(struct amdgpu_device *adev);
void amdgpu_pm_sysfs_fini(struct amdgpu_device *adev);
void amdgpu_pm_virt_sysfs_fini(struct amdgpu_device *adev);

void amdgpu_debugfs_pm_init(struct amdgpu_device *adev);

#endif
