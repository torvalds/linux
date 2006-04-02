/*
 * Definitions for the M48Txx and mc146818 series of Time of day/Real Time
 * Clock chips.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * Support for the M48T37/M48T59/.../mc146818 Real Time Clock chips.
 * Purpose is to make one generic file that handles all of these chips instead
 * of every platform implementing the same code over & over again.
 */

#ifndef __PPC_KERNEL_TODC_H
#define __PPC_KERNEL_TODC_H

typedef struct {
	uint rtc_type;		/* your particular chip */

	/*
	 * Following are the addresses of the AS0, AS1, and DATA registers
	 * of these chips.  Note that these are board-specific.
	 */
	unsigned int nvram_as0;
	unsigned int nvram_as1;
	unsigned int nvram_data;

	/*
	 * Define bits to stop external set of regs from changing so
	 * the chip can be read/written reliably.
	 */
	unsigned char enable_read;
	unsigned char enable_write;

	/*
	 * Following is the number of AS0 address bits.  This is normally
	 * 8 but some bad hardware routes address lines incorrectly.
	 */
	int as0_bits;

	int nvram_size;	/* Size of NVRAM on chip */
	int sw_flags;	/* Software control flags */

	/* Following are the register offsets for the particular chip */
	int year;
	int month;
	int day_of_month;
	int day_of_week;
	int hours;
	int minutes;
	int seconds;
	int control_b;
	int control_a;
	int watchdog;
	int interrupts;
	int alarm_date;
	int alarm_hour;
	int alarm_minutes;
	int alarm_seconds;
	int century;
	int flags;

	/*
	 * Some RTC chips have their NVRAM buried behind a addr/data pair of
	 * regs on the first level/clock registers.  The following fields
	 * are the addresses for those addr/data regs.
	 */
	int nvram_addr_reg;
	int nvram_data_reg;
} todc_info_t;

/*
 * Define the types of TODC/RTC variants that are supported in
 * arch/ppc/kernel/todc_time.c
 * Make a new one of these for any chip somehow differs from what's already
 * defined.  That way, if you ever need to put in code to touch those
 * bits/registers in todc_time.c, you can put it inside an
 * 'if (todc_info->rtc_type ==  TODC_TYPE_XXX)' so you won't break
 * anyone else.
 */
#define	TODC_TYPE_MK48T35		1
#define	TODC_TYPE_MK48T37		2
#define	TODC_TYPE_MK48T59		3
#define	TODC_TYPE_DS1693		4	/* Dallas DS1693 RTC */
#define	TODC_TYPE_DS1743		5	/* Dallas DS1743 RTC */
#define	TODC_TYPE_DS1746		6	/* Dallas DS1746 RTC */
#define	TODC_TYPE_DS1747		7	/* Dallas DS1747 RTC */
#define	TODC_TYPE_DS1501		8	/* Dallas DS1501 RTC */
#define TODC_TYPE_DS1643		9	/* Dallas DS1643 RTC */
#define TODC_TYPE_PC97307		10	/* PC97307 internal RTC */
#define TODC_TYPE_DS1557		11	/* Dallas DS1557 RTC */
#define TODC_TYPE_DS17285		12	/* Dallas DS17285 RTC */
#define TODC_TYPE_DS1553		13	/* Dallas DS1553 RTC */
#define	TODC_TYPE_MC146818		100	/* Leave room for m48txx's */

/*
 * Bit to clear/set to enable reads/writes to the chip
 */
#define	TODC_MK48TXX_CNTL_A_R		0x40
#define	TODC_MK48TXX_CNTL_A_W		0x80
#define	TODC_MK48TXX_DAY_CB		0x80

#define	TODC_DS1501_CNTL_B_TE		0x80

/*
 * Define flag bits used by todc routines.
 */
#define	TODC_FLAG_2_LEVEL_NVRAM		0x00000001

/*
 * Define the values for the various RTC's that should to into the todc_info
 * table.
 * Note: The XXX_NVRAM_SIZE, XXX_NVRAM_ADDR_REG, and XXX_NVRAM_DATA_REG only
 * matter if XXX_SW_FLAGS has TODC_FLAG_2_LEVEL_NVRAM set.
 */
