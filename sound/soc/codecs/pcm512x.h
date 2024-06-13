/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for the PCM512x CODECs
 *
 * Author:	Mark Brown <broonie@kernel.org>
 *		Copyright 2014 Linaro Ltd
 */

#ifndef _SND_SOC_PCM512X
#define _SND_SOC_PCM512X

#include <linux/pm.h>
#include <linux/regmap.h>

#define PCM512x_VIRT_BASE 0x100
#define PCM512x_PAGE_LEN  0x100
#define PCM512x_PAGE_BASE(n)  (PCM512x_VIRT_BASE + (PCM512x_PAGE_LEN * n))

#define PCM512x_PAGE              0

#define PCM512x_RESET             (PCM512x_PAGE_BASE(0) +   1)
#define PCM512x_POWER             (PCM512x_PAGE_BASE(0) +   2)
#define PCM512x_MUTE              (PCM512x_PAGE_BASE(0) +   3)
#define PCM512x_PLL_EN            (PCM512x_PAGE_BASE(0) +   4)
#define PCM512x_SPI_MISO_FUNCTION (PCM512x_PAGE_BASE(0) +   6)
#define PCM512x_DSP               (PCM512x_PAGE_BASE(0) +   7)
#define PCM512x_GPIO_EN           (PCM512x_PAGE_BASE(0) +   8)
#define PCM512x_BCLK_LRCLK_CFG    (PCM512x_PAGE_BASE(0) +   9)
#define PCM512x_DSP_GPIO_INPUT    (PCM512x_PAGE_BASE(0) +  10)
#define PCM512x_MASTER_MODE       (PCM512x_PAGE_BASE(0) +  12)
#define PCM512x_PLL_REF           (PCM512x_PAGE_BASE(0) +  13)
#define PCM512x_DAC_REF           (PCM512x_PAGE_BASE(0) +  14)
#define PCM512x_GPIO_DACIN        (PCM512x_PAGE_BASE(0) +  16)
#define PCM512x_GPIO_PLLIN        (PCM512x_PAGE_BASE(0) +  18)
#define PCM512x_SYNCHRONIZE       (PCM512x_PAGE_BASE(0) +  19)
#define PCM512x_PLL_COEFF_0       (PCM512x_PAGE_BASE(0) +  20)
#define PCM512x_PLL_COEFF_1       (PCM512x_PAGE_BASE(0) +  21)
#define PCM512x_PLL_COEFF_2       (PCM512x_PAGE_BASE(0) +  22)
#define PCM512x_PLL_COEFF_3       (PCM512x_PAGE_BASE(0) +  23)
#define PCM512x_PLL_COEFF_4       (PCM512x_PAGE_BASE(0) +  24)
#define PCM512x_DSP_CLKDIV        (PCM512x_PAGE_BASE(0) +  27)
#define PCM512x_DAC_CLKDIV        (PCM512x_PAGE_BASE(0) +  28)
#define PCM512x_NCP_CLKDIV        (PCM512x_PAGE_BASE(0) +  29)
#define PCM512x_OSR_CLKDIV        (PCM512x_PAGE_BASE(0) +  30)
#define PCM512x_MASTER_CLKDIV_1   (PCM512x_PAGE_BASE(0) +  32)
#define PCM512x_MASTER_CLKDIV_2   (PCM512x_PAGE_BASE(0) +  33)
#define PCM512x_FS_SPEED_MODE     (PCM512x_PAGE_BASE(0) +  34)
#define PCM512x_IDAC_1            (PCM512x_PAGE_BASE(0) +  35)
#define PCM512x_IDAC_2            (PCM512x_PAGE_BASE(0) +  36)
#define PCM512x_ERROR_DETECT      (PCM512x_PAGE_BASE(0) +  37)
#define PCM512x_I2S_1             (PCM512x_PAGE_BASE(0) +  40)
#define PCM512x_I2S_2             (PCM512x_PAGE_BASE(0) +  41)
#define PCM512x_DAC_ROUTING       (PCM512x_PAGE_BASE(0) +  42)
#define PCM512x_DSP_PROGRAM       (PCM512x_PAGE_BASE(0) +  43)
#define PCM512x_CLKDET            (PCM512x_PAGE_BASE(0) +  44)
#define PCM512x_AUTO_MUTE         (PCM512x_PAGE_BASE(0) +  59)
#define PCM512x_DIGITAL_VOLUME_1  (PCM512x_PAGE_BASE(0) +  60)
#define PCM512x_DIGITAL_VOLUME_2  (PCM512x_PAGE_BASE(0) +  61)
#define PCM512x_DIGITAL_VOLUME_3  (PCM512x_PAGE_BASE(0) +  62)
#define PCM512x_DIGITAL_MUTE_1    (PCM512x_PAGE_BASE(0) +  63)
#define PCM512x_DIGITAL_MUTE_2    (PCM512x_PAGE_BASE(0) +  64)
#define PCM512x_DIGITAL_MUTE_3    (PCM512x_PAGE_BASE(0) +  65)
#define PCM512x_GPIO_OUTPUT_1     (PCM512x_PAGE_BASE(0) +  80)
#define PCM512x_GPIO_OUTPUT_2     (PCM512x_PAGE_BASE(0) +  81)
#define PCM512x_GPIO_OUTPUT_3     (PCM512x_PAGE_BASE(0) +  82)
#define PCM512x_GPIO_OUTPUT_4     (PCM512x_PAGE_BASE(0) +  83)
#define PCM512x_GPIO_OUTPUT_5     (PCM512x_PAGE_BASE(0) +  84)
#define PCM512x_GPIO_OUTPUT_6     (PCM512x_PAGE_BASE(0) +  85)
#define PCM512x_GPIO_CONTROL_1    (PCM512x_PAGE_BASE(0) +  86)
#define PCM512x_GPIO_CONTROL_2    (PCM512x_PAGE_BASE(0) +  87)
#define PCM512x_OVERFLOW          (PCM512x_PAGE_BASE(0) +  90)
#define PCM512x_RATE_DET_1        (PCM512x_PAGE_BASE(0) +  91)
#define PCM512x_RATE_DET_2        (PCM512x_PAGE_BASE(0) +  92)
#define PCM512x_RATE_DET_3        (PCM512x_PAGE_BASE(0) +  93)
#define PCM512x_RATE_DET_4        (PCM512x_PAGE_BASE(0) +  94)
#define PCM512x_CLOCK_STATUS      (PCM512x_PAGE_BASE(0) +  95)
#define PCM512x_ANALOG_MUTE_DET   (PCM512x_PAGE_BASE(0) + 108)
#define PCM512x_GPIN              (PCM512x_PAGE_BASE(0) + 119)
#define PCM512x_DIGITAL_MUTE_DET  (PCM512x_PAGE_BASE(0) + 120)

