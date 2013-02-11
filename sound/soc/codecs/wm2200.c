/*
 * wm2200.c  --  WM2200 ALSA SoC Audio driver
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/firmware.h>
#include <linux/gcd.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/fixed.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/wm2200.h>

#include "wm2200.h"
#include "wmfw.h"
#include "wm_adsp.h"

#define WM2200_DSP_CONTROL_1                   0x00
#define WM2200_DSP_CONTROL_2                   0x02
#define WM2200_DSP_CONTROL_3                   0x03
#define WM2200_DSP_CONTROL_4                   0x04
#define WM2200_DSP_CONTROL_5                   0x06
#define WM2200_DSP_CONTROL_6                   0x07
#define WM2200_DSP_CONTROL_7                   0x08
#define WM2200_DSP_CONTROL_8                   0x09
#define WM2200_DSP_CONTROL_9                   0x0A
#define WM2200_DSP_CONTROL_10                  0x0B
#define WM2200_DSP_CONTROL_11                  0x0C
#define WM2200_DSP_CONTROL_12                  0x0D
#define WM2200_DSP_CONTROL_13                  0x0F
#define WM2200_DSP_CONTROL_14                  0x10
#define WM2200_DSP_CONTROL_15                  0x11
#define WM2200_DSP_CONTROL_16                  0x12
#define WM2200_DSP_CONTROL_17                  0x13
#define WM2200_DSP_CONTROL_18                  0x14
#define WM2200_DSP_CONTROL_19                  0x16
#define WM2200_DSP_CONTROL_20                  0x17
#define WM2200_DSP_CONTROL_21                  0x18
#define WM2200_DSP_CONTROL_22                  0x1A
#define WM2200_DSP_CONTROL_23                  0x1B
#define WM2200_DSP_CONTROL_24                  0x1C
#define WM2200_DSP_CONTROL_25                  0x1E
#define WM2200_DSP_CONTROL_26                  0x20
#define WM2200_DSP_CONTROL_27                  0x21
#define WM2200_DSP_CONTROL_28                  0x22
#define WM2200_DSP_CONTROL_29                  0x23
#define WM2200_DSP_CONTROL_30                  0x24
#define WM2200_DSP_CONTROL_31                  0x26

/* The code assumes DCVDD is generated internally */
#define WM2200_NUM_CORE_SUPPLIES 2
static const char *wm2200_core_supply_names[WM2200_NUM_CORE_SUPPLIES] = {
	"DBVDD",
	"LDOVDD",
};

struct wm2200_fll {
	int fref;
	int fout;
	int src;
	struct completion lock;
};

/* codec private data */
struct wm2200_priv {
	struct wm_adsp dsp[2];
	struct regmap *regmap;
	struct device *dev;
	struct snd_soc_codec *codec;
	struct wm2200_pdata pdata;
	struct regulator_bulk_data core_supplies[WM2200_NUM_CORE_SUPPLIES];

	struct completion fll_lock;
	int fll_fout;
	int fll_fref;
	int fll_src;

	int rev;
	int sysclk;
};

#define WM2200_DSP_RANGE_BASE (WM2200_MAX_REGISTER + 1)
#define WM2200_DSP_SPACING 12288

#define WM2200_DSP1_DM_BASE (WM2200_DSP_RANGE_BASE + (0 * WM2200_DSP_SPACING))
#define WM2200_DSP1_PM_BASE (WM2200_DSP_RANGE_BASE + (1 * WM2200_DSP_SPACING))
#define WM2200_DSP1_ZM_BASE (WM2200_DSP_RANGE_BASE + (2 * WM2200_DSP_SPACING))
#define WM2200_DSP2_DM_BASE (WM2200_DSP_RANGE_BASE + (3 * WM2200_DSP_SPACING))
#define WM2200_DSP2_PM_BASE (WM2200_DSP_RANGE_BASE + (4 * WM2200_DSP_SPACING))
#define WM2200_DSP2_ZM_BASE (WM2200_DSP_RANGE_BASE + (5 * WM2200_DSP_SPACING))

static const struct regmap_range_cfg wm2200_ranges[] = {
	{ .name = "DSP1DM", .range_min = WM2200_DSP1_DM_BASE,
	  .range_max = WM2200_DSP1_DM_BASE + 12287,
	  .selector_reg = WM2200_DSP1_CONTROL_3,
	  .selector_mask = WM2200_DSP1_PAGE_BASE_DM_0_MASK,
	  .selector_shift = WM2200_DSP1_PAGE_BASE_DM_0_SHIFT,
	  .window_start = WM2200_DSP1_DM_0, .window_len = 2048, },

	{ .name = "DSP1PM", .range_min = WM2200_DSP1_PM_BASE,
	  .range_max = WM2200_DSP1_PM_BASE + 12287,
	  .selector_reg = WM2200_DSP1_CONTROL_2,
	  .selector_mask = WM2200_DSP1_PAGE_BASE_PM_0_MASK,
	  .selector_shift = WM2200_DSP1_PAGE_BASE_PM_0_SHIFT,
	  .window_start = WM2200_DSP1_PM_0, .window_len = 768, },

	{ .name = "DSP1ZM", .range_min = WM2200_DSP1_ZM_BASE,
	  .range_max = WM2200_DSP1_ZM_BASE + 2047,
	  .selector_reg = WM2200_DSP1_CONTROL_4,
	  .selector_mask = WM2200_DSP1_PAGE_BASE_ZM_0_MASK,
	  .selector_shift = WM2200_DSP1_PAGE_BASE_ZM_0_SHIFT,
	  .window_start = WM2200_DSP1_ZM_0, .window_len = 1024, },

	{ .name = "DSP2DM", .range_min = WM2200_DSP2_DM_BASE,
	  .range_max = WM2200_DSP2_DM_BASE + 4095,
	  .selector_reg = WM2200_DSP2_CONTROL_3,
	  .selector_mask = WM2200_DSP2_PAGE_BASE_DM_0_MASK,
	  .selector_shift = WM2200_DSP2_PAGE_BASE_DM_0_SHIFT,
	  .window_start = WM2200_DSP2_DM_0, .window_len = 2048, },

	{ .name = "DSP2PM", .range_min = WM2200_DSP2_PM_BASE,
	  .range_max = WM2200_DSP2_PM_BASE + 11287,
	  .selector_reg = WM2200_DSP2_CONTROL_2,
	  .selector_mask = WM2200_DSP2_PAGE_BASE_PM_0_MASK,
	  .selector_shift = WM2200_DSP2_PAGE_BASE_PM_0_SHIFT,
	  .window_start = WM2200_DSP2_PM_0, .window_len = 768, },

	{ .name = "DSP2ZM", .range_min = WM2200_DSP2_ZM_BASE,
	  .range_max = WM2200_DSP2_ZM_BASE + 2047,
	  .selector_reg = WM2200_DSP2_CONTROL_4,
	  .selector_mask = WM2200_DSP2_PAGE_BASE_ZM_0_MASK,
	  .selector_shift = WM2200_DSP2_PAGE_BASE_ZM_0_SHIFT,
	  .window_start = WM2200_DSP2_ZM_0, .window_len = 1024, },
};

static const struct wm_adsp_region wm2200_dsp1_regions[] = {
	{ .type = WMFW_ADSP1_PM, .base = WM2200_DSP1_PM_BASE },
	{ .type = WMFW_ADSP1_DM, .base = WM2200_DSP1_DM_BASE },
	{ .type = WMFW_ADSP1_ZM, .base = WM2200_DSP1_ZM_BASE },
};

static const struct wm_adsp_region wm2200_dsp2_regions[] = {
	{ .type = WMFW_ADSP1_PM, .base = WM2200_DSP2_PM_BASE },
	{ .type = WMFW_ADSP1_DM, .base = WM2200_DSP2_DM_BASE },
	{ .type = WMFW_ADSP1_ZM, .base = WM2200_DSP2_ZM_BASE },
};