#define	TODC_TYPE_MK48T35_NVRAM_SIZE		0x7ff8
#define	TODC_TYPE_MK48T35_SW_FLAGS		0
#define	TODC_TYPE_MK48T35_YEAR			0x7fff
#define	TODC_TYPE_MK48T35_MONTH			0x7ffe
#define	TODC_TYPE_MK48T35_DOM			0x7ffd	/* Day of Month */
#define	TODC_TYPE_MK48T35_DOW			0x7ffc	/* Day of Week */
#define	TODC_TYPE_MK48T35_HOURS			0x7ffb
#define	TODC_TYPE_MK48T35_MINUTES		0x7ffa
#define	TODC_TYPE_MK48T35_SECONDS		0x7ff9
#define	TODC_TYPE_MK48T35_CNTL_B		0x7ff9
#define	TODC_TYPE_MK48T35_CNTL_A		0x7ff8
#define	TODC_TYPE_MK48T35_WATCHDOG		0x0000
#define	TODC_TYPE_MK48T35_INTERRUPTS		0x0000
#define	TODC_TYPE_MK48T35_ALARM_DATE		0x0000
#define	TODC_TYPE_MK48T35_ALARM_HOUR		0x0000
#define	TODC_TYPE_MK48T35_ALARM_MINUTES		0x0000
#define	TODC_TYPE_MK48T35_ALARM_SECONDS		0x0000
#define	TODC_TYPE_MK48T35_CENTURY		0x0000
#define	TODC_TYPE_MK48T35_FLAGS			0x0000
#define	TODC_TYPE_MK48T35_NVRAM_ADDR_REG	0
#define	TODC_TYPE_MK48T35_NVRAM_DATA_REG	0

#define	TODC_TYPE_MK48T37_NVRAM_SIZE		0x7ff0
#define	TODC_TYPE_MK48T37_SW_FLAGS		0
#define	TODC_TYPE_MK48T37_YEAR			0x7fff
#define	TODC_TYPE_MK48T37_MONTH			0x7ffe
#define	TODC_TYPE_MK48T37_DOM			0x7ffd	/* Day of Month */
#define	TODC_TYPE_MK48T37_DOW			0x7ffc	/* Day of Week */
#define	TODC_TYPE_MK48T37_HOURS			0x7ffb
#define	TODC_TYPE_MK48T37_MINUTES		0x7ffa
#define	TODC_TYPE_MK48T37_SECONDS		0x7ff9
#define	TODC_TYPE_MK48T37_CNTL_B		0x7ff9
#define	TODC_TYPE_MK48T37_CNTL_A		0x7ff8
#define	TODC_TYPE_MK48T37_WATCHDOG		0x7ff7
#define	TODC_TYPE_MK48T37_INTERRUPTS		0x7ff6
#define	TODC_TYPE_MK48T37_ALARM_DATE		0x7ff5
#define	TODC_TYPE_MK48T37_ALARM_HOUR		0x7ff4
#define	TODC_TYPE_MK48T37_ALARM_MINUTES		0x7ff3
#define	TODC_TYPE_MK48T37_ALARM_SECONDS		0x7ff2
#define	TODC_TYPE_MK48T37_CENTURY		0x7ff1
#define	TODC_TYPE_MK48T37_FLAGS			0x7ff0
#define	TODC_TYPE_MK48T37_NVRAM_ADDR_REG	0
#define	TODC_TYPE_MK48T37_NVRAM_DATA_REG	0

