/*
 * linux/include/asm-arm/arch-pxa/mfp.h
 *
 * Multi-Function Pin Definitions
 *
 * Copyright (C) 2007 Marvell International Ltd.
 *
 * 2007-8-21: eric miao <eric.miao@marvell.com>
 *            initial version
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MFP_H
#define __ASM_ARCH_MFP_H

#define MFPR_BASE	(0x40e10000)
#define MFPR_SIZE	(PAGE_SIZE)

#define mfp_to_gpio(m)	((m) % 128)

/* list of all the configurable MFP pins */
enum {
	MFP_PIN_INVALID = -1,

	MFP_PIN_GPIO0 = 0,
	MFP_PIN_GPIO1,
	MFP_PIN_GPIO2,
	MFP_PIN_GPIO3,
	MFP_PIN_GPIO4,
	MFP_PIN_GPIO5,
	MFP_PIN_GPIO6,
	MFP_PIN_GPIO7,
	MFP_PIN_GPIO8,
	MFP_PIN_GPIO9,
	MFP_PIN_GPIO10,
	MFP_PIN_GPIO11,
	MFP_PIN_GPIO12,
	MFP_PIN_GPIO13,
	MFP_PIN_GPIO14,
	MFP_PIN_GPIO15,
	MFP_PIN_GPIO16,
	MFP_PIN_GPIO17,
	MFP_PIN_GPIO18,
	MFP_PIN_GPIO19,
	MFP_PIN_GPIO20,
	MFP_PIN_GPIO21,
	MFP_PIN_GPIO22,
	MFP_PIN_GPIO23,
	MFP_PIN_GPIO24,
	MFP_PIN_GPIO25,
	MFP_PIN_GPIO26,
	MFP_PIN_GPIO27,
	MFP_PIN_GPIO28,
	MFP_PIN_GPIO29,
	MFP_PIN_GPIO30,
	MFP_PIN_GPIO31,
	MFP_PIN_GPIO32,
	MFP_PIN_GPIO33,
	MFP_PIN_GPIO34,
	MFP_PIN_GPIO35,
	MFP_PIN_GPIO36,
	MFP_PIN_GPIO37,
	MFP_PIN_GPIO38,
	MFP_PIN_GPIO39,
	MFP_PIN_GPIO40,
	MFP_PIN_GPIO41,
	MFP_PIN_GPIO42,
	MFP_PIN_GPIO43,
	MFP_PIN_GPIO44,
	MFP_PIN_GPIO45,
	MFP_PIN_GPIO46,
	MFP_PIN_GPIO47,
	MFP_PIN_GPIO48,
	MFP_PIN_GPIO49,
	MFP_PIN_GPIO50,
	MFP_PIN_GPIO51,
	MFP_PIN_GPIO52,
	MFP_PIN_GPIO53,
	MFP_PIN_GPIO54,
	MFP_PIN_GPIO55,
	MFP_PIN_GPIO56,
	MFP_PIN_GPIO57,
	MFP_PIN_GPIO58,
	MFP_PIN_GPIO59,
	MFP_PIN_GPIO60,
	MFP_PIN_GPIO61,
	MFP_PIN_GPIO62,
	MFP_PIN_GPIO63,
	MFP_PIN_GPIO64,
	MFP_PIN_GPIO65,
	MFP_PIN_GPIO66,
	MFP_PIN_GPIO67,
	MFP_PIN_GPIO68,
	MFP_PIN_GPIO69,
	MFP_PIN_GPIO70,
	MFP_PIN_GPIO71,
	MFP_PIN_GPIO72,
	MFP_PIN_GPIO73,
	MFP_PIN_GPIO74,
	MFP_PIN_GPIO75,
	MFP_PIN_GPIO76,
	MFP_PIN_GPIO77,
	MFP_PIN_GPIO78,
	MFP_PIN_GPIO79,
	MFP_PIN_GPIO80,
	MFP_PIN_GPIO81,
	MFP_PIN_GPIO82,
	MFP_PIN_GPIO83,
	MFP_PIN_GPIO84,
	MFP_PIN_GPIO85,
	MFP_PIN_GPIO86,
	MFP_PIN_GPIO87,
	MFP_PIN_GPIO88,
	MFP_PIN_GPIO89,
	MFP_PIN_GPIO90,
	MFP_PIN_GPIO91,
	MFP_PIN_GPIO92,
	MFP_PIN_GPIO93,
	MFP_PIN_GPIO94,
	MFP_PIN_GPIO95,
	MFP_PIN_GPIO96,
	MFP_PIN_GPIO97,
	MFP_PIN_GPIO98,
	MFP_PIN_GPIO99,
	MFP_PIN_GPIO100,
	MFP_PIN_GPIO101,
	MFP_PIN_GPIO102,
	MFP_PIN_GPIO103,
	MFP_PIN_GPIO104,
	MFP_PIN_GPIO105,
	MFP_PIN_GPIO106,
	MFP_PIN_GPIO107,
	MFP_PIN_GPIO108,
	MFP_PIN_GPIO109,
	MFP_PIN_GPIO110,
	MFP_PIN_GPIO111,
	MFP_PIN_GPIO112,
	MFP_PIN_GPIO113,
	MFP_PIN_GPIO114,
	MFP_PIN_GPIO115,
	MFP_PIN_GPIO116,
	MFP_PIN_GPIO117,
	MFP_PIN_GPIO118,
	MFP_PIN_GPIO119,
	MFP_PIN_GPIO120,
	MFP_PIN_GPIO121,
	MFP_PIN_GPIO122,
	MFP_PIN_GPIO123,
	MFP_PIN_GPIO124,
	MFP_PIN_GPIO125,
	MFP_PIN_GPIO126,
	MFP_PIN_GPIO127,
	MFP_PIN_GPIO0_2,
	MFP_PIN_GPIO1_2,
	MFP_PIN_GPIO2_2,
	MFP_PIN_GPIO3_2,
	MFP_PIN_GPIO4_2,
	MFP_PIN_GPIO5_2,
	MFP_PIN_GPIO6_2,
	MFP_PIN_GPIO7_2,
	MFP_PIN_GPIO8_2,
	MFP_PIN_GPIO9_2,
	MFP_PIN_GPIO10_2,
	MFP_PIN_GPIO11_2,
	MFP_PIN_GPIO12_2,
	MFP_PIN_GPIO13_2,
	MFP_PIN_GPIO14_2,
	MFP_PIN_GPIO15_2,
	MFP_PIN_GPIO16_2,
	MFP_PIN_GPIO17_2,