static struct reg_default wm2200_reg_defaults[] = {
	{ 0x000B, 0x0000 },   /* R11    - Tone Generator 1 */
	{ 0x0102, 0x0000 },   /* R258   - Clocking 3 */
	{ 0x0103, 0x0011 },   /* R259   - Clocking 4 */
	{ 0x0111, 0x0000 },   /* R273   - FLL Control 1 */
	{ 0x0112, 0x0000 },   /* R274   - FLL Control 2 */
	{ 0x0113, 0x0000 },   /* R275   - FLL Control 3 */
	{ 0x0114, 0x0000 },   /* R276   - FLL Control 4 */
	{ 0x0116, 0x0177 },   /* R278   - FLL Control 6 */
	{ 0x0117, 0x0004 },   /* R279   - FLL Control 7 */
	{ 0x0119, 0x0000 },   /* R281   - FLL EFS 1 */
	{ 0x011A, 0x0002 },   /* R282   - FLL EFS 2 */
	{ 0x0200, 0x0000 },   /* R512   - Mic Charge Pump 1 */
	{ 0x0201, 0x03FF },   /* R513   - Mic Charge Pump 2 */
	{ 0x0202, 0x9BDE },   /* R514   - DM Charge Pump 1 */
	{ 0x020C, 0x0000 },   /* R524   - Mic Bias Ctrl 1 */
	{ 0x020D, 0x0000 },   /* R525   - Mic Bias Ctrl 2 */
	{ 0x020F, 0x0000 },   /* R527   - Ear Piece Ctrl 1 */
	{ 0x0210, 0x0000 },   /* R528   - Ear Piece Ctrl 2 */
	{ 0x0301, 0x0000 },   /* R769   - Input Enables */
	{ 0x0302, 0x2240 },   /* R770   - IN1L Control */
	{ 0x0303, 0x0040 },   /* R771   - IN1R Control */
	{ 0x0304, 0x2240 },   /* R772   - IN2L Control */
	{ 0x0305, 0x0040 },   /* R773   - IN2R Control */
	{ 0x0306, 0x2240 },   /* R774   - IN3L Control */
	{ 0x0307, 0x0040 },   /* R775   - IN3R Control */
	{ 0x030A, 0x0000 },   /* R778   - RXANC_SRC */
	{ 0x030B, 0x0022 },   /* R779   - Input Volume Ramp */
	{ 0x030C, 0x0180 },   /* R780   - ADC Digital Volume 1L */
	{ 0x030D, 0x0180 },   /* R781   - ADC Digital Volume 1R */
	{ 0x030E, 0x0180 },   /* R782   - ADC Digital Volume 2L */
	{ 0x030F, 0x0180 },   /* R783   - ADC Digital Volume 2R */
	{ 0x0310, 0x0180 },   /* R784   - ADC Digital Volume 3L */
	{ 0x0311, 0x0180 },   /* R785   - ADC Digital Volume 3R */
	{ 0x0400, 0x0000 },   /* R1024  - Output Enables */
	{ 0x0401, 0x0000 },   /* R1025  - DAC Volume Limit 1L */
	{ 0x0402, 0x0000 },   /* R1026  - DAC Volume Limit 1R */
	{ 0x0403, 0x0000 },   /* R1027  - DAC Volume Limit 2L */
	{ 0x0404, 0x0000 },   /* R1028  - DAC Volume Limit 2R */
	{ 0x0409, 0x0000 },   /* R1033  - DAC AEC Control 1 */
	{ 0x040A, 0x0022 },   /* R1034  - Output Volume Ramp */
	{ 0x040B, 0x0180 },   /* R1035  - DAC Digital Volume 1L */
	{ 0x040C, 0x0180 },   /* R1036  - DAC Digital Volume 1R */
	{ 0x040D, 0x0180 },   /* R1037  - DAC Digital Volume 2L */
	{ 0x040E, 0x0180 },   /* R1038  - DAC Digital Volume 2R */
	{ 0x0417, 0x0069 },   /* R1047  - PDM 1 */
	{ 0x0418, 0x0000 },   /* R1048  - PDM 2 */
	{ 0x0500, 0x0000 },   /* R1280  - Audio IF 1_1 */
	{ 0x0501, 0x0008 },   /* R1281  - Audio IF 1_2 */
	{ 0x0502, 0x0000 },   /* R1282  - Audio IF 1_3 */
	{ 0x0503, 0x0000 },   /* R1283  - Audio IF 1_4 */
	{ 0x0504, 0x0000 },   /* R1284  - Audio IF 1_5 */
	{ 0x0505, 0x0001 },   /* R1285  - Audio IF 1_6 */
	{ 0x0506, 0x0001 },   /* R1286  - Audio IF 1_7 */
	{ 0x0507, 0x0000 },   /* R1287  - Audio IF 1_8 */
	{ 0x0508, 0x0000 },   /* R1288  - Audio IF 1_9 */
	{ 0x0509, 0x0000 },   /* R1289  - Audio IF 1_10 */
	{ 0x050A, 0x0000 },   /* R1290  - Audio IF 1_11 */
	{ 0x050B, 0x0000 },   /* R1291  - Audio IF 1_12 */
	{ 0x050C, 0x0000 },   /* R1292  - Audio IF 1_13 */
	{ 0x050D, 0x0000 },   /* R1293  - Audio IF 1_14 */
	{ 0x050E, 0x0000 },   /* R1294  - Audio IF 1_15 */
	{ 0x050F, 0x0000 },   /* R1295  - Audio IF 1_16 */
	{ 0x0510, 0x0000 },   /* R1296  - Audio IF 1_17 */
	{ 0x0511, 0x0000 },   /* R1297  - Audio IF 1_18 */
	{ 0x0512, 0x0000 },   /* R1298  - Audio IF 1_19 */
	{ 0x0513, 0x0000 },   /* R1299  - Audio IF 1_20 */
	{ 0x0514, 0x0000 },   /* R1300  - Audio IF 1_21 */
	{ 0x0515, 0x0001 },   /* R1301  - Audio IF 1_22 */
	{ 0x0600, 0x0000 },   /* R1536  - OUT1LMIX Input 1 Source */
	{ 0x0601, 0x0080 },   /* R1537  - OUT1LMIX Input 1 Volume */
	{ 0x0602, 0x0000 },   /* R1538  - OUT1LMIX Input 2 Source */
	{ 0x0603, 0x0080 },   /* R1539  - OUT1LMIX Input 2 Volume */
	{ 0x0604, 0x0000 },   /* R1540  - OUT1LMIX Input 3 Source */
	{ 0x0605, 0x0080 },   /* R1541  - OUT1LMIX Input 3 Volume */
	{ 0x0606, 0x0000 },   /* R1542  - OUT1LMIX Input 4 Source */
	{ 0x0607, 0x0080 },   /* R1543  - OUT1LMIX Input 4 Volume */
	{ 0x0608, 0x0000 },   /* R1544  - OUT1RMIX Input 1 Source */
	{ 0x0609, 0x0080 },   /* R1545  - OUT1RMIX Input 1 Volume */
	{ 0x060A, 0x0000 },   /* R1546  - OUT1RMIX Input 2 Source */
	{ 0x060B, 0x0080 },   /* R1547  - OUT1RMIX Input 2 Volume */
	{ 0x060C, 0x0000 },   /* R1548  - OUT1RMIX Input 3 Source */
	{ 0x060D, 0x0080 },   /* R1549  - OUT1RMIX Input 3 Volume */
	{ 0x060E, 0x0000 },   /* R1550  - OUT1RMIX Input 4 Source */
	{ 0x060F, 0x0080 },   /* R1551  - OUT1RMIX Input 4 Volume */
	{ 0x0610, 0x0000 },   /* R1552  - OUT2LMIX Input 1 Source */
	{ 0x0611, 0x0080 },   /* R1553  - OUT2LMIX Input 1 Volume */
	{ 0x0612, 0x0000 },   /* R1554  - OUT2LMIX Input 2 Source */
	{ 0x0613, 0x0080 },   /* R1555  - OUT2LMIX Input 2 Volume */
	{ 0x0614, 0x0000 },   /* R1556  - OUT2LMIX Input 3 Source */
	{ 0x0615, 0x0080 },   /* R1557  - OUT2LMIX Input 3 Volume */
	{ 0x0616, 0x0000 },   /* R1558  - OUT2LMIX Input 4 Source */
	{ 0x0617, 0x0080 },   /* R1559  - OUT2LMIX Input 4 Volume */
	{ 0x0618, 0x0000 },   /* R1560  - OUT2RMIX Input 1 Source */
	{ 0x0619, 0x0080 },   /* R1561  - OUT2RMIX Input 1 Volume */
	{ 0x061A, 0x0000 },   /* R1562  - OUT2RMIX Input 2 Source */
	{ 0x061B, 0x0080 },   /* R1563  - OUT2RMIX Input 2 Volume */
	{ 0x061C, 0x0000 },   /* R1564  - OUT2RMIX Input 3 Source */
	{ 0x061D, 0x0080 },   /* R1565  - OUT2RMIX Input 3 Volume */
	{ 0x061E, 0x0000 },   /* R1566  - OUT2RMIX Input 4 Source */
	{ 0x061F, 0x0080 },   /* R1567  - OUT2RMIX Input 4 Volume */
	{ 0x0620, 0x0000 },   /* R1568  - AIF1TX1MIX Input 1 Source */
	{ 0x0621, 0x0080 },   /* R1569  - AIF1TX1MIX Input 1 Volume */
	{ 0x0622, 0x0000 },   /* R1570  - AIF1TX1MIX Input 2 Source */
	{ 0x0623, 0x0080 },   /* R1571  - AIF1TX1MIX Input 2 Volume */
	{ 0x0624, 0x0000 },   /* R1572  - AIF1TX1MIX Input 3 Source */
	{ 0x0625, 0x0080 },   /* R1573  - AIF1TX1MIX Input 3 Volume */
	{ 0x0626, 0x0000 },   /* R1574  - AIF1TX1MIX Input 4 Source */
	{ 0x0627, 0x0080 },   /* R1575  - AIF1TX1MIX Input 4 Volume */
	{ 0x0628, 0x0000 },   /* R1576  - AIF1TX2MIX Input 1 Source */
	{ 0x0629, 0x0080 },   /* R1577  - AIF1TX2MIX Input 1 Volume */
	{ 0x062A, 0x0000 },   /* R1578  - AIF1TX2MIX Input 2 Source */
	{ 0x062B, 0x0080 },   /* R1579  - AIF1TX2MIX Input 2 Volume */
	{ 0x062C, 0x0000 },   /* R1580  - AIF1TX2MIX Input 3 Source */
	{ 0x062D, 0x0080 },   /* R1581  - AIF1TX2MIX Input 3 Volume */
	{ 0x062E, 0x0000 },   /* R1582  - AIF1TX2MIX Input 4 Source */
	{ 0x062F, 0x0080 },   /* R1583  - AIF1TX2MIX Input 4 Volume */
	{ 0x0630, 0x0000 },   /* R1584  - AIF1TX3MIX Input 1 Source */
	{ 0x0631, 0x0080 },   /* R1585  - AIF1TX3MIX Input 1 Volume */
	{ 0x0632, 0x0000 },   /* R1586  - AIF1TX3MIX Input 2 Source */
	{ 0x0633, 0x0080 },   /* R1587  - AIF1TX3MIX Input 2 Volume */
	{ 0x0634, 0x0000 },   /* R1588  - AIF1TX3MIX Input 3 Source */
	{ 0x0635, 0x0080 },   /* R1589  - AIF1TX3MIX Input 3 Volume */
	{ 0x0636, 0x0000 },   /* R1590  - AIF1TX3MIX Input 4 Source */
	{ 0x0637, 0x0080 },   /* R1591  - AIF1TX3MIX Input 4 Volume */
	{ 0x0638, 0x0000 },   /* R1592  - AIF1TX4MIX Input 1 Source */
	{ 0x0639, 0x0080 },   /* R1593  - AIF1TX4MIX Input 1 Volume */
	{ 0x063A, 0x0000 },   /* R1594  - AIF1TX4MIX Input 2 Source */
	{ 0x063B, 0x0080 },   /* R1595  - AIF1TX4MIX Input 2 Volume */
	{ 0x063C, 0x0000 },   /* R1596  - AIF1TX4MIX Input 3 Source */
	{ 0x063D, 0x0080 },   /* R1597  - AIF1TX4MIX Input 3 Volume */
	{ 0x063E, 0x0000 },   /* R1598  - AIF1TX4MIX Input 4 Source */
	{ 0x063F, 0x0080 },   /* R1599  - AIF1TX4MIX Input 4 Volume */
	{ 0x0640, 0x0000 },   /* R1600  - AIF1TX5MIX Input 1 Source */
	{ 0x0641, 0x0080 },   /* R1601  - AIF1TX5MIX Input 1 Volume */
	{ 0x0642, 0x0000 },   /* R1602  - AIF1TX5MIX Input 2 Source */
	{ 0x0643, 0x0080 },   /* R1603  - AIF1TX5MIX Input 2 Volume */
	{ 0x0644, 0x0000 },   /* R1604  - AIF1TX5MIX Input 3 Source */
	{ 0x0645, 0x0080 },   /* R1605  - AIF1TX5MIX Input 3 Volume */
	{ 0x0646, 0x0000 },   /* R1606  - AIF1TX5MIX Input 4 Source */
	{ 0x0647, 0x0080 },   /* R1607  - AIF1TX5MIX Input 4 Volume */
	{ 0x0648, 0x0000 },   /* R1608  - AIF1TX6MIX Input 1 Source */
	{ 0x0649, 0x0080 },   /* R1609  - AIF1TX6MIX Input 1 Volume */
	{ 0x064A, 0x0000 },   /* R1610  - AIF1TX6MIX Input 2 Source */
	{ 0x064B, 0x0080 },   /* R1611  - AIF1TX6MIX Input 2 Volume */
	{ 0x064C, 0x0000 },   /* R1612  - AIF1TX6MIX Input 3 Source */
	{ 0x064D, 0x0080 },   /* R1613  - AIF1TX6MIX Input 3 Volume */
	{ 0x064E, 0x0000 },   /* R1614  - AIF1TX6MIX Input 4 Source */
	{ 0x064F, 0x0080 },   /* R1615  - AIF1TX6MIX Input 4 Volume */
	{ 0x0650, 0x0000 },   /* R1616  - EQLMIX Input 1 Source */
	{ 0x0651, 0x0080 },   /* R1617  - EQLMIX Input 1 Volume */
	{ 0x0652, 0x0000 },   /* R1618  - EQLMIX Input 2 Source */
	{ 0x0653, 0x0080 },   /* R1619  - EQLMIX Input 2 Volume */
	{ 0x0654, 0x0000 },   /* R1620  - EQLMIX Input 3 Source */
	{ 0x0655, 0x0080 },   /* R1621  - EQLMIX Input 3 Volume */
	{ 0x0656, 0x0000 },   /* R1622  - EQLMIX Input 4 Source */
	{ 0x0657, 0x0080 },   /* R1623  - EQLMIX Input 4 Volume */
	{ 0x0658, 0x0000 },   /* R1624  - EQRMIX Input 1 Source */
	{ 0x0659, 0x0080 },   /* R1625  - EQRMIX Input 1 Volume */
	{ 0x065A, 0x0000 },   /* R1626  - EQRMIX Input 2 Source */
	{ 0x065B, 0x0080 },   /* R1627  - EQRMIX Input 2 Volume */
	{ 0x065C, 0x0000 },   /* R1628  - EQRMIX Input 3 Source */
	{ 0x065D, 0x0080 },   /* R1629  - EQRMIX Input 3 Volume */
	{ 0x065E, 0x0000 },   /* R1630  - EQRMIX Input 4 Source */
	{ 0x065F, 0x0080 },   /* R1631  - EQRMIX Input 4 Volume */
	{ 0x0660, 0x0000 },   /* R1632  - LHPF1MIX Input 1 Source */
	{ 0x0661, 0x0080 },   /* R1633  - LHPF1MIX Input 1 Volume */
	{ 0x0662, 0x0000 },   /* R1634  - LHPF1MIX Input 2 Source */
	{ 0x0663, 0x0080 },   /* R1635  - LHPF1MIX Input 2 Volume */
	{ 0x0664, 0x0000 },   /* R1636  - LHPF1MIX Input 3 Source */
	{ 0x0665, 0x0080 },   /* R1637  - LHPF1MIX Input 3 Volume */
	{ 0x0666, 0x0000 },   /* R1638  - LHPF1MIX Input 4 Source */
	{ 0x0667, 0x0080 },   /* R1639  - LHPF1MIX Input 4 Volume */
	{ 0x0668, 0x0000 },   /* R1640  - LHPF2MIX Input 1 Source */
	{ 0x0669, 0x0080 },   /* R1641  - LHPF2MIX Input 1 Volume */
	{ 0x066A, 0x0000 },   /* R1642  - LHPF2MIX Input 2 Source */
	{ 0x066B, 0x0080 },   /* R1643  - LHPF2MIX Input 2 Volume */
	{ 0x066C, 0x0000 },   /* R1644  - LHPF2MIX Input 3 Source */
	{ 0x066D, 0x0080 },   /* R1645  - LHPF2MIX Input 3 Volume */
	{ 0x066E, 0x0000 },   /* R1646  - LHPF2MIX Input 4 Source */
	{ 0x066F, 0x0080 },   /* R1647  - LHPF2MIX Input 4 Volume */
	{ 0x0670, 0x0000 },   /* R1648  - DSP1LMIX Input 1 Source */
	{ 0x0671, 0x0080 },   /* R1649  - DSP1LMIX Input 1 Volume */
	{ 0x0672, 0x0000 },   /* R1650  - DSP1LMIX Input 2 Source */
	{ 0x0673, 0x0080 },   /* R1651  - DSP1LMIX Input 2 Volume */
	{ 0x0674, 0x0000 },   /* R1652  - DSP1LMIX Input 3 Source */
	{ 0x0675, 0x0080 },   /* R1653  - DSP1LMIX Input 3 Volume */
	{ 0x0676, 0x0000 },   /* R1654  - DSP1LMIX Input 4 Source */
	{ 0x0677, 0x0080 },   /* R1655  - DSP1LMIX Input 4 Volume */
	{ 0x0678, 0x0000 },   /* R1656  - DSP1RMIX Input 1 Source */
	{ 0x0679, 0x0080 },   /* R1657  - DSP1RMIX Input 1 Volume */
	{ 0x067A, 0x0000 },   /* R1658  - DSP1RMIX Input 2 Source */
	{ 0x067B, 0x0080 },   /* R1659  - DSP1RMIX Input 2 Volume */
	{ 0x067C, 0x0000 },   /* R1660  - DSP1RMIX Input 3 Source */
	{ 0x067D, 0x0080 },   /* R1661  - DSP1RMIX Input 3 Volume */
	{ 0x067E, 0x0000 },   /* R1662  - DSP1RMIX Input 4 Source */
	{ 0x067F, 0x0080 },   /* R1663  - DSP1RMIX Input 4 Volume */
	{ 0x0680, 0x0000 },   /* R1664  - DSP1AUX1MIX Input 1 Source */
	{ 0x0681, 0x0000 },   /* R1665  - DSP1AUX2MIX Input 1 Source */
	{ 0x0682, 0x0000 },   /* R1666  - DSP1AUX3MIX Input 1 Source */
	{ 0x0683, 0x0000 },   /* R1667  - DSP1AUX4MIX Input 1 Source */
	{ 0x0684, 0x0000 },   /* R1668  - DSP1AUX5MIX Input 1 Source */
	{ 0x0685, 0x0000 },   /* R1669  - DSP1AUX6MIX Input 1 Source */
	{ 0x0686, 0x0000 },   /* R1670  - DSP2LMIX Input 1 Source */
	{ 0x0687, 0x0080 },   /* R1671  - DSP2LMIX Input 1 Volume */
	{ 0x0688, 0x0000 },   /* R1672  - DSP2LMIX Input 2 Source */
	{ 0x0689, 0x0080 },   /* R1673  - DSP2LMIX Input 2 Volume */
	{ 0x068A, 0x0000 },   /* R1674  - DSP2LMIX Input 3 Source */
	{ 0x068B, 0x0080 },   /* R1675  - DSP2LMIX Input 3 Volume */
	{ 0x068C, 0x0000 },   /* R1676  - DSP2LMIX Input 4 Source */
	{ 0x068D, 0x0080 },   /* R1677  - DSP2LMIX Input 4 Volume */
	{ 0x068E, 0x0000 },   /* R1678  - DSP2RMIX Input 1 Source */
	{ 0x068F, 0x0080 },   /* R1679  - DSP2RMIX Input 1 Volume */
	{ 0x0690, 0x0000 },   /* R1680  - DSP2RMIX Input 2 Source */
	{ 0x0691, 0x0080 },   /* R1681  - DSP2RMIX Input 2 Volume */
	{ 0x0692, 0x0000 },   /* R1682  - DSP2RMIX Input 3 Source */
	{ 0x0693, 0x0080 },   /* R1683  - DSP2RMIX Input 3 Volume */
	{ 0x0694, 0x0000 },   /* R1684  - DSP2RMIX Input 4 Source */
	{ 0x0695, 0x0080 },   /* R1685  - DSP2RMIX Input 4 Volume */
	{ 0x0696, 0x0000 },   /* R1686  - DSP2AUX1MIX Input 1 Source */
	{ 0x0697, 0x0000 },   /* R1687  - DSP2AUX2MIX Input 1 Source */
	{ 0x0698, 0x0000 },   /* R1688  - DSP2AUX3MIX Input 1 Source */
	{ 0x0699, 0x0000 },   /* R1689  - DSP2AUX4MIX Input 1 Source */
	{ 0x069A, 0x0000 },   /* R1690  - DSP2AUX5MIX Input 1 Source */
	{ 0x069B, 0x0000 },   /* R1691  - DSP2AUX6MIX Input 1 Source */
	{ 0x0700, 0xA101 },   /* R1792  - GPIO CTRL 1 */
	{ 0x0701, 0xA101 },   /* R1793  - GPIO CTRL 2 */
	{ 0x0702, 0xA101 },   /* R1794  - GPIO CTRL 3 */
	{ 0x0703, 0xA101 },   /* R1795  - GPIO CTRL 4 */
	{ 0x0709, 0x0000 },   /* R1801  - Misc Pad Ctrl 1 */
	{ 0x0801, 0x00FF },   /* R2049  - Interrupt Status 1 Mask */
	{ 0x0804, 0xFFFF },   /* R2052  - Interrupt Status 2 Mask */
	{ 0x0808, 0x0000 },   /* R2056  - Interrupt Control */
	{ 0x0900, 0x0000 },   /* R2304  - EQL_1 */
	{ 0x0901, 0x0000 },   /* R2305  - EQL_2 */
	{ 0x0902, 0x0000 },   /* R2306  - EQL_3 */
	{ 0x0903, 0x0000 },   /* R2307  - EQL_4 */
	{ 0x0904, 0x0000 },   /* R2308  - EQL_5 */
	{ 0x0905, 0x0000 },   /* R2309  - EQL_6 */
	{ 0x0906, 0x0000 },   /* R2310  - EQL_7 */
	{ 0x0907, 0x0000 },   /* R2311  - EQL_8 */
	{ 0x0908, 0x0000 },   /* R2312  - EQL_9 */
	{ 0x0909, 0x0000 },   /* R2313  - EQL_10 */
	{ 0x090A, 0x0000 },   /* R2314  - EQL_11 */
	{ 0x090B, 0x0000 },   /* R2315  - EQL_12 */
	{ 0x090C, 0x0000 },   /* R2316  - EQL_13 */
	{ 0x090D, 0x0000 },   /* R2317  - EQL_14 */
	{ 0x090E, 0x0000 },   /* R2318  - EQL_15 */
	{ 0x090F, 0x0000 },   /* R2319  - EQL_16 */
	{ 0x0910, 0x0000 },   /* R2320  - EQL_17 */
	{ 0x0911, 0x0000 },   /* R2321  - EQL_18 */
	{ 0x0912, 0x0000 },   /* R2322  - EQL_19 */
	{ 0x0913, 0x0000 },   /* R2323  - EQL_20 */
	{ 0x0916, 0x0000 },   /* R2326  - EQR_1 */
	{ 0x0917, 0x0000 },   /* R2327  - EQR_2 */
	{ 0x0918, 0x0000 },   /* R2328  - EQR_3 */
	{ 0x0919, 0x0000 },   /* R2329  - EQR_4 */
	{ 0x091A, 0x0000 },   /* R2330  - EQR_5 */
	{ 0x091B, 0x0000 },   /* R2331  - EQR_6 */
	{ 0x091C, 0x0000 },   /* R2332  - EQR_7 */
	{ 0x091D, 0x0000 },   /* R2333  - EQR_8 */
	{ 0x091E, 0x0000 },   /* R2334  - EQR_9 */
	{ 0x091F, 0x0000 },   /* R2335  - EQR_10 */
	{ 0x0920, 0x0000 },   /* R2336  - EQR_11 */
	{ 0x0921, 0x0000 },   /* R2337  - EQR_12 */
	{ 0x0922, 0x0000 },   /* R2338  - EQR_13 */
	{ 0x0923, 0x0000 },   /* R2339  - EQR_14 */
	{ 0x0924, 0x0000 },   /* R2340  - EQR_15 */
	{ 0x0925, 0x0000 },   /* R2341  - EQR_16 */
	{ 0x0926, 0x0000 },   /* R2342  - EQR_17 */
	{ 0x0927, 0x0000 },   /* R2343  - EQR_18 */
	{ 0x0928, 0x0000 },   /* R2344  - EQR_19 */
	{ 0x0929, 0x0000 },   /* R2345  - EQR_20 */
	{ 0x093E, 0x0000 },   /* R2366  - HPLPF1_1 */
	{ 0x093F, 0x0000 },   /* R2367  - HPLPF1_2 */
	{ 0x0942, 0x0000 },   /* R2370  - HPLPF2_1 */
	{ 0x0943, 0x0000 },   /* R2371  - HPLPF2_2 */
	{ 0x0A00, 0x0000 },   /* R2560  - DSP1 Control 1 */
	{ 0x0A02, 0x0000 },   /* R2562  - DSP1 Control 2 */
	{ 0x0A03, 0x0000 },   /* R2563  - DSP1 Control 3 */
	{ 0x0A04, 0x0000 },   /* R2564  - DSP1 Control 4 */
	{ 0x0A06, 0x0000 },   /* R2566  - DSP1 Control 5 */
	{ 0x0A07, 0x0000 },   /* R2567  - DSP1 Control 6 */
	{ 0x0A08, 0x0000 },   /* R2568  - DSP1 Control 7 */
	{ 0x0A09, 0x0000 },   /* R2569  - DSP1 Control 8 */
	{ 0x0A0A, 0x0000 },   /* R2570  - DSP1 Control 9 */
	{ 0x0A0B, 0x0000 },   /* R2571  - DSP1 Control 10 */
	{ 0x0A0C, 0x0000 },   /* R2572  - DSP1 Control 11 */
	{ 0x0A0D, 0x0000 },   /* R2573  - DSP1 Control 12 */
	{ 0x0A0F, 0x0000 },   /* R2575  - DSP1 Control 13 */
	{ 0x0A10, 0x0000 },   /* R2576  - DSP1 Control 14 */
	{ 0x0A11, 0x0000 },   /* R2577  - DSP1 Control 15 */
	{ 0x0A12, 0x0000 },   /* R2578  - DSP1 Control 16 */
	{ 0x0A13, 0x0000 },   /* R2579  - DSP1 Control 17 */
	{ 0x0A14, 0x0000 },   /* R2580  - DSP1 Control 18 */
	{ 0x0A16, 0x0000 },   /* R2582  - DSP1 Control 19 */
	{ 0x0A17, 0x0000 },   /* R2583  - DSP1 Control 20 */
	{ 0x0A18, 0x0000 },   /* R2584  - DSP1 Control 21 */
	{ 0x0A1A, 0x1800 },   /* R2586  - DSP1 Control 22 */
	{ 0x0A1B, 0x1000 },   /* R2587  - DSP1 Control 23 */
	{ 0x0A1C, 0x0400 },   /* R2588  - DSP1 Control 24 */
	{ 0x0A1E, 0x0000 },   /* R2590  - DSP1 Control 25 */
	{ 0x0A20, 0x0000 },   /* R2592  - DSP1 Control 26 */
	{ 0x0A21, 0x0000 },   /* R2593  - DSP1 Control 27 */
	{ 0x0A22, 0x0000 },   /* R2594  - DSP1 Control 28 */
	{ 0x0A23, 0x0000 },   /* R2595  - DSP1 Control 29 */
	{ 0x0A24, 0x0000 },   /* R2596  - DSP1 Control 30 */
	{ 0x0A26, 0x0000 },   /* R2598  - DSP1 Control 31 */
	{ 0x0B00, 0x0000 },   /* R2816  - DSP2 Control 1 */
	{ 0x0B02, 0x0000 },   /* R2818  - DSP2 Control 2 */
	{ 0x0B03, 0x0000 },   /* R2819  - DSP2 Control 3 */
	{ 0x0B04, 0x0000 },   /* R2820  - DSP2 Control 4 */
	{ 0x0B06, 0x0000 },   /* R2822  - DSP2 Control 5 */
	{ 0x0B07, 0x0000 },   /* R2823  - DSP2 Control 6 */
	{ 0x0B08, 0x0000 },   /* R2824  - DSP2 Control 7 */
	{ 0x0B09, 0x0000 },   /* R2825  - DSP2 Control 8 */
	{ 0x0B0A, 0x0000 },   /* R2826  - DSP2 Control 9 */
	{ 0x0B0B, 0x0000 },   /* R2827  - DSP2 Control 10 */
	{ 0x0B0C, 0x0000 },   /* R2828  - DSP2 Control 11 */
	{ 0x0B0D, 0x0000 },   /* R2829  - DSP2 Control 12 */
	{ 0x0B0F, 0x0000 },   /* R2831  - DSP2 Control 13 */
	{ 0x0B10, 0x0000 },   /* R2832  - DSP2 Control 14 */
	{ 0x0B11, 0x0000 },   /* R2833  - DSP2 Control 15 */
	{ 0x0B12, 0x0000 },   /* R2834  - DSP2 Control 16 */
	{ 0x0B13, 0x0000 },   /* R2835  - DSP2 Control 17 */
	{ 0x0B14, 0x0000 },   /* R2836  - DSP2 Control 18 */
	{ 0x0B16, 0x0000 },   /* R2838  - DSP2 Control 19 */
	{ 0x0B17, 0x0000 },   /* R2839  - DSP2 Control 20 */
	{ 0x0B18, 0x0000 },   /* R2840  - DSP2 Control 21 */
	{ 0x0B1A, 0x0800 },   /* R2842  - DSP2 Control 22 */
	{ 0x0B1B, 0x1000 },   /* R2843  - DSP2 Control 23 */
	{ 0x0B1C, 0x0400 },   /* R2844  - DSP2 Control 24 */
	{ 0x0B1E, 0x0000 },   /* R2846  - DSP2 Control 25 */
	{ 0x0B20, 0x0000 },   /* R2848  - DSP2 Control 26 */
	{ 0x0B21, 0x0000 },   /* R2849  - DSP2 Control 27 */
	{ 0x0B22, 0x0000 },   /* R2850  - DSP2 Control 28 */
	{ 0x0B23, 0x0000 },   /* R2851  - DSP2 Control 29 */
	{ 0x0B24, 0x0000 },   /* R2852  - DSP2 Control 30 */
	{ 0x0B26, 0x0000 },   /* R2854  - DSP2 Control 31 */
};

