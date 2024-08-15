/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sound/soc/codec/wl1273.h
 *
 * ALSA SoC WL1273 codec driver
 *
 * Copyright (C) Nokia Corporation
 * Author: Matti Aaltonen <matti.j.aaltonen@nokia.com>
 */

#ifndef __WL1273_CODEC_H__
#define __WL1273_CODEC_H__

int wl1273_get_format(struct snd_soc_component *component, unsigned int *fmt);

#endif	/* End of __WL1273_CODEC_H__ */
