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
	void	*had_substream;
	void	(*period_elapsed)(void *had_substream);
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

struct had_pvt_data {
	enum had_status_stream		stream_type;
};

struct had_callback_ops {
	had_event_call_back intel_had_event_call_back;
};

/**
 * struct snd_intelhad - intelhad driver structure
 *
 * @card: ptr to hold card details
 * @card_index: sound card index
 * @card_id: detected sound card id
 * @query_ops: caps call backs for get/set operations
 * @drv_status: driver status
 * @buf_info: ring buffer info
 * @stream_info: stream information
 * @eeld: holds EELD info
 * @curr_buf: pointer to hold current active ring buf
 * @valid_buf_cnt: ring buffer count for stream
 * @had_spinlock: driver lock
 * @aes_bits: IEC958 status bits
 * @buff_done: id of current buffer done intr
 * @dev: platoform device handle
 * @kctl: holds kctl ptrs used for channel map
 * @chmap: holds channel map info
 * @audio_reg_base: hdmi audio register base offset
 * @hw_silence: flag indicates SoC support for HW silence/Keep alive
 */
struct snd_intelhad {
	struct snd_card	*card;
	int		card_index;
	char		*card_id;
	struct hdmi_audio_query_set_ops	query_ops;
	enum had_drv_status	drv_status;
	struct		ring_buf_info buf_info[HAD_NUM_OF_RING_BUFS];
	struct		pcm_stream_info stream_info;
	union otm_hdmi_eld_t	eeld;
	bool dp_output;
	enum		intel_had_aud_buf_type curr_buf;
	int		valid_buf_cnt;
	unsigned int	aes_bits;
	int flag_underrun;
	struct had_pvt_data *private_data;
	spinlock_t had_spinlock;
	enum		intel_had_aud_buf_type buff_done;
	struct device *dev;
	struct snd_kcontrol *kctl;
	struct snd_pcm_chmap *chmap;
	unsigned int	*audio_reg_base;
	unsigned int	audio_cfg_offset;
	bool		hw_silence;
};

int had_event_handler(enum had_event_type event_type, void *data);

int hdmi_audio_query(void *drv_data, struct hdmi_audio_event event);
int hdmi_audio_suspend(void *drv_data, struct hdmi_audio_event event);
int hdmi_audio_resume(void *drv_data);
int hdmi_audio_mode_change(struct snd_pcm_substream *substream);
extern struct snd_pcm_ops snd_intelhad_playback_ops;

int snd_intelhad_init_audio_ctrl(struct snd_pcm_substream *substream,
					struct snd_intelhad *intelhaddata,
					int flag_silence);
int snd_intelhad_prog_buffer(struct snd_intelhad *intelhaddata,
					int start, int end);
int snd_intelhad_invd_buffer(int start, int end);
int snd_intelhad_read_len(struct snd_intelhad *intelhaddata);
void had_build_channel_allocation_map(struct snd_intelhad *intelhaddata);

void snd_intelhad_enable_audio(struct snd_pcm_substream *substream, u8 enable);
void snd_intelhad_handle_underrun(struct snd_intelhad *intelhaddata);

/* Register access functions */
int had_get_hwstate(struct snd_intelhad *intelhaddata);
int had_get_caps(enum had_caps_list query_element, void *capabilties);
int had_set_caps(enum had_caps_list set_element, void *capabilties);
int had_read_register(u32 reg_addr, u32 *data);
int had_write_register(u32 reg_addr, u32 data);
int had_read_modify(u32 reg_addr, u32 data, u32 mask);

int hdmi_audio_probe(void *devptr);
int hdmi_audio_remove(void *pdev);

#endif /* _INTEL_HDMI_AUDIO_ */
