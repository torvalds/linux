// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA driver for VT1720/VT1724 (Envy24PT/Envy24HT)
 *
 *   Lowlevel functions for VT1720-based motherboards
 *
 *	Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 */      

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <sound/core.h>

#include "ice1712.h"
#include "envy24ht.h"
#include "vt1720_mobo.h"


static int k8x800_init(struct snd_ice1712 *ice)
{
	ice->vt1720 = 1;

	/* VT1616 codec */
	ice->num_total_dacs = 6;
	ice->num_total_adcs = 2;

	/* WM8728 codec */
	/* FIXME: TODO */

	return 0;
}

static int k8x800_add_controls(struct snd_ice1712 *ice)
{
	/* FIXME: needs some quirks for VT1616? */
	return 0;
}

/* EEPROM image */

static const unsigned char k8x800_eeprom[] = {
	[ICE_EEP2_SYSCONF]     = 0x01,	/* clock 256, 1ADC, 2DACs */
	[ICE_EEP2_ACLINK]      = 0x02,	/* ACLINK, packed */
	[ICE_EEP2_I2S]         = 0x00,	/* - */
	[ICE_EEP2_SPDIF]       = 0x00,	/* - */
	[ICE_EEP2_GPIO_DIR]    = 0xff,
	[ICE_EEP2_GPIO_DIR1]   = 0xff,
	[ICE_EEP2_GPIO_DIR2]   = 0x00,	/* - */
	[ICE_EEP2_GPIO_MASK]   = 0xff,
	[ICE_EEP2_GPIO_MASK1]  = 0xff,
	[ICE_EEP2_GPIO_MASK2]  = 0x00,	/* - */
	[ICE_EEP2_GPIO_STATE]  = 0x00,
	[ICE_EEP2_GPIO_STATE1] = 0x00,
	[ICE_EEP2_GPIO_STATE2] = 0x00,	/* - */
};

static const unsigned char sn25p_eeprom[] = {
	[ICE_EEP2_SYSCONF]     = 0x01,	/* clock 256, 1ADC, 2DACs */
	[ICE_EEP2_ACLINK]      = 0x02,	/* ACLINK, packed */
	[ICE_EEP2_I2S]         = 0x00,	/* - */
	[ICE_EEP2_SPDIF]       = 0x41,	/* - */
	[ICE_EEP2_GPIO_DIR]    = 0xff,
	[ICE_EEP2_GPIO_DIR1]   = 0xff,
	[ICE_EEP2_GPIO_DIR2]   = 0x00,	/* - */
	[ICE_EEP2_GPIO_MASK]   = 0xff,
	[ICE_EEP2_GPIO_MASK1]  = 0xff,
	[ICE_EEP2_GPIO_MASK2]  = 0x00,	/* - */
	[ICE_EEP2_GPIO_STATE]  = 0x00,
	[ICE_EEP2_GPIO_STATE1] = 0x00,
	[ICE_EEP2_GPIO_STATE2] = 0x00,	/* - */
};


/* entry point */
struct snd_ice1712_card_info snd_vt1720_mobo_cards[] = {
	{
		.subvendor = VT1720_SUBDEVICE_K8X800,
		.name = "Albatron K8X800 Pro II",
		.model = "k8x800",
		.chip_init = k8x800_init,
		.build_controls = k8x800_add_controls,
		.eeprom_size = sizeof(k8x800_eeprom),
		.eeprom_data = k8x800_eeprom,
	},
	{
		.subvendor = VT1720_SUBDEVICE_ZNF3_150,
		.name = "Chaintech ZNF3-150",
		/* identical with k8x800 */
		.chip_init = k8x800_init,
		.build_controls = k8x800_add_controls,
		.eeprom_size = sizeof(k8x800_eeprom),
		.eeprom_data = k8x800_eeprom,
	},
	{
		.subvendor = VT1720_SUBDEVICE_ZNF3_250,
		.name = "Chaintech ZNF3-250",
		/* identical with k8x800 */
		.chip_init = k8x800_init,
		.build_controls = k8x800_add_controls,
		.eeprom_size = sizeof(k8x800_eeprom),
		.eeprom_data = k8x800_eeprom,
	},
	{
		.subvendor = VT1720_SUBDEVICE_9CJS,
		.name = "Chaintech 9CJS",
		/* identical with k8x800 */
		.chip_init = k8x800_init,
		.build_controls = k8x800_add_controls,
		.eeprom_size = sizeof(k8x800_eeprom),
		.eeprom_data = k8x800_eeprom,
	},
	{
		.subvendor = VT1720_SUBDEVICE_SN25P,
		.name = "Shuttle SN25P",
		.model = "sn25p",
		.chip_init = k8x800_init,
		.build_controls = k8x800_add_controls,
		.eeprom_size = sizeof(k8x800_eeprom),
		.eeprom_data = sn25p_eeprom,
	},
	{ } /* terminator */
};

