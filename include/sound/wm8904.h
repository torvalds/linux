/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Platform data for WM8904
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#ifndef __MFD_WM8994_PDATA_H__
#define __MFD_WM8994_PDATA_H__

/* Used to enable configuration of a GPIO to all zeros */
#define WM8904_GPIO_NO_CONFIG 0x8000

/*
 * R6 (0x06) - Mic Bias Control 0
 */
#define WM8904_MICDET_THR_MASK                  0x0070  /* MICDET_THR - [6:4] */
#define WM8904_MICDET_THR_SHIFT                      4  /* MICDET_THR - [6:4] */
#define WM8904_MICDET_THR_WIDTH                      3  /* MICDET_THR - [6:4] */
#define WM8904_MICSHORT_THR_MASK                0x000C  /* MICSHORT_THR - [3:2] */
#define WM8904_MICSHORT_THR_SHIFT                    2  /* MICSHORT_THR - [3:2] */
#define WM8904_MICSHORT_THR_WIDTH                    2  /* MICSHORT_THR - [3:2] */
#define WM8904_MICDET_ENA                       0x0002  /* MICDET_ENA */
#define WM8904_MICDET_ENA_MASK                  0x0002  /* MICDET_ENA */
#define WM8904_MICDET_ENA_SHIFT                      1  /* MICDET_ENA */
#define WM8904_MICDET_ENA_WIDTH                      1  /* MICDET_ENA */
#define WM8904_MICBIAS_ENA                      0x0001  /* MICBIAS_ENA */
#define WM8904_MICBIAS_ENA_MASK                 0x0001  /* MICBIAS_ENA */
#define WM8904_MICBIAS_ENA_SHIFT                     0  /* MICBIAS_ENA */
#define WM8904_MICBIAS_ENA_WIDTH                     1  /* MICBIAS_ENA */

/*
 * R7 (0x07) - Mic Bias Control 1
 */
#define WM8904_MIC_DET_FILTER_ENA               0x8000  /* MIC_DET_FILTER_ENA */
#define WM8904_MIC_DET_FILTER_ENA_MASK          0x8000  /* MIC_DET_FILTER_ENA */
#define WM8904_MIC_DET_FILTER_ENA_SHIFT             15  /* MIC_DET_FILTER_ENA */
#define WM8904_MIC_DET_FILTER_ENA_WIDTH              1  /* MIC_DET_FILTER_ENA */
#define WM8904_MIC_SHORT_FILTER_ENA             0x4000  /* MIC_SHORT_FILTER_ENA */
#define WM8904_MIC_SHORT_FILTER_ENA_MASK        0x4000  /* MIC_SHORT_FILTER_ENA */
#define WM8904_MIC_SHORT_FILTER_ENA_SHIFT           14  /* MIC_SHORT_FILTER_ENA */
#define WM8904_MIC_SHORT_FILTER_ENA_WIDTH            1  /* MIC_SHORT_FILTER_ENA */
#define WM8904_MICBIAS_SEL_MASK                 0x0007  /* MICBIAS_SEL - [2:0] */
#define WM8904_MICBIAS_SEL_SHIFT                     0  /* MICBIAS_SEL - [2:0] */
#define WM8904_MICBIAS_SEL_WIDTH                     3  /* MICBIAS_SEL - [2:0] */


/*
 * R121 (0x79) - GPIO Control 1
 */
#define WM8904_GPIO1_PU                         0x0020  /* GPIO1_PU */
#define WM8904_GPIO1_PU_MASK                    0x0020  /* GPIO1_PU */
#define WM8904_GPIO1_PU_SHIFT                        5  /* GPIO1_PU */
#define WM8904_GPIO1_PU_WIDTH                        1  /* GPIO1_PU */
#define WM8904_GPIO1_PD                         0x0010  /* GPIO1_PD */
#define WM8904_GPIO1_PD_MASK                    0x0010  /* GPIO1_PD */
#define WM8904_GPIO1_PD_SHIFT                        4  /* GPIO1_PD */
#define WM8904_GPIO1_PD_WIDTH                        1  /* GPIO1_PD */
#define WM8904_GPIO1_SEL_MASK                   0x000F  /* GPIO1_SEL - [3:0] */
#define WM8904_GPIO1_SEL_SHIFT                       0  /* GPIO1_SEL - [3:0] */
#define WM8904_GPIO1_SEL_WIDTH                       4  /* GPIO1_SEL - [3:0] */

/*
 * R122 (0x7A) - GPIO Control 2
 */
