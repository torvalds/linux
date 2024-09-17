/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * adv7842 - Analog Devices ADV7842 video decoder driver
 *
 * Copyright 2013 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _ADV7842_
#define _ADV7842_

/* Analog input muxing modes (AFE register 0x02, [2:0]) */
enum adv7842_ain_sel {
	ADV7842_AIN1_2_3_NC_SYNC_1_2 = 0,
	ADV7842_AIN4_5_6_NC_SYNC_2_1 = 1,
	ADV7842_AIN7_8_9_NC_SYNC_3_1 = 2,
	ADV7842_AIN10_11_12_NC_SYNC_4_1 = 3,
	ADV7842_AIN9_4_5_6_SYNC_2_1 = 4,
};

/*
 * Bus rotation and reordering. This is used to specify component reordering on
 * the board and describes the components order on the bus when the ADV7842
 * outputs RGB.
 */
enum adv7842_bus_order {
	ADV7842_BUS_ORDER_RGB,		/* No operation	*/
	ADV7842_BUS_ORDER_GRB,		/* Swap 1-2	*/
	ADV7842_BUS_ORDER_RBG,		/* Swap 2-3	*/
	ADV7842_BUS_ORDER_BGR,		/* Swap 1-3	*/
	ADV7842_BUS_ORDER_BRG,		/* Rotate right	*/
	ADV7842_BUS_ORDER_GBR,		/* Rotate left	*/
};

/* Input Color Space (IO register 0x02, [7:4]) */
enum adv7842_inp_color_space {
	ADV7842_INP_COLOR_SPACE_LIM_RGB = 0,
	ADV7842_INP_COLOR_SPACE_FULL_RGB = 1,
	ADV7842_INP_COLOR_SPACE_LIM_YCbCr_601 = 2,
	ADV7842_INP_COLOR_SPACE_LIM_YCbCr_709 = 3,
	ADV7842_INP_COLOR_SPACE_XVYCC_601 = 4,
	ADV7842_INP_COLOR_SPACE_XVYCC_709 = 5,
	ADV7842_INP_COLOR_SPACE_FULL_YCbCr_601 = 6,
	ADV7842_INP_COLOR_SPACE_FULL_YCbCr_709 = 7,
	ADV7842_INP_COLOR_SPACE_AUTO = 0xf,
};

/* Select output format (IO register 0x03, [4:2]) */
enum adv7842_op_format_mode_sel {
	ADV7842_OP_FORMAT_MODE0 = 0x00,
	ADV7842_OP_FORMAT_MODE1 = 0x04,
	ADV7842_OP_FORMAT_MODE2 = 0x08,
};

/* Mode of operation */
enum adv7842_mode {
	ADV7842_MODE_SDP,
	ADV7842_MODE_COMP,
	ADV7842_MODE_RGB,
	ADV7842_MODE_HDMI
};

/* Video standard select (IO register 0x00, [5:0]) */
enum adv7842_vid_std_select {
	/* SDP */
	ADV7842_SDP_VID_STD_CVBS_SD_4x1 = 0x01,
	ADV7842_SDP_VID_STD_YC_SD4_x1 = 0x09,
	/* RGB */
	ADV7842_RGB_VID_STD_AUTO_GRAPH_MODE = 0x07,
	/* HDMI GR */
	ADV7842_HDMI_GR_VID_STD_AUTO_GRAPH_MODE = 0x02,
	/* HDMI COMP */
	ADV7842_HDMI_COMP_VID_STD_HD_1250P = 0x1e,
};

enum adv7842_select_input {
	ADV7842_SELECT_HDMI_PORT_A,
	ADV7842_SELECT_HDMI_PORT_B,
	ADV7842_SELECT_VGA_RGB,
	ADV7842_SELECT_VGA_COMP,
	ADV7842_SELECT_SDP_CVBS,
	ADV7842_SELECT_SDP_YC,
};

enum adv7842_drive_strength {
	ADV7842_DR_STR_LOW = 0,
	ADV7842_DR_STR_MEDIUM_LOW = 1,
	ADV7842_DR_STR_MEDIUM_HIGH = 2,
	ADV7842_DR_STR_HIGH = 3,
};

struct adv7842_sdp_csc_coeff {
	bool manual;
	u16 scaling;
	u16 A1;
	u16 A2;
	u16 A3;
	u16 A4;
	u16 B1;
	u16 B2;
	u16 B3;
	u16 B4;
	u16 C1;
	u16 C2;
	u16 C3;
	u16 C4;
};