	MFP_PIN_ULPI_STP,
	MFP_PIN_ULPI_NXT,
	MFP_PIN_ULPI_DIR,

	MFP_PIN_nXCVREN,
	MFP_PIN_DF_CLE_nOE,
	MFP_PIN_DF_nADV1_ALE,
	MFP_PIN_DF_SCLK_E,
	MFP_PIN_DF_SCLK_S,
	MFP_PIN_nBE0,
	MFP_PIN_nBE1,
	MFP_PIN_DF_nADV2_ALE,
	MFP_PIN_DF_INT_RnB,
	MFP_PIN_DF_nCS0,
	MFP_PIN_DF_nCS1,
	MFP_PIN_nLUA,
	MFP_PIN_nLLA,
	MFP_PIN_DF_nWE,
	MFP_PIN_DF_ALE_nWE,
	MFP_PIN_DF_nRE_nOE,
	MFP_PIN_DF_ADDR0,
	MFP_PIN_DF_ADDR1,
	MFP_PIN_DF_ADDR2,
	MFP_PIN_DF_ADDR3,
	MFP_PIN_DF_IO0,
	MFP_PIN_DF_IO1,
	MFP_PIN_DF_IO2,
	MFP_PIN_DF_IO3,
	MFP_PIN_DF_IO4,
	MFP_PIN_DF_IO5,
	MFP_PIN_DF_IO6,
	MFP_PIN_DF_IO7,
	MFP_PIN_DF_IO8,
	MFP_PIN_DF_IO9,
	MFP_PIN_DF_IO10,
	MFP_PIN_DF_IO11,
	MFP_PIN_DF_IO12,
	MFP_PIN_DF_IO13,
	MFP_PIN_DF_IO14,
	MFP_PIN_DF_IO15,

