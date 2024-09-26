/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/types.h>
#ifndef __LLCC_QCOM__
#define __LLCC_QCOM__

#define LLCC_CPUSS       1
#define LLCC_VIDSC0      2
#define LLCC_VIDSC1      3
#define LLCC_ROTATOR     4
#define LLCC_VOICE       5
#define LLCC_AUDIO       6
#define LLCC_MDMHPGRW    7
#define LLCC_MDM         8
#define LLCC_MDMHW       9
#define LLCC_CMPT        10
#define LLCC_GPUHTW      11
#define LLCC_GPU         12
#define LLCC_MMUHWT      13
#define LLCC_CMPTDMA     15
#define LLCC_DISP        16
#define LLCC_VIDFW       17
#define LLCC_CAMFW       18
#define LLCC_MDMHPFX     20
#define LLCC_MDMPNG      21
#define LLCC_AUDHW       22
#define LLCC_NPU         23
#define LLCC_WLNHW       24
#define LLCC_PIMEM       25
#define LLCC_ECC         26
#define LLCC_CVP         28
#define LLCC_MDMVPE      29
#define LLCC_APTCM       30
#define LLCC_WRTCH       31
#define LLCC_CVPFW       32
#define LLCC_CPUSS1      33
#define LLCC_CAMEXP0     34
#define LLCC_CPUMTE      35
#define LLCC_CPUHWT      36
#define LLCC_MDMCLAD2    37
#define LLCC_CAMEXP1     38
#define LLCC_CMPTHCP     39
#define LLCC_LCPDARE     40
#define LLCC_AENPU       45
#define LLCC_ISLAND1     46
#define LLCC_ISLAND2     47
#define LLCC_ISLAND3     48
#define LLCC_ISLAND4     49
#define LLCC_CAMEXP2     50
#define LLCC_CAMEXP3     51
#define LLCC_CAMEXP4     52
#define LLCC_DISP_WB     53
#define LLCC_DISP_1      54
#define LLCC_SAIL        55
#define LLCC_VIDVSP      64
#define LLCC_DISLFT      65
#define LLCC_DISRGHT     66
#define LLCC_EVCSLFT     67
#define LLCC_EVCSRGHT    68
#define LLCC_EVA_3DR     69

/**
 * llcc_slice_desc - Cache slice descriptor
 * @slice_id: llcc slice id
 * @slice_size: Size allocated for the llcc slice
 */
struct llcc_slice_desc {
	u32 slice_id;
	size_t slice_size;
	atomic_t refcount;
};

/**
 * llcc_edac_reg_data - llcc edac registers data for each error type
 * @name: Name of the error
 * @synd_reg: Syndrome register address
 * @count_status_reg: Status register address to read the error count
 * @ways_status_reg: Status register address to read the error ways
 * @reg_cnt: Number of registers
 * @count_mask: Mask value to get the error count
 * @ways_mask: Mask value to get the error ways
 * @count_shift: Shift value to get the error count
 * @ways_shift: Shift value to get the error ways
 */
struct llcc_edac_reg_data {
	char *name;
	u64 synd_reg;
	u64 count_status_reg;
	u64 ways_status_reg;
	u32 reg_cnt;
	u32 count_mask;
	u32 ways_mask;
	u8  count_shift;
	u8  ways_shift;
};

struct llcc_edac_reg_offset {
	/* LLCC TRP registers */
	u32 trp_ecc_error_status0;
	u32 trp_ecc_error_status1;
	u32 trp_ecc_sb_err_syn0;
	u32 trp_ecc_db_err_syn0;
	u32 trp_ecc_error_cntr_clear;
	u32 trp_interrupt_0_status;
	u32 trp_interrupt_0_clear;
	u32 trp_interrupt_0_enable;

	/* LLCC Common registers */
	u32 cmn_status0;
	u32 cmn_interrupt_0_enable;
	u32 cmn_interrupt_2_enable;

	/* LLCC DRP registers */
	u32 drp_ecc_error_cfg;
	u32 drp_ecc_error_cntr_clear;
	u32 drp_interrupt_status;
	u32 drp_interrupt_clear;
	u32 drp_interrupt_enable;
	u32 drp_ecc_error_status0;
	u32 drp_ecc_error_status1;
	u32 drp_ecc_sb_err_syn0;
	u32 drp_ecc_db_err_syn0;
};

