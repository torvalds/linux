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

enum {
	LEGACY_PCM_MODE = 0,
	LOW_LATENCY_PCM_MODE,
	ULTRA_LOW_LATENCY_PCM_MODE,
	ULL_POST_PROCESSING_PCM_MODE,
};

#define MAX_SESSIONS	8
#define NO_TIMESTAMP    0xFF00
#define FORMAT_LINEAR_PCM   0x0000

typedef void (*q6asm_cb) (uint32_t opcode, uint32_t token,
			  void *payload, void *priv);
struct audio_client;
struct audio_client *q6asm_audio_client_alloc(struct device *dev,
					      q6asm_cb cb, void *priv,
					      int session_id, int perf_mode);
void q6asm_audio_client_free(struct audio_client *ac);
int q6asm_write_async(struct audio_client *ac, uint32_t len, uint32_t msw_ts,
		       uint32_t lsw_ts, uint32_t flags);
int q6asm_open_write(struct audio_client *ac, uint32_t format,
		     uint16_t bits_per_sample);

int q6asm_open_read(struct audio_client *ac, uint32_t format,
		     uint16_t bits_per_sample);
int q6asm_enc_cfg_blk_pcm_format_support(struct audio_client *ac,
		uint32_t rate, uint32_t channels, uint16_t bits_per_sample);
int q6asm_read(struct audio_client *ac);

int q6asm_media_format_block_multi_ch_pcm(struct audio_client *ac,
					  uint32_t rate, uint32_t channels,
					  u8 channel_map[PCM_MAX_NUM_CHANNEL],
					  uint16_t bits_per_sample);
int q6asm_run(struct audio_client *ac, uint32_t flags, uint32_t msw_ts,
	      uint32_t lsw_ts);
int q6asm_run_nowait(struct audio_client *ac, uint32_t flags, uint32_t msw_ts,
		     uint32_t lsw_ts);
int q6asm_cmd(struct audio_client *ac, int cmd);
int q6asm_cmd_nowait(struct audio_client *ac, int cmd);
int q6asm_get_session_id(struct audio_client *ac);
int q6asm_map_memory_regions(unsigned int dir,
			     struct audio_client *ac,
			     phys_addr_t phys,
			     size_t bufsz, unsigned int bufcnt);
int q6asm_unmap_memory_regions(unsigned int dir, struct audio_client *ac);
#endif /* __Q6_ASM_H__ */
