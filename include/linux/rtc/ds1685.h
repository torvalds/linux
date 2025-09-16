/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Definitions for the registers, addresses, and platform data of the
 * DS1685/DS1687-series RTC chips.
 *
 * This Driver also works for the DS17X85/DS17X87 RTC chips.  Functionally
 * similar to the DS1685/DS1687, they support a few extra features which
 * include larger, battery-backed NV-SRAM, burst-mode access, and an RTC
 * write counter.
 *
 * Copyright (C) 2011-2014 Joshua Kinard <linux@kumba.dev>.
 * Copyright (C) 2009 Matthias Fuchs <matthias.fuchs@esd-electronics.com>.
 *
 * References:
 *    DS1685/DS1687 3V/5V Real-Time Clocks, 19-5215, Rev 4/10.
 *    DS17x85/DS17x87 3V/5V Real-Time Clocks, 19-5222, Rev 4/10.
 *    DS1689/DS1693 3V/5V Serialized Real-Time Clocks, Rev 112105.
 *    Application Note 90, Using the Multiplex Bus RTC Extended Features.
 */

#ifndef _LINUX_RTC_DS1685_H_
#define _LINUX_RTC_DS1685_H_

#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

/**
 * struct ds1685_priv - DS1685 private data structure.
 * @dev: pointer to the rtc_device structure.
 * @regs: iomapped base address pointer of the RTC registers.
 * @regstep: padding/step size between registers (optional).
 * @baseaddr: base address of the RTC device.
 * @size: resource size.
 * @lock: private lock variable for spin locking/unlocking.
 * @work: private workqueue.
 * @irq: IRQ number assigned to the RTC device.
 * @prepare_poweroff: pointer to platform pre-poweroff function.
 * @wake_alarm: pointer to platform wake alarm function.
 * @post_ram_clear: pointer to platform post ram-clear function.
 */
struct ds1685_priv {
	struct rtc_device *dev;
	void __iomem *regs;
	void __iomem *data;
	u32 regstep;
	int irq_num;
	bool bcd_mode;
	u8 (*read)(struct ds1685_priv *, int);
	void (*write)(struct ds1685_priv *, int, u8);
	void (*prepare_poweroff)(void);
	void (*wake_alarm)(void);
	void (*post_ram_clear)(void);
};


/**
 * struct ds1685_rtc_platform_data - platform data structure.
 * @plat_prepare_poweroff: platform-specific pre-poweroff function.
 * @plat_wake_alarm: platform-specific wake alarm function.
 * @plat_post_ram_clear: platform-specific post ram-clear function.
 *
 * If your platform needs to use a custom padding/step size between
 * registers, or uses one or more of the extended interrupts and needs special
 * handling, then include this header file in your platform definition and
 * set regstep and the plat_* pointers as appropriate.
 */
struct ds1685_rtc_platform_data {
	const u32 regstep;
	const bool bcd_mode;
	const bool no_irq;
	const bool uie_unsupported;
	void (*plat_prepare_poweroff)(void);
	void (*plat_wake_alarm)(void);
	void (*plat_post_ram_clear)(void);
	enum {
		ds1685_reg_direct,
		ds1685_reg_indirect
	} access_type;
};


/*
 * Time Registers.
 */
#define RTC_SECS		0x00	/* Seconds 00-59 */
#define RTC_SECS_ALARM		0x01	/* Alarm Seconds 00-59 */
#define RTC_MINS		0x02	/* Minutes 00-59 */
#define RTC_MINS_ALARM		0x03	/* Alarm Minutes 00-59 */
#define RTC_HRS			0x04	/* Hours 01-12 AM/PM || 00-23 */
#define RTC_HRS_ALARM		0x05	/* Alarm Hours 01-12 AM/PM || 00-23 */
#define RTC_WDAY		0x06	/* Day of Week 01-07 */
#define RTC_MDAY		0x07	/* Day of Month 01-31 */
#define RTC_MONTH		0x08	/* Month 01-12 */
#define RTC_YEAR		0x09	/* Year 00-99 */
#define RTC_CENTURY		0x48	/* Century 00-99 */
#define RTC_MDAY_ALARM		0x49	/* Alarm Day of Month 01-31 */


