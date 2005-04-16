/*     
 **********************************************************************
 *     ecard.c - E-card initialization code
 *     Copyright 1999, 2000 Creative Labs, Inc. 
 * 
 ********************************************************************** 
 * 
 *     Date                 Author          Summary of changes 
 *     ----                 ------          ------------------ 
 *     October 20, 1999     Bertrand Lee    base code release 
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

#include "ecard.h"
#include "hwaccess.h"

/* Private routines */
static void ecard_setadcgain(struct emu10k1_card *, struct ecard_state *, u16);
static void ecard_write(struct emu10k1_card *, u32);

/**************************************************************************
 * @func Set the gain of the ECARD's CS3310 Trim/gain controller.  The
 * trim value consists of a 16bit value which is composed of two
 * 8 bit gain/trim values, one for the left channel and one for the
 * right channel.  The following table maps from the Gain/Attenuation
 * value in decibels into the corresponding bit pattern for a single
 * channel.
 */

static void ecard_setadcgain(struct emu10k1_card *card, struct ecard_state *ecard, u16 gain)
{
	u32 currbit;
	ecard->adc_gain = gain;

	/* Enable writing to the TRIM registers */
	ecard_write(card, ecard->control_bits & ~EC_TRIM_CSN);

	/* Do it again to insure that we meet hold time requirements */
	ecard_write(card, ecard->control_bits & ~EC_TRIM_CSN);

	for (currbit = (1L << 15); currbit; currbit >>= 1) {

		u32 value = ecard->control_bits & ~(EC_TRIM_CSN|EC_TRIM_SDATA);

		if (gain & currbit)
		      value |= EC_TRIM_SDATA;

		/* Clock the bit */
		ecard_write(card, value);
		ecard_write(card, value | EC_TRIM_SCLK);
		ecard_write(card, value);
	}

	ecard_write(card, ecard->control_bits);
}

/**************************************************************************
 * @func Clock bits into the Ecard's control latch.  The Ecard uses a
 *  control latch will is loaded bit-serially by toggling the Modem control
 *  lines from function 2 on the E8010.  This function hides these details
 *  and presents the illusion that we are actually writing to a distinct
 *  register.
 */
static void ecard_write(struct emu10k1_card *card, u32 value)
{
	u16 count;
	u32 data, hcvalue;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	hcvalue = inl(card->iobase + HCFG) & ~(HOOKN_BIT|HANDN_BIT|PULSEN_BIT);

	outl(card->iobase + HCFG, hcvalue);

	for (count = 0 ; count < EC_NUM_CONTROL_BITS; count++) {
	
		/* Set up the value */
		data = ((value & 0x1) ? PULSEN_BIT : 0);
		value >>= 1;

		outl(card->iobase + HCFG, hcvalue | data);

		/* Clock the shift register */
		outl(card->iobase + HCFG, hcvalue | data | HANDN_BIT);
		outl(card->iobase + HCFG, hcvalue | data);
	}

	/* Latch the bits */
	outl(card->iobase + HCFG, hcvalue | HOOKN_BIT);
	outl(card->iobase + HCFG, hcvalue);

	spin_unlock_irqrestore(&card->lock, flags);
}

void __devinit emu10k1_ecard_init(struct emu10k1_card *card)
{
	u32 hcvalue;
	struct ecard_state ecard;

	/* Set up the initial settings */
	ecard.mux0_setting = EC_DEFAULT_SPDIF0_SEL;
	ecard.mux1_setting = EC_DEFAULT_SPDIF1_SEL;
	ecard.mux2_setting = 0;
	ecard.adc_gain = EC_DEFAULT_ADC_GAIN;
	ecard.control_bits = EC_RAW_RUN_MODE | 
                             EC_SPDIF0_SELECT(ecard.mux0_setting) |
			     EC_SPDIF1_SELECT(ecard.mux1_setting);


	/* Step 0: Set the codec type in the hardware control register 
	 * and enable audio output */
	hcvalue = emu10k1_readfn0(card, HCFG);
	emu10k1_writefn0(card, HCFG, hcvalue | HCFG_AUDIOENABLE | HCFG_CODECFORMAT_I2S);

	/* Step 1: Turn off the led and deassert TRIM_CS */
	ecard_write(card, EC_ADCCAL | EC_LEDN | EC_TRIM_CSN);

	/* Step 2: Calibrate the ADC and DAC */
	ecard_write(card, EC_DACCAL | EC_LEDN | EC_TRIM_CSN);

	/* Step 3: Wait for awhile; FIXME: Is this correct? */

	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(HZ);

	/* Step 4: Switch off the DAC and ADC calibration.  Note
	 * That ADC_CAL is actually an inverted signal, so we assert
	 * it here to stop calibration.  */
	ecard_write(card, EC_ADCCAL | EC_LEDN | EC_TRIM_CSN);

	/* Step 4: Switch into run mode */
	ecard_write(card, ecard.control_bits);

	/* Step 5: Set the analog input gain */
	ecard_setadcgain(card, &ecard, ecard.adc_gain);
}


