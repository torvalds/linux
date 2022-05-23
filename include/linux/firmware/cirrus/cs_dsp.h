/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cs_dsp.h  --  Cirrus Logic DSP firmware support
 *
 * Based on sound/soc/codecs/wm_adsp.h
 *
 * Copyright 2012 Wolfson Microelectronics plc
 * Copyright (C) 2015-2021 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 */
#ifndef __CS_DSP_H
#define __CS_DSP_H

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/list.h>
#include <linux/regmap.h>

#define CS_ADSP2_REGION_0 BIT(0)
#define CS_ADSP2_REGION_1 BIT(1)
#define CS_ADSP2_REGION_2 BIT(2)
#define CS_ADSP2_REGION_3 BIT(3)
#define CS_ADSP2_REGION_4 BIT(4)
#define CS_ADSP2_REGION_5 BIT(5)
#define CS_ADSP2_REGION_6 BIT(6)
#define CS_ADSP2_REGION_7 BIT(7)
#define CS_ADSP2_REGION_8 BIT(8)
#define CS_ADSP2_REGION_9 BIT(9)
#define CS_ADSP2_REGION_1_9 (CS_ADSP2_REGION_1 | \
		CS_ADSP2_REGION_2 | CS_ADSP2_REGION_3 | \
		CS_ADSP2_REGION_4 | CS_ADSP2_REGION_5 | \
		CS_ADSP2_REGION_6 | CS_ADSP2_REGION_7 | \
		CS_ADSP2_REGION_8 | CS_ADSP2_REGION_9)
#define CS_ADSP2_REGION_ALL (CS_ADSP2_REGION_0 | CS_ADSP2_REGION_1_9)

#define CS_DSP_DATA_WORD_SIZE                3

#define CS_DSP_ACKED_CTL_TIMEOUT_MS          100
#define CS_DSP_ACKED_CTL_N_QUICKPOLLS        10
#define CS_DSP_ACKED_CTL_MIN_VALUE           0
#define CS_DSP_ACKED_CTL_MAX_VALUE           0xFFFFFF

/**
 * struct cs_dsp_region - Describes a logical memory region in DSP address space
 * @type:	Memory region type
 * @base:	Address of region
 */
struct cs_dsp_region {
	int type;
	unsigned int base;
};

/**
 * struct cs_dsp_alg_region - Describes a logical algorithm region in DSP address space
 * @list:	List node for internal use
 * @alg:	Algorithm id
 * @ver:	Expected algorithm version
 * @type:	Memory region type
 * @base:	Address of region
 */
struct cs_dsp_alg_region {
	struct list_head list;
	unsigned int alg;
	unsigned int ver;
	int type;
	unsigned int base;
};

/**
 * struct cs_dsp_coeff_ctl - Describes a coefficient control
 * @list:		List node for internal use
 * @dsp:		DSP instance associated with this control
 * @cache:		Cached value of the control
 * @fw_name:		Name of the firmware
 * @subname:		Name of the control parsed from the WMFW
 * @subname_len:	Length of subname
 * @offset:		Offset of control within alg_region in words
 * @len:		Length of the cached value in bytes
 * @type:		One of the WMFW_CTL_TYPE_ control types defined in wmfw.h
 * @flags:		Bitfield of WMFW_CTL_FLAG_ control flags defined in wmfw.h
 * @set:		Flag indicating the value has been written by the user
 * @enabled:		Flag indicating whether control is enabled
 * @alg_region:		Logical region associated with this control
 * @priv:		For use by the client
 */
struct cs_dsp_coeff_ctl {
	struct list_head list;
	struct cs_dsp *dsp;
	void *cache;
	const char *fw_name;
	/* Subname is needed to match with firmware */
	const char *subname;
	unsigned int subname_len;
	unsigned int offset;
	size_t len;
	unsigned int type;
	unsigned int flags;
	unsigned int set:1;
	unsigned int enabled:1;
	struct cs_dsp_alg_region alg_region;

	void *priv;
};

struct cs_dsp_ops;
struct cs_dsp_client_ops;

/**
 * struct cs_dsp - Configuration and state of a Cirrus Logic DSP
 * @name:		The name of the DSP instance
 * @rev:		Revision of the DSP
 * @num:		DSP instance number
 * @type:		Type of DSP
 * @dev:		Driver model representation of the device
 * @regmap:		Register map of the device
 * @ops:		Function pointers for internal callbacks
 * @client_ops:		Function pointers for client callbacks
 * @base:		Address of the DSP registers
 * @base_sysinfo:	Address of the sysinfo register (Halo only)
 * @sysclk_reg:		Address of the sysclk register (ADSP1 only)
 * @sysclk_mask:	Mask of frequency bits within sysclk register (ADSP1 only)
 * @sysclk_shift:	Shift of frequency bits within sysclk register (ADSP1 only)
 * @alg_regions:	List of currently loaded algorithm regions
 * @fw_file_name:	Filename of the current firmware
 * @fw_name:		Name of the current firmware
 * @fw_id:		ID of the current firmware, obtained from the wmfw
 * @fw_id_version:	Version of the firmware, obtained from the wmfw
 * @fw_vendor_id:	Vendor of the firmware, obtained from the wmfw
 * @mem:		DSP memory region descriptions
 * @num_mems:		Number of memory regions in this DSP
 * @fw_ver:		Version of the wmfw file format
 * @booted:		Flag indicating DSP has been configured
 * @running:		Flag indicating DSP is executing firmware
 * @ctl_list:		Controls defined within the loaded DSP firmware
 * @lock_regions:	Enable MPU traps on specified memory regions
 * @pwr_lock:		Lock used to serialize accesses
 * @debugfs_root:	Debugfs directory for this DSP instance
 * @wmfw_file_name:	Filename of the currently loaded firmware
 * @bin_file_name:	Filename of the currently loaded coefficients
 */