#define WM8904_GPIO2_PU                         0x0020  /* GPIO2_PU */
#define WM8904_GPIO2_PU_MASK                    0x0020  /* GPIO2_PU */
#define WM8904_GPIO2_PU_SHIFT                        5  /* GPIO2_PU */
#define WM8904_GPIO2_PU_WIDTH                        1  /* GPIO2_PU */
#define WM8904_GPIO2_PD                         0x0010  /* GPIO2_PD */
#define WM8904_GPIO2_PD_MASK                    0x0010  /* GPIO2_PD */
#define WM8904_GPIO2_PD_SHIFT                        4  /* GPIO2_PD */
#define WM8904_GPIO2_PD_WIDTH                        1  /* GPIO2_PD */
#define WM8904_GPIO2_SEL_MASK                   0x000F  /* GPIO2_SEL - [3:0] */
#define WM8904_GPIO2_SEL_SHIFT                       0  /* GPIO2_SEL - [3:0] */
#define WM8904_GPIO2_SEL_WIDTH                       4  /* GPIO2_SEL - [3:0] */

/*
 * R123 (0x7B) - GPIO Control 3
 */
#define WM8904_GPIO3_PU                         0x0020  /* GPIO3_PU */
#define WM8904_GPIO3_PU_MASK                    0x0020  /* GPIO3_PU */
#define WM8904_GPIO3_PU_SHIFT                        5  /* GPIO3_PU */
#define WM8904_GPIO3_PU_WIDTH                        1  /* GPIO3_PU */
#define WM8904_GPIO3_PD                         0x0010  /* GPIO3_PD */
#define WM8904_GPIO3_PD_MASK                    0x0010  /* GPIO3_PD */
#define WM8904_GPIO3_PD_SHIFT                        4  /* GPIO3_PD */
#define WM8904_GPIO3_PD_WIDTH                        1  /* GPIO3_PD */
#define WM8904_GPIO3_SEL_MASK                   0x000F  /* GPIO3_SEL - [3:0] */
#define WM8904_GPIO3_SEL_SHIFT                       0  /* GPIO3_SEL - [3:0] */
#define WM8904_GPIO3_SEL_WIDTH                       4  /* GPIO3_SEL - [3:0] */

/*
 * R124 (0x7C) - GPIO Control 4
 */
#define WM8904_GPI7_ENA                         0x0200  /* GPI7_ENA */
#define WM8904_GPI7_ENA_MASK                    0x0200  /* GPI7_ENA */
#define WM8904_GPI7_ENA_SHIFT                        9  /* GPI7_ENA */
#define WM8904_GPI7_ENA_WIDTH                        1  /* GPI7_ENA */
#define WM8904_GPI8_ENA                         0x0100  /* GPI8_ENA */
#define WM8904_GPI8_ENA_MASK                    0x0100  /* GPI8_ENA */
#define WM8904_GPI8_ENA_SHIFT                        8  /* GPI8_ENA */
#define WM8904_GPI8_ENA_WIDTH                        1  /* GPI8_ENA */
#define WM8904_GPIO_BCLK_MODE_ENA               0x0080  /* GPIO_BCLK_MODE_ENA */
#define WM8904_GPIO_BCLK_MODE_ENA_MASK          0x0080  /* GPIO_BCLK_MODE_ENA */
#define WM8904_GPIO_BCLK_MODE_ENA_SHIFT              7  /* GPIO_BCLK_MODE_ENA */
#define WM8904_GPIO_BCLK_MODE_ENA_WIDTH              1  /* GPIO_BCLK_MODE_ENA */
#define WM8904_GPIO_BCLK_SEL_MASK               0x000F  /* GPIO_BCLK_SEL - [3:0] */
#define WM8904_GPIO_BCLK_SEL_SHIFT                   0  /* GPIO_BCLK_SEL - [3:0] */
#define WM8904_GPIO_BCLK_SEL_WIDTH                   4  /* GPIO_BCLK_SEL - [3:0] */

#define WM8904_MIC_REGS  2
#define WM8904_GPIO_REGS 4
#define WM8904_DRC_REGS  4
#define WM8904_EQ_REGS   24

/**
 * DRC configurations are specified with a label and a set of register
 * values to write (the enable bits will be ignored).  At runtime an
 * enumerated control will be presented for each DRC block allowing
 * the user to choose the configuration to use.
 *
 * Configurations may be generated by hand or by using the DRC control
 * panel provided by the WISCE - see  http://www.wolfsonmicro.com/wisce/
 * for details.
 */
struct wm8904_drc_cfg {
	const char *name;
	u16 regs[WM8904_DRC_REGS];
};

/**
 * ReTune Mobile configurations are specified with a label, sample
 * rate and set of values to write (the enable bits will be ignored).
 *
 * Configurations are expected to be generated using the ReTune Mobile
 * control panel in WISCE - see http://www.wolfsonmicro.com/wisce/
 */
struct wm8904_retune_mobile_cfg {
	const char *name;
	unsigned int rate;
	u16 regs[WM8904_EQ_REGS];
};

struct wm8904_pdata {
	int num_drc_cfgs;
	struct wm8904_drc_cfg *drc_cfgs;

	int num_retune_mobile_cfgs;
	struct wm8904_retune_mobile_cfg *retune_mobile_cfgs;

	bool in1l_as_dmicdat1;
	bool in1r_as_dmicdat2;

	u32 gpio_cfg[WM8904_GPIO_REGS];
	u32 mic_cfg[WM8904_MIC_REGS];
};

#endif
