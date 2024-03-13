/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Register Map - Based on PolarBear_CSRs.RevA.xlsx (2023-04-21)
 *
 * Copyright (C) 2023 Integrated Device Technology, Inc., a Renesas Company.
 */
#ifndef MFD_IDTRC38XXX_REG
#define MFD_IDTRC38XXX_REG

/* GLOBAL */
#define SOFT_RESET_CTRL		(0x15) /* Specific to FC3W */
#define MISC_CTRL		(0x14) /* Specific to FC3A */
#define APLL_REINIT		BIT(1)
#define APLL_REINIT_VFC3A	BIT(2)

#define DEVICE_ID		(0x2)
#define DEVICE_ID_MASK		(0x1000) /* Bit 12 is 1 if FC3W and 0 if FC3A */
#define DEVICE_ID_SHIFT		(12)

/* FOD */
#define FOD_0		(0x300)
#define FOD_0_VFC3A	(0x400)
#define FOD_1		(0x340)
#define FOD_1_VFC3A	(0x440)
#define FOD_2		(0x380)
#define FOD_2_VFC3A	(0x480)

/* TDCAPLL */
#define TDC_CTRL		(0x44a) /* Specific to FC3W */
#define TDC_ENABLE_CTRL		(0x169) /* Specific to FC3A */
#define TDC_DAC_CAL_CTRL	(0x16a) /* Specific to FC3A */
#define TDC_EN			BIT(0)
#define TDC_DAC_RECAL_REQ	BIT(1)
#define TDC_DAC_RECAL_REQ_VFC3A	BIT(0)

#define TDC_FB_DIV_INT_CNFG		(0x442)
#define TDC_FB_DIV_INT_CNFG_VFC3A	(0x162)
#define TDC_FB_DIV_INT_MASK		GENMASK(7, 0)
#define TDC_REF_DIV_CNFG		(0x443)
#define TDC_REF_DIV_CNFG_VFC3A		(0x163)
#define TDC_REF_DIV_CONFIG_MASK		GENMASK(2, 0)

/* TIME SYNC CHANNEL */
#define TIME_CLOCK_SRC		(0xa01) /* Specific to FC3W */
#define TIME_CLOCK_COUNT	(0xa00) /* Specific to FC3W */
#define TIME_CLOCK_COUNT_MASK	GENMASK(5, 0)

#define SUB_SYNC_GEN_CNFG	(0xa04)

#define TOD_COUNTER_READ_REQ		(0xa5f)
#define TOD_COUNTER_READ_REQ_VFC3A	(0x6df)
#define TOD_SYNC_LOAD_VAL_CTRL		(0xa10)
#define TOD_SYNC_LOAD_VAL_CTRL_VFC3A	(0x690)
#define SYNC_COUNTER_MASK		GENMASK_ULL(51, 0)
#define SUB_SYNC_COUNTER_MASK		GENMASK(30, 0)
#define TOD_SYNC_LOAD_REQ_CTRL		(0xa21)
#define TOD_SYNC_LOAD_REQ_CTRL_VFC3A	(0x6a1)
#define SYNC_LOAD_ENABLE		BIT(1)
#define SUB_SYNC_LOAD_ENABLE		BIT(0)
#define SYNC_LOAD_REQ			BIT(0)

#define LPF_MODE_CNFG		(0xa80)
#define LPF_MODE_CNFG_VFC3A	(0x700)
enum lpf_mode {
	LPF_DISABLED = 0,
	LPF_WP       = 1,
	LPF_HOLDOVER = 2,
	LPF_WF       = 3,
	LPF_INVALID  = 4
};
#define LPF_CTRL	(0xa98)
#define LPF_CTRL_VFC3A	(0x718)
#define LPF_EN		BIT(0)

#define LPF_BW_CNFG	(0xa81)
#define LPF_BW_SHIFT	GENMASK(7, 3)
#define LPF_BW_MULT		GENMASK(2, 0)
#define LPF_BW_SHIFT_DEFAULT	(0xb)
#define LPF_BW_MULT_DEFAULT		(0x0)
#define LPF_BW_SHIFT_1PPS		(0x5)