#define	TODC_TYPE_MK48T59_NVRAM_SIZE		0x1ff0
#define	TODC_TYPE_MK48T59_SW_FLAGS		0
#define	TODC_TYPE_MK48T59_YEAR			0x1fff
#define	TODC_TYPE_MK48T59_MONTH			0x1ffe
#define	TODC_TYPE_MK48T59_DOM			0x1ffd	/* Day of Month */
#define	TODC_TYPE_MK48T59_DOW			0x1ffc	/* Day of Week */
#define	TODC_TYPE_MK48T59_HOURS			0x1ffb
#define	TODC_TYPE_MK48T59_MINUTES		0x1ffa
#define	TODC_TYPE_MK48T59_SECONDS		0x1ff9
#define	TODC_TYPE_MK48T59_CNTL_B		0x1ff9
#define	TODC_TYPE_MK48T59_CNTL_A		0x1ff8
#define	TODC_TYPE_MK48T59_WATCHDOG		0x1fff
#define	TODC_TYPE_MK48T59_INTERRUPTS		0x1fff
#define	TODC_TYPE_MK48T59_ALARM_DATE		0x1fff
#define	TODC_TYPE_MK48T59_ALARM_HOUR		0x1fff
#define	TODC_TYPE_MK48T59_ALARM_MINUTES		0x1fff
#define	TODC_TYPE_MK48T59_ALARM_SECONDS		0x1fff
#define	TODC_TYPE_MK48T59_CENTURY		0x1fff
#define	TODC_TYPE_MK48T59_FLAGS			0x1fff
#define	TODC_TYPE_MK48T59_NVRAM_ADDR_REG	0
#define	TODC_TYPE_MK48T59_NVRAM_DATA_REG	0

#define	TODC_TYPE_DS1501_NVRAM_SIZE	0x100
#define	TODC_TYPE_DS1501_SW_FLAGS	TODC_FLAG_2_LEVEL_NVRAM
#define	TODC_TYPE_DS1501_YEAR		(TODC_TYPE_DS1501_NVRAM_SIZE + 0x06)
#define	TODC_TYPE_DS1501_MONTH		(TODC_TYPE_DS1501_NVRAM_SIZE + 0x05)
#define	TODC_TYPE_DS1501_DOM		(TODC_TYPE_DS1501_NVRAM_SIZE + 0x04)
#define	TODC_TYPE_DS1501_DOW		(TODC_TYPE_DS1501_NVRAM_SIZE + 0x03)
#define	TODC_TYPE_DS1501_HOURS		(TODC_TYPE_DS1501_NVRAM_SIZE + 0x02)
#define	TODC_TYPE_DS1501_MINUTES	(TODC_TYPE_DS1501_NVRAM_SIZE + 0x01)
#define	TODC_TYPE_DS1501_SECONDS	(TODC_TYPE_DS1501_NVRAM_SIZE + 0x00)
#define	TODC_TYPE_DS1501_CNTL_B		(TODC_TYPE_DS1501_NVRAM_SIZE + 0x0f)
#define	TODC_TYPE_DS1501_CNTL_A		(TODC_TYPE_DS1501_NVRAM_SIZE + 0x0f)
#define	TODC_TYPE_DS1501_WATCHDOG	(TODC_TYPE_DS1501_NVRAM_SIZE + 0xff)
#define	TODC_TYPE_DS1501_INTERRUPTS	(TODC_TYPE_DS1501_NVRAM_SIZE + 0xff)
#define	TODC_TYPE_DS1501_ALARM_DATE	(TODC_TYPE_DS1501_NVRAM_SIZE + 0x0b)
#define	TODC_TYPE_DS1501_ALARM_HOUR	(TODC_TYPE_DS1501_NVRAM_SIZE + 0x0a)
#define	TODC_TYPE_DS1501_ALARM_MINUTES	(TODC_TYPE_DS1501_NVRAM_SIZE + 0x09)
#define	TODC_TYPE_DS1501_ALARM_SECONDS	(TODC_TYPE_DS1501_NVRAM_SIZE + 0x08)
#define	TODC_TYPE_DS1501_CENTURY	(TODC_TYPE_DS1501_NVRAM_SIZE + 0x07)
#define	TODC_TYPE_DS1501_FLAGS		(TODC_TYPE_DS1501_NVRAM_SIZE + 0xff)
#define	TODC_TYPE_DS1501_NVRAM_ADDR_REG	0x10
#define	TODC_TYPE_DS1501_NVRAM_DATA_REG	0x13

