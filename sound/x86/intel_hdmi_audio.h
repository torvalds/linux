/*
 * Copyright (C) 2016 Intel Corporation
 *  Authors:	Sailaja Bandarupalli <sailaja.bandarupalli@intel.com>
 *		Ramesh Babu K V	<ramesh.babu@intel.com>
 *		Vaibhav Agarwal <vaibhav.agarwal@intel.com>
 *		Jerome Anand <jerome.anand@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _INTEL_HDMI_AUDIO_H_
#define _INTEL_HDMI_AUDIO_H_

#include "intel_hdmi_lpe_audio.h"

#define MAX_PB_STREAMS		1
#define MAX_CAP_STREAMS		0
#define BYTES_PER_WORD		0x4
#define INTEL_HAD		"HdmiLpeAudio"

/*
 *	CEA speaker placement:
 *
 *	FL  FLC   FC   FRC   FR
 *
 *						LFE
 *
 *	RL  RLC   RC   RRC   RR
 *
 *	The Left/Right Surround channel _notions_ LS/RS in SMPTE 320M
 *	corresponds to CEA RL/RR; The SMPTE channel _assignment_ C/LFE is
 *	swapped to CEA LFE/FC.
 */
enum cea_speaker_placement {
	FL  = (1 <<  0),        /* Front Left           */
	FC  = (1 <<  1),        /* Front Center         */
	FR  = (1 <<  2),        /* Front Right          */
	FLC = (1 <<  3),        /* Front Left Center    */
	FRC = (1 <<  4),        /* Front Right Center   */
	RL  = (1 <<  5),        /* Rear Left            */
	RC  = (1 <<  6),        /* Rear Center          */
	RR  = (1 <<  7),        /* Rear Right           */
	RLC = (1 <<  8),        /* Rear Left Center     */
	RRC = (1 <<  9),        /* Rear Right Center    */
	LFE = (1 << 10),        /* Low Frequency Effect */
};

struct cea_channel_speaker_allocation {
	int ca_index;
	int speakers[8];

	/* derived values, just for convenience */
	int channels;
	int spk_mask;
};

struct channel_map_table {
	unsigned char map;              /* ALSA API channel map position */
	unsigned char cea_slot;         /* CEA slot value */
	int spk_mask;                   /* speaker position bit mask */
};

struct pcm_stream_info {
	struct snd_pcm_substream *substream;
	int substream_refcount;
};

/*
 * struct snd_intelhad - intelhad driver structure
 *
 * @card: ptr to hold card details
 * @connected: the monitor connection status
 * @stream_info: stream information
 * @eld: holds ELD info
 * @curr_buf: pointer to hold current active ring buf
 * @valid_buf_cnt: ring buffer count for stream
 * @had_spinlock: driver lock
 * @aes_bits: IEC958 status bits
 * @buff_done: id of current buffer done intr
 * @dev: platform device handle
 * @chmap: holds channel map info
 */
struct snd_intelhad {
	struct snd_intelhad_card *card_ctx;
	bool		connected;
	struct		pcm_stream_info stream_info;
	unsigned char	eld[HDMI_MAX_ELD_BYTES];
	bool dp_output;
	unsigned int	aes_bits;
	spinlock_t had_spinlock;
	struct device *dev;
	struct snd_pcm_chmap *chmap;
	int tmds_clock_speed;
	int link_rate;
	int port; /* fixed */
	int pipe; /* can change dynamically */

	/* ring buffer (BD) position index */
	unsigned int bd_head;
	/* PCM buffer position indices */
	unsigned int pcmbuf_head;	/* being processed */
	unsigned int pcmbuf_filled;	/* to be filled */

	unsigned int num_bds;		/* number of BDs */
	unsigned int period_bytes;	/* PCM period size in bytes */

	/* internal stuff */
	union aud_cfg aud_config;	/* AUD_CONFIG reg value cache */
	struct work_struct hdmi_audio_wq;
	struct mutex mutex; /* for protecting chmap and eld */
	struct snd_jack *jack;
};

struct snd_intelhad_card {
	struct snd_card	*card;
	struct device *dev;

	/* internal stuff */
	int irq;
	void __iomem *mmio_start;
	int num_pipes;
	int num_ports;
	struct snd_intelhad pcm_ctx[3]; /* one for each port */
};

#endif /* _INTEL_HDMI_AUDIO_ */
