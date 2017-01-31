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

#include <linux/types.h>
#include <sound/initval.h>
#include <linux/version.h>
#include <linux/pm_runtime.h>
#include <sound/asoundef.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include "intel_hdmi_lpe_audio.h"

#define PCM_INDEX		0
#define MAX_PB_STREAMS		1
#define MAX_CAP_STREAMS		0
#define HDMI_AUDIO_DRIVER	"hdmi-audio"

#define HDMI_INFO_FRAME_WORD1	0x000a0184
#define DP_INFO_FRAME_WORD1	0x00441b84
#define FIFO_THRESHOLD		0xFE
#define DMA_FIFO_THRESHOLD	0x7
#define BYTES_PER_WORD		0x4

/* Sampling rate as per IEC60958 Ver 3 */
#define CH_STATUS_MAP_32KHZ	0x3
#define CH_STATUS_MAP_44KHZ	0x0
#define CH_STATUS_MAP_48KHZ	0x2
#define CH_STATUS_MAP_88KHZ	0x8
#define CH_STATUS_MAP_96KHZ	0xA
#define CH_STATUS_MAP_176KHZ	0xC
#define CH_STATUS_MAP_192KHZ	0xE

#define MAX_SMPL_WIDTH_20	0x0
#define MAX_SMPL_WIDTH_24	0x1
#define SMPL_WIDTH_16BITS	0x1
#define SMPL_WIDTH_24BITS	0x5
#define CHANNEL_ALLOCATION	0x1F
#define MASK_BYTE0		0x000000FF
#define VALID_DIP_WORDS		3
#define LAYOUT0			0
#define LAYOUT1			1
#define SWAP_LFE_CENTER		0x00fac4c8
#define AUD_CONFIG_CH_MASK_V2	0x70

struct pcm_stream_info {
	int		str_id;
	struct snd_pcm_substream	*had_substream;
	u32		buffer_ptr;
	u64		buffer_rendered;
	u32		ring_buf_size;
	int		sfreq;
};

struct ring_buf_info {
	u32	buf_addr;
	u32	buf_size;
	u8	is_valid;
};

struct had_stream_pvt {
	enum had_stream_status		stream_status;
	int				stream_ops;
	ssize_t				dbg_cum_bytes;
};

struct had_stream_data {
	enum had_status_stream		stream_type;
};

/**
 * struct snd_intelhad - intelhad driver structure
 *
 * @card: ptr to hold card details
 * @drv_status: driver status
 * @buf_info: ring buffer info
 * @stream_info: stream information
 * @eld: holds ELD info
 * @curr_buf: pointer to hold current active ring buf
 * @valid_buf_cnt: ring buffer count for stream
 * @had_spinlock: driver lock
 * @aes_bits: IEC958 status bits
 * @buff_done: id of current buffer done intr
 * @dev: platoform device handle
 * @chmap: holds channel map info
 * @underrun_count: PCM stream underrun counter
 */
struct snd_intelhad {
	struct snd_card	*card;
	enum had_drv_status	drv_status;
	struct		ring_buf_info buf_info[HAD_NUM_OF_RING_BUFS];
	struct		pcm_stream_info stream_info;
	union otm_hdmi_eld_t	eld;
	bool dp_output;
	enum		intel_had_aud_buf_type curr_buf;
	int		valid_buf_cnt;
	unsigned int	aes_bits;
	bool flag_underrun;
	struct had_stream_data stream_data;
	spinlock_t had_spinlock;
	enum		intel_had_aud_buf_type buff_done;
	struct device *dev;
	struct snd_pcm_chmap *chmap;
	int underrun_count;
	enum hdmi_connector_status state;
	int tmds_clock_speed;
	int link_rate;

	/* internal stuff */
	int irq;
	void __iomem *mmio_start;
	unsigned int had_config_offset;
	struct work_struct hdmi_audio_wq;
};

#endif /* _INTEL_HDMI_AUDIO_ */
