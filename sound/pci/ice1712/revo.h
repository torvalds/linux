#ifndef __SOUND_REVO_H
#define __SOUND_REVO_H

/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
 *
 *   Lowlevel functions for M-Audio Revolution 7.1
 *
 *	Copyright (c) 2003 Takashi Iwai <tiwai@suse.de>
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

#define REVO_DEVICE_DESC \
		"{MidiMan M Audio,Revolution 7.1},"\
		"{MidiMan M Audio,Revolution 5.1},"\
		"{MidiMan M Audio,Audiophile 192},"

#define VT1724_SUBDEVICE_REVOLUTION71	0x12143036
#define VT1724_SUBDEVICE_REVOLUTION51	0x12143136
#define VT1724_SUBDEVICE_AUDIOPHILE192	0x12143236

/* entry point */
extern struct snd_ice1712_card_info snd_vt1724_revo_cards[];


/*
 *  MidiMan M-Audio Revolution GPIO definitions
 */

#define VT1724_REVO_CCLK	0x02
#define VT1724_REVO_CDIN	0x04	/* not used */
#define VT1724_REVO_CDOUT	0x08
#define VT1724_REVO_CS0		0x10	/* AK5365 chipselect for (revo51) */
#define VT1724_REVO_CS1		0x20	/* front AKM4381 chipselect */
#define VT1724_REVO_CS2		0x40	/* surround AKM4355 CS (revo71) */
#define VT1724_REVO_I2C_DATA    0x40    /* I2C: PT 2258 SDA (on revo51) */
#define VT1724_REVO_I2C_CLOCK   0x80    /* I2C: PT 2258 SCL (on revo51) */
#define VT1724_REVO_CS3		0x80	/* AK4114 for AP192 */
#define VT1724_REVO_MUTE	(1<<22)	/* 0 = all mute, 1 = normal operation */

#endif /* __SOUND_REVO_H */