static bool wm2200_volatile_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wm2200_ranges); i++)
		if ((reg >= wm2200_ranges[i].window_start &&
		     reg <= wm2200_ranges[i].window_start +
		     wm2200_ranges[i].window_len) ||
		    (reg >= wm2200_ranges[i].range_min &&
		     reg <= wm2200_ranges[i].range_max))
			return true;

	switch (reg) {
	case WM2200_SOFTWARE_RESET:
	case WM2200_DEVICE_REVISION:
	case WM2200_ADPS1_IRQ0:
	case WM2200_ADPS1_IRQ1:
	case WM2200_INTERRUPT_STATUS_1:
	case WM2200_INTERRUPT_STATUS_2:
	case WM2200_INTERRUPT_RAW_STATUS_2:
		return true;
	default:
		return false;
	}
}

static bool wm2200_readable_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wm2200_ranges); i++)
		if ((reg >= wm2200_ranges[i].window_start &&
		     reg <= wm2200_ranges[i].window_start +
		     wm2200_ranges[i].window_len) ||
		    (reg >= wm2200_ranges[i].range_min &&
		     reg <= wm2200_ranges[i].range_max))
			return true;

	switch (reg) {
	case WM2200_SOFTWARE_RESET:
	case WM2200_DEVICE_REVISION:
	case WM2200_TONE_GENERATOR_1:
	case WM2200_CLOCKING_3:
	case WM2200_CLOCKING_4:
	case WM2200_FLL_CONTROL_1:
	case WM2200_FLL_CONTROL_2:
	case WM2200_FLL_CONTROL_3:
	case WM2200_FLL_CONTROL_4:
	case WM2200_FLL_CONTROL_6:
	case WM2200_FLL_CONTROL_7:
	case WM2200_FLL_EFS_1:
	case WM2200_FLL_EFS_2:
	case WM2200_MIC_CHARGE_PUMP_1:
	case WM2200_MIC_CHARGE_PUMP_2:
	case WM2200_DM_CHARGE_PUMP_1:
	case WM2200_MIC_BIAS_CTRL_1:
	case WM2200_MIC_BIAS_CTRL_2:
	case WM2200_EAR_PIECE_CTRL_1:
	case WM2200_EAR_PIECE_CTRL_2:
	case WM2200_INPUT_ENABLES:
	case WM2200_IN1L_CONTROL:
	case WM2200_IN1R_CONTROL:
	case WM2200_IN2L_CONTROL:
	case WM2200_IN2R_CONTROL:
	case WM2200_IN3L_CONTROL:
	case WM2200_IN3R_CONTROL:
	case WM2200_RXANC_SRC:
	case WM2200_INPUT_VOLUME_RAMP:
	case WM2200_ADC_DIGITAL_VOLUME_1L:
	case WM2200_ADC_DIGITAL_VOLUME_1R:
	case WM2200_ADC_DIGITAL_VOLUME_2L:
	case WM2200_ADC_DIGITAL_VOLUME_2R:
	case WM2200_ADC_DIGITAL_VOLUME_3L:
	case WM2200_ADC_DIGITAL_VOLUME_3R:
	case WM2200_OUTPUT_ENABLES:
	case WM2200_DAC_VOLUME_LIMIT_1L:
	case WM2200_DAC_VOLUME_LIMIT_1R:
	case WM2200_DAC_VOLUME_LIMIT_2L:
	case WM2200_DAC_VOLUME_LIMIT_2R:
	case WM2200_DAC_AEC_CONTROL_1:
	case WM2200_OUTPUT_VOLUME_RAMP:
	case WM2200_DAC_DIGITAL_VOLUME_1L:
	case WM2200_DAC_DIGITAL_VOLUME_1R:
	case WM2200_DAC_DIGITAL_VOLUME_2L:
	case WM2200_DAC_DIGITAL_VOLUME_2R:
	case WM2200_PDM_1:
	case WM2200_PDM_2:
	case WM2200_AUDIO_IF_1_1:
	case WM2200_AUDIO_IF_1_2:
	case WM2200_AUDIO_IF_1_3:
	case WM2200_AUDIO_IF_1_4:
	case WM2200_AUDIO_IF_1_5:
	case WM2200_AUDIO_IF_1_6:
	case WM2200_AUDIO_IF_1_7:
	case WM2200_AUDIO_IF_1_8:
	case WM2200_AUDIO_IF_1_9:
	case WM2200_AUDIO_IF_1_10:
	case WM2200_AUDIO_IF_1_11:
	case WM2200_AUDIO_IF_1_12:
	case WM2200_AUDIO_IF_1_13:
	case WM2200_AUDIO_IF_1_14:
	case WM2200_AUDIO_IF_1_15:
	case WM2200_AUDIO_IF_1_16:
	case WM2200_AUDIO_IF_1_17:
	case WM2200_AUDIO_IF_1_18:
	case WM2200_AUDIO_IF_1_19:
	case WM2200_AUDIO_IF_1_20:
	case WM2200_AUDIO_IF_1_21:
	case WM2200_AUDIO_IF_1_22:
	case WM2200_OUT1LMIX_INPUT_1_SOURCE:
	case WM2200_OUT1LMIX_INPUT_1_VOLUME:
	case WM2200_OUT1LMIX_INPUT_2_SOURCE:
	case WM2200_OUT1LMIX_INPUT_2_VOLUME:
	case WM2200_OUT1LMIX_INPUT_3_SOURCE:
	case WM2200_OUT1LMIX_INPUT_3_VOLUME:
	case WM2200_OUT1LMIX_INPUT_4_SOURCE:
	case WM2200_OUT1LMIX_INPUT_4_VOLUME:
	case WM2200_OUT1RMIX_INPUT_1_SOURCE:
	case WM2200_OUT1RMIX_INPUT_1_VOLUME:
	case WM2200_OUT1RMIX_INPUT_2_SOURCE:
	case WM2200_OUT1RMIX_INPUT_2_VOLUME:
	case WM2200_OUT1RMIX_INPUT_3_SOURCE:
	case WM2200_OUT1RMIX_INPUT_3_VOLUME:
	case WM2200_OUT1RMIX_INPUT_4_SOURCE:
	case WM2200_OUT1RMIX_INPUT_4_VOLUME:
	case WM2200_OUT2LMIX_INPUT_1_SOURCE:
	case WM2200_OUT2LMIX_INPUT_1_VOLUME:
	case WM2200_OUT2LMIX_INPUT_2_SOURCE:
	case WM2200_OUT2LMIX_INPUT_2_VOLUME:
	case WM2200_OUT2LMIX_INPUT_3_SOURCE:
	case WM2200_OUT2LMIX_INPUT_3_VOLUME:
	case WM2200_OUT2LMIX_INPUT_4_SOURCE:
	case WM2200_OUT2LMIX_INPUT_4_VOLUME:
	case WM2200_OUT2RMIX_INPUT_1_SOURCE:
	case WM2200_OUT2RMIX_INPUT_1_VOLUME:
	case WM2200_OUT2RMIX_INPUT_2_SOURCE:
	case WM2200_OUT2RMIX_INPUT_2_VOLUME:
	case WM2200_OUT2RMIX_INPUT_3_SOURCE:
	case WM2200_OUT2RMIX_INPUT_3_VOLUME:
	case WM2200_OUT2RMIX_INPUT_4_SOURCE:
	case WM2200_OUT2RMIX_INPUT_4_VOLUME:
	case WM2200_AIF1TX1MIX_INPUT_1_SOURCE:
	case WM2200_AIF1TX1MIX_INPUT_1_VOLUME:
	case WM2200_AIF1TX1MIX_INPUT_2_SOURCE:
	case WM2200_AIF1TX1MIX_INPUT_2_VOLUME:
	case WM2200_AIF1TX1MIX_INPUT_3_SOURCE:
	case WM2200_AIF1TX1MIX_INPUT_3_VOLUME:
	case WM2200_AIF1TX1MIX_INPUT_4_SOURCE:
	case WM2200_AIF1TX1MIX_INPUT_4_VOLUME:
	case WM2200_AIF1TX2MIX_INPUT_1_SOURCE:
	case WM2200_AIF1TX2MIX_INPUT_1_VOLUME:
	case WM2200_AIF1TX2MIX_INPUT_2_SOURCE:
	case WM2200_AIF1TX2MIX_INPUT_2_VOLUME:
	case WM2200_AIF1TX2MIX_INPUT_3_SOURCE:
	case WM2200_AIF1TX2MIX_INPUT_3_VOLUME:
	case WM2200_AIF1TX2MIX_INPUT_4_SOURCE:
	case WM2200_AIF1TX2MIX_INPUT_4_VOLUME:
	case WM2200_AIF1TX3MIX_INPUT_1_SOURCE:
	case WM2200_AIF1TX3MIX_INPUT_1_VOLUME:
	case WM2200_AIF1TX3MIX_INPUT_2_SOURCE:
	case WM2200_AIF1TX3MIX_INPUT_2_VOLUME:
	case WM2200_AIF1TX3MIX_INPUT_3_SOURCE:
	case WM2200_AIF1TX3MIX_INPUT_3_VOLUME:
	case WM2200_AIF1TX3MIX_INPUT_4_SOURCE:
	case WM2200_AIF1TX3MIX_INPUT_4_VOLUME:
	case WM2200_AIF1TX4MIX_INPUT_1_SOURCE:
	case WM2200_AIF1TX4MIX_INPUT_1_VOLUME:
	case WM2200_AIF1TX4MIX_INPUT_2_SOURCE:
	case WM2200_AIF1TX4MIX_INPUT_2_VOLUME:
	case WM2200_AIF1TX4MIX_INPUT_3_SOURCE:
	case WM2200_AIF1TX4MIX_INPUT_3_VOLUME:
	case WM2200_AIF1TX4MIX_INPUT_4_SOURCE:
	case WM2200_AIF1TX4MIX_INPUT_4_VOLUME:
	case WM2200_AIF1TX5MIX_INPUT_1_SOURCE:
	case WM2200_AIF1TX5MIX_INPUT_1_VOLUME:
	case WM2200_AIF1TX5MIX_INPUT_2_SOURCE:
	case WM2200_AIF1TX5MIX_INPUT_2_VOLUME:
	case WM2200_AIF1TX5MIX_INPUT_3_SOURCE:
	case WM2200_AIF1TX5MIX_INPUT_3_VOLUME:
	case WM2200_AIF1TX5MIX_INPUT_4_SOURCE:
	case WM2200_AIF1TX5MIX_INPUT_4_VOLUME:
	case WM2200_AIF1TX6MIX_INPUT_1_SOURCE:
	case WM2200_AIF1TX6MIX_INPUT_1_VOLUME:
	case WM2200_AIF1TX6MIX_INPUT_2_SOURCE:
	case WM2200_AIF1TX6MIX_INPUT_2_VOLUME:
	case WM2200_AIF1TX6MIX_INPUT_3_SOURCE:
	case WM2200_AIF1TX6MIX_INPUT_3_VOLUME:
	case WM2200_AIF1TX6MIX_INPUT_4_SOURCE:
	case WM2200_AIF1TX6MIX_INPUT_4_VOLUME:
	case WM2200_EQLMIX_INPUT_1_SOURCE:
	case WM2200_EQLMIX_INPUT_1_VOLUME:
	case WM2200_EQLMIX_INPUT_2_SOURCE:
	case WM2200_EQLMIX_INPUT_2_VOLUME:
	case WM2200_EQLMIX_INPUT_3_SOURCE:
	case WM2200_EQLMIX_INPUT_3_VOLUME:
	case WM2200_EQLMIX_INPUT_4_SOURCE:
	case WM2200_EQLMIX_INPUT_4_VOLUME:
	case WM2200_EQRMIX_INPUT_1_SOURCE:
	case WM2200_EQRMIX_INPUT_1_VOLUME:
	case WM2200_EQRMIX_INPUT_2_SOURCE:
	case WM2200_EQRMIX_INPUT_2_VOLUME:
	case WM2200_EQRMIX_INPUT_3_SOURCE:
	case WM2200_EQRMIX_INPUT_3_VOLUME:
	case WM2200_EQRMIX_INPUT_4_SOURCE:
	case WM2200_EQRMIX_INPUT_4_VOLUME:
	case WM2200_LHPF1MIX_INPUT_1_SOURCE:
	case WM2200_LHPF1MIX_INPUT_1_VOLUME:
	case WM2200_LHPF1MIX_INPUT_2_SOURCE:
	case WM2200_LHPF1MIX_INPUT_2_VOLUME:
	case WM2200_LHPF1MIX_INPUT_3_SOURCE:
	case WM2200_LHPF1MIX_INPUT_3_VOLUME:
	case WM2200_LHPF1MIX_INPUT_4_SOURCE:
	case WM2200_LHPF1MIX_INPUT_4_VOLUME:
	case WM2200_LHPF2MIX_INPUT_1_SOURCE:
	case WM2200_LHPF2MIX_INPUT_1_VOLUME:
	case WM2200_LHPF2MIX_INPUT_2_SOURCE:
	case WM2200_LHPF2MIX_INPUT_2_VOLUME:
	case WM2200_LHPF2MIX_INPUT_3_SOURCE:
	case WM2200_LHPF2MIX_INPUT_3_VOLUME:
	case WM2200_LHPF2MIX_INPUT_4_SOURCE:
	case WM2200_LHPF2MIX_INPUT_4_VOLUME:
	case WM2200_DSP1LMIX_INPUT_1_SOURCE:
	case WM2200_DSP1LMIX_INPUT_1_VOLUME:
	case WM2200_DSP1LMIX_INPUT_2_SOURCE:
	case WM2200_DSP1LMIX_INPUT_2_VOLUME:
	case WM2200_DSP1LMIX_INPUT_3_SOURCE:
	case WM2200_DSP1LMIX_INPUT_3_VOLUME:
	case WM2200_DSP1LMIX_INPUT_4_SOURCE:
	case WM2200_DSP1LMIX_INPUT_4_VOLUME:
	case WM2200_DSP1RMIX_INPUT_1_SOURCE:
	case WM2200_DSP1RMIX_INPUT_1_VOLUME:
	case WM2200_DSP1RMIX_INPUT_2_SOURCE:
	case WM2200_DSP1RMIX_INPUT_2_VOLUME:
	case WM2200_DSP1RMIX_INPUT_3_SOURCE:
	case WM2200_DSP1RMIX_INPUT_3_VOLUME:
	case WM2200_DSP1RMIX_INPUT_4_SOURCE:
	case WM2200_DSP1RMIX_INPUT_4_VOLUME:
	case WM2200_DSP1AUX1MIX_INPUT_1_SOURCE:
	case WM2200_DSP1AUX2MIX_INPUT_1_SOURCE:
	case WM2200_DSP1AUX3MIX_INPUT_1_SOURCE:
	case WM2200_DSP1AUX4MIX_INPUT_1_SOURCE:
	case WM2200_DSP1AUX5MIX_INPUT_1_SOURCE:
	case WM2200_DSP1AUX6MIX_INPUT_1_SOURCE:
	case WM2200_DSP2LMIX_INPUT_1_SOURCE:
	case WM2200_DSP2LMIX_INPUT_1_VOLUME:
	case WM2200_DSP2LMIX_INPUT_2_SOURCE:
	case WM2200_DSP2LMIX_INPUT_2_VOLUME:
	case WM2200_DSP2LMIX_INPUT_3_SOURCE:
	case WM2200_DSP2LMIX_INPUT_3_VOLUME:
	case WM2200_DSP2LMIX_INPUT_4_SOURCE:
	case WM2200_DSP2LMIX_INPUT_4_VOLUME:
	case WM2200_DSP2RMIX_INPUT_1_SOURCE:
	case WM2200_DSP2RMIX_INPUT_1_VOLUME:
	case WM2200_DSP2RMIX_INPUT_2_SOURCE:
	case WM2200_DSP2RMIX_INPUT_2_VOLUME:
	case WM2200_DSP2RMIX_INPUT_3_SOURCE:
	case WM2200_DSP2RMIX_INPUT_3_VOLUME:
	case WM2200_DSP2RMIX_INPUT_4_SOURCE:
	case WM2200_DSP2RMIX_INPUT_4_VOLUME:
	case WM2200_DSP2AUX1MIX_INPUT_1_SOURCE:
	case WM2200_DSP2AUX2MIX_INPUT_1_SOURCE:
	case WM2200_DSP2AUX3MIX_INPUT_1_SOURCE:
	case WM2200_DSP2AUX4MIX_INPUT_1_SOURCE:
	case WM2200_DSP2AUX5MIX_INPUT_1_SOURCE:
	case WM2200_DSP2AUX6MIX_INPUT_1_SOURCE:
	case WM2200_GPIO_CTRL_1:
	case WM2200_GPIO_CTRL_2:
	case WM2200_GPIO_CTRL_3:
	case WM2200_GPIO_CTRL_4:
	case WM2200_ADPS1_IRQ0:
	case WM2200_ADPS1_IRQ1:
	case WM2200_MISC_PAD_CTRL_1:
	case WM2200_INTERRUPT_STATUS_1:
	case WM2200_INTERRUPT_STATUS_1_MASK:
	case WM2200_INTERRUPT_STATUS_2:
	case WM2200_INTERRUPT_RAW_STATUS_2:
	case WM2200_INTERRUPT_STATUS_2_MASK:
	case WM2200_INTERRUPT_CONTROL:
	case WM2200_EQL_1:
	case WM2200_EQL_2:
	case WM2200_EQL_3:
	case WM2200_EQL_4:
	case WM2200_EQL_5:
	case WM2200_EQL_6:
	case WM2200_EQL_7:
	case WM2200_EQL_8:
	case WM2200_EQL_9:
	case WM2200_EQL_10:
	case WM2200_EQL_11:
	case WM2200_EQL_12:
	case WM2200_EQL_13:
	case WM2200_EQL_14:
	case WM2200_EQL_15:
	case WM2200_EQL_16:
	case WM2200_EQL_17:
	case WM2200_EQL_18:
	case WM2200_EQL_19:
	case WM2200_EQL_20:
	case WM2200_EQR_1:
	case WM2200_EQR_2:
	case WM2200_EQR_3:
	case WM2200_EQR_4:
	case WM2200_EQR_5:
	case WM2200_EQR_6:
	case WM2200_EQR_7:
	case WM2200_EQR_8:
	case WM2200_EQR_9:
	case WM2200_EQR_10:
	case WM2200_EQR_11:
	case WM2200_EQR_12:
	case WM2200_EQR_13:
	case WM2200_EQR_14:
	case WM2200_EQR_15:
	case WM2200_EQR_16:
	case WM2200_EQR_17:
	case WM2200_EQR_18:
	case WM2200_EQR_19:
	case WM2200_EQR_20:
	case WM2200_HPLPF1_1:
	case WM2200_HPLPF1_2:
	case WM2200_HPLPF2_1:
	case WM2200_HPLPF2_2:
	case WM2200_DSP1_CONTROL_1:
	case WM2200_DSP1_CONTROL_2:
	case WM2200_DSP1_CONTROL_3:
	case WM2200_DSP1_CONTROL_4:
	case WM2200_DSP1_CONTROL_5:
	case WM2200_DSP1_CONTROL_6:
	case WM2200_DSP1_CONTROL_7:
	case WM2200_DSP1_CONTROL_8:
	case WM2200_DSP1_CONTROL_9:
	case WM2200_DSP1_CONTROL_10:
	case WM2200_DSP1_CONTROL_11:
	case WM2200_DSP1_CONTROL_12:
	case WM2200_DSP1_CONTROL_13:
	case WM2200_DSP1_CONTROL_14:
	case WM2200_DSP1_CONTROL_15:
	case WM2200_DSP1_CONTROL_16:
	case WM2200_DSP1_CONTROL_17:
	case WM2200_DSP1_CONTROL_18:
	case WM2200_DSP1_CONTROL_19:
	case WM2200_DSP1_CONTROL_20:
	case WM2200_DSP1_CONTROL_21:
	case WM2200_DSP1_CONTROL_22:
	case WM2200_DSP1_CONTROL_23:
	case WM2200_DSP1_CONTROL_24:
	case WM2200_DSP1_CONTROL_25:
	case WM2200_DSP1_CONTROL_26:
	case WM2200_DSP1_CONTROL_27:
	case WM2200_DSP1_CONTROL_28:
	case WM2200_DSP1_CONTROL_29:
	case WM2200_DSP1_CONTROL_30:
	case WM2200_DSP1_CONTROL_31:
	case WM2200_DSP2_CONTROL_1:
	case WM2200_DSP2_CONTROL_2:
	case WM2200_DSP2_CONTROL_3:
	case WM2200_DSP2_CONTROL_4:
	case WM2200_DSP2_CONTROL_5:
	case WM2200_DSP2_CONTROL_6:
	case WM2200_DSP2_CONTROL_7:
	case WM2200_DSP2_CONTROL_8:
	case WM2200_DSP2_CONTROL_9:
	case WM2200_DSP2_CONTROL_10:
	case WM2200_DSP2_CONTROL_11:
	case WM2200_DSP2_CONTROL_12:
	case WM2200_DSP2_CONTROL_13:
	case WM2200_DSP2_CONTROL_14:
	case WM2200_DSP2_CONTROL_15:
	case WM2200_DSP2_CONTROL_16:
	case WM2200_DSP2_CONTROL_17:
	case WM2200_DSP2_CONTROL_18:
	case WM2200_DSP2_CONTROL_19:
	case WM2200_DSP2_CONTROL_20:
	case WM2200_DSP2_CONTROL_21:
	case WM2200_DSP2_CONTROL_22:
	case WM2200_DSP2_CONTROL_23:
	case WM2200_DSP2_CONTROL_24:
	case WM2200_DSP2_CONTROL_25:
	case WM2200_DSP2_CONTROL_26:
	case WM2200_DSP2_CONTROL_27:
	case WM2200_DSP2_CONTROL_28:
	case WM2200_DSP2_CONTROL_29:
	case WM2200_DSP2_CONTROL_30:
	case WM2200_DSP2_CONTROL_31:
		return true;
	default:
		return false;
	}
}

