/*
 * ALSA SoC TWL4030 codec driver
 *
 * Author: Steve Sakoman <steve@sakoman.com>
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

#ifndef __TWL4030_AUDIO_H__
#define __TWL4030_AUDIO_H__

/* Register descriptions are here */
#include <linux/mfd/twl4030-codec.h>

/* Sgadow register used by the audio driver */
#define TWL4030_REG_SW_SHADOW		0x4A
#define TWL4030_CACHEREGNUM	(TWL4030_REG_SW_SHADOW + 1)

/* TWL4030_REG_SW_SHADOW (0x4A) Fields */
#define TWL4030_HFL_EN			0x01
#define TWL4030_HFR_EN			0x02

#define TWL4030_DAI_HIFI		0
#define TWL4030_DAI_VOICE		1

extern struct snd_soc_dai twl4030_dai[2];
extern struct snd_soc_codec_device soc_codec_dev_twl4030;

struct twl4030_setup_data {
	unsigned int ramp_delay_value;
	unsigned int sysclk;
	unsigned int hs_extmute:1;
	void (*set_hs_extmute)(int mute);
};

#endif	/* End of __TWL4030_AUDIO_H__ */


