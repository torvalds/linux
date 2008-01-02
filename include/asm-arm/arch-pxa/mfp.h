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
