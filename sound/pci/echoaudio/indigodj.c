// SPDX-License-Identifier: GPL-2.0-only
/*
 *  ALSA driver for Echoaudio soundcards.
 *  Copyright (C) 2003-2004 Giuliano Pochini <pochini@shiny.it>
 */

#define INDIGO_FAMILY
#define ECHOCARD_INDIGO_DJ
#define ECHOCARD_NAME "Indigo DJ"
#define ECHOCARD_HAS_SUPER_INTERLEAVE
#define ECHOCARD_HAS_VMIXER
#define ECHOCARD_HAS_STEREO_BIG_ENDIAN32

/* Pipe indexes */
#define PX_ANALOG_OUT	0	/* 8 */
#define PX_DIGITAL_OUT	8	/* 0 */
#define PX_ANALOG_IN	8	/* 0 */
#define PX_DIGITAL_IN	8	/* 0 */
#define PX_NUM		8

/* Bus indexes */
#define BX_ANALOG_OUT	0	/* 4 */
#define BX_DIGITAL_OUT	4	/* 0 */
#define BX_ANALOG_IN	4	/* 0 */
#define BX_DIGITAL_IN	4	/* 0 */
#define BX_NUM		4


#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/asoundef.h>
#include <sound/initval.h>
#include <linux/atomic.h>
#include "echoaudio.h"

MODULE_FIRMWARE("ea/loader_dsp.fw");
MODULE_FIRMWARE("ea/indigo_dj_dsp.fw");

#define FW_361_LOADER		0
#define FW_INDIGO_DJ_DSP	1

static const struct firmware card_fw[] = {
	{0, "loader_dsp.fw"},
	{0, "indigo_dj_dsp.fw"}
};

static const struct pci_device_id snd_echo_ids[] = {
	{0x1057, 0x3410, 0xECC0, 0x00B0, 0, 0, 0},	/* Indigo DJ*/
	{0,}
};

static const struct snd_pcm_hardware pcm_hardware_skel = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_SYNC_START,
	.formats =	SNDRV_PCM_FMTBIT_U8 |
			SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_3LE |
			SNDRV_PCM_FMTBIT_S32_LE |
			SNDRV_PCM_FMTBIT_S32_BE,
	.rates = 	SNDRV_PCM_RATE_32000 |
			SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000 |
			SNDRV_PCM_RATE_88200 |
			SNDRV_PCM_RATE_96000,
	.rate_min = 32000,
	.rate_max = 96000,
	.channels_min = 1,
	.channels_max = 4,
	.buffer_bytes_max = 262144,
	.period_bytes_min = 32,
	.period_bytes_max = 131072,
	.periods_min = 2,
	.periods_max = 220,
};

#include "indigodj_dsp.c"
#include "echoaudio_dsp.c"
#include "echoaudio.c"