/*
 * Bit masks for the Time registers in BCD Mode (DM = 0).
 */
#define RTC_SECS_BCD_MASK	0x7f	/* - x x x x x x x */
#define RTC_MINS_BCD_MASK	0x7f	/* - x x x x x x x */
#define RTC_HRS_12_BCD_MASK	0x1f	/* - - - x x x x x */
#define RTC_HRS_24_BCD_MASK	0x3f	/* - - x x x x x x */
#define RTC_MDAY_BCD_MASK	0x3f	/* - - x x x x x x */
#define RTC_MONTH_BCD_MASK	0x1f	/* - - - x x x x x */
#define RTC_YEAR_BCD_MASK	0xff	/* x x x x x x x x */

/*
 * Bit masks for the Time registers in BIN Mode (DM = 1).
 */
#define RTC_SECS_BIN_MASK	0x3f	/* - - x x x x x x */
#define RTC_MINS_BIN_MASK	0x3f	/* - - x x x x x x */
#define RTC_HRS_12_BIN_MASK	0x0f	/* - - - - x x x x */
#define RTC_HRS_24_BIN_MASK	0x1f	/* - - - x x x x x */
#define RTC_MDAY_BIN_MASK	0x1f	/* - - - x x x x x */
#define RTC_MONTH_BIN_MASK	0x0f	/* - - - - x x x x */
#define RTC_YEAR_BIN_MASK	0x7f	/* - x x x x x x x */

/*
 * Bit masks common for the Time registers in BCD or BIN Mode.
 */
#define RTC_WDAY_MASK		0x07	/* - - - - - x x x */
#define RTC_CENTURY_MASK	0xff	/* x x x x x x x x */
#define RTC_MDAY_ALARM_MASK	0xff	/* x x x x x x x x */
#define RTC_HRS_AMPM_MASK	BIT(7)	/* Mask for the AM/PM bit */



/*
 * Control Registers.
 */
#define RTC_CTRL_A		0x0a	/* Control Register A */
#define RTC_CTRL_B		0x0b	/* Control Register B */
#define RTC_CTRL_C		0x0c	/* Control Register C */
#define RTC_CTRL_D		0x0d	/* Control Register D */
#define RTC_EXT_CTRL_4A		0x4a	/* Extended Control Register 4A */
#define RTC_EXT_CTRL_4B		0x4b	/* Extended Control Register 4B */


/*
 * Bit names in Control Register A.
 */
#define RTC_CTRL_A_UIP		BIT(7)	/* Update In Progress */
#define RTC_CTRL_A_DV2		BIT(6)	/* Countdown Chain */
#define RTC_CTRL_A_DV1		BIT(5)	/* Oscillator Enable */
#define RTC_CTRL_A_DV0		BIT(4)	/* Bank Select */
#define RTC_CTRL_A_RS2		BIT(2)	/* Rate-Selection Bit 2 */
#define RTC_CTRL_A_RS3		BIT(3)	/* Rate-Selection Bit 3 */
#define RTC_CTRL_A_RS1		BIT(1)	/* Rate-Selection Bit 1 */
#define RTC_CTRL_A_RS0		BIT(0)	/* Rate-Selection Bit 0 */
#define RTC_CTRL_A_RS_MASK	0x0f	/* RS3 + RS2 + RS1 + RS0 */

/*
 * Bit names in Control Register B.
 */
#define RTC_CTRL_B_SET		BIT(7)	/* SET Bit */
#define RTC_CTRL_B_PIE		BIT(6)	/* Periodic-Interrupt Enable */
#define RTC_CTRL_B_AIE		BIT(5)	/* Alarm-Interrupt Enable */
#define RTC_CTRL_B_UIE		BIT(4)	/* Update-Ended Interrupt-Enable */
#define RTC_CTRL_B_SQWE		BIT(3)	/* Square-Wave Enable */
#define RTC_CTRL_B_DM		BIT(2)	/* Data Mode */
#define RTC_CTRL_B_2412		BIT(1)	/* 12-Hr/24-Hr Mode */
#define RTC_CTRL_B_DSE		BIT(0)	/* Daylight Savings Enable */
#define RTC_CTRL_B_PAU_MASK	0x70	/* PIE + AIE + UIE */