static const struct reg_default wm2200_reva_patch[] = {
	{ 0x07, 0x0003 },
	{ 0x102, 0x0200 },
	{ 0x203, 0x0084 },
	{ 0x201, 0x83FF },
	{ 0x20C, 0x0062 },
	{ 0x20D, 0x0062 },
	{ 0x207, 0x2002 },
	{ 0x208, 0x20C0 },
	{ 0x21D, 0x01C0 },
	{ 0x50A, 0x0001 },
	{ 0x50B, 0x0002 },
	{ 0x50C, 0x0003 },
	{ 0x50D, 0x0004 },
	{ 0x50E, 0x0005 },
	{ 0x510, 0x0001 },
	{ 0x511, 0x0002 },
	{ 0x512, 0x0003 },
	{ 0x513, 0x0004 },
	{ 0x514, 0x0005 },
	{ 0x515, 0x0000 },
	{ 0x201, 0x8084 },
	{ 0x202, 0xBBDE },
	{ 0x203, 0x00EC },
	{ 0x500, 0x8000 },
	{ 0x507, 0x1820 },
	{ 0x508, 0x1820 },
	{ 0x505, 0x0300 },
	{ 0x506, 0x0300 },
	{ 0x302, 0x2280 },
	{ 0x303, 0x0080 },
	{ 0x304, 0x2280 },
	{ 0x305, 0x0080 },
	{ 0x306, 0x2280 },
	{ 0x307, 0x0080 },
	{ 0x401, 0x0080 },
	{ 0x402, 0x0080 },
	{ 0x417, 0x3069 },
	{ 0x900, 0x6318 },
	{ 0x901, 0x6300 },
	{ 0x902, 0x0FC8 },
	{ 0x903, 0x03FE },
	{ 0x904, 0x00E0 },
	{ 0x905, 0x1EC4 },
	{ 0x906, 0xF136 },
	{ 0x907, 0x0409 },
	{ 0x908, 0x04CC },
	{ 0x909, 0x1C9B },
	{ 0x90A, 0xF337 },
	{ 0x90B, 0x040B },
	{ 0x90C, 0x0CBB },
	{ 0x90D, 0x16F8 },
	{ 0x90E, 0xF7D9 },
	{ 0x90F, 0x040A },
	{ 0x910, 0x1F14 },
	{ 0x911, 0x058C },
	{ 0x912, 0x0563 },
	{ 0x913, 0x4000 },
	{ 0x916, 0x6318 },
	{ 0x917, 0x6300 },
	{ 0x918, 0x0FC8 },
	{ 0x919, 0x03FE },
	{ 0x91A, 0x00E0 },
	{ 0x91B, 0x1EC4 },
	{ 0x91C, 0xF136 },
	{ 0x91D, 0x0409 },
	{ 0x91E, 0x04CC },
	{ 0x91F, 0x1C9B },
	{ 0x920, 0xF337 },
	{ 0x921, 0x040B },
	{ 0x922, 0x0CBB },
	{ 0x923, 0x16F8 },
	{ 0x924, 0xF7D9 },
	{ 0x925, 0x040A },
	{ 0x926, 0x1F14 },
	{ 0x927, 0x058C },
	{ 0x928, 0x0563 },
	{ 0x929, 0x4000 },
	{ 0x709, 0x2000 },
	{ 0x207, 0x200E },
	{ 0x208, 0x20D4 },
	{ 0x20A, 0x0080 },
	{ 0x07, 0x0000 },
};

