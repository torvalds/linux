/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCM3008 ALSA SoC Layer
 *
 * Author:	Hugo Villeneuve
 * Copyright (C) 2008 Lyrtech inc
 */

#ifndef __LINUX_SND_SOC_PCM3008_H
#define __LINUX_SND_SOC_PCM3008_H

struct pcm3008_setup_data {
	unsigned dem0_pin;
	unsigned dem1_pin;
	unsigned pdad_pin;
	unsigned pdda_pin;
};

#endif