#define	TODC_TYPE_DS1553_NVRAM_SIZE		0x1ff0
#define	TODC_TYPE_DS1553_SW_FLAGS		0
#define	TODC_TYPE_DS1553_YEAR			0x1fff
#define	TODC_TYPE_DS1553_MONTH			0x1ffe
#define	TODC_TYPE_DS1553_DOM			0x1ffd	/* Day of Month */
#define	TODC_TYPE_DS1553_DOW			0x1ffc	/* Day of Week */
#define	TODC_TYPE_DS1553_HOURS			0x1ffb
#define	TODC_TYPE_DS1553_MINUTES		0x1ffa
#define	TODC_TYPE_DS1553_SECONDS		0x1ff9
#define	TODC_TYPE_DS1553_CNTL_B			0x1ff9
#define	TODC_TYPE_DS1553_CNTL_A			0x1ff8	/* control_a R/W regs */
#define	TODC_TYPE_DS1553_WATCHDOG		0x1ff7
#define	TODC_TYPE_DS1553_INTERRUPTS		0x1ff6
#define	TODC_TYPE_DS1553_ALARM_DATE		0x1ff5
#define	TODC_TYPE_DS1553_ALARM_HOUR		0x1ff4
#define	TODC_TYPE_DS1553_ALARM_MINUTES		0x1ff3
#define	TODC_TYPE_DS1553_ALARM_SECONDS		0x1ff2
#define	TODC_TYPE_DS1553_CENTURY		0x1ff8
#define	TODC_TYPE_DS1553_FLAGS			0x1ff0
#define	TODC_TYPE_DS1553_NVRAM_ADDR_REG		0
#define	TODC_TYPE_DS1553_NVRAM_DATA_REG		0

#define	TODC_TYPE_DS1557_NVRAM_SIZE		0x7fff0
#define	TODC_TYPE_DS1557_SW_FLAGS		0
#define	TODC_TYPE_DS1557_YEAR			0x7ffff
#define	TODC_TYPE_DS1557_MONTH			0x7fffe
#define	TODC_TYPE_DS1557_DOM			0x7fffd	/* Day of Month */
#define	TODC_TYPE_DS1557_DOW			0x7fffc	/* Day of Week */
#define	TODC_TYPE_DS1557_HOURS			0x7fffb
#define	TODC_TYPE_DS1557_MINUTES		0x7fffa
#define	TODC_TYPE_DS1557_SECONDS		0x7fff9
#define	TODC_TYPE_DS1557_CNTL_B			0x7fff9
#define	TODC_TYPE_DS1557_CNTL_A			0x7fff8	/* control_a R/W regs */
#define	TODC_TYPE_DS1557_WATCHDOG		0x7fff7
#define	TODC_TYPE_DS1557_INTERRUPTS		0x7fff6
#define	TODC_TYPE_DS1557_ALARM_DATE		0x7fff5
#define	TODC_TYPE_DS1557_ALARM_HOUR		0x7fff4
#define	TODC_TYPE_DS1557_ALARM_MINUTES		0x7fff3
#define	TODC_TYPE_DS1557_ALARM_SECONDS		0x7fff2
#define	TODC_TYPE_DS1557_CENTURY		0x7fff8
#define	TODC_TYPE_DS1557_FLAGS			0x7fff0
#define	TODC_TYPE_DS1557_NVRAM_ADDR_REG		0
#define	TODC_TYPE_DS1557_NVRAM_DATA_REG		0