static int wm2200_reset(struct wm2200_priv *wm2200)
{
	if (wm2200->pdata.reset) {
		gpio_set_value_cansleep(wm2200->pdata.reset, 0);
		gpio_set_value_cansleep(wm2200->pdata.reset, 1);

		return 0;
	} else {
		return regmap_write(wm2200->regmap, WM2200_SOFTWARE_RESET,
				    0x2200);
	}
}

static DECLARE_TLV_DB_SCALE(in_tlv, -6300, 100, 0);
static DECLARE_TLV_DB_SCALE(digital_tlv, -6400, 50, 0);
static DECLARE_TLV_DB_SCALE(out_tlv, -6400, 100, 0);

static const char *wm2200_mixer_texts[] = {
	"None",
	"Tone Generator",
	"AEC Loopback",
	"IN1L",
	"IN1R",
	"IN2L",
	"IN2R",
	"IN3L",
	"IN3R",
	"AIF1RX1",
	"AIF1RX2",
	"AIF1RX3",
	"AIF1RX4",
	"AIF1RX5",
	"AIF1RX6",
	"EQL",
	"EQR",
	"LHPF1",
	"LHPF2",
	"DSP1.1",
	"DSP1.2",
	"DSP1.3",
	"DSP1.4",
	"DSP1.5",
	"DSP1.6",
	"DSP2.1",
	"DSP2.2",
	"DSP2.3",
	"DSP2.4",
	"DSP2.5",
	"DSP2.6",
};

static int wm2200_mixer_values[] = {
	0x00,
	0x04,   /* Tone */
	0x08,   /* AEC */
	0x10,   /* Input */
	0x11,
	0x12,
	0x13,
	0x14,
	0x15,
	0x20,   /* AIF */
	0x21,
	0x22,
	0x23,
	0x24,
	0x25,
	0x50,   /* EQ */
	0x51,
	0x60,   /* LHPF1 */
	0x61,   /* LHPF2 */
	0x68,   /* DSP1 */
	0x69,
	0x6a,
	0x6b,
	0x6c,
	0x6d,
	0x70,   /* DSP2 */
	0x71,
	0x72,
	0x73,
	0x74,
	0x75,
};

#define WM2200_MIXER_CONTROLS(name, base) \
	SOC_SINGLE_TLV(name " Input 1 Volume", base + 1 , \
		       WM2200_MIXER_VOL_SHIFT, 80, 0, mixer_tlv), \
	SOC_SINGLE_TLV(name " Input 2 Volume", base + 3 , \
		       WM2200_MIXER_VOL_SHIFT, 80, 0, mixer_tlv), \
	SOC_SINGLE_TLV(name " Input 3 Volume", base + 5 , \
		       WM2200_MIXER_VOL_SHIFT, 80, 0, mixer_tlv), \
	SOC_SINGLE_TLV(name " Input 4 Volume", base + 7 , \
		       WM2200_MIXER_VOL_SHIFT, 80, 0, mixer_tlv)

#define WM2200_MUX_ENUM_DECL(name, reg) \
	SOC_VALUE_ENUM_SINGLE_DECL(name, reg, 0, 0xff, 			\
				   wm2200_mixer_texts, wm2200_mixer_values)

