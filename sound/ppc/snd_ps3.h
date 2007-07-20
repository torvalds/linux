/*
 * Audio support for PS3
 * Copyright (C) 2007 Sony Computer Entertainment Inc.
 * All rights reserved.
 * Copyright 2006, 2007 Sony Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the Licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#if !defined(_SND_PS3_H_)
#define _SND_PS3_H_

#include <linux/irqreturn.h>

#define SND_PS3_DRIVER_NAME "snd_ps3"

enum snd_ps3_out_channel {
	SND_PS3_OUT_SPDIF_0,
	SND_PS3_OUT_SPDIF_1,
	SND_PS3_OUT_SERIAL_0,
	SND_PS3_OUT_DEVS
};

enum snd_ps3_dma_filltype {
	SND_PS3_DMA_FILLTYPE_FIRSTFILL,
	SND_PS3_DMA_FILLTYPE_RUNNING,
	SND_PS3_DMA_FILLTYPE_SILENT_FIRSTFILL,
	SND_PS3_DMA_FILLTYPE_SILENT_RUNNING
};

enum snd_ps3_ch {
	SND_PS3_CH_L = 0,
	SND_PS3_CH_R = 1,
	SND_PS3_CH_MAX = 2
};

struct snd_ps3_avsetting_info {
	uint32_t avs_audio_ch;     /* fixed */
	uint32_t avs_audio_rate;
	uint32_t avs_audio_width;
	uint32_t avs_audio_format; /* fixed */
	uint32_t avs_audio_source; /* fixed */
};
/*
 * PS3 audio 'card' instance
 * there should be only ONE hardware.
 */
struct snd_ps3_card_info {
	struct ps3_system_bus_device *ps3_dev;
	struct snd_card *card;

	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;

	/* hvc info */
	u64 audio_lpar_addr;
	u64 audio_lpar_size;

	/* registers */
	void __iomem *mapped_mmio_vaddr;

	/* irq */
	u64 audio_irq_outlet;
	unsigned int irq_no;

	/* remember avsetting */
	struct snd_ps3_avsetting_info avs;

	/* dma buffer management */
	spinlock_t dma_lock;
		/* dma_lock start */
		void * dma_start_vaddr[2]; /* 0 for L, 1 for R */
		dma_addr_t dma_start_bus_addr[2];
		size_t dma_buffer_size;
		void * dma_last_transfer_vaddr[2];
		void * dma_next_transfer_vaddr[2];
		int    silent;
		/* dma_lock end */

	int running;

	/* null buffer */
	void *null_buffer_start_vaddr;
	dma_addr_t null_buffer_start_dma_addr;

	/* start delay */
	unsigned int start_delay;

};


/* PS3 audio DMAC block size in bytes */
#define PS3_AUDIO_DMAC_BLOCK_SIZE (128)
/* one stage (stereo)  of audio FIFO in bytes */
#define PS3_AUDIO_FIFO_STAGE_SIZE (256)
/* how many stages the fifo have */
#define PS3_AUDIO_FIFO_STAGE_COUNT (8)
/* fifo size 128 bytes * 8 stages * stereo (2ch) */
#define PS3_AUDIO_FIFO_SIZE \
	(PS3_AUDIO_FIFO_STAGE_SIZE * PS3_AUDIO_FIFO_STAGE_COUNT)

/* PS3 audio DMAC max block count in one dma shot = 128 (0x80) blocks*/
#define PS3_AUDIO_DMAC_MAX_BLOCKS  (PS3_AUDIO_DMASIZE_BLOCKS_MASK + 1)

#define PS3_AUDIO_NORMAL_DMA_START_CH (0)
#define PS3_AUDIO_NORMAL_DMA_COUNT    (8)
#define PS3_AUDIO_NULL_DMA_START_CH \
	(PS3_AUDIO_NORMAL_DMA_START_CH + PS3_AUDIO_NORMAL_DMA_COUNT)
#define PS3_AUDIO_NULL_DMA_COUNT      (2)

#define SND_PS3_MAX_VOL (0x0F)
#define SND_PS3_MIN_VOL (0x00)
#define SND_PS3_MIN_ATT SND_PS3_MIN_VOL
#define SND_PS3_MAX_ATT SND_PS3_MAX_VOL

#define SND_PS3_PCM_PREALLOC_SIZE \
	(PS3_AUDIO_DMAC_BLOCK_SIZE * PS3_AUDIO_DMAC_MAX_BLOCKS * 4)

#define SND_PS3_DMA_REGION_SIZE \
	(SND_PS3_PCM_PREALLOC_SIZE + PAGE_SIZE)

#define PS3_AUDIO_IOID       (1UL)

#endif /* _SND_PS3_H_ */