	MFP_PIN_MAX,
};

/*
 * Table that determines the low power modes outputs, with actual settings
 * used in parentheses for don't-care values. Except for the float output,
 * the configured driven and pulled levels match, so if there is a need for
 * non-LPM pulled output, the same configuration could probably be used.
 *
 * Output value  sleep_oe_n  sleep_data  pullup_en  pulldown_en  pull_sel
 *                 (bit 7)    (bit 8)    (bit 14d)   (bit 13d)
 *
 * Drive 0          0          0           0           X (1)      0
 * Drive 1          0          1           X (1)       0	  0
 * Pull hi (1)      1          X(1)        1           0	  0
 * Pull lo (0)      1          X(0)        0           1	  0
 * Z (float)        1          X(0)        0           0	  0
 */
#define MFP_LPM_DRIVE_LOW	0x8
#define MFP_LPM_DRIVE_HIGH    	0x6
#define MFP_LPM_PULL_HIGH     	0x7
#define MFP_LPM_PULL_LOW      	0x9
#define MFP_LPM_FLOAT         	0x1
#define MFP_LPM_PULL_NEITHER	0x0

/*
 * The pullup and pulldown state of the MFP pin is by default determined by
 * selected alternate function. In case some buggy devices need to override
 * this default behavior,  pxa3xx_mfp_set_pull() can be invoked with one of
 * the following definition as the parameter.
 *
 * Definition       pull_sel  pullup_en  pulldown_en
 * MFP_PULL_HIGH        1         1        0
 * MFP_PULL_LOW         1         0        1
 * MFP_PULL_BOTH        1         1        1
 * MFP_PULL_NONE        1         0        0
 * MFP_PULL_DEFAULT     0         X        X
 *
 * NOTE: pxa3xx_mfp_set_pull() will modify the PULLUP_EN and PULLDOWN_EN
 * bits,  which will cause potential conflicts with the low power mode
 * setting, device drivers should take care of this
 */
#define MFP_PULL_BOTH		(0x7u)
#define MFP_PULL_HIGH		(0x6u)
#define MFP_PULL_LOW		(0x5u)
#define MFP_PULL_NONE		(0x4u)
#define MFP_PULL_DEFAULT	(0x0u)

#define MFP_AF0			(0)
#define MFP_AF1			(1)
#define MFP_AF2			(2)
#define MFP_AF3			(3)
#define MFP_AF4			(4)
#define MFP_AF5			(5)
#define MFP_AF6			(6)
#define MFP_AF7			(7)

#define MFP_DS01X		(0)
#define MFP_DS02X		(1)
#define MFP_DS03X		(2)
#define MFP_DS04X		(3)
#define MFP_DS06X		(4)
#define MFP_DS08X		(5)
#define MFP_DS10X		(6)
#define MFP_DS12X		(7)

#define MFP_EDGE_BOTH		0x3
#define MFP_EDGE_RISE		0x2
#define MFP_EDGE_FALL		0x1
#define MFP_EDGE_NONE		0x0

#define MFPR_AF_MASK		0x0007
#define MFPR_DRV_MASK		0x1c00
#define MFPR_RDH_MASK		0x0200
#define MFPR_LPM_MASK		0xe180
#define MFPR_PULL_MASK		0xe000
#define MFPR_EDGE_MASK		0x0070

#define MFPR_ALT_OFFSET		0
#define MFPR_ERE_OFFSET		4
#define MFPR_EFE_OFFSET		5
#define MFPR_EC_OFFSET		6
#define MFPR_SON_OFFSET		7
#define MFPR_SD_OFFSET		8
#define MFPR_SS_OFFSET		9
#define MFPR_DRV_OFFSET		10
#define MFPR_PD_OFFSET		13
#define MFPR_PU_OFFSET		14
#define MFPR_PS_OFFSET		15

