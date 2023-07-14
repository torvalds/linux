/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtk-soundcard-driver.h  --  MediaTek soundcard driver common definition
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Trevor Wu <trevor.wu@mediatek.com>
 */

#ifndef _MTK_SOUNDCARD_DRIVER_H_
#define _MTK_SOUNDCARD_DRIVER_H_

int parse_dai_link_info(struct snd_soc_card *card);
void clean_card_reference(struct snd_soc_card *card);
#endif
