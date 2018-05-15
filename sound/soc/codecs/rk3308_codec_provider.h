/*
 * rk3308_codec_provider.h -- RK3308 ALSA Soc Audio Driver
 *
 * Copyright (c) 2018, Fuzhou Rockchip Electronics Co., Ltd All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __RK3308_CODEC_PROVIDER_H__
#define __RK3308_CODEC_PROVIDER_H__

#ifdef CONFIG_SND_SOC_RK3308
void rk3308_codec_set_jack_detect(struct snd_soc_codec *codec,
				  struct snd_soc_jack *hpdet_jack);
#else
static inline void rk3308_codec_set_jack_detect(struct snd_soc_codec *codec,
						struct snd_soc_jack *hpdet_jack)
{
}
#endif

#endif /* __RK3308_CODEC_PROVIDER_H__ */