struct cs_dsp {
	const char *name;
	int rev;
	int num;
	int type;
	struct device *dev;
	struct regmap *regmap;

	const struct cs_dsp_ops *ops;
	const struct cs_dsp_client_ops *client_ops;

	unsigned int base;
	unsigned int base_sysinfo;
	unsigned int sysclk_reg;
	unsigned int sysclk_mask;
	unsigned int sysclk_shift;

	struct list_head alg_regions;

	const char *fw_name;
	unsigned int fw_id;
	unsigned int fw_id_version;
	unsigned int fw_vendor_id;

	const struct cs_dsp_region *mem;
	int num_mems;

	int fw_ver;

	bool booted;
	bool running;

	struct list_head ctl_list;

	struct mutex pwr_lock;

	unsigned int lock_regions;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_root;
	char *wmfw_file_name;
	char *bin_file_name;
#endif
};

/**
 * struct cs_dsp_client_ops - client callbacks
 * @control_add:	Called under the pwr_lock when a control is created
 * @control_remove:	Called under the pwr_lock when a control is destroyed
 * @pre_run:		Called under the pwr_lock by cs_dsp_run() before the core is started
 * @post_run:		Called under the pwr_lock by cs_dsp_run() after the core is started
 * @post_stop:		Called under the pwr_lock by cs_dsp_stop()
 * @watchdog_expired:	Called when a watchdog expiry is detected
 *
 * These callbacks give the cs_dsp client an opportunity to respond to events
 * or to perform actions atomically.
 */
struct cs_dsp_client_ops {
	int (*control_add)(struct cs_dsp_coeff_ctl *ctl);
	void (*control_remove)(struct cs_dsp_coeff_ctl *ctl);
	int (*pre_run)(struct cs_dsp *dsp);
	int (*post_run)(struct cs_dsp *dsp);
	void (*post_stop)(struct cs_dsp *dsp);
	void (*watchdog_expired)(struct cs_dsp *dsp);
};

int cs_dsp_adsp1_init(struct cs_dsp *dsp);
int cs_dsp_adsp2_init(struct cs_dsp *dsp);
int cs_dsp_halo_init(struct cs_dsp *dsp);

int cs_dsp_adsp1_power_up(struct cs_dsp *dsp,
			  const struct firmware *wmfw_firmware, char *wmfw_filename,
			  const struct firmware *coeff_firmware, char *coeff_filename,
			  const char *fw_name);
void cs_dsp_adsp1_power_down(struct cs_dsp *dsp);
int cs_dsp_power_up(struct cs_dsp *dsp,
		    const struct firmware *wmfw_firmware, char *wmfw_filename,
		    const struct firmware *coeff_firmware, char *coeff_filename,
		    const char *fw_name);
void cs_dsp_power_down(struct cs_dsp *dsp);
int cs_dsp_run(struct cs_dsp *dsp);
void cs_dsp_stop(struct cs_dsp *dsp);

void cs_dsp_remove(struct cs_dsp *dsp);

int cs_dsp_set_dspclk(struct cs_dsp *dsp, unsigned int freq);
void cs_dsp_adsp2_bus_error(struct cs_dsp *dsp);
void cs_dsp_halo_bus_error(struct cs_dsp *dsp);
void cs_dsp_halo_wdt_expire(struct cs_dsp *dsp);

void cs_dsp_init_debugfs(struct cs_dsp *dsp, struct dentry *debugfs_root);
void cs_dsp_cleanup_debugfs(struct cs_dsp *dsp);

int cs_dsp_coeff_write_acked_control(struct cs_dsp_coeff_ctl *ctl, unsigned int event_id);
int cs_dsp_coeff_write_ctrl(struct cs_dsp_coeff_ctl *ctl, unsigned int off,
			    const void *buf, size_t len);
int cs_dsp_coeff_read_ctrl(struct cs_dsp_coeff_ctl *ctl, unsigned int off,
			   void *buf, size_t len);
struct cs_dsp_coeff_ctl *cs_dsp_get_ctl(struct cs_dsp *dsp, const char *name, int type,
					unsigned int alg);

int cs_dsp_read_raw_data_block(struct cs_dsp *dsp, int mem_type, unsigned int mem_addr,
			       unsigned int num_words, __be32 *data);
int cs_dsp_read_data_word(struct cs_dsp *dsp, int mem_type, unsigned int mem_addr, u32 *data);
int cs_dsp_write_data_word(struct cs_dsp *dsp, int mem_type, unsigned int mem_addr, u32 data);
void cs_dsp_remove_padding(u32 *buf, int nwords);

struct cs_dsp_alg_region *cs_dsp_find_alg_region(struct cs_dsp *dsp,
						 int type, unsigned int id);

const char *cs_dsp_mem_region_name(unsigned int type);

#endif
