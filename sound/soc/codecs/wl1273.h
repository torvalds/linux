/*
 * sound/soc/codec/wl1273.h
 *
 * ALSA SoC WL1273 codec driver
 *
 * Copyright (C) Nokia Corporation
 * Author: Matti Aaltonen <matti.j.aaltonen@nokia.com>
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

#ifndef __WL1273_CODEC_H__
#define __WL1273_CODEC_H__

/* I2S protocol, left channel first, data width 16 bits */
#define WL1273_PCM_DEF_MODE		0x00

/* Rx */
#define WL1273_AUDIO_ENABLE_I2S		(1 << 0)
#define WL1273_AUDIO_ENABLE_ANALOG	(1 << 1)

/* Tx */
#define WL1273_AUDIO_IO_SET_ANALOG	0
#define WL1273_AUDIO_IO_SET_I2S		1

#define WL1273_POWER_SET_OFF		0
#define WL1273_POWER_SET_FM		(1 << 0)
#define WL1273_POWER_SET_RDS		(1 << 1)
#define WL1273_POWER_SET_RETENTION	(1 << 4)

#define WL1273_PUPD_SET_OFF		0x00
#define WL1273_PUPD_SET_ON		0x01
#define WL1273_PUPD_SET_RETENTION	0x10

/* I2S mode */
#define WL1273_IS2_WIDTH_32	0x0
#define WL1273_IS2_WIDTH_40	0x1
#define WL1273_IS2_WIDTH_22_23	0x2
#define WL1273_IS2_WIDTH_23_22	0x3
#define WL1273_IS2_WIDTH_48	0x4
#define WL1273_IS2_WIDTH_50	0x5
#define WL1273_IS2_WIDTH_60	0x6
#define WL1273_IS2_WIDTH_64	0x7
#define WL1273_IS2_WIDTH_80	0x8
#define WL1273_IS2_WIDTH_96	0x9
#define WL1273_IS2_WIDTH_128	0xa
#define WL1273_IS2_WIDTH	0xf

#define WL1273_IS2_FORMAT_STD	(0x0 << 4)
#define WL1273_IS2_FORMAT_LEFT	(0x1 << 4)
#define WL1273_IS2_FORMAT_RIGHT	(0x2 << 4)
#define WL1273_IS2_FORMAT_USER	(0x3 << 4)

#define WL1273_IS2_MASTER	(0x0 << 6)
#define WL1273_IS2_SLAVEW	(0x1 << 6)

#define WL1273_IS2_TRI_AFTER_SENDING	(0x0 << 7)
#define WL1273_IS2_TRI_ALWAYS_ACTIVE	(0x1 << 7)

#define WL1273_IS2_SDOWS_RR	(0x0 << 8)
#define WL1273_IS2_SDOWS_RF	(0x1 << 8)
#define WL1273_IS2_SDOWS_FR	(0x2 << 8)
#define WL1273_IS2_SDOWS_FF	(0x3 << 8)

#define WL1273_IS2_TRI_OPT	(0x0 << 10)
#define WL1273_IS2_TRI_ALWAYS	(0x1 << 10)

#define WL1273_IS2_RATE_48K	(0x0 << 12)
#define WL1273_IS2_RATE_44_1K	(0x1 << 12)
#define WL1273_IS2_RATE_32K	(0x2 << 12)
#define WL1273_IS2_RATE_22_05K	(0x4 << 12)
#define WL1273_IS2_RATE_16K	(0x5 << 12)
#define WL1273_IS2_RATE_12K	(0x8 << 12)
#define WL1273_IS2_RATE_11_025	(0x9 << 12)
#define WL1273_IS2_RATE_8K	(0xa << 12)
#define WL1273_IS2_RATE		(0xf << 12)

#define WL1273_I2S_DEF_MODE	(WL1273_IS2_WIDTH_32 | \
				 WL1273_IS2_FORMAT_STD | \
				 WL1273_IS2_MASTER | \
				 WL1273_IS2_TRI_AFTER_SENDING | \
				 WL1273_IS2_SDOWS_RR | \
				 WL1273_IS2_TRI_OPT | \
				 WL1273_IS2_RATE_48K)

int wl1273_get_format(struct snd_soc_codec *codec, unsigned int *fmt);

#endif	/* End of __WL1273_CODEC_H__ */
