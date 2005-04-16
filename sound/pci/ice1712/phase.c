/*
 *   ALSA driver for ICEnsemble ICE1724 (Envy24)
 *
 *   Lowlevel functions for Terratec PHASE 22
 *
 *	Copyright (c) 2005 Misha Zhilin <misha@epiphan.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

/* PHASE 22 overview:
 *   Audio controller: VIA Envy24HT-S (slightly trimmed down version of Envy24HT)
 *   Analog chip: AK4524 (partially via Philip's 74HCT125)
 *   Digital receiver: CS8414-CS (not supported in this release)
 *
 *   Envy connects to AK4524
 *	- CS directly from GPIO 10
 *	- CCLK via 74HCT125's gate #4 from GPIO 4
 *	- CDTI via 74HCT125's gate #2 from GPIO 5
 *		CDTI may be completely blocked by 74HCT125's gate #1 controlled by GPIO 3
 */

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>

#include "ice1712.h"
#include "envy24ht.h"
#include "phase.h"

static akm4xxx_t akm_phase22 __devinitdata = {
	.type = SND_AK4524,
	.num_dacs = 2,
	.num_adcs = 2,
};

static struct snd_ak4xxx_private akm_phase22_priv __devinitdata = {
	.caddr =	2,
	.cif =		1,
	.data_mask =	1 << 4,
	.clk_mask =	1 << 5,
	.cs_mask =	1 << 10,
	.cs_addr =	1 << 10,
	.cs_none =	0,
	.add_flags = 	1 << 3,
	.mask_flags =	0,
};

static int __devinit phase22_init(ice1712_t *ice)
{
	akm4xxx_t *ak;
	int err;

	// Configure DAC/ADC description for generic part of ice1724
	switch (ice->eeprom.subvendor) {
	case VT1724_SUBDEVICE_PHASE22:
		ice->num_total_dacs = 2;
		ice->num_total_adcs = 2;
		ice->vt1720 = 1; // Envy24HT-S have 16 bit wide GPIO
		break;
	default:
		snd_BUG();
		return -EINVAL;
	}

	// Initialize analog chips
	ak = ice->akm = kcalloc(1, sizeof(akm4xxx_t), GFP_KERNEL);
	if (! ak)
		return -ENOMEM;
	ice->akm_codecs = 1;
	switch (ice->eeprom.subvendor) {
	case VT1724_SUBDEVICE_PHASE22:
		if ((err = snd_ice1712_akm4xxx_init(ak, &akm_phase22, &akm_phase22_priv, ice)) < 0)
			return err;
		break;
	}

	return 0;
}

static int __devinit phase22_add_controls(ice1712_t *ice)
{
	int err = 0;

	switch (ice->eeprom.subvendor) {
	case VT1724_SUBDEVICE_PHASE22:
		err = snd_ice1712_akm4xxx_build_controls(ice);
		if (err < 0)
			return err;
	}
	return 0;
}

static unsigned char phase22_eeprom[] __devinitdata = {
	0x00,	/* SYSCONF: 1xADC, 1xDACs */
	0x80,	/* ACLINK: I2S */
	0xf8,	/* I2S: vol, 96k, 24bit*/
	0xc3,	/* SPDIF: out-en, out-int, spdif-in */
	0xFF,	/* GPIO_DIR */
	0xFF,	/* GPIO_DIR1 */
	0xFF,	/* GPIO_DIR2 */
	0x00,	/* GPIO_MASK */
	0x00,	/* GPIO_MASK1 */
	0x00,	/* GPIO_MASK2 */
	0x00,	/* GPIO_STATE: */
	0x00,	/* GPIO_STATE1: */
	0x00,	/* GPIO_STATE2 */
};

struct snd_ice1712_card_info snd_vt1724_phase_cards[] __devinitdata = {
	{
		.subvendor = VT1724_SUBDEVICE_PHASE22,
		.name = "Terratec PHASE 22",
		.model = "phase22",
		.chip_init = phase22_init,
		.build_controls = phase22_add_controls,
		.eeprom_size = sizeof(phase22_eeprom),
		.eeprom_data = phase22_eeprom,
	},
	{ } /* terminator */
};
