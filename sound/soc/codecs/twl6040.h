/*
 * ALSA SoC TWL6040 codec driver
 *
 * Author:	Misael Lopez Cruz <x0052729@ti.com>
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

#ifndef __TWL6040_H__
#define __TWL6040_H__

enum twl6040_trim {
	TWL6040_TRIM_TRIM1 = 0,
	TWL6040_TRIM_TRIM2,
	TWL6040_TRIM_TRIM3,
	TWL6040_TRIM_HSOTRIM,
	TWL6040_TRIM_HFOTRIM,
	TWL6040_TRIM_INVAL,
};

#define TWL6040_HSF_TRIM_LEFT(x)	(x & 0x0f)
#define TWL6040_HSF_TRIM_RIGHT(x)	((x >> 4) & 0x0f)

void twl6040_hs_jack_detect(struct snd_soc_codec *codec,
			    struct snd_soc_jack *jack, int report);
int twl6040_get_clk_id(struct snd_soc_codec *codec);
int twl6040_get_trim_value(struct snd_soc_codec *codec, enum twl6040_trim trim);

#endif /* End of __TWL6040_H__ */