struct adv7842_sdp_io_sync_adjustment {
	bool adjust;
	u16 hs_beg;
	u16 hs_width;
	u16 de_beg;
	u16 de_end;
	u8 vs_beg_o;
	u8 vs_beg_e;
	u8 vs_end_o;
	u8 vs_end_e;
	u8 de_v_beg_o;
	u8 de_v_beg_e;
	u8 de_v_end_o;
	u8 de_v_end_e;
};

/* Platform dependent definition */
struct adv7842_platform_data {
	/* chip reset during probe */
	unsigned chip_reset:1;

	/* DIS_PWRDNB: 1 if the PWRDNB pin is unused and unconnected */
	unsigned disable_pwrdnb:1;

	/* DIS_CABLE_DET_RST: 1 if the 5V pins are unused and unconnected */
	unsigned disable_cable_det_rst:1;

	/* Analog input muxing mode */
	enum adv7842_ain_sel ain_sel;

	/* Bus rotation and reordering */
	enum adv7842_bus_order bus_order;

	/* Select output format mode */
	enum adv7842_op_format_mode_sel op_format_mode_sel;

	/* Default mode */
	enum adv7842_mode mode;

	/* Default input */
	unsigned input;

	/* Video standard */
	enum adv7842_vid_std_select vid_std_select;

	/* IO register 0x02 */
	unsigned alt_gamma:1;

	/* IO register 0x05 */
	unsigned blank_data:1;
	unsigned insert_av_codes:1;
	unsigned replicate_av_codes:1;

	/* IO register 0x30 */
	unsigned output_bus_lsb_to_msb:1;

	/* IO register 0x14 */
	enum adv7842_drive_strength dr_str_data;
	enum adv7842_drive_strength dr_str_clk;
	enum adv7842_drive_strength dr_str_sync;

	/*
	 * IO register 0x19: Adjustment to the LLC DLL phase in
	 * increments of 1/32 of a clock period.
	 */
	unsigned llc_dll_phase:5;

	/* External RAM for 3-D comb or frame synchronizer */
	unsigned sd_ram_size; /* ram size in MB */
	unsigned sd_ram_ddr:1; /* ddr or sdr sdram */

	/* HDMI free run, CP-reg 0xBA */
	unsigned hdmi_free_run_enable:1;
	/* 0 = Mode 0: run when there is no TMDS clock
	   1 = Mode 1: run when there is no TMDS clock or the
	       video resolution does not match programmed one. */
	unsigned hdmi_free_run_mode:1;

	/* SDP free run, CP-reg 0xDD */
	unsigned sdp_free_run_auto:1;
	unsigned sdp_free_run_man_col_en:1;
	unsigned sdp_free_run_cbar_en:1;
	unsigned sdp_free_run_force:1;

	/* HPA manual (0) or auto (1), affects HDMI register 0x69 */
	unsigned hpa_auto:1;

	struct adv7842_sdp_csc_coeff sdp_csc_coeff;

	struct adv7842_sdp_io_sync_adjustment sdp_io_sync_625;
	struct adv7842_sdp_io_sync_adjustment sdp_io_sync_525;

	/* i2c addresses */
	u8 i2c_sdp_io;
	u8 i2c_sdp;
	u8 i2c_cp;
	u8 i2c_vdp;
	u8 i2c_afe;
	u8 i2c_hdmi;
	u8 i2c_repeater;
	u8 i2c_edid;
	u8 i2c_infoframe;
	u8 i2c_cec;
	u8 i2c_avlink;
};

#define V4L2_CID_ADV_RX_ANALOG_SAMPLING_PHASE	(V4L2_CID_DV_CLASS_BASE + 0x1000)
#define V4L2_CID_ADV_RX_FREE_RUN_COLOR_MANUAL	(V4L2_CID_DV_CLASS_BASE + 0x1001)
#define V4L2_CID_ADV_RX_FREE_RUN_COLOR		(V4L2_CID_DV_CLASS_BASE + 0x1002)

/* custom ioctl, used to test the external RAM that's used by the
 * deinterlacer. */
#define ADV7842_CMD_RAM_TEST _IO('V', BASE_VIDIOC_PRIVATE)

#define ADV7842_EDID_PORT_A   0
#define ADV7842_EDID_PORT_B   1
#define ADV7842_EDID_PORT_VGA 2
#define ADV7842_PAD_SOURCE    3

#endif
