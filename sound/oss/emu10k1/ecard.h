/*     
 **********************************************************************
 *     ecard.h
 *     Copyright 1999, 2000 Creative Labs, Inc. 
 * 
 ********************************************************************** 
 * 
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version. 
 * 
 *     This program is distributed in the hope that it will be useful, 
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 *     GNU General Public License for more details. 
 * 
 *     You should have received a copy of the GNU General Public 
 *     License along with this program; if not, write to the Free 
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, 
 *     USA. 
 * 
 ********************************************************************** 
 */ 

#ifndef _ECARD_H
#define _ECARD_H

#include "8010.h"
#include "hwaccess.h"
#include <linux/init.h>

/* In A1 Silicon, these bits are in the HC register */
#define HOOKN_BIT   (1L << 12)
#define HANDN_BIT   (1L << 11)
#define PULSEN_BIT  (1L << 10)

#define EC_GDI1 (1 << 13)
#define EC_GDI0 (1 << 14)

#define EC_NUM_CONTROL_BITS 20

#define EC_AC3_DATA_SELN  0x0001L
#define EC_EE_DATA_SEL    0x0002L
#define EC_EE_CNTRL_SELN  0x0004L
#define EC_EECLK          0x0008L
#define EC_EECS           0x0010L
#define EC_EESDO          0x0020L
#define EC_TRIM_CSN	  0x0040L
#define EC_TRIM_SCLK	  0x0080L
#define EC_TRIM_SDATA	  0x0100L
#define EC_TRIM_MUTEN	  0x0200L
#define EC_ADCCAL	  0x0400L
#define EC_ADCRSTN	  0x0800L
#define EC_DACCAL	  0x1000L
#define EC_DACMUTEN	  0x2000L
#define EC_LEDN		  0x4000L

#define EC_SPDIF0_SEL_SHIFT	15
#define EC_SPDIF1_SEL_SHIFT	17	
#define EC_SPDIF0_SEL_MASK	(0x3L << EC_SPDIF0_SEL_SHIFT)
#define EC_SPDIF1_SEL_MASK	(0x7L << EC_SPDIF1_SEL_SHIFT)
#define EC_SPDIF0_SELECT(_x) (((_x) << EC_SPDIF0_SEL_SHIFT) & EC_SPDIF0_SEL_MASK)
#define EC_SPDIF1_SELECT(_x) (((_x) << EC_SPDIF1_SEL_SHIFT) & EC_SPDIF1_SEL_MASK)
#define EC_CURRENT_PROM_VERSION 0x01 /* Self-explanatory.  This should
                                      * be incremented any time the EEPROM's
                                      * format is changed.  */

#define EC_EEPROM_SIZE	        0x40 /* ECARD EEPROM has 64 16-bit words */

/* Addresses for special values stored in to EEPROM */
#define EC_PROM_VERSION_ADDR	0x20	/* Address of the current prom version */
#define EC_BOARDREV0_ADDR	0x21	/* LSW of board rev */
#define EC_BOARDREV1_ADDR 	0x22	/* MSW of board rev */ 

#define EC_LAST_PROMFILE_ADDR	0x2f

#define EC_SERIALNUM_ADD	0x30	/* First word of serial number.  The number
                                         * can be up to 30 characters in length
                                         * and is stored as a NULL-terminated
                                         * ASCII string.  Any unused bytes must be
                                         * filled with zeros */
#define EC_CHECKSUM_ADDR	0x3f    /* Location at which checksum is stored */



/* Most of this stuff is pretty self-evident.  According to the hardware 
 * dudes, we need to leave the ADCCAL bit low in order to avoid a DC 
 * offset problem.  Weird.
 */
#define EC_RAW_RUN_MODE	(EC_DACMUTEN | EC_ADCRSTN | EC_TRIM_MUTEN | EC_TRIM_CSN)


#define EC_DEFAULT_ADC_GAIN   0xC4C4
#define EC_DEFAULT_SPDIF0_SEL 0x0
#define EC_DEFAULT_SPDIF1_SEL 0x4

#define HC_EA 0x01L

/* ECARD state structure.  This structure maintains the state
 * for various portions of the ECARD's onboard hardware.
 */
struct ecard_state {
	u32 control_bits;
	u16 adc_gain;
	u16 mux0_setting;
	u16 mux1_setting;
	u16 mux2_setting;
};

void emu10k1_ecard_init(struct emu10k1_card *) __devinit;

#endif /* _ECARD_H */
