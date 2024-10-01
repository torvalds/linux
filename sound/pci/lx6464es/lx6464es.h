/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- linux-c -*- *
 *
 * ALSA driver for the digigram lx6464es interface
 *
 * Copyright (c) 2009 Tim Blechmann <tim@klingt.org>
 */

#ifndef LX6464ES_H
#define LX6464ES_H

#include <linux/spinlock.h>
#include <linux/atomic.h>

#include <sound/core.h>
#include <sound/pcm.h>

#include "lx_core.h"

#define LXP "LX6464ES: "

enum {
    ES_cmd_free         = 0,    /* no command executing */
    ES_cmd_processing   = 1,	/* execution of a read/write command */
    ES_read_pending     = 2,    /* a asynchron read command is pending */
    ES_read_finishing   = 3,    /* a read command has finished waiting (set by
				 * Interrupt or CancelIrp) */
};

enum lx_stream_status {
	LX_STREAM_STATUS_FREE,
/* 	LX_STREAM_STATUS_OPEN, */
	LX_STREAM_STATUS_SCHEDULE_RUN,
/* 	LX_STREAM_STATUS_STARTED, */
	LX_STREAM_STATUS_RUNNING,
	LX_STREAM_STATUS_SCHEDULE_STOP,
/* 	LX_STREAM_STATUS_STOPPED, */
/* 	LX_STREAM_STATUS_PAUSED */
};


struct lx_stream {
	struct snd_pcm_substream  *stream;
	snd_pcm_uframes_t          frame_pos;
	enum lx_stream_status      status; /* free, open, running, draining
					    * pause */
	unsigned int               is_capture:1;
};


struct lx6464es {
	struct snd_card        *card;
	struct pci_dev         *pci;
	int			irq;

	u8			mac_address[6];

	struct mutex		lock;        /* interrupt lock */
	struct mutex            setup_mutex; /* mutex used in hw_params, open
					      * and close */

	/* ports */
	unsigned long		port_plx;	   /* io port (size=256) */
	void __iomem           *port_plx_remapped; /* remapped plx port */
	void __iomem           *port_dsp_bar;      /* memory port (32-bit,
						    * non-prefetchable,
						    * size=8K) */

	/* messaging */
	struct mutex		msg_lock;          /* message lock */
	struct lx_rmh           rmh;
	u32			irqsrc;

	/* configuration */
	uint			freq_ratio : 2;
	uint                    playback_mute : 1;
	uint                    hardware_running[2];
	u32                     board_sample_rate; /* sample rate read from
						    * board */
	u16                     pcm_granularity;   /* board blocksize */

	/* dma */
	struct snd_dma_buffer   capture_dma_buf;
	struct snd_dma_buffer   playback_dma_buf;

	/* pcm */
	struct snd_pcm         *pcm;

	/* streams */
	struct lx_stream        capture_stream;
	struct lx_stream        playback_stream;
};


#endif /* LX6464ES_H */
