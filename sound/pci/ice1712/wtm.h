/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOUND_WTM_H
#define __SOUND_WTM_H

/* ID */
#define WTM_DEVICE_DESC		"{EGO SYS INC,WaveTerminal 192M},"
#define VT1724_SUBDEVICE_WTM	0x36495345	/* WT192M ver1.0 */

/*
 *chip addresses on I2C bus
 */

#define	AK4114_ADDR		0x20	/*S/PDIF receiver*/
#define STAC9460_I2C_ADDR	0x54	/* ADC*2 | DAC*6 */
#define STAC9460_2_I2C_ADDR	0x56	/* ADC|DAC *2 */


extern struct snd_ice1712_card_info snd_vt1724_wtm_cards[];

#endif /* __SOUND_WTM_H */

