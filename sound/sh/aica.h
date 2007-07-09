/* aica.h
 * Header file for ALSA driver for
 * Sega Dreamcast Yamaha AICA sound
 * Copyright Adrian McMenamin
 * <adrian@mcmen.demon.co.uk>
 * 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/* SPU memory and register constants etc */
#define G2_FIFO 0xa05f688c
#define SPU_MEMORY_BASE 0xA0800000
#define ARM_RESET_REGISTER 0xA0702C00
#define SPU_REGISTER_BASE 0xA0700000

/* AICA channels stuff */
#define AICA_CONTROL_POINT 0xA0810000
#define AICA_CONTROL_CHANNEL_SAMPLE_NUMBER 0xA0810008
#define AICA_CHANNEL0_CONTROL_OFFSET 0x10004

/* Command values */
#define AICA_CMD_KICK 0x80000000
#define AICA_CMD_NONE 0
#define AICA_CMD_START 1
#define AICA_CMD_STOP 2
#define AICA_CMD_VOL 3

/* Sound modes */
#define SM_8BIT		1
#define SM_16BIT	0
#define SM_ADPCM	2

/* Buffer and period size */
#define AICA_BUFFER_SIZE 0x8000
#define AICA_PERIOD_SIZE 0x800
#define AICA_PERIOD_NUMBER 16

#define AICA_CHANNEL0_OFFSET 0x11000
#define AICA_CHANNEL1_OFFSET 0x21000
#define CHANNEL_OFFSET 0x10000

#define AICA_DMA_CHANNEL 0
#define AICA_DMA_MODE 5

#define SND_AICA_DRIVER "AICA"

struct aica_channel {
	uint32_t cmd;		/* Command ID           */
	uint32_t pos;		/* Sample position      */
	uint32_t length;	/* Sample length        */
	uint32_t freq;		/* Frequency            */
	uint32_t vol;		/* Volume 0-255         */
	uint32_t pan;		/* Pan 0-255            */
	uint32_t sfmt;		/* Sound format         */
	uint32_t flags;		/* Bit flags            */
};

struct snd_card_aica {
	struct work_struct spu_dma_work;
	struct snd_card *card;
	struct aica_channel *channel;
	struct snd_pcm_substream *substream;
	int clicks;
	int current_period;
	struct timer_list timer;
	int master_volume;
	int dma_check;
};