#define	TODC_TYPE_DS1643_NVRAM_SIZE		0x1ff8
#define	TODC_TYPE_DS1643_SW_FLAGS		0
#define	TODC_TYPE_DS1643_YEAR			0x1fff
#define	TODC_TYPE_DS1643_MONTH			0x1ffe
#define	TODC_TYPE_DS1643_DOM			0x1ffd	/* Day of Month */
#define	TODC_TYPE_DS1643_DOW			0x1ffc	/* Day of Week */
#define	TODC_TYPE_DS1643_HOURS			0x1ffb
#define	TODC_TYPE_DS1643_MINUTES		0x1ffa
#define	TODC_TYPE_DS1643_SECONDS		0x1ff9
#define	TODC_TYPE_DS1643_CNTL_B			0x1ff9
#define	TODC_TYPE_DS1643_CNTL_A			0x1ff8	/* control_a R/W regs */
#define	TODC_TYPE_DS1643_WATCHDOG		0x1fff
#define	TODC_TYPE_DS1643_INTERRUPTS		0x1fff
#define	TODC_TYPE_DS1643_ALARM_DATE		0x1fff
#define	TODC_TYPE_DS1643_ALARM_HOUR		0x1fff
#define	TODC_TYPE_DS1643_ALARM_MINUTES		0x1fff
#define	TODC_TYPE_DS1643_ALARM_SECONDS		0x1fff
#define	TODC_TYPE_DS1643_CENTURY		0x1ff8
#define	TODC_TYPE_DS1643_FLAGS			0x1fff
#define	TODC_TYPE_DS1643_NVRAM_ADDR_REG		0
#define	TODC_TYPE_DS1643_NVRAM_DATA_REG		0

#define	TODC_TYPE_DS1693_NVRAM_SIZE		0 /* Not handled yet */
#define	TODC_TYPE_DS1693_SW_FLAGS		0
#define	TODC_TYPE_DS1693_YEAR			0x09
#define	TODC_TYPE_DS1693_MONTH			0x08
#define	TODC_TYPE_DS1693_DOM			0x07	/* Day of Month */
#define	TODC_TYPE_DS1693_DOW			0x06	/* Day of Week */
#define	TODC_TYPE_DS1693_HOURS			0x04
#define	TODC_TYPE_DS1693_MINUTES		0x02
#define	TODC_TYPE_DS1693_SECONDS		0x00
#define	TODC_TYPE_DS1693_CNTL_B			0x0b
#define	TODC_TYPE_DS1693_CNTL_A			0x0a
#define	TODC_TYPE_DS1693_WATCHDOG		0xff
#define	TODC_TYPE_DS1693_INTERRUPTS		0xff
#define	TODC_TYPE_DS1693_ALARM_DATE		0x49
#define	TODC_TYPE_DS1693_ALARM_HOUR		0x05
#define	TODC_TYPE_DS1693_ALARM_MINUTES		0x03
#define	TODC_TYPE_DS1693_ALARM_SECONDS		0x01
#define	TODC_TYPE_DS1693_CENTURY		0x48
#define	TODC_TYPE_DS1693_FLAGS			0xff
#define	TODC_TYPE_DS1693_NVRAM_ADDR_REG		0
#define	TODC_TYPE_DS1693_NVRAM_DATA_REG		0

#define	TODC_TYPE_DS1743_NVRAM_SIZE		0x1ff8
#define	TODC_TYPE_DS1743_SW_FLAGS		0
#define	TODC_TYPE_DS1743_YEAR			0x1fff
#define	TODC_TYPE_DS1743_MONTH			0x1ffe
#define	TODC_TYPE_DS1743_DOM			0x1ffd	/* Day of Month */
#define	TODC_TYPE_DS1743_DOW			0x1ffc	/* Day of Week */
#define	TODC_TYPE_DS1743_HOURS			0x1ffb
#define	TODC_TYPE_DS1743_MINUTES		0x1ffa
#define	TODC_TYPE_DS1743_SECONDS		0x1ff9
#define	TODC_TYPE_DS1743_CNTL_B			0x1ff9
#define	TODC_TYPE_DS1743_CNTL_A			0x1ff8	/* control_a R/W regs */
#define	TODC_TYPE_DS1743_WATCHDOG		0x1fff
#define	TODC_TYPE_DS1743_INTERRUPTS		0x1fff
#define	TODC_TYPE_DS1743_ALARM_DATE		0x1fff
#define	TODC_TYPE_DS1743_ALARM_HOUR		0x1fff
#define	TODC_TYPE_DS1743_ALARM_MINUTES		0x1fff
#define	TODC_TYPE_DS1743_ALARM_SECONDS		0x1fff
#define	TODC_TYPE_DS1743_CENTURY		0x1ff8
#define	TODC_TYPE_DS1743_FLAGS			0x1fff
#define	TODC_TYPE_DS1743_NVRAM_ADDR_REG		0
#define	TODC_TYPE_DS1743_NVRAM_DATA_REG		0

