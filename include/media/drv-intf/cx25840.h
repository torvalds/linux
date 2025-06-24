/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 *   cx25840.h - definition for cx25840/1/2/3 inputs
 *
 *   Copyright (C) 2006 Hans Verkuil (hverkuil@kernel.org)
 */

#ifndef _CX25840_H_
#define _CX25840_H_

/*
 * Note that the cx25840 driver requires that the bridge driver calls the
 * v4l2_subdev's load_fw operation in order to load the driver's firmware.
 * This will load the firmware on the first invocation (further ones are NOP).
 * Without this the audio standard detection will fail and you will
 * only get mono.
 * Alternatively, you can call the reset operation (this can be done
 * multiple times if needed, each invocation will fully reinitialize
 * the device).
 *
 * Since loading the firmware is often problematic when the driver is
 * compiled into the kernel I recommend postponing calling this function
 * until the first open of the video device. Another reason for
 * postponing it is that loading this firmware takes a long time (seconds)
 * due to the slow i2c bus speed. So it will speed up the boot process if
 * you can avoid loading the fw as long as the video device isn't used.
 */

enum cx25840_video_input {
	/* Composite video inputs In1-In8 */
	CX25840_COMPOSITE1 = 1,
	CX25840_COMPOSITE2,
	CX25840_COMPOSITE3,
	CX25840_COMPOSITE4,
	CX25840_COMPOSITE5,
	CX25840_COMPOSITE6,
	CX25840_COMPOSITE7,
	CX25840_COMPOSITE8,

	/*
	 * S-Video inputs consist of one luma input (In1-In8) ORed with one
	 * chroma input (In5-In8)
	 */
	CX25840_SVIDEO_LUMA1 = 0x10,
	CX25840_SVIDEO_LUMA2 = 0x20,
	CX25840_SVIDEO_LUMA3 = 0x30,
	CX25840_SVIDEO_LUMA4 = 0x40,
	CX25840_SVIDEO_LUMA5 = 0x50,
	CX25840_SVIDEO_LUMA6 = 0x60,
	CX25840_SVIDEO_LUMA7 = 0x70,
	CX25840_SVIDEO_LUMA8 = 0x80,
	CX25840_SVIDEO_CHROMA4 = 0x400,
	CX25840_SVIDEO_CHROMA5 = 0x500,
	CX25840_SVIDEO_CHROMA6 = 0x600,
	CX25840_SVIDEO_CHROMA7 = 0x700,
	CX25840_SVIDEO_CHROMA8 = 0x800,

	/* S-Video aliases for common luma/chroma combinations */
	CX25840_SVIDEO1 = 0x510,
	CX25840_SVIDEO2 = 0x620,
	CX25840_SVIDEO3 = 0x730,
	CX25840_SVIDEO4 = 0x840,

	/* Allow frames to specify specific input configurations */
	CX25840_VIN1_CH1  = 0x80000000,
	CX25840_VIN2_CH1  = 0x80000001,
	CX25840_VIN3_CH1  = 0x80000002,
	CX25840_VIN4_CH1  = 0x80000003,
	CX25840_VIN5_CH1  = 0x80000004,
	CX25840_VIN6_CH1  = 0x80000005,
	CX25840_VIN7_CH1  = 0x80000006,
	CX25840_VIN8_CH1  = 0x80000007,
	CX25840_VIN4_CH2  = 0x80000000,
	CX25840_VIN5_CH2  = 0x80000010,
	CX25840_VIN6_CH2  = 0x80000020,
	CX25840_NONE_CH2  = 0x80000030,
	CX25840_VIN7_CH3  = 0x80000000,
	CX25840_VIN8_CH3  = 0x80000040,
	CX25840_NONE0_CH3 = 0x80000080,
	CX25840_NONE1_CH3 = 0x800000c0,
	CX25840_SVIDEO_ON = 0x80000100,
	CX25840_COMPONENT_ON = 0x80000200,
	CX25840_DIF_ON = 0x80000400,
};

/*
 * The defines below are used to set the chip video output settings
 * in the generic mode that can be enabled by calling the subdevice
 * init core op.
 *
 * The requested settings can be passed to the init core op as
 * @val parameter and to the s_routing video op as @config parameter.
 *
 * For details please refer to the section 3.7 Video Output Formatting and
 * to Video Out Control 1 to 4 registers in the section 5.6 Video Decoder Core
 * of the chip datasheet.
 */