/*
 * Bit names in Control Register C.
 *
 * BIT(0), BIT(1), BIT(2), & BIT(3) are unused, always return 0, and cannot
 * be written to.
 */
#define RTC_CTRL_C_IRQF		BIT(7)	/* Interrupt-Request Flag */
#define RTC_CTRL_C_PF		BIT(6)	/* Periodic-Interrupt Flag */
#define RTC_CTRL_C_AF		BIT(5)	/* Alarm-Interrupt Flag */
#define RTC_CTRL_C_UF		BIT(4)	/* Update-Ended Interrupt Flag */
#define RTC_CTRL_C_PAU_MASK	0x70	/* PF + AF + UF */


/*
 * Bit names in Control Register D.
 *
 * BIT(0) through BIT(6) are unused, always return 0, and cannot
 * be written to.
 */
#define RTC_CTRL_D_VRT		BIT(7)	/* Valid RAM and Time */


/*
 * Bit names in Extended Control Register 4A.
 *
 * On the DS1685/DS1687/DS1689/DS1693, BIT(4) and BIT(5) are reserved for
 * future use.  They can be read from and written to, but have no effect
 * on the RTC's operation.
 *
 * On the DS17x85/DS17x87, BIT(5) is Burst-Mode Enable (BME), and allows
 * access to the extended NV-SRAM by automatically incrementing the address
 * register when they are read from or written to.
 */
#define RTC_CTRL_4A_VRT2	BIT(7)	/* Auxillary Battery Status */
#define RTC_CTRL_4A_INCR	BIT(6)	/* Increment-in-Progress Status */
#define RTC_CTRL_4A_PAB		BIT(3)	/* Power-Active Bar Control */
#define RTC_CTRL_4A_RF		BIT(2)	/* RAM-Clear Flag */
#define RTC_CTRL_4A_WF		BIT(1)	/* Wake-Up Alarm Flag */
#define RTC_CTRL_4A_KF		BIT(0)	/* Kickstart Flag */
#if !defined(CONFIG_RTC_DRV_DS1685) && !defined(CONFIG_RTC_DRV_DS1689)
#define RTC_CTRL_4A_BME		BIT(5)	/* Burst-Mode Enable */
#endif
#define RTC_CTRL_4A_RWK_MASK	0x07	/* RF + WF + KF */


/*
 * Bit names in Extended Control Register 4B.
 */
#define RTC_CTRL_4B_ABE		BIT(7)	/* Auxillary Battery Enable */
#define RTC_CTRL_4B_E32K	BIT(6)	/* Enable 32.768Hz on SQW Pin */
#define RTC_CTRL_4B_CS		BIT(5)	/* Crystal Select */
#define RTC_CTRL_4B_RCE		BIT(4)	/* RAM Clear-Enable */
#define RTC_CTRL_4B_PRS		BIT(3)	/* PAB Reset-Select */
#define RTC_CTRL_4B_RIE		BIT(2)	/* RAM Clear-Interrupt Enable */
#define RTC_CTRL_4B_WIE		BIT(1)	/* Wake-Up Alarm-Interrupt Enable */
#define RTC_CTRL_4B_KSE		BIT(0)	/* Kickstart Interrupt-Enable */
#define RTC_CTRL_4B_RWK_MASK	0x07	/* RIE + WIE + KSE */


/*
 * Misc register names in Bank 1.
 *
 * The DV0 bit in Control Register A must be set to 1 for these registers
 * to become available, including Extended Control Registers 4A & 4B.
 */