#define	TODC_TYPE_DS1746_NVRAM_SIZE		0x1fff8
#define	TODC_TYPE_DS1746_SW_FLAGS		0
#define	TODC_TYPE_DS1746_YEAR			0x1ffff
#define	TODC_TYPE_DS1746_MONTH			0x1fffe
#define	TODC_TYPE_DS1746_DOM			0x1fffd	/* Day of Month */
#define	TODC_TYPE_DS1746_DOW			0x1fffc	/* Day of Week */
#define	TODC_TYPE_DS1746_HOURS			0x1fffb
#define	TODC_TYPE_DS1746_MINUTES		0x1fffa
#define	TODC_TYPE_DS1746_SECONDS		0x1fff9
#define	TODC_TYPE_DS1746_CNTL_B			0x1fff9
#define	TODC_TYPE_DS1746_CNTL_A			0x1fff8	/* control_a R/W regs */
#define	TODC_TYPE_DS1746_WATCHDOG		0x00000
#define	TODC_TYPE_DS1746_INTERRUPTS		0x00000
#define	TODC_TYPE_DS1746_ALARM_DATE		0x00000
#define	TODC_TYPE_DS1746_ALARM_HOUR		0x00000
#define	TODC_TYPE_DS1746_ALARM_MINUTES		0x00000
#define	TODC_TYPE_DS1746_ALARM_SECONDS		0x00000
#define	TODC_TYPE_DS1746_CENTURY		0x00000
#define	TODC_TYPE_DS1746_FLAGS			0x00000
#define	TODC_TYPE_DS1746_NVRAM_ADDR_REG		0
#define	TODC_TYPE_DS1746_NVRAM_DATA_REG		0

#define	TODC_TYPE_DS1747_NVRAM_SIZE		0x7fff8
#define	TODC_TYPE_DS1747_SW_FLAGS		0
#define	TODC_TYPE_DS1747_YEAR			0x7ffff
#define	TODC_TYPE_DS1747_MONTH			0x7fffe
#define	TODC_TYPE_DS1747_DOM			0x7fffd	/* Day of Month */
#define	TODC_TYPE_DS1747_DOW			0x7fffc	/* Day of Week */
#define	TODC_TYPE_DS1747_HOURS			0x7fffb
#define	TODC_TYPE_DS1747_MINUTES		0x7fffa
#define	TODC_TYPE_DS1747_SECONDS		0x7fff9
#define	TODC_TYPE_DS1747_CNTL_B			0x7fff9
#define	TODC_TYPE_DS1747_CNTL_A			0x7fff8	/* control_a R/W regs */
#define	TODC_TYPE_DS1747_WATCHDOG		0x00000
#define	TODC_TYPE_DS1747_INTERRUPTS		0x00000
#define	TODC_TYPE_DS1747_ALARM_DATE		0x00000
#define	TODC_TYPE_DS1747_ALARM_HOUR		0x00000
#define	TODC_TYPE_DS1747_ALARM_MINUTES		0x00000
#define	TODC_TYPE_DS1747_ALARM_SECONDS		0x00000
#define	TODC_TYPE_DS1747_CENTURY		0x00000
#define	TODC_TYPE_DS1747_FLAGS			0x00000
#define	TODC_TYPE_DS1747_NVRAM_ADDR_REG		0
#define	TODC_TYPE_DS1747_NVRAM_DATA_REG		0

