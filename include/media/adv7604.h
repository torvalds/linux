/*
 * adv7604 - Analog Devices ADV7604 video decoder driver
 *
 * Copyright 2012 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _ADV7604_
#define _ADV7604_

/* Analog input muxing modes (AFE register 0x02, [2:0]) */
enum adv7604_ain_sel {
	ADV7604_AIN1_2_3_NC_SYNC_1_2 = 0,
	ADV7604_AIN4_5_6_NC_SYNC_2_1 = 1,
	ADV7604_AIN7_8_9_NC_SYNC_3_1 = 2,
	ADV7604_AIN10_11_12_NC_SYNC_4_1 = 3,
	ADV7604_AIN9_4_5_6_SYNC_2_1 = 4,
};

/* Bus rotation and reordering (IO register 0x04, [7:5]) */
enum adv7604_op_ch_sel {
	ADV7604_OP_CH_SEL_GBR = 0,
	ADV7604_OP_CH_SEL_GRB = 1,
	ADV7604_OP_CH_SEL_BGR = 2,
	ADV7604_OP_CH_SEL_RGB = 3,
	ADV7604_OP_CH_SEL_BRG = 4,
	ADV7604_OP_CH_SEL_RBG = 5,
};

/* Input Color Space (IO register 0x02, [7:4]) */
enum adv7604_inp_color_space {
	ADV7604_INP_COLOR_SPACE_LIM_RGB = 0,
	ADV7604_INP_COLOR_SPACE_FULL_RGB = 1,
	ADV7604_INP_COLOR_SPACE_LIM_YCbCr_601 = 2,
	ADV7604_INP_COLOR_SPACE_LIM_YCbCr_709 = 3,
	ADV7604_INP_COLOR_SPACE_XVYCC_601 = 4,
	ADV7604_INP_COLOR_SPACE_XVYCC_709 = 5,
	ADV7604_INP_COLOR_SPACE_FULL_YCbCr_601 = 6,
	ADV7604_INP_COLOR_SPACE_FULL_YCbCr_709 = 7,
	ADV7604_INP_COLOR_SPACE_AUTO = 0xf,
};

/* Select output format (IO register 0x03, [7:0]) */
enum adv7604_op_format_sel {
	ADV7604_OP_FORMAT_SEL_SDR_ITU656_8 = 0x00,
	ADV7604_OP_FORMAT_SEL_SDR_ITU656_10 = 0x01,
	ADV7604_OP_FORMAT_SEL_SDR_ITU656_12_MODE0 = 0x02,
	ADV7604_OP_FORMAT_SEL_SDR_ITU656_12_MODE1 = 0x06,
	ADV7604_OP_FORMAT_SEL_SDR_ITU656_12_MODE2 = 0x0a,
	ADV7604_OP_FORMAT_SEL_DDR_422_8 = 0x20,
	ADV7604_OP_FORMAT_SEL_DDR_422_10 = 0x21,
	ADV7604_OP_FORMAT_SEL_DDR_422_12_MODE0 = 0x22,
	ADV7604_OP_FORMAT_SEL_DDR_422_12_MODE1 = 0x23,
	ADV7604_OP_FORMAT_SEL_DDR_422_12_MODE2 = 0x24,
	ADV7604_OP_FORMAT_SEL_SDR_444_24 = 0x40,
	ADV7604_OP_FORMAT_SEL_SDR_444_30 = 0x41,
	ADV7604_OP_FORMAT_SEL_SDR_444_36_MODE0 = 0x42,
	ADV7604_OP_FORMAT_SEL_DDR_444_24 = 0x60,
	ADV7604_OP_FORMAT_SEL_DDR_444_30 = 0x61,
	ADV7604_OP_FORMAT_SEL_DDR_444_36 = 0x62,
	ADV7604_OP_FORMAT_SEL_SDR_ITU656_16 = 0x80,
	ADV7604_OP_FORMAT_SEL_SDR_ITU656_20 = 0x81,
	ADV7604_OP_FORMAT_SEL_SDR_ITU656_24_MODE0 = 0x82,
	ADV7604_OP_FORMAT_SEL_SDR_ITU656_24_MODE1 = 0x86,
	ADV7604_OP_FORMAT_SEL_SDR_ITU656_24_MODE2 = 0x8a,
};

enum adv7604_drive_strength {
	ADV7604_DR_STR_MEDIUM_LOW = 1,
	ADV7604_DR_STR_MEDIUM_HIGH = 2,
	ADV7604_DR_STR_HIGH = 3,
};

/* Platform dependent definition */
struct adv7604_platform_data {
	/* connector - HDMI or DVI? */
	unsigned connector_hdmi:1;

	/* DIS_PWRDNB: 1 if the PWRDNB pin is unused and unconnected */
	unsigned disable_pwrdnb:1;

	/* DIS_CABLE_DET_RST: 1 if the 5V pins are unused and unconnected */
	unsigned disable_cable_det_rst:1;

	/* Analog input muxing mode */
	enum adv7604_ain_sel ain_sel;

	/* Bus rotation and reordering */
	enum adv7604_op_ch_sel op_ch_sel;

	/* Select output format */
	enum adv7604_op_format_sel op_format_sel;

	/* IO register 0x02 */
	unsigned alt_gamma:1;
	unsigned op_656_range:1;
	unsigned rgb_out:1;
	unsigned alt_data_sat:1;

	/* IO register 0x05 */
	unsigned blank_data:1;
	unsigned insert_av_codes:1;
	unsigned replicate_av_codes:1;
	unsigned invert_cbcr:1;

	/* IO register 0x14 */
	enum adv7604_drive_strength dr_str_data;
	enum adv7604_drive_strength dr_str_clk;
	enum adv7604_drive_strength dr_str_sync;

	/* IO register 0x30 */
	unsigned output_bus_lsb_to_msb:1;

	/* Free run */
	unsigned hdmi_free_run_mode;

	/* i2c addresses: 0 == use default */
	u8 i2c_avlink;
	u8 i2c_cec;
	u8 i2c_infoframe;
	u8 i2c_esdp;
	u8 i2c_dpp;
	u8 i2c_afe;
	u8 i2c_repeater;
	u8 i2c_edid;
	u8 i2c_hdmi;
	u8 i2c_test;
	u8 i2c_cp;
	u8 i2c_vdp;
};

enum adv7604_input_port {
	ADV7604_INPUT_HDMI_PORT_A,
	ADV7604_INPUT_HDMI_PORT_B,
	ADV7604_INPUT_HDMI_PORT_C,
	ADV7604_INPUT_HDMI_PORT_D,
	ADV7604_INPUT_VGA_RGB,
	ADV7604_INPUT_VGA_COMP,
};

#define ADV7604_EDID_PORT_A 0
#define ADV7604_EDID_PORT_B 1
#define ADV7604_EDID_PORT_C 2
#define ADV7604_EDID_PORT_D 3

#define V4L2_CID_ADV_RX_ANALOG_SAMPLING_PHASE	(V4L2_CID_DV_CLASS_BASE + 0x1000)
#define V4L2_CID_ADV_RX_FREE_RUN_COLOR_MANUAL	(V4L2_CID_DV_CLASS_BASE + 0x1001)
#define V4L2_CID_ADV_RX_FREE_RUN_COLOR		(V4L2_CID_DV_CLASS_BASE + 0x1002)

/* notify events */
#define ADV7604_HOTPLUG		1
#define ADV7604_FMT_CHANGE	2

#endif
