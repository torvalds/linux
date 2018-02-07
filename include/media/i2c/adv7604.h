/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * adv7604 - Analog Devices ADV7604 video decoder driver
 *
 * Copyright 2012 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _ADV7604_
#define _ADV7604_

#include <linux/types.h>

/* Analog input muxing modes (AFE register 0x02, [2:0]) */
enum adv7604_ain_sel {
	ADV7604_AIN1_2_3_NC_SYNC_1_2 = 0,
	ADV7604_AIN4_5_6_NC_SYNC_2_1 = 1,
	ADV7604_AIN7_8_9_NC_SYNC_3_1 = 2,
	ADV7604_AIN10_11_12_NC_SYNC_4_1 = 3,
	ADV7604_AIN9_4_5_6_SYNC_2_1 = 4,
};

/*
 * Bus rotation and reordering. This is used to specify component reordering on
 * the board and describes the components order on the bus when the ADV7604
 * outputs RGB.
 */
enum adv7604_bus_order {
	ADV7604_BUS_ORDER_RGB,		/* No operation	*/
	ADV7604_BUS_ORDER_GRB,		/* Swap 1-2	*/
	ADV7604_BUS_ORDER_RBG,		/* Swap 2-3	*/
	ADV7604_BUS_ORDER_BGR,		/* Swap 1-3	*/
	ADV7604_BUS_ORDER_BRG,		/* Rotate right	*/
	ADV7604_BUS_ORDER_GBR,		/* Rotate left	*/
};

/* Input Color Space (IO register 0x02, [7:4]) */
enum adv76xx_inp_color_space {
	ADV76XX_INP_COLOR_SPACE_LIM_RGB = 0,
	ADV76XX_INP_COLOR_SPACE_FULL_RGB = 1,
	ADV76XX_INP_COLOR_SPACE_LIM_YCbCr_601 = 2,
	ADV76XX_INP_COLOR_SPACE_LIM_YCbCr_709 = 3,
	ADV76XX_INP_COLOR_SPACE_XVYCC_601 = 4,
	ADV76XX_INP_COLOR_SPACE_XVYCC_709 = 5,
	ADV76XX_INP_COLOR_SPACE_FULL_YCbCr_601 = 6,
	ADV76XX_INP_COLOR_SPACE_FULL_YCbCr_709 = 7,
	ADV76XX_INP_COLOR_SPACE_AUTO = 0xf,
};

/* Select output format (IO register 0x03, [4:2]) */
enum adv7604_op_format_mode_sel {
	ADV7604_OP_FORMAT_MODE0 = 0x00,
	ADV7604_OP_FORMAT_MODE1 = 0x04,
	ADV7604_OP_FORMAT_MODE2 = 0x08,
};

enum adv76xx_drive_strength {
	ADV76XX_DR_STR_MEDIUM_LOW = 1,
	ADV76XX_DR_STR_MEDIUM_HIGH = 2,
	ADV76XX_DR_STR_HIGH = 3,
};

/* INT1 Configuration (IO register 0x40, [1:0]) */
enum adv76xx_int1_config {
	ADV76XX_INT1_CONFIG_OPEN_DRAIN,
	ADV76XX_INT1_CONFIG_ACTIVE_LOW,
	ADV76XX_INT1_CONFIG_ACTIVE_HIGH,
	ADV76XX_INT1_CONFIG_DISABLED,
};

enum adv76xx_page {
	ADV76XX_PAGE_IO,
	ADV7604_PAGE_AVLINK,
	ADV76XX_PAGE_CEC,
	ADV76XX_PAGE_INFOFRAME,
	ADV7604_PAGE_ESDP,
	ADV7604_PAGE_DPP,
	ADV76XX_PAGE_AFE,
	ADV76XX_PAGE_REP,
	ADV76XX_PAGE_EDID,
	ADV76XX_PAGE_HDMI,
	ADV76XX_PAGE_TEST,
	ADV76XX_PAGE_CP,
	ADV7604_PAGE_VDP,
	ADV76XX_PAGE_MAX,
};

/* Platform dependent definition */
struct adv76xx_platform_data {
	/* DIS_PWRDNB: 1 if the PWRDNB pin is unused and unconnected */
	unsigned disable_pwrdnb:1;

	/* DIS_CABLE_DET_RST: 1 if the 5V pins are unused and unconnected */
	unsigned disable_cable_det_rst:1;

	int default_input;

	/* Analog input muxing mode */
	enum adv7604_ain_sel ain_sel;

	/* Bus rotation and reordering */
	enum adv7604_bus_order bus_order;

	/* Select output format mode */
	enum adv7604_op_format_mode_sel op_format_mode_sel;

	/* Configuration of the INT1 pin */
	enum adv76xx_int1_config int1_config;

	/* IO register 0x02 */
	unsigned alt_gamma:1;

	/* IO register 0x05 */
	unsigned blank_data:1;
	unsigned insert_av_codes:1;
	unsigned replicate_av_codes:1;

	/* IO register 0x06 */
	unsigned inv_vs_pol:1;
	unsigned inv_hs_pol:1;
	unsigned inv_llc_pol:1;

	/* IO register 0x14 */
	enum adv76xx_drive_strength dr_str_data;
	enum adv76xx_drive_strength dr_str_clk;
	enum adv76xx_drive_strength dr_str_sync;

	/* IO register 0x30 */
	unsigned output_bus_lsb_to_msb:1;

	/* Free run */
	unsigned hdmi_free_run_mode;

	/* i2c addresses: 0 == use default */
	u8 i2c_addresses[ADV76XX_PAGE_MAX];
};

enum adv76xx_pad {
	ADV76XX_PAD_HDMI_PORT_A = 0,
	ADV7604_PAD_HDMI_PORT_B = 1,
	ADV7604_PAD_HDMI_PORT_C = 2,
	ADV7604_PAD_HDMI_PORT_D = 3,
	ADV7604_PAD_VGA_RGB = 4,
	ADV7604_PAD_VGA_COMP = 5,
	/* The source pad is either 1 (ADV7611) or 6 (ADV7604) */
	ADV7604_PAD_SOURCE = 6,
	ADV7611_PAD_SOURCE = 1,
	ADV76XX_PAD_MAX = 7,
};

#define V4L2_CID_ADV_RX_ANALOG_SAMPLING_PHASE	(V4L2_CID_DV_CLASS_BASE + 0x1000)
#define V4L2_CID_ADV_RX_FREE_RUN_COLOR_MANUAL	(V4L2_CID_DV_CLASS_BASE + 0x1001)
#define V4L2_CID_ADV_RX_FREE_RUN_COLOR		(V4L2_CID_DV_CLASS_BASE + 0x1002)

/* notify events */
#define ADV76XX_HOTPLUG		1

#endif
