/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __TI_SYSC_DATA_H__
#define __TI_SYSC_DATA_H__

enum ti_sysc_module_type {
	TI_SYSC_OMAP2,
	TI_SYSC_OMAP2_TIMER,
	TI_SYSC_OMAP3_SHAM,
	TI_SYSC_OMAP3_AES,
	TI_SYSC_OMAP4,
	TI_SYSC_OMAP4_TIMER,
	TI_SYSC_OMAP4_SIMPLE,
	TI_SYSC_OMAP34XX_SR,
	TI_SYSC_OMAP36XX_SR,
	TI_SYSC_OMAP4_SR,
	TI_SYSC_OMAP4_MCASP,
	TI_SYSC_OMAP4_USB_HOST_FS,
	TI_SYSC_DRA7_MCAN,
	TI_SYSC_PRUSS,
};

struct ti_sysc_cookie {
	void *data;
	void *clkdm;
};

/**
 * struct sysc_regbits - TI OCP_SYSCONFIG register field offsets
 * @midle_shift: Offset of the midle bit
 * @clkact_shift: Offset of the clockactivity bit
 * @sidle_shift: Offset of the sidle bit
 * @enwkup_shift: Offset of the enawakeup bit
 * @srst_shift: Offset of the softreset bit
 * @autoidle_shift: Offset of the autoidle bit
 * @dmadisable_shift: Offset of the dmadisable bit
 * @emufree_shift; Offset of the emufree bit
 *
 * Note that 0 is a valid shift, and for ti-sysc.c -ENODEV can be used if a
 * feature is not available.
 */
struct sysc_regbits {
	s8 midle_shift;
	s8 clkact_shift;
	s8 sidle_shift;
	s8 enwkup_shift;
	s8 srst_shift;
	s8 autoidle_shift;
	s8 dmadisable_shift;
	s8 emufree_shift;
};

#define SYSC_MODULE_QUIRK_PRUSS		BIT(24)
#define SYSC_MODULE_QUIRK_DSS_RESET	BIT(23)
#define SYSC_MODULE_QUIRK_RTC_UNLOCK	BIT(22)
#define SYSC_QUIRK_CLKDM_NOAUTO		BIT(21)
#define SYSC_QUIRK_FORCE_MSTANDBY	BIT(20)
#define SYSC_MODULE_QUIRK_AESS		BIT(19)
#define SYSC_MODULE_QUIRK_SGX		BIT(18)
#define SYSC_MODULE_QUIRK_HDQ1W		BIT(17)
#define SYSC_MODULE_QUIRK_I2C		BIT(16)
#define SYSC_MODULE_QUIRK_WDT		BIT(15)
#define SYSS_QUIRK_RESETDONE_INVERTED	BIT(14)
#define SYSC_QUIRK_SWSUP_MSTANDBY	BIT(13)
#define SYSC_QUIRK_SWSUP_SIDLE_ACT	BIT(12)
#define SYSC_QUIRK_SWSUP_SIDLE		BIT(11)
#define SYSC_QUIRK_EXT_OPT_CLOCK	BIT(10)
#define SYSC_QUIRK_LEGACY_IDLE		BIT(9)
#define SYSC_QUIRK_RESET_STATUS		BIT(8)
#define SYSC_QUIRK_NO_IDLE		BIT(7)
#define SYSC_QUIRK_NO_IDLE_ON_INIT	BIT(6)
#define SYSC_QUIRK_NO_RESET_ON_INIT	BIT(5)
#define SYSC_QUIRK_OPT_CLKS_NEEDED	BIT(4)
#define SYSC_QUIRK_OPT_CLKS_IN_RESET	BIT(3)
#define SYSC_QUIRK_16BIT		BIT(2)
#define SYSC_QUIRK_UNCACHED		BIT(1)
#define SYSC_QUIRK_USE_CLOCKACT		BIT(0)

#define SYSC_NR_IDLEMODES		4

/**
 * struct sysc_capabilities - capabilities for an interconnect target module
 * @type: sysc type identifier for the module
 * @sysc_mask: bitmask of supported SYSCONFIG register bits
 * @regbits: bitmask of SYSCONFIG register bits
 * @mod_quirks: bitmask of module specific quirks
 */
struct sysc_capabilities {
	const enum ti_sysc_module_type type;
	const u32 sysc_mask;
	const struct sysc_regbits *regbits;
	const u32 mod_quirks;
};

/**
 * struct sysc_config - configuration for an interconnect target module
 * @sysc_val: configured value for sysc register
 * @syss_mask: configured mask value for SYSSTATUS register
 * @midlemodes: bitmask of supported master idle modes
 * @sidlemodes: bitmask of supported slave idle modes
 * @srst_udelay: optional delay needed after OCP soft reset
 * @quirks: bitmask of enabled quirks
 */
struct sysc_config {
	u32 sysc_val;
	u32 syss_mask;
	u8 midlemodes;
	u8 sidlemodes;
	u8 srst_udelay;
	u32 quirks;
};

enum sysc_registers {
	SYSC_REVISION,
	SYSC_SYSCONFIG,
	SYSC_SYSSTATUS,
	SYSC_MAX_REGS,
};

/**
 * struct ti_sysc_module_data - ti-sysc to hwmod translation data for a module
 * @name: legacy "ti,hwmods" module name
 * @module_pa: physical address of the interconnect target module
 * @module_size: size of the interconnect target module
 * @offsets: array of register offsets as listed in enum sysc_registers
 * @nr_offsets: number of registers
 * @cap: interconnect target module capabilities
 * @cfg: interconnect target module configuration
 *
 * This data is enough to allocate a new struct omap_hwmod_class_sysconfig
 * based on device tree data parsed by ti-sysc driver.
 */
struct ti_sysc_module_data {
	const char *name;
	u64 module_pa;
	u32 module_size;
	int *offsets;
	int nr_offsets;
	const struct sysc_capabilities *cap;
	struct sysc_config *cfg;
};

struct device;
struct clk;

struct ti_sysc_platform_data {
	struct of_dev_auxdata *auxdata;
	bool (*soc_type_gp)(void);
	int (*init_clockdomain)(struct device *dev, struct clk *fck,
				struct clk *ick, struct ti_sysc_cookie *cookie);
	void (*clkdm_deny_idle)(struct device *dev,
				const struct ti_sysc_cookie *cookie);
	void (*clkdm_allow_idle)(struct device *dev,
				 const struct ti_sysc_cookie *cookie);
	int (*init_module)(struct device *dev,
			   const struct ti_sysc_module_data *data,
			   struct ti_sysc_cookie *cookie);
	int (*enable_module)(struct device *dev,
			     const struct ti_sysc_cookie *cookie);
	int (*idle_module)(struct device *dev,
			   const struct ti_sysc_cookie *cookie);
	int (*shutdown_module)(struct device *dev,
			       const struct ti_sysc_cookie *cookie);
};

#endif	/* __TI_SYSC_DATA_H__ */