#define PCM512x_OUTPUT_AMPLITUDE  (PCM512x_PAGE_BASE(1) +   1)
#define PCM512x_ANALOG_GAIN_CTRL  (PCM512x_PAGE_BASE(1) +   2)
#define PCM512x_UNDERVOLTAGE_PROT (PCM512x_PAGE_BASE(1) +   5)
#define PCM512x_ANALOG_MUTE_CTRL  (PCM512x_PAGE_BASE(1) +   6)
#define PCM512x_ANALOG_GAIN_BOOST (PCM512x_PAGE_BASE(1) +   7)
#define PCM512x_VCOM_CTRL_1       (PCM512x_PAGE_BASE(1) +   8)
#define PCM512x_VCOM_CTRL_2       (PCM512x_PAGE_BASE(1) +   9)

#define PCM512x_CRAM_CTRL         (PCM512x_PAGE_BASE(44) +  1)

#define PCM512x_FLEX_A            (PCM512x_PAGE_BASE(253) + 63)
#define PCM512x_FLEX_B            (PCM512x_PAGE_BASE(253) + 64)

#define PCM512x_MAX_REGISTER      (PCM512x_PAGE_BASE(253) + 64)

/* Page 0, Register 1 - reset */
#define PCM512x_RSTR (1 << 0)
#define PCM512x_RSTM (1 << 4)

