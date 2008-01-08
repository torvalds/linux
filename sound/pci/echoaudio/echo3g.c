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

#define ECHO3G_FAMILY
#define ECHOCARD_ECHO3G
#define ECHOCARD_NAME "Echo3G"
#define ECHOCARD_HAS_MONITOR
#define ECHOCARD_HAS_ASIC
#define ECHOCARD_HAS_INPUT_NOMINAL_LEVEL
#define ECHOCARD_HAS_OUTPUT_NOMINAL_LEVEL
#define ECHOCARD_HAS_SUPER_INTERLEAVE
#define ECHOCARD_HAS_DIGITAL_IO
#define ECHOCARD_HAS_DIGITAL_MODE_SWITCH
#define ECHOCARD_HAS_ADAT	6
#define ECHOCARD_HAS_EXTERNAL_CLOCK
#define ECHOCARD_HAS_STEREO_BIG_ENDIAN32
#define ECHOCARD_HAS_MIDI
#define ECHOCARD_HAS_PHANTOM_POWER

/* Pipe indexes */
#define PX_ANALOG_OUT	0
#define PX_DIGITAL_OUT	chip->px_digital_out
#define PX_ANALOG_IN	chip->px_analog_in
#define PX_DIGITAL_IN	chip->px_digital_in
#define PX_NUM		chip->px_num

/* Bus indexes */
#define BX_ANALOG_OUT	0
#define BX_DIGITAL_OUT	chip->bx_digital_out
#define BX_ANALOG_IN	chip->bx_analog_in
#define BX_DIGITAL_IN	chip->bx_digital_in
#define BX_NUM		chip->bx_num


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
#include <sound/rawmidi.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include "echoaudio.h"

MODULE_FIRMWARE("ea/loader_dsp.fw");
MODULE_FIRMWARE("ea/echo3g_dsp.fw");
MODULE_FIRMWARE("ea/3g_asic.fw");

#define FW_361_LOADER	0
#define FW_ECHO3G_DSP	1
#define FW_3G_ASIC	2

static const struct firmware card_fw[] = {
	{0, "loader_dsp.fw"},
	{0, "echo3g_dsp.fw"},
	{0, "3g_asic.fw"}
};

static struct pci_device_id snd_echo_ids[] = {
	{0x1057, 0x3410, 0xECC0, 0x0100, 0, 0, 0},	/* Echo 3G */
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
	.rates = 	SNDRV_PCM_RATE_32000 |
			SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000 |
			SNDRV_PCM_RATE_88200 |
			SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_CONTINUOUS,
	.rate_min = 32000,
	.rate_max = 100000,
	.channels_min = 1,
	.channels_max = 8,
	.buffer_bytes_max = 262144,
	.period_bytes_min = 32,
	.period_bytes_max = 131072,
	.periods_min = 2,
	.periods_max = 220,
};

#include "echo3g_dsp.c"
#include "echoaudio_dsp.c"
#include "echoaudio_3g.c"
#include "echoaudio.c"
#include "midi.c"
