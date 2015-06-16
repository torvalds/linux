/*
 * card_info.h - ALSA PCM interface for Rockchip
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _SND_SOC_ROCKCHIP_CARD_INFO_H
#define _SND_SOC_ROCKCHIP_CARD_INFO_H

int rockchip_of_get_sound_card_info_(struct snd_soc_card *card,
				     bool is_need_fmt);
int rockchip_of_get_sound_card_info(struct snd_soc_card *card);

#endif /* _SND_SOC_ROCKCHIP_CARD_INFO_H */