/* Page 0, Register 2 - power */
#define PCM512x_RQPD       (1 << 0)
#define PCM512x_RQPD_SHIFT 0
#define PCM512x_RQST       (1 << 4)
#define PCM512x_RQST_SHIFT 4

/* Page 0, Register 3 - mute */
#define PCM512x_RQMR (1 << 0)
#define PCM512x_RQMR_SHIFT 0
#define PCM512x_RQML (1 << 4)
#define PCM512x_RQML_SHIFT 4

/* Page 0, Register 4 - PLL */
#define PCM512x_PLLE       (1 << 0)
#define PCM512x_PLLE_SHIFT 0
#define PCM512x_PLCK       (1 << 4)
#define PCM512x_PLCK_SHIFT 4

/* Page 0, Register 7 - DSP */
#define PCM512x_SDSL       (1 << 0)
#define PCM512x_SDSL_SHIFT 0
#define PCM512x_DEMP       (1 << 4)
#define PCM512x_DEMP_SHIFT 4

/* Page 0, Register 8 - GPIO output enable */
#define PCM512x_G1OE       (1 << 0)
#define PCM512x_G2OE       (1 << 1)
#define PCM512x_G3OE       (1 << 2)
#define PCM512x_G4OE       (1 << 3)
#define PCM512x_G5OE       (1 << 4)
#define PCM512x_G6OE       (1 << 5)

/* Page 0, Register 9 - BCK, LRCLK configuration */
#define PCM512x_LRKO       (1 << 0)
#define PCM512x_LRKO_SHIFT 0
#define PCM512x_BCKO       (1 << 4)
#define PCM512x_BCKO_SHIFT 4
#define PCM512x_BCKP       (1 << 5)
#define PCM512x_BCKP_SHIFT 5

/* Page 0, Register 12 - Master mode BCK, LRCLK reset */
#define PCM512x_RLRK       (1 << 0)
#define PCM512x_RLRK_SHIFT 0
#define PCM512x_RBCK       (1 << 1)
#define PCM512x_RBCK_SHIFT 1

/* Page 0, Register 13 - PLL reference */
#define PCM512x_SREF        (7 << 4)
#define PCM512x_SREF_SHIFT  4
#define PCM512x_SREF_SCK    (0 << 4)
#define PCM512x_SREF_BCK    (1 << 4)
#define PCM512x_SREF_GPIO   (3 << 4)

/* Page 0, Register 14 - DAC reference */
#define PCM512x_SDAC        (7 << 4)
#define PCM512x_SDAC_SHIFT  4
#define PCM512x_SDAC_MCK    (0 << 4)
#define PCM512x_SDAC_PLL    (1 << 4)
#define PCM512x_SDAC_SCK    (3 << 4)
#define PCM512x_SDAC_BCK    (4 << 4)
#define PCM512x_SDAC_GPIO   (5 << 4)

/* Page 0, Register 16, 18 - GPIO source for DAC, PLL */
#define PCM512x_GREF        (7 << 0)
#define PCM512x_GREF_SHIFT  0
#define PCM512x_GREF_GPIO1  (0 << 0)
#define PCM512x_GREF_GPIO2  (1 << 0)
#define PCM512x_GREF_GPIO3  (2 << 0)
#define PCM512x_GREF_GPIO4  (3 << 0)
#define PCM512x_GREF_GPIO5  (4 << 0)
#define PCM512x_GREF_GPIO6  (5 << 0)

/* Page 0, Register 19 - synchronize */
#define PCM512x_RQSY        (1 << 0)
#define PCM512x_RQSY_RESUME (0 << 0)
#define PCM512x_RQSY_HALT   (1 << 0)