#define WM2200_MUX_CTL_DECL(name) \
	const struct snd_kcontrol_new name##_mux =	\
		SOC_DAPM_VALUE_ENUM("Route", name##_enum)

#define WM2200_MIXER_ENUMS(name, base_reg) \
	static WM2200_MUX_ENUM_DECL(name##_in1_enum, base_reg);	     \
	static WM2200_MUX_ENUM_DECL(name##_in2_enum, base_reg + 2);  \
	static WM2200_MUX_ENUM_DECL(name##_in3_enum, base_reg + 4);  \
	static WM2200_MUX_ENUM_DECL(name##_in4_enum, base_reg + 6);  \
	static WM2200_MUX_CTL_DECL(name##_in1); \
	static WM2200_MUX_CTL_DECL(name##_in2); \
	static WM2200_MUX_CTL_DECL(name##_in3); \
	static WM2200_MUX_CTL_DECL(name##_in4)

#define WM2200_DSP_ENUMS(name, base_reg) \
	static WM2200_MUX_ENUM_DECL(name##_aux1_enum, base_reg);     \
	static WM2200_MUX_ENUM_DECL(name##_aux2_enum, base_reg + 1); \
	static WM2200_MUX_ENUM_DECL(name##_aux3_enum, base_reg + 2); \
	static WM2200_MUX_ENUM_DECL(name##_aux4_enum, base_reg + 3); \
	static WM2200_MUX_ENUM_DECL(name##_aux5_enum, base_reg + 4); \
	static WM2200_MUX_ENUM_DECL(name##_aux6_enum, base_reg + 5); \
	static WM2200_MUX_CTL_DECL(name##_aux1); \
	static WM2200_MUX_CTL_DECL(name##_aux2); \
	static WM2200_MUX_CTL_DECL(name##_aux3); \
	static WM2200_MUX_CTL_DECL(name##_aux4); \
	static WM2200_MUX_CTL_DECL(name##_aux5); \
	static WM2200_MUX_CTL_DECL(name##_aux6);

static const char *wm2200_rxanc_input_sel_texts[] = {
	"None", "IN1", "IN2", "IN3",
};

static const struct soc_enum wm2200_rxanc_input_sel =
	SOC_ENUM_SINGLE(WM2200_RXANC_SRC,
			WM2200_IN_RXANC_SEL_SHIFT,
			ARRAY_SIZE(wm2200_rxanc_input_sel_texts),
			wm2200_rxanc_input_sel_texts);

static const struct snd_kcontrol_new wm2200_snd_controls[] = {
SOC_SINGLE("IN1 High Performance Switch", WM2200_IN1L_CONTROL,
	   WM2200_IN1_OSR_SHIFT, 1, 0),
SOC_SINGLE("IN2 High Performance Switch", WM2200_IN2L_CONTROL,
	   WM2200_IN2_OSR_SHIFT, 1, 0),
SOC_SINGLE("IN3 High Performance Switch", WM2200_IN3L_CONTROL,
	   WM2200_IN3_OSR_SHIFT, 1, 0),

SOC_DOUBLE_R_TLV("IN1 Volume", WM2200_IN1L_CONTROL, WM2200_IN1R_CONTROL,
		 WM2200_IN1L_PGA_VOL_SHIFT, 0x5f, 0, in_tlv),
SOC_DOUBLE_R_TLV("IN2 Volume", WM2200_IN2L_CONTROL, WM2200_IN2R_CONTROL,
		 WM2200_IN2L_PGA_VOL_SHIFT, 0x5f, 0, in_tlv),
SOC_DOUBLE_R_TLV("IN3 Volume", WM2200_IN3L_CONTROL, WM2200_IN3R_CONTROL,
		 WM2200_IN3L_PGA_VOL_SHIFT, 0x5f, 0, in_tlv),

SOC_DOUBLE_R("IN1 Digital Switch", WM2200_ADC_DIGITAL_VOLUME_1L,
	     WM2200_ADC_DIGITAL_VOLUME_1R, WM2200_IN1L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("IN2 Digital Switch", WM2200_ADC_DIGITAL_VOLUME_2L,
	     WM2200_ADC_DIGITAL_VOLUME_2R, WM2200_IN2L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("IN3 Digital Switch", WM2200_ADC_DIGITAL_VOLUME_3L,
	     WM2200_ADC_DIGITAL_VOLUME_3R, WM2200_IN3L_MUTE_SHIFT, 1, 1),

SOC_DOUBLE_R_TLV("IN1 Digital Volume", WM2200_ADC_DIGITAL_VOLUME_1L,
		 WM2200_ADC_DIGITAL_VOLUME_1R, WM2200_IN1L_DIG_VOL_SHIFT,
		 0xbf, 0, digital_tlv),
SOC_DOUBLE_R_TLV("IN2 Digital Volume", WM2200_ADC_DIGITAL_VOLUME_2L,
		 WM2200_ADC_DIGITAL_VOLUME_2R, WM2200_IN2L_DIG_VOL_SHIFT,
		 0xbf, 0, digital_tlv),
SOC_DOUBLE_R_TLV("IN3 Digital Volume", WM2200_ADC_DIGITAL_VOLUME_3L,
		 WM2200_ADC_DIGITAL_VOLUME_3R, WM2200_IN3L_DIG_VOL_SHIFT,
		 0xbf, 0, digital_tlv),

SND_SOC_BYTES_MASK("EQL Coefficients", WM2200_EQL_1, 20, WM2200_EQL_ENA),
SND_SOC_BYTES_MASK("EQR Coefficients", WM2200_EQR_1, 20, WM2200_EQR_ENA),

SND_SOC_BYTES("LHPF1 Coefficeints", WM2200_HPLPF1_2, 1),
SND_SOC_BYTES("LHPF2 Coefficeints", WM2200_HPLPF2_2, 1),

SOC_SINGLE("OUT1 High Performance Switch", WM2200_DAC_DIGITAL_VOLUME_1L,
	   WM2200_OUT1_OSR_SHIFT, 1, 0),
SOC_SINGLE("OUT2 High Performance Switch", WM2200_DAC_DIGITAL_VOLUME_2L,
	   WM2200_OUT2_OSR_SHIFT, 1, 0),

SOC_DOUBLE_R("OUT1 Digital Switch", WM2200_DAC_DIGITAL_VOLUME_1L,
	     WM2200_DAC_DIGITAL_VOLUME_1R, WM2200_OUT1L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R_TLV("OUT1 Digital Volume", WM2200_DAC_DIGITAL_VOLUME_1L,
		 WM2200_DAC_DIGITAL_VOLUME_1R, WM2200_OUT1L_VOL_SHIFT, 0x9f, 0,
		 digital_tlv),
SOC_DOUBLE_R_TLV("OUT1 Volume", WM2200_DAC_VOLUME_LIMIT_1L,
		 WM2200_DAC_VOLUME_LIMIT_1R, WM2200_OUT1L_PGA_VOL_SHIFT,
		 0x46, 0, out_tlv),

SOC_DOUBLE_R("OUT2 Digital Switch", WM2200_DAC_DIGITAL_VOLUME_2L,
	     WM2200_DAC_DIGITAL_VOLUME_2R, WM2200_OUT2L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R_TLV("OUT2 Digital Volume", WM2200_DAC_DIGITAL_VOLUME_2L,
		 WM2200_DAC_DIGITAL_VOLUME_2R, WM2200_OUT2L_VOL_SHIFT, 0x9f, 0,
		 digital_tlv),
SOC_DOUBLE("OUT2 Switch", WM2200_PDM_1, WM2200_SPK1L_MUTE_SHIFT,
	   WM2200_SPK1R_MUTE_SHIFT, 1, 1),
SOC_ENUM("RxANC Src", wm2200_rxanc_input_sel),
};

WM2200_MIXER_ENUMS(OUT1L, WM2200_OUT1LMIX_INPUT_1_SOURCE);
WM2200_MIXER_ENUMS(OUT1R, WM2200_OUT1RMIX_INPUT_1_SOURCE);
WM2200_MIXER_ENUMS(OUT2L, WM2200_OUT2LMIX_INPUT_1_SOURCE);
WM2200_MIXER_ENUMS(OUT2R, WM2200_OUT2RMIX_INPUT_1_SOURCE);

WM2200_MIXER_ENUMS(AIF1TX1, WM2200_AIF1TX1MIX_INPUT_1_SOURCE);
WM2200_MIXER_ENUMS(AIF1TX2, WM2200_AIF1TX2MIX_INPUT_1_SOURCE);
WM2200_MIXER_ENUMS(AIF1TX3, WM2200_AIF1TX3MIX_INPUT_1_SOURCE);
WM2200_MIXER_ENUMS(AIF1TX4, WM2200_AIF1TX4MIX_INPUT_1_SOURCE);
WM2200_MIXER_ENUMS(AIF1TX5, WM2200_AIF1TX5MIX_INPUT_1_SOURCE);
WM2200_MIXER_ENUMS(AIF1TX6, WM2200_AIF1TX6MIX_INPUT_1_SOURCE);

WM2200_MIXER_ENUMS(EQL, WM2200_EQLMIX_INPUT_1_SOURCE);
WM2200_MIXER_ENUMS(EQR, WM2200_EQRMIX_INPUT_1_SOURCE);

WM2200_MIXER_ENUMS(DSP1L, WM2200_DSP1LMIX_INPUT_1_SOURCE);
WM2200_MIXER_ENUMS(DSP1R, WM2200_DSP1RMIX_INPUT_1_SOURCE);
WM2200_MIXER_ENUMS(DSP2L, WM2200_DSP2LMIX_INPUT_1_SOURCE);
WM2200_MIXER_ENUMS(DSP2R, WM2200_DSP2RMIX_INPUT_1_SOURCE);

WM2200_DSP_ENUMS(DSP1, WM2200_DSP1AUX1MIX_INPUT_1_SOURCE);
WM2200_DSP_ENUMS(DSP2, WM2200_DSP2AUX1MIX_INPUT_1_SOURCE);

WM2200_MIXER_ENUMS(LHPF1, WM2200_LHPF1MIX_INPUT_1_SOURCE);
WM2200_MIXER_ENUMS(LHPF2, WM2200_LHPF2MIX_INPUT_1_SOURCE);

#define WM2200_MUX(name, ctrl) \
	SND_SOC_DAPM_VALUE_MUX(name, SND_SOC_NOPM, 0, 0, ctrl)

#define WM2200_MIXER_WIDGETS(name, name_str)	\
	WM2200_MUX(name_str " Input 1", &name##_in1_mux), \
	WM2200_MUX(name_str " Input 2", &name##_in2_mux), \
	WM2200_MUX(name_str " Input 3", &name##_in3_mux), \
	WM2200_MUX(name_str " Input 4", &name##_in4_mux), \
	SND_SOC_DAPM_MIXER(name_str " Mixer", SND_SOC_NOPM, 0, 0, NULL, 0)

#define WM2200_DSP_WIDGETS(name, name_str) \
	WM2200_MIXER_WIDGETS(name##L, name_str "L"), \
	WM2200_MIXER_WIDGETS(name##R, name_str "R"), \
	WM2200_MUX(name_str " Aux 1", &name##_aux1_mux), \
	WM2200_MUX(name_str " Aux 2", &name##_aux2_mux), \
	WM2200_MUX(name_str " Aux 3", &name##_aux3_mux), \
	WM2200_MUX(name_str " Aux 4", &name##_aux4_mux), \
	WM2200_MUX(name_str " Aux 5", &name##_aux5_mux), \
	WM2200_MUX(name_str " Aux 6", &name##_aux6_mux)

#define WM2200_MIXER_INPUT_ROUTES(name)	\
	{ name, "Tone Generator", "Tone Generator" }, \
	{ name, "AEC Loopback", "AEC Loopback" }, \
        { name, "IN1L", "IN1L PGA" }, \
        { name, "IN1R", "IN1R PGA" }, \
        { name, "IN2L", "IN2L PGA" }, \
        { name, "IN2R", "IN2R PGA" }, \
        { name, "IN3L", "IN3L PGA" }, \
        { name, "IN3R", "IN3R PGA" }, \
        { name, "DSP1.1", "DSP1" }, \
        { name, "DSP1.2", "DSP1" }, \
        { name, "DSP1.3", "DSP1" }, \
        { name, "DSP1.4", "DSP1" }, \
        { name, "DSP1.5", "DSP1" }, \
        { name, "DSP1.6", "DSP1" }, \
        { name, "DSP2.1", "DSP2" }, \
        { name, "DSP2.2", "DSP2" }, \
        { name, "DSP2.3", "DSP2" }, \
        { name, "DSP2.4", "DSP2" }, \
        { name, "DSP2.5", "DSP2" }, \
        { name, "DSP2.6", "DSP2" }, \
        { name, "AIF1RX1", "AIF1RX1" }, \
        { name, "AIF1RX2", "AIF1RX2" }, \
        { name, "AIF1RX3", "AIF1RX3" }, \
        { name, "AIF1RX4", "AIF1RX4" }, \
        { name, "AIF1RX5", "AIF1RX5" }, \
        { name, "AIF1RX6", "AIF1RX6" }, \
        { name, "EQL", "EQL" }, \
        { name, "EQR", "EQR" }, \
        { name, "LHPF1", "LHPF1" }, \
        { name, "LHPF2", "LHPF2" }

#define WM2200_MIXER_ROUTES(widget, name) \
	{ widget, NULL, name " Mixer" },         \
	{ name " Mixer", NULL, name " Input 1" }, \
	{ name " Mixer", NULL, name " Input 2" }, \
	{ name " Mixer", NULL, name " Input 3" }, \
	{ name " Mixer", NULL, name " Input 4" }, \
	WM2200_MIXER_INPUT_ROUTES(name " Input 1"), \
	WM2200_MIXER_INPUT_ROUTES(name " Input 2"), \
	WM2200_MIXER_INPUT_ROUTES(name " Input 3"), \
	WM2200_MIXER_INPUT_ROUTES(name " Input 4")

#define WM2200_DSP_AUX_ROUTES(name) \
	{ name, NULL, name " Aux 1" }, \
	{ name, NULL, name " Aux 2" }, \
	{ name, NULL, name " Aux 3" }, \
	{ name, NULL, name " Aux 4" }, \
	{ name, NULL, name " Aux 5" }, \
	{ name, NULL, name " Aux 6" }, \
	WM2200_MIXER_INPUT_ROUTES(name " Aux 1"), \
	WM2200_MIXER_INPUT_ROUTES(name " Aux 2"), \
	WM2200_MIXER_INPUT_ROUTES(name " Aux 3"), \
	WM2200_MIXER_INPUT_ROUTES(name " Aux 4"), \
	WM2200_MIXER_INPUT_ROUTES(name " Aux 5"), \
	WM2200_MIXER_INPUT_ROUTES(name " Aux 6")

static const char *wm2200_aec_loopback_texts[] = {
	"OUT1L", "OUT1R", "OUT2L", "OUT2R",
};

static const struct soc_enum wm2200_aec_loopback =
	SOC_ENUM_SINGLE(WM2200_DAC_AEC_CONTROL_1,
			WM2200_AEC_LOOPBACK_SRC_SHIFT,
			ARRAY_SIZE(wm2200_aec_loopback_texts),
			wm2200_aec_loopback_texts);

static const struct snd_kcontrol_new wm2200_aec_loopback_mux =
	SOC_DAPM_ENUM("AEC Loopback", wm2200_aec_loopback);

static const struct snd_soc_dapm_widget wm2200_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("SYSCLK", WM2200_CLOCKING_3, WM2200_SYSCLK_ENA_SHIFT, 0,
		    NULL, 0),
SND_SOC_DAPM_SUPPLY("CP1", WM2200_DM_CHARGE_PUMP_1, WM2200_CPDM_ENA_SHIFT, 0,
		    NULL, 0),
SND_SOC_DAPM_SUPPLY("CP2", WM2200_MIC_CHARGE_PUMP_1, WM2200_CPMIC_ENA_SHIFT, 0,
		    NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS1", WM2200_MIC_BIAS_CTRL_1, WM2200_MICB1_ENA_SHIFT,
		    0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS2", WM2200_MIC_BIAS_CTRL_2, WM2200_MICB2_ENA_SHIFT,
		    0, NULL, 0),
SND_SOC_DAPM_REGULATOR_SUPPLY("CPVDD", 20, 0),
SND_SOC_DAPM_REGULATOR_SUPPLY("AVDD", 20, 0),

SND_SOC_DAPM_INPUT("IN1L"),
SND_SOC_DAPM_INPUT("IN1R"),
SND_SOC_DAPM_INPUT("IN2L"),
SND_SOC_DAPM_INPUT("IN2R"),
SND_SOC_DAPM_INPUT("IN3L"),
SND_SOC_DAPM_INPUT("IN3R"),

SND_SOC_DAPM_SIGGEN("TONE"),
SND_SOC_DAPM_PGA("Tone Generator", WM2200_TONE_GENERATOR_1,
		 WM2200_TONE_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("IN1L PGA", WM2200_INPUT_ENABLES, WM2200_IN1L_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("IN1R PGA", WM2200_INPUT_ENABLES, WM2200_IN1R_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("IN2L PGA", WM2200_INPUT_ENABLES, WM2200_IN2L_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("IN2R PGA", WM2200_INPUT_ENABLES, WM2200_IN2R_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("IN3L PGA", WM2200_INPUT_ENABLES, WM2200_IN3L_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("IN3R PGA", WM2200_INPUT_ENABLES, WM2200_IN3R_ENA_SHIFT, 0,
		 NULL, 0),

SND_SOC_DAPM_AIF_IN("AIF1RX1", "Playback", 0,
		    WM2200_AUDIO_IF_1_22, WM2200_AIF1RX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX2", "Playback", 1,
		    WM2200_AUDIO_IF_1_22, WM2200_AIF1RX2_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX3", "Playback", 2,
		    WM2200_AUDIO_IF_1_22, WM2200_AIF1RX3_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX4", "Playback", 3,
		    WM2200_AUDIO_IF_1_22, WM2200_AIF1RX4_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX5", "Playback", 4,
		    WM2200_AUDIO_IF_1_22, WM2200_AIF1RX5_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX6", "Playback", 5,
		    WM2200_AUDIO_IF_1_22, WM2200_AIF1RX6_ENA_SHIFT, 0),

SND_SOC_DAPM_PGA("EQL", WM2200_EQL_1, WM2200_EQL_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("EQR", WM2200_EQR_1, WM2200_EQR_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("LHPF1", WM2200_HPLPF1_1, WM2200_LHPF1_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("LHPF2", WM2200_HPLPF2_1, WM2200_LHPF2_ENA_SHIFT, 0,
		 NULL, 0),

WM_ADSP1("DSP1", 0),
WM_ADSP1("DSP2", 1),

SND_SOC_DAPM_AIF_OUT("AIF1TX1", "Capture", 0,
		    WM2200_AUDIO_IF_1_22, WM2200_AIF1TX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX2", "Capture", 1,
		    WM2200_AUDIO_IF_1_22, WM2200_AIF1TX2_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX3", "Capture", 2,
		    WM2200_AUDIO_IF_1_22, WM2200_AIF1TX3_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX4", "Capture", 3,
		    WM2200_AUDIO_IF_1_22, WM2200_AIF1TX4_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX5", "Capture", 4,
		    WM2200_AUDIO_IF_1_22, WM2200_AIF1TX5_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX6", "Capture", 5,
		    WM2200_AUDIO_IF_1_22, WM2200_AIF1TX6_ENA_SHIFT, 0),

SND_SOC_DAPM_MUX("AEC Loopback", WM2200_DAC_AEC_CONTROL_1,
		 WM2200_AEC_LOOPBACK_ENA_SHIFT, 0, &wm2200_aec_loopback_mux),

SND_SOC_DAPM_PGA_S("OUT1L", 0, WM2200_OUTPUT_ENABLES,
		   WM2200_OUT1L_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("OUT1R", 0, WM2200_OUTPUT_ENABLES,
		   WM2200_OUT1R_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA_S("EPD_LP", 1, WM2200_EAR_PIECE_CTRL_1,
		   WM2200_EPD_LP_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("EPD_OUTP_LP", 1, WM2200_EAR_PIECE_CTRL_1,
		   WM2200_EPD_OUTP_LP_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("EPD_RMV_SHRT_LP", 1, WM2200_EAR_PIECE_CTRL_1,
		   WM2200_EPD_RMV_SHRT_LP_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA_S("EPD_LN", 1, WM2200_EAR_PIECE_CTRL_1,
		   WM2200_EPD_LN_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("EPD_OUTP_LN", 1, WM2200_EAR_PIECE_CTRL_1,
		   WM2200_EPD_OUTP_LN_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("EPD_RMV_SHRT_LN", 1, WM2200_EAR_PIECE_CTRL_1,
		   WM2200_EPD_RMV_SHRT_LN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA_S("EPD_RP", 1, WM2200_EAR_PIECE_CTRL_2,
		   WM2200_EPD_RP_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("EPD_OUTP_RP", 1, WM2200_EAR_PIECE_CTRL_2,
		   WM2200_EPD_OUTP_RP_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("EPD_RMV_SHRT_RP", 1, WM2200_EAR_PIECE_CTRL_2,
		   WM2200_EPD_RMV_SHRT_RP_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA_S("EPD_RN", 1, WM2200_EAR_PIECE_CTRL_2,
		   WM2200_EPD_RN_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("EPD_OUTP_RN", 1, WM2200_EAR_PIECE_CTRL_2,
		   WM2200_EPD_OUTP_RN_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("EPD_RMV_SHRT_RN", 1, WM2200_EAR_PIECE_CTRL_2,
		   WM2200_EPD_RMV_SHRT_RN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("OUT2L", WM2200_OUTPUT_ENABLES, WM2200_OUT2L_ENA_SHIFT,
		 0, NULL, 0),
SND_SOC_DAPM_PGA("OUT2R", WM2200_OUTPUT_ENABLES, WM2200_OUT2R_ENA_SHIFT,
		 0, NULL, 0),

SND_SOC_DAPM_OUTPUT("EPOUTLN"),
SND_SOC_DAPM_OUTPUT("EPOUTLP"),
SND_SOC_DAPM_OUTPUT("EPOUTRN"),
SND_SOC_DAPM_OUTPUT("EPOUTRP"),
SND_SOC_DAPM_OUTPUT("SPK"),

WM2200_MIXER_WIDGETS(EQL, "EQL"),
WM2200_MIXER_WIDGETS(EQR, "EQR"),

WM2200_MIXER_WIDGETS(LHPF1, "LHPF1"),
WM2200_MIXER_WIDGETS(LHPF2, "LHPF2"),

WM2200_DSP_WIDGETS(DSP1, "DSP1"),
WM2200_DSP_WIDGETS(DSP2, "DSP2"),

WM2200_MIXER_WIDGETS(AIF1TX1, "AIF1TX1"),
WM2200_MIXER_WIDGETS(AIF1TX2, "AIF1TX2"),
WM2200_MIXER_WIDGETS(AIF1TX3, "AIF1TX3"),
WM2200_MIXER_WIDGETS(AIF1TX4, "AIF1TX4"),
WM2200_MIXER_WIDGETS(AIF1TX5, "AIF1TX5"),
WM2200_MIXER_WIDGETS(AIF1TX6, "AIF1TX6"),

WM2200_MIXER_WIDGETS(OUT1L, "OUT1L"),
WM2200_MIXER_WIDGETS(OUT1R, "OUT1R"),
WM2200_MIXER_WIDGETS(OUT2L, "OUT2L"),
WM2200_MIXER_WIDGETS(OUT2R, "OUT2R"),
};

static const struct snd_soc_dapm_route wm2200_dapm_routes[] = {
	/* Everything needs SYSCLK but only hook up things on the edge
	 * of the chip */
	{ "IN1L", NULL, "SYSCLK" },
	{ "IN1R", NULL, "SYSCLK" },
	{ "IN2L", NULL, "SYSCLK" },
	{ "IN2R", NULL, "SYSCLK" },
	{ "IN3L", NULL, "SYSCLK" },
	{ "IN3R", NULL, "SYSCLK" },
	{ "OUT1L", NULL, "SYSCLK" },
	{ "OUT1R", NULL, "SYSCLK" },
	{ "OUT2L", NULL, "SYSCLK" },
	{ "OUT2R", NULL, "SYSCLK" },
	{ "AIF1RX1", NULL, "SYSCLK" },
	{ "AIF1RX2", NULL, "SYSCLK" },
	{ "AIF1RX3", NULL, "SYSCLK" },
	{ "AIF1RX4", NULL, "SYSCLK" },
	{ "AIF1RX5", NULL, "SYSCLK" },
	{ "AIF1RX6", NULL, "SYSCLK" },
	{ "AIF1TX1", NULL, "SYSCLK" },
	{ "AIF1TX2", NULL, "SYSCLK" },
	{ "AIF1TX3", NULL, "SYSCLK" },
	{ "AIF1TX4", NULL, "SYSCLK" },
	{ "AIF1TX5", NULL, "SYSCLK" },
	{ "AIF1TX6", NULL, "SYSCLK" },

	{ "IN1L", NULL, "AVDD" },
	{ "IN1R", NULL, "AVDD" },
	{ "IN2L", NULL, "AVDD" },
	{ "IN2R", NULL, "AVDD" },
	{ "IN3L", NULL, "AVDD" },
	{ "IN3R", NULL, "AVDD" },
	{ "OUT1L", NULL, "AVDD" },
	{ "OUT1R", NULL, "AVDD" },

	{ "IN1L PGA", NULL, "IN1L" },
	{ "IN1R PGA", NULL, "IN1R" },
	{ "IN2L PGA", NULL, "IN2L" },
	{ "IN2R PGA", NULL, "IN2R" },
	{ "IN3L PGA", NULL, "IN3L" },
	{ "IN3R PGA", NULL, "IN3R" },

	{ "Tone Generator", NULL, "TONE" },

	{ "CP2", NULL, "CPVDD" },
	{ "MICBIAS1", NULL, "CP2" },
	{ "MICBIAS2", NULL, "CP2" },

	{ "CP1", NULL, "CPVDD" },
	{ "EPD_LN", NULL, "CP1" },
	{ "EPD_LP", NULL, "CP1" },
	{ "EPD_RN", NULL, "CP1" },
	{ "EPD_RP", NULL, "CP1" },

	{ "EPD_LP", NULL, "OUT1L" },
	{ "EPD_OUTP_LP", NULL, "EPD_LP" },
	{ "EPD_RMV_SHRT_LP", NULL, "EPD_OUTP_LP" },
	{ "EPOUTLP", NULL, "EPD_RMV_SHRT_LP" },

	{ "EPD_LN", NULL, "OUT1L" },
	{ "EPD_OUTP_LN", NULL, "EPD_LN" },
	{ "EPD_RMV_SHRT_LN", NULL, "EPD_OUTP_LN" },
	{ "EPOUTLN", NULL, "EPD_RMV_SHRT_LN" },

	{ "EPD_RP", NULL, "OUT1R" },
	{ "EPD_OUTP_RP", NULL, "EPD_RP" },
	{ "EPD_RMV_SHRT_RP", NULL, "EPD_OUTP_RP" },
	{ "EPOUTRP", NULL, "EPD_RMV_SHRT_RP" },

	{ "EPD_RN", NULL, "OUT1R" },
	{ "EPD_OUTP_RN", NULL, "EPD_RN" },
	{ "EPD_RMV_SHRT_RN", NULL, "EPD_OUTP_RN" },
	{ "EPOUTRN", NULL, "EPD_RMV_SHRT_RN" },

	{ "SPK", NULL, "OUT2L" },
	{ "SPK", NULL, "OUT2R" },

	{ "AEC Loopback", "OUT1L", "OUT1L" },
	{ "AEC Loopback", "OUT1R", "OUT1R" },
	{ "AEC Loopback", "OUT2L", "OUT2L" },
	{ "AEC Loopback", "OUT2R", "OUT2R" },

	WM2200_MIXER_ROUTES("DSP1", "DSP1L"),
	WM2200_MIXER_ROUTES("DSP1", "DSP1R"),
	WM2200_MIXER_ROUTES("DSP2", "DSP2L"),
	WM2200_MIXER_ROUTES("DSP2", "DSP2R"),

	WM2200_DSP_AUX_ROUTES("DSP1"),
	WM2200_DSP_AUX_ROUTES("DSP2"),

	WM2200_MIXER_ROUTES("OUT1L", "OUT1L"),
	WM2200_MIXER_ROUTES("OUT1R", "OUT1R"),
	WM2200_MIXER_ROUTES("OUT2L", "OUT2L"),
	WM2200_MIXER_ROUTES("OUT2R", "OUT2R"),

	WM2200_MIXER_ROUTES("AIF1TX1", "AIF1TX1"),
	WM2200_MIXER_ROUTES("AIF1TX2", "AIF1TX2"),
	WM2200_MIXER_ROUTES("AIF1TX3", "AIF1TX3"),
	WM2200_MIXER_ROUTES("AIF1TX4", "AIF1TX4"),
	WM2200_MIXER_ROUTES("AIF1TX5", "AIF1TX5"),
	WM2200_MIXER_ROUTES("AIF1TX6", "AIF1TX6"),

	WM2200_MIXER_ROUTES("EQL", "EQL"),
	WM2200_MIXER_ROUTES("EQR", "EQR"),

	WM2200_MIXER_ROUTES("LHPF1", "LHPF1"),
	WM2200_MIXER_ROUTES("LHPF2", "LHPF2"),
};

static int wm2200_probe(struct snd_soc_codec *codec)
{
	struct wm2200_priv *wm2200 = dev_get_drvdata(codec->dev);
	int ret;

	wm2200->codec = codec;
	codec->control_data = wm2200->regmap;
	codec->dapm.bias_level = SND_SOC_BIAS_OFF;

	ret = snd_soc_codec_set_cache_io(codec, 16, 16, SND_SOC_REGMAP);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_codec_controls(codec, wm_adsp_fw_controls, 2);
	if (ret != 0)
		return ret;

	return ret;
}

static int wm2200_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	int lrclk, bclk, fmt_val;

	lrclk = 0;
	bclk = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		fmt_val = 0;
		break;
	case SND_SOC_DAIFMT_I2S:
		fmt_val = 2;
		break;
	default:
		dev_err(codec->dev, "Unsupported DAI format %d\n",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		lrclk |= WM2200_AIF1TX_LRCLK_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		bclk |= WM2200_AIF1_BCLK_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		lrclk |= WM2200_AIF1TX_LRCLK_MSTR;
		bclk |= WM2200_AIF1_BCLK_MSTR;
		break;
	default:
		dev_err(codec->dev, "Unsupported master mode %d\n",
			fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		bclk |= WM2200_AIF1_BCLK_INV;
		lrclk |= WM2200_AIF1TX_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		bclk |= WM2200_AIF1_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		lrclk |= WM2200_AIF1TX_LRCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM2200_AUDIO_IF_1_1, WM2200_AIF1_BCLK_MSTR |
			    WM2200_AIF1_BCLK_INV, bclk);
	snd_soc_update_bits(codec, WM2200_AUDIO_IF_1_2,
			    WM2200_AIF1TX_LRCLK_MSTR | WM2200_AIF1TX_LRCLK_INV,
			    lrclk);
	snd_soc_update_bits(codec, WM2200_AUDIO_IF_1_3,
			    WM2200_AIF1TX_LRCLK_MSTR | WM2200_AIF1TX_LRCLK_INV,
			    lrclk);
	snd_soc_update_bits(codec, WM2200_AUDIO_IF_1_5,
			    WM2200_AIF1_FMT_MASK, fmt_val);

	return 0;
}

static int wm2200_sr_code[] = {
	0,
	12000,
	24000,
	48000,
	96000,
	192000,
	384000,
	768000,
	0,
	11025,
	22050,
	44100,
	88200,
	176400,
	352800,
	705600,
	4000,
	8000,
	16000,
	32000,
	64000,
	128000,
	256000,
	512000,
};

#define WM2200_NUM_BCLK_RATES 12

static int wm2200_bclk_rates_dat[WM2200_NUM_BCLK_RATES] = {
	6144000,
	3072000,
	2048000,
	1536000,
	768000,
	512000,
	384000,
	256000,
	192000,
	128000,
	96000,
	64000,
};	

static int wm2200_bclk_rates_cd[WM2200_NUM_BCLK_RATES] = {
	5644800,
	3763200,
	2882400,
	1881600,
	1411200,
	705600,
	470400,
	352800,
	176400,
	117600,
	88200,
	58800,
};

static int wm2200_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm2200_priv *wm2200 = snd_soc_codec_get_drvdata(codec);
	int i, bclk, lrclk, wl, fl, sr_code;
	int *bclk_rates;

	/* Data sizes if not using TDM */
	wl = snd_pcm_format_width(params_format(params));
	if (wl < 0)
		return wl;
	fl = snd_soc_params_to_frame_size(params);
	if (fl < 0)
		return fl;

	dev_dbg(codec->dev, "Word length %d bits, frame length %d bits\n",
		wl, fl);

	/* Target BCLK rate */
	bclk = snd_soc_params_to_bclk(params);
	if (bclk < 0)
		return bclk;

	if (!wm2200->sysclk) {
		dev_err(codec->dev, "SYSCLK has no rate set\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(wm2200_sr_code); i++)
		if (wm2200_sr_code[i] == params_rate(params))
			break;
	if (i == ARRAY_SIZE(wm2200_sr_code)) {
		dev_err(codec->dev, "Unsupported sample rate: %dHz\n",
			params_rate(params));
		return -EINVAL;
	}
	sr_code = i;

	dev_dbg(codec->dev, "Target BCLK is %dHz, using %dHz SYSCLK\n",
		bclk, wm2200->sysclk);

	if (wm2200->sysclk % 4000)
		bclk_rates = wm2200_bclk_rates_cd;
	else
		bclk_rates = wm2200_bclk_rates_dat;

	for (i = 0; i < WM2200_NUM_BCLK_RATES; i++)
		if (bclk_rates[i] >= bclk && (bclk_rates[i] % bclk == 0))
			break;
	if (i == WM2200_NUM_BCLK_RATES) {
		dev_err(codec->dev,
			"No valid BCLK for %dHz found from %dHz SYSCLK\n",
			bclk, wm2200->sysclk);
		return -EINVAL;
	}

	bclk = i;
	dev_dbg(codec->dev, "Setting %dHz BCLK\n", bclk_rates[bclk]);
	snd_soc_update_bits(codec, WM2200_AUDIO_IF_1_1,
			    WM2200_AIF1_BCLK_DIV_MASK, bclk);

	lrclk = bclk_rates[bclk] / params_rate(params);
	dev_dbg(codec->dev, "Setting %dHz LRCLK\n", bclk_rates[bclk] / lrclk);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK ||
	    dai->symmetric_rates)
		snd_soc_update_bits(codec, WM2200_AUDIO_IF_1_7,
				    WM2200_AIF1RX_BCPF_MASK, lrclk);
	else
		snd_soc_update_bits(codec, WM2200_AUDIO_IF_1_6,
				    WM2200_AIF1TX_BCPF_MASK, lrclk);

	i = (wl << WM2200_AIF1TX_WL_SHIFT) | wl;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_soc_update_bits(codec, WM2200_AUDIO_IF_1_9,
				    WM2200_AIF1RX_WL_MASK |
				    WM2200_AIF1RX_SLOT_LEN_MASK, i);
	else
		snd_soc_update_bits(codec, WM2200_AUDIO_IF_1_8,
				    WM2200_AIF1TX_WL_MASK |
				    WM2200_AIF1TX_SLOT_LEN_MASK, i);

	snd_soc_update_bits(codec, WM2200_CLOCKING_4,
			    WM2200_SAMPLE_RATE_1_MASK, sr_code);

	return 0;
}

static const struct snd_soc_dai_ops wm2200_dai_ops = {
	.set_fmt = wm2200_set_fmt,
	.hw_params = wm2200_hw_params,
};

static int wm2200_set_sysclk(struct snd_soc_codec *codec, int clk_id,
			     int source, unsigned int freq, int dir)
{
	struct wm2200_priv *wm2200 = snd_soc_codec_get_drvdata(codec);
	int fval;

	switch (clk_id) {
	case WM2200_CLK_SYSCLK:
		break;

	default:
		dev_err(codec->dev, "Unknown clock %d\n", clk_id);
		return -EINVAL;
	}

	switch (source) {
	case WM2200_CLKSRC_MCLK1:
	case WM2200_CLKSRC_MCLK2:
	case WM2200_CLKSRC_FLL:
	case WM2200_CLKSRC_BCLK1:
		break;
	default:
		dev_err(codec->dev, "Invalid source %d\n", source);
		return -EINVAL;
	}

	switch (freq) {
	case 22579200:
	case 24576000:
		fval = 2;
		break;
	default:
		dev_err(codec->dev, "Invalid clock rate: %d\n", freq);
		return -EINVAL;
	}

	/* TODO: Check if MCLKs are in use and enable/disable pulls to
	 * match.
	 */

	snd_soc_update_bits(codec, WM2200_CLOCKING_3, WM2200_SYSCLK_FREQ_MASK |
			    WM2200_SYSCLK_SRC_MASK,
			    fval << WM2200_SYSCLK_FREQ_SHIFT | source);

	wm2200->sysclk = freq;

	return 0;
}

struct _fll_div {
	u16 fll_fratio;
	u16 fll_outdiv;
	u16 fll_refclk_div;
	u16 n;
	u16 theta;
	u16 lambda;
};

static struct {
	unsigned int min;
	unsigned int max;
	u16 fll_fratio;
	int ratio;
} fll_fratios[] = {
	{       0,    64000, 4, 16 },
	{   64000,   128000, 3,  8 },
	{  128000,   256000, 2,  4 },
	{  256000,  1000000, 1,  2 },
	{ 1000000, 13500000, 0,  1 },
};

static int fll_factors(struct _fll_div *fll_div, unsigned int Fref,
		       unsigned int Fout)
{
	unsigned int target;
	unsigned int div;
	unsigned int fratio, gcd_fll;
	int i;

	/* Fref must be <=13.5MHz */
	div = 1;
	fll_div->fll_refclk_div = 0;
	while ((Fref / div) > 13500000) {
		div *= 2;
		fll_div->fll_refclk_div++;

		if (div > 8) {
			pr_err("Can't scale %dMHz input down to <=13.5MHz\n",
			       Fref);
			return -EINVAL;
		}
	}

	pr_debug("FLL Fref=%u Fout=%u\n", Fref, Fout);

	/* Apply the division for our remaining calculations */
	Fref /= div;

	/* Fvco should be 90-100MHz; don't check the upper bound */
	div = 2;
	while (Fout * div < 90000000) {
		div++;
		if (div > 64) {
			pr_err("Unable to find FLL_OUTDIV for Fout=%uHz\n",
			       Fout);
			return -EINVAL;
		}
	}
	target = Fout * div;
	fll_div->fll_outdiv = div - 1;

	pr_debug("FLL Fvco=%dHz\n", target);

	/* Find an appropraite FLL_FRATIO and factor it out of the target */
	for (i = 0; i < ARRAY_SIZE(fll_fratios); i++) {
		if (fll_fratios[i].min <= Fref && Fref <= fll_fratios[i].max) {
			fll_div->fll_fratio = fll_fratios[i].fll_fratio;
			fratio = fll_fratios[i].ratio;
			break;
		}
	}
	if (i == ARRAY_SIZE(fll_fratios)) {
		pr_err("Unable to find FLL_FRATIO for Fref=%uHz\n", Fref);
		return -EINVAL;
	}

	fll_div->n = target / (fratio * Fref);

	if (target % Fref == 0) {
		fll_div->theta = 0;
		fll_div->lambda = 0;
	} else {
		gcd_fll = gcd(target, fratio * Fref);

		fll_div->theta = (target - (fll_div->n * fratio * Fref))
			/ gcd_fll;
		fll_div->lambda = (fratio * Fref) / gcd_fll;
	}

	pr_debug("FLL N=%x THETA=%x LAMBDA=%x\n",
		 fll_div->n, fll_div->theta, fll_div->lambda);
	pr_debug("FLL_FRATIO=%x(%d) FLL_OUTDIV=%x FLL_REFCLK_DIV=%x\n",
		 fll_div->fll_fratio, fratio, fll_div->fll_outdiv,
		 fll_div->fll_refclk_div);

	return 0;
}

static int wm2200_set_fll(struct snd_soc_codec *codec, int fll_id, int source,
			  unsigned int Fref, unsigned int Fout)
{
	struct i2c_client *i2c = to_i2c_client(codec->dev);
	struct wm2200_priv *wm2200 = snd_soc_codec_get_drvdata(codec);
	struct _fll_div factors;
	int ret, i, timeout;

	if (!Fout) {
		dev_dbg(codec->dev, "FLL disabled");

		if (wm2200->fll_fout)
			pm_runtime_put(codec->dev);

		wm2200->fll_fout = 0;
		snd_soc_update_bits(codec, WM2200_FLL_CONTROL_1,
				    WM2200_FLL_ENA, 0);
		return 0;
	}

	switch (source) {
	case WM2200_FLL_SRC_MCLK1:
	case WM2200_FLL_SRC_MCLK2:
	case WM2200_FLL_SRC_BCLK:
		break;
	default:
		dev_err(codec->dev, "Invalid FLL source %d\n", source);
		return -EINVAL;
	}

	ret = fll_factors(&factors, Fref, Fout);
	if (ret < 0)
		return ret;

	/* Disable the FLL while we reconfigure */
	snd_soc_update_bits(codec, WM2200_FLL_CONTROL_1, WM2200_FLL_ENA, 0);

	snd_soc_update_bits(codec, WM2200_FLL_CONTROL_2,
			    WM2200_FLL_OUTDIV_MASK | WM2200_FLL_FRATIO_MASK,
			    (factors.fll_outdiv << WM2200_FLL_OUTDIV_SHIFT) |
			    factors.fll_fratio);
	if (factors.theta) {
		snd_soc_update_bits(codec, WM2200_FLL_CONTROL_3,
				    WM2200_FLL_FRACN_ENA,
				    WM2200_FLL_FRACN_ENA);
		snd_soc_update_bits(codec, WM2200_FLL_EFS_2,
				    WM2200_FLL_EFS_ENA,
				    WM2200_FLL_EFS_ENA);
	} else {
		snd_soc_update_bits(codec, WM2200_FLL_CONTROL_3,
				    WM2200_FLL_FRACN_ENA, 0);
		snd_soc_update_bits(codec, WM2200_FLL_EFS_2,
				    WM2200_FLL_EFS_ENA, 0);
	}

	snd_soc_update_bits(codec, WM2200_FLL_CONTROL_4, WM2200_FLL_THETA_MASK,
			    factors.theta);
	snd_soc_update_bits(codec, WM2200_FLL_CONTROL_6, WM2200_FLL_N_MASK,
			    factors.n);
	snd_soc_update_bits(codec, WM2200_FLL_CONTROL_7,
			    WM2200_FLL_CLK_REF_DIV_MASK |
			    WM2200_FLL_CLK_REF_SRC_MASK,
			    (factors.fll_refclk_div
			     << WM2200_FLL_CLK_REF_DIV_SHIFT) | source);
	snd_soc_update_bits(codec, WM2200_FLL_EFS_1,
			    WM2200_FLL_LAMBDA_MASK, factors.lambda);

	/* Clear any pending completions */
	try_wait_for_completion(&wm2200->fll_lock);

	pm_runtime_get_sync(codec->dev);

	snd_soc_update_bits(codec, WM2200_FLL_CONTROL_1,
			    WM2200_FLL_ENA, WM2200_FLL_ENA);

	if (i2c->irq)
		timeout = 2;
	else
		timeout = 50;

	snd_soc_update_bits(codec, WM2200_CLOCKING_3, WM2200_SYSCLK_ENA,
			    WM2200_SYSCLK_ENA);

	/* Poll for the lock; will use the interrupt to exit quickly */
	for (i = 0; i < timeout; i++) {
		if (i2c->irq) {
			ret = wait_for_completion_timeout(&wm2200->fll_lock,
							  msecs_to_jiffies(25));
			if (ret > 0)
				break;
		} else {
			msleep(1);
		}

		ret = snd_soc_read(codec,
				   WM2200_INTERRUPT_RAW_STATUS_2);
		if (ret < 0) {
			dev_err(codec->dev,
				"Failed to read FLL status: %d\n",
				ret);
			continue;
		}
		if (ret & WM2200_FLL_LOCK_STS)
			break;
	}
	if (i == timeout) {
		dev_err(codec->dev, "FLL lock timed out\n");
		pm_runtime_put(codec->dev);
		return -ETIMEDOUT;
	}

	wm2200->fll_src = source;
	wm2200->fll_fref = Fref;
	wm2200->fll_fout = Fout;

	dev_dbg(codec->dev, "FLL running %dHz->%dHz\n", Fref, Fout);

	return 0;
}

static int wm2200_dai_probe(struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int val = 0;
	int ret;

	ret = snd_soc_read(codec, WM2200_GPIO_CTRL_1);
	if (ret >= 0) {
		if ((ret & WM2200_GP1_FN_MASK) != 0) {
			dai->symmetric_rates = true;
			val = WM2200_AIF1TX_LRCLK_SRC;
		}
	} else {
		dev_err(codec->dev, "Failed to read GPIO 1 config: %d\n", ret);
	}

	snd_soc_update_bits(codec, WM2200_AUDIO_IF_1_2,
			    WM2200_AIF1TX_LRCLK_SRC, val);

	return 0;
}

#define WM2200_RATES SNDRV_PCM_RATE_8000_48000

#define WM2200_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver wm2200_dai = {
	.name = "wm2200",
	.probe = wm2200_dai_probe,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM2200_RATES,
		.formats = WM2200_FORMATS,
	},
	.capture = {
		 .stream_name = "Capture",
		 .channels_min = 2,
		 .channels_max = 2,
		 .rates = WM2200_RATES,
		 .formats = WM2200_FORMATS,
	 },
	.ops = &wm2200_dai_ops,
};

static struct snd_soc_codec_driver soc_codec_wm2200 = {
	.probe = wm2200_probe,

	.idle_bias_off = true,
	.ignore_pmdown_time = true,
	.set_sysclk = wm2200_set_sysclk,
	.set_pll = wm2200_set_fll,

	.controls = wm2200_snd_controls,
	.num_controls = ARRAY_SIZE(wm2200_snd_controls),
	.dapm_widgets = wm2200_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm2200_dapm_widgets),
	.dapm_routes = wm2200_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(wm2200_dapm_routes),
};

static irqreturn_t wm2200_irq(int irq, void *data)
{
	struct wm2200_priv *wm2200 = data;
	unsigned int val, mask;
	int ret;

	ret = regmap_read(wm2200->regmap, WM2200_INTERRUPT_STATUS_2, &val);
	if (ret != 0) {
		dev_err(wm2200->dev, "Failed to read IRQ status: %d\n", ret);
		return IRQ_NONE;
	}

	ret = regmap_read(wm2200->regmap, WM2200_INTERRUPT_STATUS_2_MASK,
			   &mask);
	if (ret != 0) {
		dev_warn(wm2200->dev, "Failed to read IRQ mask: %d\n", ret);
		mask = 0;
	}

	val &= ~mask;

	if (val & WM2200_FLL_LOCK_EINT) {
		dev_dbg(wm2200->dev, "FLL locked\n");
		complete(&wm2200->fll_lock);
	}

	if (val) {
		regmap_write(wm2200->regmap, WM2200_INTERRUPT_STATUS_2, val);
		
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static const struct regmap_config wm2200_regmap = {
	.reg_bits = 16,
	.val_bits = 16,

	.max_register = WM2200_MAX_REGISTER + (ARRAY_SIZE(wm2200_ranges) *
					       WM2200_DSP_SPACING),
	.reg_defaults = wm2200_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm2200_reg_defaults),
	.volatile_reg = wm2200_volatile_register,
	.readable_reg = wm2200_readable_register,
	.cache_type = REGCACHE_RBTREE,
	.ranges = wm2200_ranges,
	.num_ranges = ARRAY_SIZE(wm2200_ranges),
};

static const unsigned int wm2200_dig_vu[] = {
	WM2200_DAC_DIGITAL_VOLUME_1L,
	WM2200_DAC_DIGITAL_VOLUME_1R,
	WM2200_DAC_DIGITAL_VOLUME_2L,
	WM2200_DAC_DIGITAL_VOLUME_2R,
	WM2200_ADC_DIGITAL_VOLUME_1L,
	WM2200_ADC_DIGITAL_VOLUME_1R,
	WM2200_ADC_DIGITAL_VOLUME_2L,
	WM2200_ADC_DIGITAL_VOLUME_2R,
	WM2200_ADC_DIGITAL_VOLUME_3L,
	WM2200_ADC_DIGITAL_VOLUME_3R,
};

static const unsigned int wm2200_mic_ctrl_reg[] = {
	WM2200_IN1L_CONTROL,
	WM2200_IN2L_CONTROL,
	WM2200_IN3L_CONTROL,
};

static int wm2200_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm2200_pdata *pdata = dev_get_platdata(&i2c->dev);
	struct wm2200_priv *wm2200;
	unsigned int reg;
	int ret, i;
	int val;

	wm2200 = devm_kzalloc(&i2c->dev, sizeof(struct wm2200_priv),
			      GFP_KERNEL);
	if (wm2200 == NULL)
		return -ENOMEM;

	wm2200->dev = &i2c->dev;
	init_completion(&wm2200->fll_lock);

	wm2200->regmap = devm_regmap_init_i2c(i2c, &wm2200_regmap);
	if (IS_ERR(wm2200->regmap)) {
		ret = PTR_ERR(wm2200->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	for (i = 0; i < 2; i++) {
		wm2200->dsp[i].type = WMFW_ADSP1;
		wm2200->dsp[i].part = "wm2200";
		wm2200->dsp[i].num = i + 1;
		wm2200->dsp[i].dev = &i2c->dev;
		wm2200->dsp[i].regmap = wm2200->regmap;
		wm2200->dsp[i].sysclk_reg = WM2200_CLOCKING_3;
		wm2200->dsp[i].sysclk_mask = WM2200_SYSCLK_FREQ_MASK;
		wm2200->dsp[i].sysclk_shift =  WM2200_SYSCLK_FREQ_SHIFT;
	}

	wm2200->dsp[0].base = WM2200_DSP1_CONTROL_1;
	wm2200->dsp[0].mem = wm2200_dsp1_regions;
	wm2200->dsp[0].num_mems = ARRAY_SIZE(wm2200_dsp1_regions);

	wm2200->dsp[1].base = WM2200_DSP2_CONTROL_1;
	wm2200->dsp[1].mem = wm2200_dsp2_regions;
	wm2200->dsp[1].num_mems = ARRAY_SIZE(wm2200_dsp2_regions);

	for (i = 0; i < ARRAY_SIZE(wm2200->dsp); i++)
		wm_adsp1_init(&wm2200->dsp[i]);

	if (pdata)
		wm2200->pdata = *pdata;

	i2c_set_clientdata(i2c, wm2200);

	for (i = 0; i < ARRAY_SIZE(wm2200->core_supplies); i++)
		wm2200->core_supplies[i].supply = wm2200_core_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c->dev,
				      ARRAY_SIZE(wm2200->core_supplies),
				      wm2200->core_supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request core supplies: %d\n",
			ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm2200->core_supplies),
				    wm2200->core_supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to enable core supplies: %d\n",
			ret);
		return ret;
	}

	if (wm2200->pdata.ldo_ena) {
		ret = devm_gpio_request_one(&i2c->dev, wm2200->pdata.ldo_ena,
					    GPIOF_OUT_INIT_HIGH,
					    "WM2200 LDOENA");
		if (ret < 0) {
			dev_err(&i2c->dev, "Failed to request LDOENA %d: %d\n",
				wm2200->pdata.ldo_ena, ret);
			goto err_enable;
		}
		msleep(2);
	}

	if (wm2200->pdata.reset) {
		ret = devm_gpio_request_one(&i2c->dev, wm2200->pdata.reset,
					    GPIOF_OUT_INIT_HIGH,
					    "WM2200 /RESET");
		if (ret < 0) {
			dev_err(&i2c->dev, "Failed to request /RESET %d: %d\n",
				wm2200->pdata.reset, ret);
			goto err_ldo;
		}
	}

	ret = regmap_read(wm2200->regmap, WM2200_SOFTWARE_RESET, &reg);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read ID register: %d\n", ret);
		goto err_reset;
	}
	switch (reg) {
	case 0x2200:
		break;

	default:
		dev_err(&i2c->dev, "Device is not a WM2200, ID is %x\n", reg);
		ret = -EINVAL;
		goto err_reset;
	}

	ret = regmap_read(wm2200->regmap, WM2200_DEVICE_REVISION, &reg);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read revision register\n");
		goto err_reset;
	}

	wm2200->rev = reg & WM2200_DEVICE_REVISION_MASK;

	dev_info(&i2c->dev, "revision %c\n", wm2200->rev + 'A');

	switch (wm2200->rev) {
	case 0:
	case 1:
		ret = regmap_register_patch(wm2200->regmap, wm2200_reva_patch,
					    ARRAY_SIZE(wm2200_reva_patch));
		if (ret != 0) {
			dev_err(&i2c->dev, "Failed to register patch: %d\n",
				ret);
		}
		break;
	default:
		break;
	}

	ret = wm2200_reset(wm2200);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to issue reset\n");
		goto err_reset;
	}

	for (i = 0; i < ARRAY_SIZE(wm2200->pdata.gpio_defaults); i++) {
		if (!wm2200->pdata.gpio_defaults[i])
			continue;

		regmap_write(wm2200->regmap, WM2200_GPIO_CTRL_1 + i,
			     wm2200->pdata.gpio_defaults[i]);
	}

	for (i = 0; i < ARRAY_SIZE(wm2200_dig_vu); i++)
		regmap_update_bits(wm2200->regmap, wm2200_dig_vu[i],
				   WM2200_OUT_VU, WM2200_OUT_VU);

	/* Assign slots 1-6 to channels 1-6 for both TX and RX */
	for (i = 0; i < 6; i++) {
		regmap_write(wm2200->regmap, WM2200_AUDIO_IF_1_10 + i, i);
		regmap_write(wm2200->regmap, WM2200_AUDIO_IF_1_16 + i, i);
	}

	for (i = 0; i < WM2200_MAX_MICBIAS; i++) {
		if (!wm2200->pdata.micbias[i].mb_lvl &&
		    !wm2200->pdata.micbias[i].bypass)
			continue;

		/* Apply default for bypass mode */
		if (!wm2200->pdata.micbias[i].mb_lvl)
			wm2200->pdata.micbias[i].mb_lvl
					= WM2200_MBIAS_LVL_1V5;

		val = (wm2200->pdata.micbias[i].mb_lvl -1)
					<< WM2200_MICB1_LVL_SHIFT;

		if (wm2200->pdata.micbias[i].discharge)
			val |= WM2200_MICB1_DISCH;

		if (wm2200->pdata.micbias[i].fast_start)
			val |= WM2200_MICB1_RATE;

		if (wm2200->pdata.micbias[i].bypass)
			val |= WM2200_MICB1_MODE;

		regmap_update_bits(wm2200->regmap,
				   WM2200_MIC_BIAS_CTRL_1 + i,
				   WM2200_MICB1_LVL_MASK |
				   WM2200_MICB1_DISCH |
				   WM2200_MICB1_MODE |
				   WM2200_MICB1_RATE, val);
	}

	for (i = 0; i < ARRAY_SIZE(wm2200->pdata.in_mode); i++) {
		regmap_update_bits(wm2200->regmap, wm2200_mic_ctrl_reg[i],
				   WM2200_IN1_MODE_MASK |
				   WM2200_IN1_DMIC_SUP_MASK,
				   (wm2200->pdata.in_mode[i] <<
				    WM2200_IN1_MODE_SHIFT) |
				   (wm2200->pdata.dmic_sup[i] <<
				    WM2200_IN1_DMIC_SUP_SHIFT));
	}

	if (i2c->irq) {
		ret = request_threaded_irq(i2c->irq, NULL, wm2200_irq,
					   IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					   "wm2200", wm2200);
		if (ret == 0)
			regmap_update_bits(wm2200->regmap,
					   WM2200_INTERRUPT_STATUS_2_MASK,
					   WM2200_FLL_LOCK_EINT, 0);
		else
			dev_err(&i2c->dev, "Failed to request IRQ %d: %d\n",
				i2c->irq, ret);
	}

	pm_runtime_set_active(&i2c->dev);
	pm_runtime_enable(&i2c->dev);
	pm_request_idle(&i2c->dev);

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_wm2200,
				     &wm2200_dai, 1);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to register CODEC: %d\n", ret);
		goto err_pm_runtime;
	}

	return 0;

err_pm_runtime:
	pm_runtime_disable(&i2c->dev);
err_reset:
	if (wm2200->pdata.reset)
		gpio_set_value_cansleep(wm2200->pdata.reset, 0);
err_ldo:
	if (wm2200->pdata.ldo_ena)
		gpio_set_value_cansleep(wm2200->pdata.ldo_ena, 0);
err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm2200->core_supplies),
			       wm2200->core_supplies);
	return ret;
}

static int wm2200_i2c_remove(struct i2c_client *i2c)
{
	struct wm2200_priv *wm2200 = i2c_get_clientdata(i2c);

	snd_soc_unregister_codec(&i2c->dev);
	if (i2c->irq)
		free_irq(i2c->irq, wm2200);
	if (wm2200->pdata.reset)
		gpio_set_value_cansleep(wm2200->pdata.reset, 0);
	if (wm2200->pdata.ldo_ena)
		gpio_set_value_cansleep(wm2200->pdata.ldo_ena, 0);

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int wm2200_runtime_suspend(struct device *dev)
{
	struct wm2200_priv *wm2200 = dev_get_drvdata(dev);

	regcache_cache_only(wm2200->regmap, true);
	regcache_mark_dirty(wm2200->regmap);
	if (wm2200->pdata.ldo_ena)
		gpio_set_value_cansleep(wm2200->pdata.ldo_ena, 0);
	regulator_bulk_disable(ARRAY_SIZE(wm2200->core_supplies),
			       wm2200->core_supplies);

	return 0;
}

static int wm2200_runtime_resume(struct device *dev)
{
	struct wm2200_priv *wm2200 = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(wm2200->core_supplies),
				    wm2200->core_supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable supplies: %d\n",
			ret);
		return ret;
	}

	if (wm2200->pdata.ldo_ena) {
		gpio_set_value_cansleep(wm2200->pdata.ldo_ena, 1);
		msleep(2);
	}

	regcache_cache_only(wm2200->regmap, false);
	regcache_sync(wm2200->regmap);

	return 0;
}
#endif

static struct dev_pm_ops wm2200_pm = {
	SET_RUNTIME_PM_OPS(wm2200_runtime_suspend, wm2200_runtime_resume,
			   NULL)
};

static const struct i2c_device_id wm2200_i2c_id[] = {
	{ "wm2200", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm2200_i2c_id);

static struct i2c_driver wm2200_i2c_driver = {
	.driver = {
		.name = "wm2200",
		.owner = THIS_MODULE,
		.pm = &wm2200_pm,
	},
	.probe =    wm2200_i2c_probe,
	.remove =   wm2200_i2c_remove,
	.id_table = wm2200_i2c_id,
};

module_i2c_driver(wm2200_i2c_driver);

MODULE_DESCRIPTION("ASoC WM2200 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