#define LPF_WR_PHASE_CTRL	(0xaa8)
#define LPF_WR_PHASE_CTRL_VFC3A	(0x728)
#define LPF_WR_FREQ_CTRL	(0xab0)
#define LPF_WR_FREQ_CTRL_VFC3A	(0x730)

#define TIME_CLOCK_TDC_FANOUT_CNFG	(0xB00)
#define TIME_SYNC_TO_TDC_EN	BIT(0)
#define SIG1_MUX_SEL_MASK	GENMASK(7, 4)
#define SIG2_MUX_SEL_MASK	GENMASK(11, 8)
enum tdc_mux_sel {
	REF0 = 0,
	REF1 = 1,
	REF2 = 2,
	REF3 = 3,
	REF_CLK5 = 4,
	REF_CLK6 = 5,
	DPLL_FB_TO_TDC = 6,
	DPLL_FB_DIVIDED_TO_TDC = 7,
	TIME_CLK_DIVIDED = 8,
	TIME_SYNC = 9,
};

#define TIME_CLOCK_MEAS_CNFG	(0xB04)
#define TDC_MEAS_MODE	BIT(0)
enum tdc_meas_mode {
	CONTINUOUS = 0,
	ONE_SHOT = 1,
	MEAS_MODE_INVALID = 2,
};

#define TIME_CLOCK_MEAS_DIV_CNFG	(0xB08)
#define TIME_REF_DIV_MASK	GENMASK(29, 24)

#define TIME_CLOCK_MEAS_CTRL	(0xB10)
#define TDC_MEAS_EN	BIT(0)
#define TDC_MEAS_START	BIT(1)

#define TDC_FIFO_READ_REQ	(0xB2F)
#define TDC_FIFO_READ		(0xB30)
#define COARSE_MEAS_MASK	GENMASK_ULL(39, 13)
#define FINE_MEAS_MASK		GENMASK(12, 0)

#define TDC_FIFO_CTRL		(0xB12)
#define FIFO_CLEAR		BIT(0)
#define TDC_FIFO_STS		(0xB38)
#define FIFO_FULL		BIT(1)
#define FIFO_EMPTY		BIT(0)
#define TDC_FIFO_EVENT		(0xB39)
#define FIFO_OVERRUN		BIT(1)

/* DPLL */
#define MAX_REFERENCE_INDEX	(3)
#define MAX_NUM_REF_PRIORITY	(4)

#define MAX_DPLL_INDEX	(2)

#define DPLL_STS		(0x580)
#define DPLL_STS_VFC3A		(0x571)
#define DPLL_STATE_STS_MASK	(0x70)
#define DPLL_STATE_STS_SHIFT	(4)
#define DPLL_REF_SEL_STS_MASK	(0x6)
#define DPLL_REF_SEL_STS_SHIFT	(1)

#define DPLL_REF_PRIORITY_CNFG			(0x502)
#define DPLL_REFX_PRIORITY_DISABLE_MASK		(0xf)
#define DPLL_REF0_PRIORITY_ENABLE_AND_SET_MASK	(0x31)
#define DPLL_REF1_PRIORITY_ENABLE_AND_SET_MASK	(0xc2)
#define DPLL_REF2_PRIORITY_ENABLE_AND_SET_MASK	(0x304)
#define DPLL_REF3_PRIORITY_ENABLE_AND_SET_MASK	(0xc08)
#define DPLL_REF0_PRIORITY_SHIFT		(4)
#define DPLL_REF1_PRIORITY_SHIFT		(6)
#define DPLL_REF2_PRIORITY_SHIFT		(8)
#define DPLL_REF3_PRIORITY_SHIFT		(10)