#define TODC_TYPE_DS17285_NVRAM_SIZE		(0x1000-0x80)    /* 4Kx8 NVRAM (minus RTC regs) */
#define TODC_TYPE_DS17285_SW_FLAGS		TODC_FLAG_2_LEVEL_NVRAM
#define TODC_TYPE_DS17285_SECONDS		(TODC_TYPE_DS17285_NVRAM_SIZE + 0x00)
#define TODC_TYPE_DS17285_ALARM_SECONDS		(TODC_TYPE_DS17285_NVRAM_SIZE + 0x01)
#define TODC_TYPE_DS17285_MINUTES		(TODC_TYPE_DS17285_NVRAM_SIZE + 0x02)
#define TODC_TYPE_DS17285_ALARM_MINUTES		(TODC_TYPE_DS17285_NVRAM_SIZE + 0x03)
#define TODC_TYPE_DS17285_HOURS			(TODC_TYPE_DS17285_NVRAM_SIZE + 0x04)
#define TODC_TYPE_DS17285_ALARM_HOUR		(TODC_TYPE_DS17285_NVRAM_SIZE + 0x05)
#define TODC_TYPE_DS17285_DOW			(TODC_TYPE_DS17285_NVRAM_SIZE + 0x06)
#define TODC_TYPE_DS17285_DOM			(TODC_TYPE_DS17285_NVRAM_SIZE + 0x07)
#define TODC_TYPE_DS17285_MONTH			(TODC_TYPE_DS17285_NVRAM_SIZE + 0x08)
#define TODC_TYPE_DS17285_YEAR			(TODC_TYPE_DS17285_NVRAM_SIZE + 0x09)
#define TODC_TYPE_DS17285_CNTL_A		(TODC_TYPE_DS17285_NVRAM_SIZE + 0x0A)
#define TODC_TYPE_DS17285_CNTL_B		(TODC_TYPE_DS17285_NVRAM_SIZE + 0x0B)
#define TODC_TYPE_DS17285_CNTL_C		(TODC_TYPE_DS17285_NVRAM_SIZE + 0x0C)
#define TODC_TYPE_DS17285_CNTL_D		(TODC_TYPE_DS17285_NVRAM_SIZE + 0x0D)
#define TODC_TYPE_DS17285_WATCHDOG		0
#define TODC_TYPE_DS17285_INTERRUPTS		0
#define TODC_TYPE_DS17285_ALARM_DATE		0
#define TODC_TYPE_DS17285_CENTURY		0
#define TODC_TYPE_DS17285_FLAGS			0
#define TODC_TYPE_DS17285_NVRAM_ADDR_REG	0x50
#define TODC_TYPE_DS17285_NVRAM_DATA_REG	0x53
 
#define	TODC_TYPE_MC146818_NVRAM_SIZE		0	/* XXXX */
#define	TODC_TYPE_MC146818_SW_FLAGS		0
#define	TODC_TYPE_MC146818_YEAR			0x09
#define	TODC_TYPE_MC146818_MONTH		0x08
#define	TODC_TYPE_MC146818_DOM			0x07	/* Day of Month */
#define	TODC_TYPE_MC146818_DOW			0x06	/* Day of Week */
#define	TODC_TYPE_MC146818_HOURS		0x04
#define	TODC_TYPE_MC146818_MINUTES		0x02
#define	TODC_TYPE_MC146818_SECONDS		0x00
#define	TODC_TYPE_MC146818_CNTL_B		0x0a
#define	TODC_TYPE_MC146818_CNTL_A		0x0b	/* control_a R/W regs */
#define	TODC_TYPE_MC146818_WATCHDOG		0
#define	TODC_TYPE_MC146818_INTERRUPTS		0x0c
#define	TODC_TYPE_MC146818_ALARM_DATE		0xff
#define	TODC_TYPE_MC146818_ALARM_HOUR		0x05
#define	TODC_TYPE_MC146818_ALARM_MINUTES	0x03
#define	TODC_TYPE_MC146818_ALARM_SECONDS	0x01
#define	TODC_TYPE_MC146818_CENTURY		0xff
#define	TODC_TYPE_MC146818_FLAGS		0xff
#define	TODC_TYPE_MC146818_NVRAM_ADDR_REG	0
#define	TODC_TYPE_MC146818_NVRAM_DATA_REG	0
  