/* Page 0, Register 34 - fs speed mode */
#define PCM512x_FSSP        (3 << 0)
#define PCM512x_FSSP_SHIFT  0
#define PCM512x_FSSP_48KHZ  (0 << 0)
#define PCM512x_FSSP_96KHZ  (1 << 0)
#define PCM512x_FSSP_192KHZ (2 << 0)
#define PCM512x_FSSP_384KHZ (3 << 0)

/* Page 0, Register 37 - Error detection */
#define PCM512x_IPLK (1 << 0)
#define PCM512x_DCAS (1 << 1)
#define PCM512x_IDCM (1 << 2)
#define PCM512x_IDCH (1 << 3)
#define PCM512x_IDSK (1 << 4)
#define PCM512x_IDBK (1 << 5)
#define PCM512x_IDFS (1 << 6)

/* Page 0, Register 40 - I2S configuration */
#define PCM512x_ALEN       (3 << 0)
#define PCM512x_ALEN_SHIFT 0
#define PCM512x_ALEN_16    (0 << 0)
#define PCM512x_ALEN_20    (1 << 0)
#define PCM512x_ALEN_24    (2 << 0)
#define PCM512x_ALEN_32    (3 << 0)
#define PCM512x_AFMT       (3 << 4)
#define PCM512x_AFMT_SHIFT 4
#define PCM512x_AFMT_I2S   (0 << 4)
#define PCM512x_AFMT_DSP   (1 << 4)
#define PCM512x_AFMT_RTJ   (2 << 4)
#define PCM512x_AFMT_LTJ   (3 << 4)

/* Page 0, Register 42 - DAC routing */
#define PCM512x_AUPR_SHIFT 0
#define PCM512x_AUPL_SHIFT 4

/* Page 0, Register 59 - auto mute */
#define PCM512x_ATMR_SHIFT 0
#define PCM512x_ATML_SHIFT 4

/* Page 0, Register 63 - ramp rates */
#define PCM512x_VNDF_SHIFT 6
#define PCM512x_VNDS_SHIFT 4
#define PCM512x_VNUF_SHIFT 2
#define PCM512x_VNUS_SHIFT 0

/* Page 0, Register 64 - emergency ramp rates */
#define PCM512x_VEDF_SHIFT 6
#define PCM512x_VEDS_SHIFT 4

/* Page 0, Register 65 - Digital mute enables */
#define PCM512x_ACTL_SHIFT 2
#define PCM512x_AMLE_SHIFT 1
#define PCM512x_AMRE_SHIFT 0

/* Page 0, Register 80-85, GPIO output selection */
#define PCM512x_GxSL       (31 << 0)
#define PCM512x_GxSL_SHIFT 0
#define PCM512x_GxSL_OFF   (0 << 0)
#define PCM512x_GxSL_DSP   (1 << 0)
#define PCM512x_GxSL_REG   (2 << 0)
#define PCM512x_GxSL_AMUTB (3 << 0)
#define PCM512x_GxSL_AMUTL (4 << 0)
#define PCM512x_GxSL_AMUTR (5 << 0)
#define PCM512x_GxSL_CLKI  (6 << 0)
#define PCM512x_GxSL_SDOUT (7 << 0)
#define PCM512x_GxSL_ANMUL (8 << 0)
#define PCM512x_GxSL_ANMUR (9 << 0)
#define PCM512x_GxSL_PLLLK (10 << 0)
#define PCM512x_GxSL_CPCLK (11 << 0)
#define PCM512x_GxSL_UV0_7 (14 << 0)
#define PCM512x_GxSL_UV0_3 (15 << 0)
#define PCM512x_GxSL_PLLCK (16 << 0)

/* Page 1, Register 2 - analog volume control */
#define PCM512x_RAGN_SHIFT 0
#define PCM512x_LAGN_SHIFT 4

/* Page 1, Register 7 - analog boost control */
#define PCM512x_AGBR_SHIFT 0
#define PCM512x_AGBL_SHIFT 4

extern const struct dev_pm_ops pcm512x_pm_ops;
extern const struct regmap_config pcm512x_regmap;

int pcm512x_probe(struct device *dev, struct regmap *regmap);
void pcm512x_remove(struct device *dev);

#endif