#define CX25840_VCONFIG_FMT_SHIFT 0
#define CX25840_VCONFIG_FMT_MASK GENMASK(2, 0)
#define CX25840_VCONFIG_FMT_BT601 BIT(0)
#define CX25840_VCONFIG_FMT_BT656 BIT(1)
#define CX25840_VCONFIG_FMT_VIP11 GENMASK(1, 0)
#define CX25840_VCONFIG_FMT_VIP2 BIT(2)

#define CX25840_VCONFIG_RES_SHIFT 3
#define CX25840_VCONFIG_RES_MASK GENMASK(4, 3)
#define CX25840_VCONFIG_RES_8BIT BIT(3)
#define CX25840_VCONFIG_RES_10BIT BIT(4)

#define CX25840_VCONFIG_VBIRAW_SHIFT 5
#define CX25840_VCONFIG_VBIRAW_MASK GENMASK(6, 5)
#define CX25840_VCONFIG_VBIRAW_DISABLED BIT(5)
#define CX25840_VCONFIG_VBIRAW_ENABLED BIT(6)

#define CX25840_VCONFIG_ANCDATA_SHIFT 7
#define CX25840_VCONFIG_ANCDATA_MASK GENMASK(8, 7)
#define CX25840_VCONFIG_ANCDATA_DISABLED BIT(7)
#define CX25840_VCONFIG_ANCDATA_ENABLED BIT(8)

#define CX25840_VCONFIG_TASKBIT_SHIFT 9
#define CX25840_VCONFIG_TASKBIT_MASK GENMASK(10, 9)
#define CX25840_VCONFIG_TASKBIT_ZERO BIT(9)
#define CX25840_VCONFIG_TASKBIT_ONE BIT(10)

#define CX25840_VCONFIG_ACTIVE_SHIFT 11
#define CX25840_VCONFIG_ACTIVE_MASK GENMASK(12, 11)
#define CX25840_VCONFIG_ACTIVE_COMPOSITE BIT(11)
#define CX25840_VCONFIG_ACTIVE_HORIZONTAL BIT(12)

#define CX25840_VCONFIG_VALID_SHIFT 13
#define CX25840_VCONFIG_VALID_MASK GENMASK(14, 13)
#define CX25840_VCONFIG_VALID_NORMAL BIT(13)
#define CX25840_VCONFIG_VALID_ANDACTIVE BIT(14)

#define CX25840_VCONFIG_HRESETW_SHIFT 15
#define CX25840_VCONFIG_HRESETW_MASK GENMASK(16, 15)
#define CX25840_VCONFIG_HRESETW_NORMAL BIT(15)
#define CX25840_VCONFIG_HRESETW_PIXCLK BIT(16)

#define CX25840_VCONFIG_CLKGATE_SHIFT 17
#define CX25840_VCONFIG_CLKGATE_MASK GENMASK(18, 17)
#define CX25840_VCONFIG_CLKGATE_NONE BIT(17)
#define CX25840_VCONFIG_CLKGATE_VALID BIT(18)
#define CX25840_VCONFIG_CLKGATE_VALIDACTIVE GENMASK(18, 17)

#define CX25840_VCONFIG_DCMODE_SHIFT 19
#define CX25840_VCONFIG_DCMODE_MASK GENMASK(20, 19)
#define CX25840_VCONFIG_DCMODE_DWORDS BIT(19)
#define CX25840_VCONFIG_DCMODE_BYTES BIT(20)

#define CX25840_VCONFIG_IDID0S_SHIFT 21
#define CX25840_VCONFIG_IDID0S_MASK GENMASK(22, 21)
#define CX25840_VCONFIG_IDID0S_NORMAL BIT(21)
#define CX25840_VCONFIG_IDID0S_LINECNT BIT(22)

#define CX25840_VCONFIG_VIPCLAMP_SHIFT 23
#define CX25840_VCONFIG_VIPCLAMP_MASK GENMASK(24, 23)
#define CX25840_VCONFIG_VIPCLAMP_ENABLED BIT(23)
#define CX25840_VCONFIG_VIPCLAMP_DISABLED BIT(24)

