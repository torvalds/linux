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

#define SYSC_QUIRK_RESET_STATUS		BIT(7)
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
 *
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
 * @midlemodes: bitmask of supported master idle modes
 * @sidlemodes: bitmask of supported master idle modes
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

#endif	/* __TI_SYSC_DATA_H__ */
