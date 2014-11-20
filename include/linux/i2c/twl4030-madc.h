/*
 * twl4030_madc.h - Header for TWL4030 MADC
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * J Keerthy <j-keerthy@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef _TWL4030_MADC_H
#define _TWL4030_MADC_H

struct twl4030_madc_conversion_method {
	u8 sel;
	u8 avg;
	u8 rbase;
	u8 ctrl;
};

#define TWL4030_MADC_MAX_CHANNELS 16


/*
 * twl4030_madc_request- madc request packet for channel conversion
 * @channels:	16 bit bitmap for individual channels
 * @do_avgP:	sample the input channel for 4 consecutive cycles
 * @method:	RT, SW1, SW2
 * @type:	Polling or interrupt based method
 * @raw:	Return raw value, do not convert it
 */

struct twl4030_madc_request {
	unsigned long channels;
	bool do_avg;
	u16 method;
	u16 type;
	bool active;
	bool result_pending;
	bool raw;
	int rbuf[TWL4030_MADC_MAX_CHANNELS];
	void (*func_cb)(int len, int channels, int *buf);
};

enum conversion_methods {
	TWL4030_MADC_RT,
	TWL4030_MADC_SW1,
	TWL4030_MADC_SW2,
	TWL4030_MADC_NUM_METHODS
};

enum sample_type {
	TWL4030_MADC_WAIT,
	TWL4030_MADC_IRQ_ONESHOT,
	TWL4030_MADC_IRQ_REARM
};

#define TWL4030_MADC_CTRL1		0x00
#define TWL4030_MADC_CTRL2		0x01

#define TWL4030_MADC_RTSELECT_LSB	0x02
#define TWL4030_MADC_SW1SELECT_LSB	0x06
#define TWL4030_MADC_SW2SELECT_LSB	0x0A

#define TWL4030_MADC_RTAVERAGE_LSB	0x04
#define TWL4030_MADC_SW1AVERAGE_LSB	0x08
#define TWL4030_MADC_SW2AVERAGE_LSB	0x0C

#define TWL4030_MADC_CTRL_SW1		0x12
#define TWL4030_MADC_CTRL_SW2		0x13

#define TWL4030_MADC_RTCH0_LSB		0x17
#define TWL4030_MADC_GPCH0_LSB		0x37

#define TWL4030_MADC_MADCON	(1 << 0)	/* MADC power on */
#define TWL4030_MADC_BUSY	(1 << 0)	/* MADC busy */
/* MADC conversion completion */
#define TWL4030_MADC_EOC_SW	(1 << 1)
/* MADC SWx start conversion */
#define TWL4030_MADC_SW_START	(1 << 5)
#define TWL4030_MADC_ADCIN0	(1 << 0)
#define TWL4030_MADC_ADCIN1	(1 << 1)
#define TWL4030_MADC_ADCIN2	(1 << 2)
#define TWL4030_MADC_ADCIN3	(1 << 3)
#define TWL4030_MADC_ADCIN4	(1 << 4)
#define TWL4030_MADC_ADCIN5	(1 << 5)
#define TWL4030_MADC_ADCIN6	(1 << 6)
#define TWL4030_MADC_ADCIN7	(1 << 7)
#define TWL4030_MADC_ADCIN8	(1 << 8)
#define TWL4030_MADC_ADCIN9	(1 << 9)
#define TWL4030_MADC_ADCIN10	(1 << 10)
#define TWL4030_MADC_ADCIN11	(1 << 11)
#define TWL4030_MADC_ADCIN12	(1 << 12)
#define TWL4030_MADC_ADCIN13	(1 << 13)
#define TWL4030_MADC_ADCIN14	(1 << 14)
#define TWL4030_MADC_ADCIN15	(1 << 15)

/* Fixed channels */
#define TWL4030_MADC_BTEMP	TWL4030_MADC_ADCIN1
#define TWL4030_MADC_VBUS	TWL4030_MADC_ADCIN8
#define TWL4030_MADC_VBKB	TWL4030_MADC_ADCIN9
#define TWL4030_MADC_ICHG	TWL4030_MADC_ADCIN10
#define TWL4030_MADC_VCHG	TWL4030_MADC_ADCIN11
#define TWL4030_MADC_VBAT	TWL4030_MADC_ADCIN12

/* Step size and prescaler ratio */
#define TEMP_STEP_SIZE          147
#define TEMP_PSR_R              100
#define CURR_STEP_SIZE		147
#define CURR_PSR_R1		44
#define CURR_PSR_R2		88

#define TWL4030_BCI_BCICTL1	0x23
#define TWL4030_BCI_CGAIN	0x020
#define TWL4030_BCI_MESBAT	(1 << 1)
#define TWL4030_BCI_TYPEN	(1 << 4)
#define TWL4030_BCI_ITHEN	(1 << 3)

#define REG_BCICTL2             0x024
#define TWL4030_BCI_ITHSENS	0x007

/* Register and bits for GPBR1 register */
#define TWL4030_REG_GPBR1		0x0c
#define TWL4030_GPBR1_MADC_HFCLK_EN	(1 << 7)

struct twl4030_madc_user_parms {
	int channel;
	int average;
	int status;
	u16 result;
};

int twl4030_madc_conversion(struct twl4030_madc_request *conv);
int twl4030_get_madc_conversion(int channel_no);
#endif
