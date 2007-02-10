/*
 *  ALSA driver for Echoaudio soundcards.
 *  Copyright (C) 2003-2004 Giuliano Pochini <pochini@shiny.it>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define ECHOGALS_FAMILY
#define ECHOCARD_DARLA24
#define ECHOCARD_NAME "Darla24"
#define ECHOCARD_HAS_MONITOR
#define ECHOCARD_HAS_INPUT_NOMINAL_LEVEL
#define ECHOCARD_HAS_OUTPUT_NOMINAL_LEVEL
#define ECHOCARD_HAS_EXTERNAL_CLOCK
#define ECHOCARD_HAS_SUPER_INTERLEAVE

/* Pipe indexes */
#define PX_ANALOG_OUT	0	/* 8 */
#define PX_DIGITAL_OUT	8	/* 0 */
#define PX_ANALOG_IN	8	/* 2 */
#define PX_DIGITAL_IN	10	/* 0 */
#define PX_NUM		10

/* Bus indexes */
#define BX_ANALOG_OUT	0	/* 8 */
#define BX_DIGITAL_OUT	8	/* 0 */
#define BX_ANALOG_IN	8	/* 2 */
#define BX_DIGITAL_IN	10	/* 0 */
#define BX_NUM		10


#include <sound/driver.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/asoundef.h>
#include <sound/initval.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include "echoaudio.h"

#define FW_DARLA24_DSP	0

static const struct firmware card_fw[] = {
	{0, "darla24_dsp.fw"}
};

static struct pci_device_id snd_echo_ids[] = {
	{0x1057, 0x1801, 0xECC0, 0x0040, 0, 0, 0},	/* DSP 56301 Darla24 rev.0 */
	{0x1057, 0x1801, 0xECC0, 0x0041, 0, 0, 0},	/* DSP 56301 Darla24 rev.1 */
	{0,}
};

static struct snd_pcm_hardware pcm_hardware_skel = {
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
	.rates =	SNDRV_PCM_RATE_8000_48000 |
			SNDRV_PCM_RATE_88200 |
			SNDRV_PCM_RATE_96000,
	.rate_min = 8000,
	.rate_max = 96000,
	.channels_min = 1,
	.channels_max = 8,
	.buffer_bytes_max = 262144,
	.period_bytes_min = 32,
	.period_bytes_max = 131072,
	.periods_min = 2,
	.periods_max = 220,
	/* One page (4k) contains 512 instructions. I don't know if the hw
	supports lists longer than this. In this case periods_max=220 is a
	safe limit to make sure the list never exceeds 512 instructions. */
};


#include "darla24_dsp.c"
#include "echoaudio_dsp.c"
#include "echoaudio.c"