#define MFPR(af, drv, rdh, lpm, edge) \
	(((af) & 0x7) | (((drv) & 0x7) << 10) |\
	 (((rdh) & 0x1) << 9) |\
	 (((lpm) & 0x3) << 7) |\
	 (((lpm) & 0x4) << 12)|\
	 (((lpm) & 0x8) << 10)|\
	 ((!(edge)) << 6) |\
	 (((edge) & 0x1) << 5) |\
	 (((edge) & 0x2) << 3))

/*
 * a possible MFP configuration is represented by a 32-bit integer
 * bit  0..15 - MFPR value (16-bit)
 * bit 16..31 - mfp pin index (used to obtain the MFPR offset)
 *
 * to facilitate the definition, the following macros are provided
 *
 * MFPR_DEFAULT - default MFPR value, with
 * 		  alternate function = 0,
 * 		  drive strength = fast 1mA (MFP_DS01X)
 * 		  low power mode = default
 * 		  release dalay hold = false (RDH bit)
 * 		  edge detection = none
 *
 * MFP_CFG	- default MFPR value with alternate function
 * MFP_CFG_DRV	- default MFPR value with alternate function and
 * 		  pin drive strength
 * MFP_CFG_LPM	- default MFPR value with alternate function and
 * 		  low power mode
 * MFP_CFG_X	- default MFPR value with alternate function,
 * 		  pin drive strength and low power mode
 *
 * use
 *
 * MFP_CFG_PIN	- to get the MFP pin index
 * MFP_CFG_VAL	- to get the corresponding MFPR value
 */

typedef uint32_t mfp_cfg_t;

#define MFP_CFG_PIN(mfp_cfg)	(((mfp_cfg) >> 16) & 0xffff)
#define MFP_CFG_VAL(mfp_cfg)	((mfp_cfg) & 0xffff)

/*
 * MFP register defaults to
 *   drive strength fast 3mA (010'b)
 *   edge detection logic disabled
 *   alternate function 0
 */
#define MFPR_DEFAULT	(0x0840)