/**
 * llcc_drv_data - Data associated with the llcc driver
 * @regmap: regmap associated with the llcc device
 * @bcast_regmap: regmap associated with llcc broadcast offset
 * @cfg: pointer to the data structure for slice configuration
 * @edac_reg_offset: Offset of the LLCC EDAC registers
 * @lock: mutex associated with each slice
 * @cfg_index: index of config table if multiple configs present for a target
 * @cfg_size: size of the config data table
 * @max_slices: max slices as read from device tree
 * @num_banks: Number of llcc banks
 * @bitmap: Bit map to track the active slice ids
 * @offsets: Pointer to the bank offsets array
 * @ecc_irq: interrupt for llcc cache error detection and reporting
 * @llcc_ver: hardware version (20 for V2.0)
 * @desc: Array pointer of llcc_slice_desc
 */
struct llcc_drv_data {
	struct regmap *regmap;
	struct regmap *bcast_regmap;
	const struct llcc_slice_config *cfg;
	const struct llcc_edac_reg_offset *edac_reg_offset;
	struct mutex lock;
	u32 cfg_index;
	u32 cfg_size;
	u32 max_slices;
	u32 num_banks;
	unsigned long *bitmap;
	u32 *offsets;
	int ecc_irq;
	int llcc_ver;
	bool cap_based_alloc_and_pwr_collapse;
	struct llcc_slice_desc *desc;
};

/**
 * Enum describing the various staling modes available for clients to use.
 */
enum llcc_staling_mode {
	LLCC_STALING_MODE_CAPACITY, /* Default option on reset */
	LLCC_STALING_MODE_NOTIFY,
	LLCC_STALING_MODE_MAX
};

enum llcc_staling_notify_op {
	LLCC_NOTIFY_STALING_WRITEBACK,
	/* LLCC_NOTIFY_STALING_NO_WRITEBACK, */
	LLCC_NOTIFY_STALING_OPS_MAX
};

struct llcc_staling_mode_params {
	enum llcc_staling_mode staling_mode;
	union {
		/* STALING_MODE_CAPACITY needs no params */
		struct staling_mode_notify_params {
			u8 staling_distance;
			enum llcc_staling_notify_op op;
		} notify_params;
	};
};

#if IS_ENABLED(CONFIG_QCOM_LLCC)
/**
 * llcc_slice_getd - get llcc slice descriptor
 * @uid: usecase_id of the client
 */
struct llcc_slice_desc *llcc_slice_getd(u32 uid);

/**
 * llcc_slice_putd - llcc slice descritpor
 * @desc: Pointer to llcc slice descriptor
 */
void llcc_slice_putd(struct llcc_slice_desc *desc);

/**
 * llcc_get_slice_id - get slice id
 * @desc: Pointer to llcc slice descriptor
 */
int llcc_get_slice_id(struct llcc_slice_desc *desc);

/**
 * llcc_get_slice_size - llcc slice size
 * @desc: Pointer to llcc slice descriptor
 */
size_t llcc_get_slice_size(struct llcc_slice_desc *desc);

/**
 * llcc_slice_activate - Activate the llcc slice
 * @desc: Pointer to llcc slice descriptor
 */
int llcc_slice_activate(struct llcc_slice_desc *desc);

/**
 * llcc_slice_deactivate - Deactivate the llcc slice
 * @desc: Pointer to llcc slice descriptor
 */
int llcc_slice_deactivate(struct llcc_slice_desc *desc);

/**
 * llcc_configure_staling_mode - Configure cache staling mode by setting the
 *				 staling_mode and corresponding
 *				 mode-specific params
 *
 * @desc: Pointer to llcc slice descriptor
 * @p: Staling mode-specific params
 *
 * Returns: zero on success or negative errno.
 */
int llcc_configure_staling_mode(struct llcc_slice_desc *desc,
				struct llcc_staling_mode_params *p);
/**
 * llcc_notif_staling_inc_counter - Trigger the staling of the sub-cache frame.
 *
 * @desc: Pointer to llcc slice descriptor
 *
 * Returns: zero on success or negative errno.
 */
int llcc_notif_staling_inc_counter(struct llcc_slice_desc *desc);
#else
static inline struct llcc_slice_desc *llcc_slice_getd(u32 uid)
{
	return NULL;
}

static inline void llcc_slice_putd(struct llcc_slice_desc *desc)
{

};

static inline int llcc_get_slice_id(struct llcc_slice_desc *desc)
{
	return -EINVAL;
}

static inline size_t llcc_get_slice_size(struct llcc_slice_desc *desc)
{
	return 0;
}
static inline int llcc_slice_activate(struct llcc_slice_desc *desc)
{
	return -EINVAL;
}

static inline int llcc_slice_deactivate(struct llcc_slice_desc *desc)
{
	return -EINVAL;
}
static inline int llcc_configure_staling_mode(struct llcc_slice_desc *desc,
				       struct llcc_staling_mode_params *p)
{
	return -EINVAL;
}
static inline int llcc_notif_staling_inc_counter(struct llcc_slice_desc *desc)
{
	return -EINVAL;
}
#endif

#endif