enum dpll_state {
	DPLL_STATE_MIN             = 0,
	DPLL_STATE_FREERUN         = DPLL_STATE_MIN,
	DPLL_STATE_LOCKED          = 1,
	DPLL_STATE_HOLDOVER        = 2,
	DPLL_STATE_WRITE_FREQUENCY = 3,
	DPLL_STATE_ACQUIRE         = 4,
	DPLL_STATE_HITLESS_SWITCH  = 5,
	DPLL_STATE_MAX             = DPLL_STATE_HITLESS_SWITCH
};

/* REFMON */
#define LOSMON_STS_0		(0x81e)
#define LOSMON_STS_0_VFC3A	(0x18e)
#define LOSMON_STS_1		(0x82e)
#define LOSMON_STS_1_VFC3A	(0x19e)
#define LOSMON_STS_2		(0x83e)
#define LOSMON_STS_2_VFC3A	(0x1ae)
#define LOSMON_STS_3		(0x84e)
#define LOSMON_STS_3_VFC3A	(0x1be)
#define LOS_STS_MASK		(0x1)

#define FREQMON_STS_0		(0x874)
#define FREQMON_STS_0_VFC3A	(0x1d4)
#define FREQMON_STS_1		(0x894)
#define FREQMON_STS_1_VFC3A	(0x1f4)
#define FREQMON_STS_2		(0x8b4)
#define FREQMON_STS_2_VFC3A	(0x214)
#define FREQMON_STS_3		(0x8d4)
#define FREQMON_STS_3_VFC3A	(0x234)
#define FREQ_FAIL_STS_SHIFT	(31)

/* Firmware interface */
#define TIME_CLK_FREQ_ADDR	(0xffa0)
#define XTAL_FREQ_ADDR		(0xffa1)

/*
 * Return register address and field mask based on passed in firmware version
 */
#define IDTFC3_FW_REG(FW, VER, REG)	(((FW) < (VER)) ? (REG) : (REG##_##VER))
#define IDTFC3_FW_FIELD(FW, VER, FIELD)	(((FW) < (VER)) ? (FIELD) : (FIELD##_##VER))
enum fw_version {
	V_DEFAULT = 0,
	VFC3W     = 1,
	VFC3A     = 2
};

/* XTAL_FREQ_ADDR/TIME_CLK_FREQ_ADDR */
enum {
	FREQ_MIN     = 0,
	FREQ_25M     = 1,
	FREQ_49_152M = 2,
	FREQ_50M     = 3,
	FREQ_100M    = 4,
	FREQ_125M    = 5,
	FREQ_250M    = 6,
	FREQ_MAX
};

struct idtfc3_hw_param {
	u32 xtal_freq;
	u32 time_clk_freq;
};

struct idtfc3_fwrc {
	u8 hiaddr;
	u8 loaddr;
	u8 value;
	u8 reserved;
} __packed;

static inline void idtfc3_default_hw_param(struct idtfc3_hw_param *hw_param)
{
	hw_param->xtal_freq = 49152000;
	hw_param->time_clk_freq = 25000000;
}

static inline int idtfc3_set_hw_param(struct idtfc3_hw_param *hw_param,
				      u16 addr, u8 val)
{
	if (addr == XTAL_FREQ_ADDR)
		switch (val) {
		case FREQ_49_152M:
			hw_param->xtal_freq = 49152000;
			break;
		case FREQ_50M:
			hw_param->xtal_freq = 50000000;
			break;
		default:
			return -EINVAL;
		}
	else if (addr == TIME_CLK_FREQ_ADDR)
		switch (val) {
		case FREQ_25M:
			hw_param->time_clk_freq = 25000000;
			break;
		case FREQ_50M:
			hw_param->time_clk_freq = 50000000;
			break;
		case FREQ_100M:
			hw_param->time_clk_freq = 100000000;
			break;
		case FREQ_125M:
			hw_param->time_clk_freq = 125000000;
			break;
		case FREQ_250M:
			hw_param->time_clk_freq = 250000000;
			break;
		default:
			return -EINVAL;
		}
	else
		return -EFAULT;

	return 0;
}

#endif