#define MFP_CFG(pin, af)		\
	((MFP_PIN_##pin << 16) | MFPR_DEFAULT | (MFP_##af))

#define MFP_CFG_DRV(pin, af, drv)	\
	((MFP_PIN_##pin << 16) | (MFPR_DEFAULT & ~MFPR_DRV_MASK) |\
	 ((MFP_##drv) << 10) | (MFP_##af))

#define MFP_CFG_LPM(pin, af, lpm)	\
	((MFP_PIN_##pin << 16) | (MFPR_DEFAULT & ~MFPR_LPM_MASK) |\
	 (((MFP_LPM_##lpm) & 0x3) << 7)  |\
	 (((MFP_LPM_##lpm) & 0x4) << 12) |\
	 (((MFP_LPM_##lpm) & 0x8) << 10) |\
	 (MFP_##af))

#define MFP_CFG_X(pin, af, drv, lpm)	\
	((MFP_PIN_##pin << 16) |\
	 (MFPR_DEFAULT & ~(MFPR_DRV_MASK | MFPR_LPM_MASK)) |\
	 ((MFP_##drv) << 10) | (MFP_##af) |\
	 (((MFP_LPM_##lpm) & 0x3) << 7)  |\
	 (((MFP_LPM_##lpm) & 0x4) << 12) |\
	 (((MFP_LPM_##lpm) & 0x8) << 10))

/* common MFP configurations - processor specific ones defined
 * in mfp-pxa3xx.h
 */
#define GPIO0_GPIO		MFP_CFG(GPIO0, AF0)
#define GPIO1_GPIO		MFP_CFG(GPIO1, AF0)
#define GPIO2_GPIO		MFP_CFG(GPIO2, AF0)
#define GPIO3_GPIO		MFP_CFG(GPIO3, AF0)
#define GPIO4_GPIO		MFP_CFG(GPIO4, AF0)
#define GPIO5_GPIO		MFP_CFG(GPIO5, AF0)
#define GPIO6_GPIO		MFP_CFG(GPIO6, AF0)
#define GPIO7_GPIO		MFP_CFG(GPIO7, AF0)
#define GPIO8_GPIO		MFP_CFG(GPIO8, AF0)
#define GPIO9_GPIO		MFP_CFG(GPIO9, AF0)
#define GPIO10_GPIO		MFP_CFG(GPIO10, AF0)
#define GPIO11_GPIO		MFP_CFG(GPIO11, AF0)
#define GPIO12_GPIO		MFP_CFG(GPIO12, AF0)
#define GPIO13_GPIO		MFP_CFG(GPIO13, AF0)
#define GPIO14_GPIO		MFP_CFG(GPIO14, AF0)
#define GPIO15_GPIO		MFP_CFG(GPIO15, AF0)
#define GPIO16_GPIO		MFP_CFG(GPIO16, AF0)
#define GPIO17_GPIO		MFP_CFG(GPIO17, AF0)
#define GPIO18_GPIO		MFP_CFG(GPIO18, AF0)
#define GPIO19_GPIO		MFP_CFG(GPIO19, AF0)
#define GPIO20_GPIO		MFP_CFG(GPIO20, AF0)
#define GPIO21_GPIO		MFP_CFG(GPIO21, AF0)
#define GPIO22_GPIO		MFP_CFG(GPIO22, AF0)
#define GPIO23_GPIO		MFP_CFG(GPIO23, AF0)
#define GPIO24_GPIO		MFP_CFG(GPIO24, AF0)
#define GPIO25_GPIO		MFP_CFG(GPIO25, AF0)
#define GPIO26_GPIO		MFP_CFG(GPIO26, AF0)
#define GPIO27_GPIO		MFP_CFG(GPIO27, AF0)
#define GPIO28_GPIO		MFP_CFG(GPIO28, AF0)
#define GPIO29_GPIO		MFP_CFG(GPIO29, AF0)
#define GPIO30_GPIO		MFP_CFG(GPIO30, AF0)
#define GPIO31_GPIO		MFP_CFG(GPIO31, AF0)
#define GPIO32_GPIO		MFP_CFG(GPIO32, AF0)
#define GPIO33_GPIO		MFP_CFG(GPIO33, AF0)
#define GPIO34_GPIO		MFP_CFG(GPIO34, AF0)
#define GPIO35_GPIO		MFP_CFG(GPIO35, AF0)
#define GPIO36_GPIO		MFP_CFG(GPIO36, AF0)
#define GPIO37_GPIO		MFP_CFG(GPIO37, AF0)
#define GPIO38_GPIO		MFP_CFG(GPIO38, AF0)
#define GPIO39_GPIO		MFP_CFG(GPIO39, AF0)
#define GPIO40_GPIO		MFP_CFG(GPIO40, AF0)
#define GPIO41_GPIO		MFP_CFG(GPIO41, AF0)
#define GPIO42_GPIO		MFP_CFG(GPIO42, AF0)
#define GPIO43_GPIO		MFP_CFG(GPIO43, AF0)
#define GPIO44_GPIO		MFP_CFG(GPIO44, AF0)
#define GPIO45_GPIO		MFP_CFG(GPIO45, AF0)

#define GPIO47_GPIO		MFP_CFG(GPIO47, AF0)
#define GPIO48_GPIO		MFP_CFG(GPIO48, AF0)

#define GPIO53_GPIO		MFP_CFG(GPIO53, AF0)
#define GPIO54_GPIO		MFP_CFG(GPIO54, AF0)
#define GPIO55_GPIO		MFP_CFG(GPIO55, AF0)

#define GPIO57_GPIO		MFP_CFG(GPIO57, AF0)

#define GPIO63_GPIO		MFP_CFG(GPIO63, AF0)
#define GPIO64_GPIO		MFP_CFG(GPIO64, AF0)
#define GPIO65_GPIO		MFP_CFG(GPIO65, AF0)
#define GPIO66_GPIO		MFP_CFG(GPIO66, AF0)
#define GPIO67_GPIO		MFP_CFG(GPIO67, AF0)
#define GPIO68_GPIO		MFP_CFG(GPIO68, AF0)
#define GPIO69_GPIO		MFP_CFG(GPIO69, AF0)
#define GPIO70_GPIO		MFP_CFG(GPIO70, AF0)
#define GPIO71_GPIO		MFP_CFG(GPIO71, AF0)
#define GPIO72_GPIO		MFP_CFG(GPIO72, AF0)
#define GPIO73_GPIO		MFP_CFG(GPIO73, AF0)
#define GPIO74_GPIO		MFP_CFG(GPIO74, AF0)
#define GPIO75_GPIO		MFP_CFG(GPIO75, AF0)
#define GPIO76_GPIO		MFP_CFG(GPIO76, AF0)
#define GPIO77_GPIO		MFP_CFG(GPIO77, AF0)
#define GPIO78_GPIO		MFP_CFG(GPIO78, AF0)
#define GPIO79_GPIO		MFP_CFG(GPIO79, AF0)
#define GPIO80_GPIO		MFP_CFG(GPIO80, AF0)
#define GPIO81_GPIO		MFP_CFG(GPIO81, AF0)
#define GPIO82_GPIO		MFP_CFG(GPIO82, AF0)
#define GPIO83_GPIO		MFP_CFG(GPIO83, AF0)
#define GPIO84_GPIO		MFP_CFG(GPIO84, AF0)
#define GPIO85_GPIO		MFP_CFG(GPIO85, AF0)
#define GPIO86_GPIO		MFP_CFG(GPIO86, AF0)
#define GPIO87_GPIO		MFP_CFG(GPIO87, AF0)
#define GPIO88_GPIO		MFP_CFG(GPIO88, AF0)
#define GPIO89_GPIO		MFP_CFG(GPIO89, AF0)
#define GPIO90_GPIO		MFP_CFG(GPIO90, AF0)
#define GPIO91_GPIO		MFP_CFG(GPIO91, AF0)
#define GPIO92_GPIO		MFP_CFG(GPIO92, AF0)
#define GPIO93_GPIO		MFP_CFG(GPIO93, AF0)
#define GPIO94_GPIO		MFP_CFG(GPIO94, AF0)
#define GPIO95_GPIO		MFP_CFG(GPIO95, AF0)
#define GPIO96_GPIO		MFP_CFG(GPIO96, AF0)
#define GPIO97_GPIO		MFP_CFG(GPIO97, AF0)
#define GPIO98_GPIO		MFP_CFG(GPIO98, AF0)
#define GPIO99_GPIO		MFP_CFG(GPIO99, AF0)
#define GPIO100_GPIO		MFP_CFG(GPIO100, AF0)
#define GPIO101_GPIO		MFP_CFG(GPIO101, AF0)
#define GPIO102_GPIO		MFP_CFG(GPIO102, AF0)
#define GPIO103_GPIO		MFP_CFG(GPIO103, AF0)
#define GPIO104_GPIO		MFP_CFG(GPIO104, AF0)
#define GPIO105_GPIO		MFP_CFG(GPIO105, AF0)
#define GPIO106_GPIO		MFP_CFG(GPIO106, AF0)
#define GPIO107_GPIO		MFP_CFG(GPIO107, AF0)
#define GPIO108_GPIO		MFP_CFG(GPIO108, AF0)
#define GPIO109_GPIO		MFP_CFG(GPIO109, AF0)
#define GPIO110_GPIO		MFP_CFG(GPIO110, AF0)
#define GPIO111_GPIO		MFP_CFG(GPIO111, AF0)
#define GPIO112_GPIO		MFP_CFG(GPIO112, AF0)
#define GPIO113_GPIO		MFP_CFG(GPIO113, AF0)
#define GPIO114_GPIO		MFP_CFG(GPIO114, AF0)
#define GPIO115_GPIO		MFP_CFG(GPIO115, AF0)
#define GPIO116_GPIO		MFP_CFG(GPIO116, AF0)
#define GPIO117_GPIO		MFP_CFG(GPIO117, AF0)
#define GPIO118_GPIO		MFP_CFG(GPIO118, AF0)
#define GPIO119_GPIO		MFP_CFG(GPIO119, AF0)
#define GPIO120_GPIO		MFP_CFG(GPIO120, AF0)
#define GPIO121_GPIO		MFP_CFG(GPIO121, AF0)
#define GPIO122_GPIO		MFP_CFG(GPIO122, AF0)
#define GPIO123_GPIO		MFP_CFG(GPIO123, AF0)
#define GPIO124_GPIO		MFP_CFG(GPIO124, AF0)
#define GPIO125_GPIO		MFP_CFG(GPIO125, AF0)
#define GPIO126_GPIO		MFP_CFG(GPIO126, AF0)
#define GPIO127_GPIO		MFP_CFG(GPIO127, AF0)

#define GPIO0_2_GPIO		MFP_CFG(GPIO0_2, AF0)
#define GPIO1_2_GPIO		MFP_CFG(GPIO1_2, AF0)
#define GPIO2_2_GPIO		MFP_CFG(GPIO2_2, AF0)
#define GPIO3_2_GPIO		MFP_CFG(GPIO3_2, AF0)
#define GPIO4_2_GPIO		MFP_CFG(GPIO4_2, AF0)
#define GPIO5_2_GPIO		MFP_CFG(GPIO5_2, AF0)
#define GPIO6_2_GPIO		MFP_CFG(GPIO6_2, AF0)

/*
 * each MFP pin will have a MFPR register, since the offset of the
 * register varies between processors, the processor specific code
 * should initialize the pin offsets by pxa3xx_mfp_init_addr()
 *
 * pxa3xx_mfp_init_addr - accepts a table of "pxa3xx_mfp_addr_map"
 * structure, which represents a range of MFP pins from "start" to
 * "end", with the offset begining at "offset", to define a single
 * pin, let "end" = -1
 *
 * use
 *
 * MFP_ADDR_X() to define a range of pins
 * MFP_ADDR()   to define a single pin
 * MFP_ADDR_END to signal the end of pin offset definitions
 */
struct pxa3xx_mfp_addr_map {
	unsigned int	start;
	unsigned int	end;
	unsigned long	offset;
};

#define MFP_ADDR_X(start, end, offset) \
	{ MFP_PIN_##start, MFP_PIN_##end, offset }

#define MFP_ADDR(pin, offset) \
	{ MFP_PIN_##pin, -1, offset }

#define MFP_ADDR_END	{ MFP_PIN_INVALID, 0 }

struct pxa3xx_mfp_pin {
	unsigned long	mfpr_off;	/* MFPRxx register offset */
	unsigned long	mfpr_val;	/* MFPRxx register value */
};

/*
 * pxa3xx_mfp_read()/pxa3xx_mfp_write() - for direct read/write access
 * to the MFPR register
 */
unsigned long pxa3xx_mfp_read(int mfp);
void pxa3xx_mfp_write(int mfp, unsigned long mfpr_val);

/*
 * pxa3xx_mfp_set_afds - set MFP alternate function and drive strength
 * pxa3xx_mfp_set_rdh  - set MFP release delay hold on/off
 * pxa3xx_mfp_set_lpm  - set MFP low power mode state
 * pxa3xx_mfp_set_edge - set MFP edge detection in low power mode
 *
 * use these functions to override/change the default configuration
 * done by pxa3xx_mfp_set_config(s)
 */
void pxa3xx_mfp_set_afds(int mfp, int af, int ds);
void pxa3xx_mfp_set_rdh(int mfp, int rdh);
void pxa3xx_mfp_set_lpm(int mfp, int lpm);
void pxa3xx_mfp_set_edge(int mfp, int edge);

/*
 * pxa3xx_mfp_config - configure the MFPR registers
 *
 * used by board specific initialization code
 */
void pxa3xx_mfp_config(mfp_cfg_t *mfp_cfgs, int num);

/*
 * pxa3xx_mfp_init_addr() - initialize the mapping between mfp pin
 * index and MFPR register offset
 *
 * used by processor specific code
 */
void __init pxa3xx_mfp_init_addr(struct pxa3xx_mfp_addr_map *);
void __init pxa3xx_init_mfp(void);

#endif /* __ASM_ARCH_MFP_H */
