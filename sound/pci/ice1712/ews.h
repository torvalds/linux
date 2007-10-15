#ifndef __SOUND_EWS_H
#define __SOUND_EWS_H

/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
 *
 *   Lowlevel functions for Terratec EWS88MT/D, EWX24/96, DMX 6Fire
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@perex.cz>
 *                    2002 Takashi Iwai <tiwai@suse.de>
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

#define EWS_DEVICE_DESC \
		"{TerraTec,EWX 24/96},"\
		"{TerraTec,EWS 88MT},"\
		"{TerraTec,EWS 88D},"\
		"{TerraTec,DMX 6Fire},"\
		"{TerraTec,Phase 88},"

#define ICE1712_SUBDEVICE_EWX2496	0x3b153011
#define ICE1712_SUBDEVICE_EWS88MT	0x3b151511
#define ICE1712_SUBDEVICE_EWS88MT_NEW	0x3b152511
#define ICE1712_SUBDEVICE_EWS88D	0x3b152b11
#define ICE1712_SUBDEVICE_DMX6FIRE	0x3b153811
#define ICE1712_SUBDEVICE_PHASE88	0x3b155111

/* entry point */
extern struct snd_ice1712_card_info snd_ice1712_ews_cards[];


/* TerraTec EWX 24/96 configuration definitions */

#define ICE1712_EWX2496_AK4524_CS	0x01	/* AK4524 chip select; low = active */
#define ICE1712_EWX2496_AIN_SEL		0x02	/* input sensitivity switch; high = louder */
#define ICE1712_EWX2496_AOUT_SEL	0x04	/* output sensitivity switch; high = louder */
#define ICE1712_EWX2496_RW		0x08	/* read/write switch for i2c; high = write  */
#define ICE1712_EWX2496_SERIAL_DATA	0x10	/* i2c & ak4524 data */
#define ICE1712_EWX2496_SERIAL_CLOCK	0x20	/* i2c & ak4524 clock */
#define ICE1712_EWX2496_TX2		0x40	/* MIDI2 (not used) */
#define ICE1712_EWX2496_RX2		0x80	/* MIDI2 (not used) */

/* TerraTec EWS 88MT/D configuration definitions */
/* RW, SDA snd SCLK are identical with EWX24/96 */
#define ICE1712_EWS88_CS8414_RATE	0x07	/* CS8414 sample rate: gpio 0-2 */
#define ICE1712_EWS88_RW		0x08	/* read/write switch for i2c; high = write  */
#define ICE1712_EWS88_SERIAL_DATA	0x10	/* i2c & ak4524 data */
#define ICE1712_EWS88_SERIAL_CLOCK	0x20	/* i2c & ak4524 clock */
#define ICE1712_EWS88_TX2		0x40	/* MIDI2 (only on 88D) */
#define ICE1712_EWS88_RX2		0x80	/* MIDI2 (only on 88D) */

/* i2c address */
#define ICE1712_EWS88MT_CS8404_ADDR	(0x40>>1)
#define ICE1712_EWS88MT_INPUT_ADDR	(0x46>>1)
#define ICE1712_EWS88MT_OUTPUT_ADDR	(0x48>>1)
#define ICE1712_EWS88MT_OUTPUT_SENSE	0x40	/* mask */
#define ICE1712_EWS88D_PCF_ADDR		(0x40>>1)

/* TerraTec DMX 6Fire configuration definitions */
#define ICE1712_6FIRE_AK4524_CS_MASK	0x07	/* AK4524 chip select #1-#3 */
#define ICE1712_6FIRE_RW		0x08	/* read/write switch for i2c; high = write  */
#define ICE1712_6FIRE_SERIAL_DATA	0x10	/* i2c & ak4524 data */
#define ICE1712_6FIRE_SERIAL_CLOCK	0x20	/* i2c & ak4524 clock */
#define ICE1712_6FIRE_TX2		0x40	/* MIDI2 */
#define ICE1712_6FIRE_RX2		0x80	/* MIDI2 */

#define ICE1712_6FIRE_PCF9554_ADDR	(0x40>>1)
#define ICE1712_6FIRE_CS8427_ADDR	(0x22)

#endif /* __SOUND_EWS_H */