#define	TODC_TYPE_PC97307_NVRAM_SIZE		0	/* No NVRAM? */
#define	TODC_TYPE_PC97307_SW_FLAGS		0
#define	TODC_TYPE_PC97307_YEAR			0x09
#define	TODC_TYPE_PC97307_MONTH			0x08
#define	TODC_TYPE_PC97307_DOM			0x07	/* Day of Month */
#define	TODC_TYPE_PC97307_DOW			0x06	/* Day of Week */
#define	TODC_TYPE_PC97307_HOURS			0x04
#define	TODC_TYPE_PC97307_MINUTES		0x02
#define	TODC_TYPE_PC97307_SECONDS		0x00
#define	TODC_TYPE_PC97307_CNTL_B		0x0a
#define	TODC_TYPE_PC97307_CNTL_A		0x0b	/* control_a R/W regs */
#define	TODC_TYPE_PC97307_WATCHDOG		0x0c
#define	TODC_TYPE_PC97307_INTERRUPTS		0x0d
#define	TODC_TYPE_PC97307_ALARM_DATE		0xff
#define	TODC_TYPE_PC97307_ALARM_HOUR		0x05
#define	TODC_TYPE_PC97307_ALARM_MINUTES		0x03
#define	TODC_TYPE_PC97307_ALARM_SECONDS		0x01
#define	TODC_TYPE_PC97307_CENTURY		0xff
#define	TODC_TYPE_PC97307_FLAGS			0xff
#define	TODC_TYPE_PC97307_NVRAM_ADDR_REG	0
#define	TODC_TYPE_PC97307_NVRAM_DATA_REG	0

/*
 * Define macros to allocate and init the todc_info_t table that will
 * be used by the todc_time.c routines.
 */
#define	TODC_ALLOC()							\
	static todc_info_t todc_info_alloc;				\
	todc_info_t *todc_info = &todc_info_alloc;

#define	TODC_INIT(clock_type, as0, as1, data, bits) {			\
	todc_info->rtc_type = clock_type;				\
									\
	todc_info->nvram_as0  = (unsigned int)(as0);			\
	todc_info->nvram_as1  = (unsigned int)(as1);			\
	todc_info->nvram_data = (unsigned int)(data);			\
									\
	todc_info->as0_bits = (bits);					\
									\
	todc_info->nvram_size     = clock_type ##_NVRAM_SIZE;		\
	todc_info->sw_flags       = clock_type ##_SW_FLAGS;		\
									\
	todc_info->year           = clock_type ##_YEAR;			\
	todc_info->month          = clock_type ##_MONTH;		\
	todc_info->day_of_month   = clock_type ##_DOM;			\
	todc_info->day_of_week    = clock_type ##_DOW;			\
	todc_info->hours          = clock_type ##_HOURS;		\
	todc_info->minutes        = clock_type ##_MINUTES;		\
	todc_info->seconds        = clock_type ##_SECONDS;		\
	todc_info->control_b      = clock_type ##_CNTL_B;		\
	todc_info->control_a      = clock_type ##_CNTL_A;		\
	todc_info->watchdog       = clock_type ##_WATCHDOG;		\
	todc_info->interrupts     = clock_type ##_INTERRUPTS;		\
	todc_info->alarm_date     = clock_type ##_ALARM_DATE;		\
	todc_info->alarm_hour     = clock_type ##_ALARM_HOUR;		\
	todc_info->alarm_minutes  = clock_type ##_ALARM_MINUTES;	\
	todc_info->alarm_seconds  = clock_type ##_ALARM_SECONDS;	\
	todc_info->century        = clock_type ##_CENTURY;		\
	todc_info->flags          = clock_type ##_FLAGS;		\
									\
	todc_info->nvram_addr_reg = clock_type ##_NVRAM_ADDR_REG;	\
	todc_info->nvram_data_reg = clock_type ##_NVRAM_DATA_REG;	\
}

extern todc_info_t *todc_info;

unsigned char todc_direct_read_val(int addr);
void todc_direct_write_val(int addr, unsigned char val);
unsigned char todc_m48txx_read_val(int addr);
void todc_m48txx_write_val(int addr, unsigned char val);
unsigned char todc_mc146818_read_val(int addr);
void todc_mc146818_write_val(int addr, unsigned char val);

long todc_time_init(void);
unsigned long todc_get_rtc_time(void);
int todc_set_rtc_time(unsigned long nowtime);
void todc_calibrate_decr(void);

#endif				/* __PPC_KERNEL_TODC_H */