#define RTC_BANK1_SSN_MODEL	0x40	/* Model Number */
#define RTC_BANK1_SSN_BYTE_1	0x41	/* 1st Byte of Serial Number */
#define RTC_BANK1_SSN_BYTE_2	0x42	/* 2nd Byte of Serial Number */
#define RTC_BANK1_SSN_BYTE_3	0x43	/* 3rd Byte of Serial Number */
#define RTC_BANK1_SSN_BYTE_4	0x44	/* 4th Byte of Serial Number */
#define RTC_BANK1_SSN_BYTE_5	0x45	/* 5th Byte of Serial Number */
#define RTC_BANK1_SSN_BYTE_6	0x46	/* 6th Byte of Serial Number */
#define RTC_BANK1_SSN_CRC	0x47	/* Serial CRC Byte */
#define RTC_BANK1_RAM_DATA_PORT	0x53	/* Extended RAM Data Port */


/*
 * Model-specific registers in Bank 1.
 *
 * The addresses below differ depending on the model of the RTC chip
 * selected in the kernel configuration.  Not all of these features are
 * supported in the main driver at present.
 *
 * DS1685/DS1687   - Extended NV-SRAM address (LSB only).
 * DS1689/DS1693   - Vcc, Vbat, Pwr Cycle Counters & Customer-specific S/N.
 * DS17x85/DS17x87 - Extended NV-SRAM addresses (MSB & LSB) & Write counter.
 */
#if defined(CONFIG_RTC_DRV_DS1685)
#define RTC_BANK1_RAM_ADDR	0x50	/* NV-SRAM Addr */
#elif defined(CONFIG_RTC_DRV_DS1689)
#define RTC_BANK1_VCC_CTR_LSB	0x54	/* Vcc Counter Addr (LSB) */
#define RTC_BANK1_VCC_CTR_MSB	0x57	/* Vcc Counter Addr (MSB) */
#define RTC_BANK1_VBAT_CTR_LSB	0x58	/* Vbat Counter Addr (LSB) */
#define RTC_BANK1_VBAT_CTR_MSB	0x5b	/* Vbat Counter Addr (MSB) */
#define RTC_BANK1_PWR_CTR_LSB	0x5c	/* Pwr Cycle Counter Addr (LSB) */
#define RTC_BANK1_PWR_CTR_MSB	0x5d	/* Pwr Cycle Counter Addr (MSB) */
#define RTC_BANK1_UNIQ_SN	0x60	/* Customer-specific S/N */
#else /* DS17x85/DS17x87 */
#define RTC_BANK1_RAM_ADDR_LSB	0x50	/* NV-SRAM Addr (LSB) */
#define RTC_BANK1_RAM_ADDR_MSB	0x51	/* NV-SRAM Addr (MSB) */
#define RTC_BANK1_WRITE_CTR	0x5e	/* RTC Write Counter */
#endif


/*
 * Model numbers.
 *
 * The DS1688/DS1691 and DS1689/DS1693 chips share the same model number
 * and the manual doesn't indicate any major differences.  As such, they
 * are regarded as the same chip in this driver.
 */
#define RTC_MODEL_DS1685	0x71	/* DS1685/DS1687 */
#define RTC_MODEL_DS17285	0x72	/* DS17285/DS17287 */
#define RTC_MODEL_DS1689	0x73	/* DS1688/DS1691/DS1689/DS1693 */
#define RTC_MODEL_DS17485	0x74	/* DS17485/DS17487 */
#define RTC_MODEL_DS17885	0x78	/* DS17885/DS17887 */


