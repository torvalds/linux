/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __Q6_ASM_H__
#define __Q6_ASM_H__
#include "q6dsp-common.h"
#include <dt-bindings/sound/qcom,q6asm.h>

/* ASM client callback events */
#define CMD_PAUSE			0x0001
#define ASM_CLIENT_EVENT_CMD_PAUSE_DONE		0x1001
#define CMD_FLUSH				0x0002
#define ASM_CLIENT_EVENT_CMD_FLUSH_DONE		0x1002
#define CMD_EOS				0x0003
#define ASM_CLIENT_EVENT_CMD_EOS_DONE		0x1003
#define CMD_CLOSE				0x0004
#define ASM_CLIENT_EVENT_CMD_CLOSE_DONE		0x1004
#define CMD_OUT_FLUSH				0x0005
#define ASM_CLIENT_EVENT_CMD_OUT_FLUSH_DONE	0x1005
#define CMD_SUSPEND				0x0006
#define ASM_CLIENT_EVENT_CMD_SUSPEND_DONE	0x1006
#define ASM_CLIENT_EVENT_CMD_RUN_DONE		0x1008
#define ASM_CLIENT_EVENT_DATA_WRITE_DONE	0x1009
#define ASM_CLIENT_EVENT_DATA_READ_DONE		0x100a
#define ASM_WRITE_TOKEN_MASK			GENMASK(15, 0)
#define ASM_WRITE_TOKEN_LEN_MASK		GENMASK(31, 16)
#define ASM_WRITE_TOKEN_LEN_SHIFT		16

enum {
	LEGACY_PCM_MODE = 0,
	LOW_LATENCY_PCM_MODE,
	ULTRA_LOW_LATENCY_PCM_MODE,
	ULL_POST_PROCESSING_PCM_MODE,
};

#define MAX_SESSIONS	8
#define FORMAT_LINEAR_PCM   0x0000
#define ASM_LAST_BUFFER_FLAG           BIT(30)

struct q6asm_flac_cfg {
        u32 sample_rate;
        u32 ext_sample_rate;
        u32 min_frame_size;
        u32 max_frame_size;
        u16 stream_info_present;
        u16 min_blk_size;
        u16 max_blk_size;
        u16 ch_cfg;
        u16 sample_size;
        u16 md5_sum;
};

struct q6asm_wma_cfg {
	u32 fmtag;
	u32 num_channels;
	u32 sample_rate;
	u32 bytes_per_sec;
	u32 block_align;
	u32 bits_per_sample;
	u32 channel_mask;
	u32 enc_options;
	u32 adv_enc_options;
	u32 adv_enc_options2;
};

struct q6asm_alac_cfg {
	u32 frame_length;
	u8 compatible_version;
	u8 bit_depth;
	u8 pb;
	u8 mb;
	u8 kb;
	u8 num_channels;
	u16 max_run;
	u32 max_frame_bytes;
	u32 avg_bit_rate;
	u32 sample_rate;
	u32 channel_layout_tag;
};

struct q6asm_ape_cfg {
	u16 compatible_version;
	u16 compression_level;
	u32 format_flags;
	u32 blocks_per_frame;
	u32 final_frame_blocks;
	u32 total_frames;
	u16 bits_per_sample;
	u16 num_channels;
	u32 sample_rate;
	u32 seek_table_present;
};

typedef void (*q6asm_cb) (uint32_t opcode, uint32_t token,
			  void *payload, void *priv);
struct audio_client;
struct audio_client *q6asm_audio_client_alloc(struct device *dev,
					      q6asm_cb cb, void *priv,
					      int session_id, int perf_mode);
void q6asm_audio_client_free(struct audio_client *ac);
int q6asm_write_async(struct audio_client *ac, uint32_t stream_id, uint32_t len,
		      uint32_t msw_ts, uint32_t lsw_ts, uint32_t wflags);
int q6asm_open_write(struct audio_client *ac, uint32_t stream_id,
		     uint32_t format, u32 codec_profile,
		     uint16_t bits_per_sample, bool is_gapless);

int q6asm_open_read(struct audio_client *ac, uint32_t stream_id,
		    uint32_t format, uint16_t bits_per_sample);
int q6asm_enc_cfg_blk_pcm_format_support(struct audio_client *ac,
					 uint32_t stream_id, uint32_t rate,
					 uint32_t channels,
					 uint16_t bits_per_sample);

int q6asm_read(struct audio_client *ac, uint32_t stream_id);

int q6asm_media_format_block_multi_ch_pcm(struct audio_client *ac,
					  uint32_t stream_id,
					  uint32_t rate, uint32_t channels,
					  u8 channel_map[PCM_MAX_NUM_CHANNEL],
					  uint16_t bits_per_sample);
int q6asm_stream_media_format_block_flac(struct audio_client *ac,
					 uint32_t stream_id,
					 struct q6asm_flac_cfg *cfg);
int q6asm_stream_media_format_block_wma_v9(struct audio_client *ac,
					   uint32_t stream_id,
					   struct q6asm_wma_cfg *cfg);
int q6asm_stream_media_format_block_wma_v10(struct audio_client *ac,
					    uint32_t stream_id,
					    struct q6asm_wma_cfg *cfg);
int q6asm_stream_media_format_block_alac(struct audio_client *ac,
					 uint32_t stream_id,
					 struct q6asm_alac_cfg *cfg);
int q6asm_stream_media_format_block_ape(struct audio_client *ac,
					uint32_t stream_id,
					struct q6asm_ape_cfg *cfg);
int q6asm_run(struct audio_client *ac, uint32_t stream_id, uint32_t flags,
	      uint32_t msw_ts, uint32_t lsw_ts);
int q6asm_run_nowait(struct audio_client *ac, uint32_t stream_id,
		     uint32_t flags, uint32_t msw_ts, uint32_t lsw_ts);
int q6asm_stream_remove_initial_silence(struct audio_client *ac,
					uint32_t stream_id,
					uint32_t initial_samples);
int q6asm_stream_remove_trailing_silence(struct audio_client *ac,
					 uint32_t stream_id,
					 uint32_t trailing_samples);
int q6asm_cmd(struct audio_client *ac, uint32_t stream_id,  int cmd);
int q6asm_cmd_nowait(struct audio_client *ac, uint32_t stream_id,  int cmd);
int q6asm_get_session_id(struct audio_client *c);
int q6asm_map_memory_regions(unsigned int dir,
			     struct audio_client *ac,
			     phys_addr_t phys,
			     size_t period_sz, unsigned int periods);
int q6asm_unmap_memory_regions(unsigned int dir, struct audio_client *ac);
#endif /* __Q6_ASM_H__ */