enum cx25840_audio_input {
	/* Audio inputs: serial or In4-In8 */
	CX25840_AUDIO_SERIAL,
	CX25840_AUDIO4 = 4,
	CX25840_AUDIO5,
	CX25840_AUDIO6,
	CX25840_AUDIO7,
	CX25840_AUDIO8,
};

enum cx25840_io_pin {
	CX25840_PIN_DVALID_PRGM0 = 0,
	CX25840_PIN_FIELD_PRGM1,
	CX25840_PIN_HRESET_PRGM2,
	CX25840_PIN_VRESET_HCTL_PRGM3,
	CX25840_PIN_IRQ_N_PRGM4,
	CX25840_PIN_IR_TX_PRGM6,
	CX25840_PIN_IR_RX_PRGM5,
	CX25840_PIN_GPIO0_PRGM8,
	CX25840_PIN_GPIO1_PRGM9,
	CX25840_PIN_SA_SDIN,		/* Alternate GP Input only */
	CX25840_PIN_SA_SDOUT,		/* Alternate GP Input only */
	CX25840_PIN_PLL_CLK_PRGM7,
	CX25840_PIN_CHIP_SEL_VIPCLK,	/* Output only */
};

enum cx25840_io_pad {
	/* Output pads, these must match the actual chip register values */
	CX25840_PAD_DEFAULT = 0,
	CX25840_PAD_ACTIVE,
	CX25840_PAD_VACTIVE,
	CX25840_PAD_CBFLAG,
	CX25840_PAD_VID_DATA_EXT0,
	CX25840_PAD_VID_DATA_EXT1,
	CX25840_PAD_GPO0,
	CX25840_PAD_GPO1,
	CX25840_PAD_GPO2,
	CX25840_PAD_GPO3,
	CX25840_PAD_IRQ_N,
	CX25840_PAD_AC_SYNC,
	CX25840_PAD_AC_SDOUT,
	CX25840_PAD_PLL_CLK,
	CX25840_PAD_VRESET,
	CX25840_PAD_RESERVED,
	/* Pads for PLL_CLK output only */
	CX25840_PAD_XTI_X5_DLL,
	CX25840_PAD_AUX_PLL,
	CX25840_PAD_VID_PLL,
	CX25840_PAD_XTI,
	/* Input Pads */
	CX25840_PAD_GPI0,
	CX25840_PAD_GPI1,
	CX25840_PAD_GPI2,
	CX25840_PAD_GPI3,
};

enum cx25840_io_pin_strength {
	CX25840_PIN_DRIVE_MEDIUM = 0,
	CX25840_PIN_DRIVE_SLOW,
	CX25840_PIN_DRIVE_FAST,
};

enum cx23885_io_pin {
	CX23885_PIN_IR_RX_GPIO19,
	CX23885_PIN_IR_TX_GPIO20,
	CX23885_PIN_I2S_SDAT_GPIO21,
	CX23885_PIN_I2S_WCLK_GPIO22,
	CX23885_PIN_I2S_BCLK_GPIO23,
	CX23885_PIN_IRQ_N_GPIO16,
};

enum cx23885_io_pad {
	CX23885_PAD_IR_RX,
	CX23885_PAD_GPIO19,
	CX23885_PAD_IR_TX,
	CX23885_PAD_GPIO20,
	CX23885_PAD_I2S_SDAT,
	CX23885_PAD_GPIO21,
	CX23885_PAD_I2S_WCLK,
	CX23885_PAD_GPIO22,
	CX23885_PAD_I2S_BCLK,
	CX23885_PAD_GPIO23,
	CX23885_PAD_IRQ_N,
	CX23885_PAD_GPIO16,
};

/*
 * pvr150_workaround activates a workaround for a hardware bug that is
 * present in Hauppauge PVR-150 (and possibly PVR-500) cards that have
 * certain NTSC tuners (tveeprom tuner model numbers 85, 99 and 112). The
 * audio autodetect fails on some channels for these models and the workaround
 * is to select the audio standard explicitly. Many thanks to Hauppauge for
 * providing this information.
 *
 * This platform data only needs to be supplied by the ivtv driver.
 */
struct cx25840_platform_data {
	int pvr150_workaround;
};

#endif