/*
 * Periodic Interrupt Rates / Square-Wave Output Frequency
 *
 * Periodic rates are selected by setting the RS3-RS0 bits in Control
 * Register A and enabled via either the E32K bit in Extended Control
 * Register 4B or the SQWE bit in Control Register B.
 *
 * E32K overrides the settings of RS3-RS0 and outputs a frequency of 32768Hz
 * on the SQW pin of the RTC chip.  While there are 16 possible selections,
 * the 1-of-16 decoder is only able to divide the base 32768Hz signal into 13
 * smaller frequencies.  The values 0x01 and 0x02 are not used and are
 * synonymous with 0x08 and 0x09, respectively.
 *
 * When E32K is set to a logic 1, periodic interrupts are disabled and reading
 * /dev/rtc will return -EINVAL.  This also applies if the periodic interrupt
 * frequency is set to 0Hz.
 *
 * Not currently used by the rtc-ds1685 driver because the RTC core removed
 * support for hardware-generated periodic-interrupts in favour of
 * hrtimer-generated interrupts.  But these defines are kept around for use
 * in userland, as documentation to the hardware, and possible future use if
 * hardware-generated periodic interrupts are ever added back.
 */
					/* E32K RS3 RS2 RS1 RS0 */
#define RTC_SQW_8192HZ		0x03	/*  0    0   0   1   1  */
#define RTC_SQW_4096HZ		0x04	/*  0    0   1   0   0  */
#define RTC_SQW_2048HZ		0x05	/*  0    0   1   0   1  */
#define RTC_SQW_1024HZ		0x06	/*  0    0   1   1   0  */
#define RTC_SQW_512HZ		0x07	/*  0    0   1   1   1  */
#define RTC_SQW_256HZ		0x08	/*  0    1   0   0   0  */
#define RTC_SQW_128HZ		0x09	/*  0    1   0   0   1  */
#define RTC_SQW_64HZ		0x0a	/*  0    1   0   1   0  */
#define RTC_SQW_32HZ		0x0b	/*  0    1   0   1   1  */
#define RTC_SQW_16HZ		0x0c	/*  0    1   1   0   0  */
#define RTC_SQW_8HZ		0x0d	/*  0    1   1   0   1  */
#define RTC_SQW_4HZ		0x0e	/*  0    1   1   1   0  */
#define RTC_SQW_2HZ		0x0f	/*  0    1   1   1   1  */
#define RTC_SQW_0HZ		0x00	/*  0    0   0   0   0  */
#define RTC_SQW_32768HZ		32768	/*  1    -   -   -   -  */
#define RTC_MAX_USER_FREQ	8192


/*
 * NVRAM data & addresses:
 *   - 50 bytes of NVRAM are available just past the clock registers.
 *   - 64 additional bytes are available in Bank0.
 *
 * Extended, battery-backed NV-SRAM:
 *   - DS1685/DS1687    - 128 bytes.
 *   - DS1689/DS1693    - 0 bytes.
 *   - DS17285/DS17287  - 2048 bytes.
 *   - DS17485/DS17487  - 4096 bytes.
 *   - DS17885/DS17887  - 8192 bytes.
 */
#define NVRAM_TIME_BASE		0x0e	/* NVRAM Addr in Time regs */
#define NVRAM_BANK0_BASE	0x40	/* NVRAM Addr in Bank0 regs */
#define NVRAM_SZ_TIME		50
#define NVRAM_SZ_BANK0		64
#if defined(CONFIG_RTC_DRV_DS1685)
#  define NVRAM_SZ_EXTND	128
#elif defined(CONFIG_RTC_DRV_DS1689)
#  define NVRAM_SZ_EXTND	0
#elif defined(CONFIG_RTC_DRV_DS17285)
#  define NVRAM_SZ_EXTND	2048
#elif defined(CONFIG_RTC_DRV_DS17485)
#  define NVRAM_SZ_EXTND	4096
#elif defined(CONFIG_RTC_DRV_DS17885)
#  define NVRAM_SZ_EXTND	8192
#endif
#define NVRAM_TOTAL_SZ_BANK0	(NVRAM_SZ_TIME + NVRAM_SZ_BANK0)
#define NVRAM_TOTAL_SZ		(NVRAM_TOTAL_SZ_BANK0 + NVRAM_SZ_EXTND)


/*
 * Function Prototypes.
 */
extern void __noreturn
ds1685_rtc_poweroff(struct platform_device *pdev);

#endif /* _LINUX_RTC_DS1685_H_ */
