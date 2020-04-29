// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/soc/qcom/apr.h>
#include <linux/device.h>
#include <linux/of_platform.h>
#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/of.h>
#include <uapi/sound/asound.h>
#include <uapi/sound/compress_params.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include "q6asm.h"
#include "q6core.h"
#include "q6dsp-errno.h"
#include "q6dsp-common.h"

#define ASM_STREAM_CMD_CLOSE			0x00010BCD
#define ASM_STREAM_CMD_FLUSH			0x00010BCE
#define ASM_SESSION_CMD_PAUSE			0x00010BD3
#define ASM_DATA_CMD_EOS			0x00010BDB
#define ASM_NULL_POPP_TOPOLOGY			0x00010C68
#define ASM_STREAM_CMD_FLUSH_READBUFS		0x00010C09
#define ASM_STREAM_CMD_SET_ENCDEC_PARAM		0x00010C10
#define ASM_STREAM_POSTPROC_TOPO_ID_NONE	0x00010C68
#define ASM_CMD_SHARED_MEM_MAP_REGIONS		0x00010D92
#define ASM_CMDRSP_SHARED_MEM_MAP_REGIONS	0x00010D93
#define ASM_CMD_SHARED_MEM_UNMAP_REGIONS	0x00010D94
#define ASM_DATA_CMD_MEDIA_FMT_UPDATE_V2	0x00010D98
#define ASM_DATA_EVENT_WRITE_DONE_V2		0x00010D99
#define ASM_PARAM_ID_ENCDEC_ENC_CFG_BLK_V2	0x00010DA3
#define ASM_SESSION_CMD_RUN_V2			0x00010DAA
#define ASM_MEDIA_FMT_MULTI_CHANNEL_PCM_V2	0x00010DA5
#define ASM_MEDIA_FMT_MP3			0x00010BE9
#define ASM_MEDIA_FMT_FLAC			0x00010C16
#define ASM_MEDIA_FMT_WMA_V9			0x00010DA8
#define ASM_MEDIA_FMT_WMA_V10			0x00010DA7
#define ASM_DATA_CMD_WRITE_V2			0x00010DAB
#define ASM_DATA_CMD_READ_V2			0x00010DAC
#define ASM_SESSION_CMD_SUSPEND			0x00010DEC
#define ASM_STREAM_CMD_OPEN_WRITE_V3		0x00010DB3
#define ASM_STREAM_CMD_OPEN_READ_V3                 0x00010DB4
#define ASM_DATA_EVENT_READ_DONE_V2 0x00010D9A
#define ASM_STREAM_CMD_OPEN_READWRITE_V2        0x00010D8D
#define ASM_MEDIA_FMT_ALAC			0x00012f31
#define ASM_MEDIA_FMT_APE			0x00012f32


#define ASM_LEGACY_STREAM_SESSION	0
/* Bit shift for the stream_perf_mode subfield. */
#define ASM_SHIFT_STREAM_PERF_MODE_FLAG_IN_OPEN_READ              29
#define ASM_END_POINT_DEVICE_MATRIX	0
#define ASM_DEFAULT_APP_TYPE		0
#define ASM_SYNC_IO_MODE		0x0001
#define ASM_ASYNC_IO_MODE		0x0002
#define ASM_TUN_READ_IO_MODE		0x0004	/* tunnel read write mode */
#define ASM_TUN_WRITE_IO_MODE		0x0008	/* tunnel read write mode */
#define ASM_SHIFT_GAPLESS_MODE_FLAG	31
#define ADSP_MEMORY_MAP_SHMEM8_4K_POOL	3

struct avs_cmd_shared_mem_map_regions {
	u16 mem_pool_id;
	u16 num_regions;
	u32 property_flag;
} __packed;

struct avs_shared_map_region_payload {
	u32 shm_addr_lsw;
	u32 shm_addr_msw;
	u32 mem_size_bytes;
} __packed;

struct avs_cmd_shared_mem_unmap_regions {
	u32 mem_map_handle;
} __packed;

struct asm_data_cmd_media_fmt_update_v2 {
	u32 fmt_blk_size;
} __packed;

struct asm_multi_channel_pcm_fmt_blk_v2 {
	struct asm_data_cmd_media_fmt_update_v2 fmt_blk;
	u16 num_channels;
	u16 bits_per_sample;
	u32 sample_rate;
	u16 is_signed;
	u16 reserved;
	u8 channel_mapping[PCM_MAX_NUM_CHANNEL];
} __packed;

struct asm_flac_fmt_blk_v2 {
	struct asm_data_cmd_media_fmt_update_v2 fmt_blk;
	u16 is_stream_info_present;
	u16 num_channels;
	u16 min_blk_size;
	u16 max_blk_size;
	u16 md5_sum[8];
	u32 sample_rate;
	u32 min_frame_size;
	u32 max_frame_size;
	u16 sample_size;
	u16 reserved;
} __packed;

struct asm_wmastdv9_fmt_blk_v2 {
	struct asm_data_cmd_media_fmt_update_v2 fmt_blk;
	u16          fmtag;
	u16          num_channels;
	u32          sample_rate;
	u32          bytes_per_sec;
	u16          blk_align;
	u16          bits_per_sample;
	u32          channel_mask;
	u16          enc_options;
	u16          reserved;
} __packed;

struct asm_wmaprov10_fmt_blk_v2 {
	struct asm_data_cmd_media_fmt_update_v2 fmt_blk;
	u16          fmtag;
	u16          num_channels;
	u32          sample_rate;
	u32          bytes_per_sec;
	u16          blk_align;
	u16          bits_per_sample;
	u32          channel_mask;
	u16          enc_options;
	u16          advanced_enc_options1;
	u32          advanced_enc_options2;
} __packed;

struct asm_alac_fmt_blk_v2 {
	struct asm_data_cmd_media_fmt_update_v2 fmt_blk;
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
} __packed;

struct asm_ape_fmt_blk_v2 {
	struct asm_data_cmd_media_fmt_update_v2 fmt_blk;
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
} __packed;

struct asm_stream_cmd_set_encdec_param {
	u32                  param_id;
	u32                  param_size;
} __packed;

struct asm_enc_cfg_blk_param_v2 {
	u32                  frames_per_buf;
	u32                  enc_cfg_blk_size;
} __packed;

struct asm_multi_channel_pcm_enc_cfg_v2 {
	struct asm_stream_cmd_set_encdec_param  encdec;
	struct asm_enc_cfg_blk_param_v2	encblk;
	uint16_t  num_channels;
	uint16_t  bits_per_sample;
	uint32_t  sample_rate;
	uint16_t  is_signed;
	uint16_t  reserved;
	uint8_t   channel_mapping[8];
} __packed;

struct asm_data_cmd_read_v2 {
	u32                  buf_addr_lsw;
	u32                  buf_addr_msw;
	u32                  mem_map_handle;
	u32                  buf_size;
	u32                  seq_id;
} __packed;

struct asm_data_cmd_read_v2_done {
	u32	status;
	u32	buf_addr_lsw;
	u32	buf_addr_msw;
};

struct asm_stream_cmd_open_read_v3 {
	u32                    mode_flags;
	u32                    src_endpointype;
	u32                    preprocopo_id;
	u32                    enc_cfg_id;
	u16                    bits_per_sample;
	u16                    reserved;
} __packed;

struct asm_data_cmd_write_v2 {
	u32 buf_addr_lsw;
	u32 buf_addr_msw;
	u32 mem_map_handle;
	u32 buf_size;
	u32 seq_id;
	u32 timestamp_lsw;
	u32 timestamp_msw;
	u32 flags;
} __packed;

struct asm_stream_cmd_open_write_v3 {
	uint32_t mode_flags;
	uint16_t sink_endpointype;
	uint16_t bits_per_sample;
	uint32_t postprocopo_id;
	uint32_t dec_fmt_id;
} __packed;

struct asm_session_cmd_run_v2 {
	u32 flags;
	u32 time_lsw;
	u32 time_msw;
} __packed;

struct audio_buffer {
	phys_addr_t phys;
	uint32_t size;		/* size of buffer */
};

struct audio_port_data {
	struct audio_buffer *buf;
	uint32_t num_periods;
	uint32_t dsp_buf;
	uint32_t mem_map_handle;
};

struct q6asm {
	struct apr_device *adev;
	struct device *dev;
	struct q6core_svc_api_info ainfo;
	wait_queue_head_t mem_wait;
	spinlock_t slock;
	struct audio_client *session[MAX_SESSIONS + 1];
};

struct audio_client {
	int session;
	q6asm_cb cb;
	void *priv;
	uint32_t io_mode;
	struct apr_device *adev;
	struct mutex cmd_lock;
	spinlock_t lock;
	struct kref refcount;
	/* idx:1 out port, 0: in port */
	struct audio_port_data port[2];
	wait_queue_head_t cmd_wait;
	struct aprv2_ibasic_rsp_result_t result;
	int perf_mode;
	int stream_id;
	struct q6asm *q6asm;
	struct device *dev;
};

static inline void q6asm_add_hdr(struct audio_client *ac, struct apr_hdr *hdr,
				 uint32_t pkt_size, bool cmd_flg,
				 uint32_t stream_id)
{
	hdr->hdr_field = APR_SEQ_CMD_HDR_FIELD;
	hdr->src_port = ((ac->session << 8) & 0xFF00) | (stream_id);
	hdr->dest_port = ((ac->session << 8) & 0xFF00) | (stream_id);
	hdr->pkt_size = pkt_size;
	if (cmd_flg)
		hdr->token = ac->session;
}

static int q6asm_apr_send_session_pkt(struct q6asm *a, struct audio_client *ac,
				      struct apr_pkt *pkt, uint32_t rsp_opcode)
{
	struct apr_hdr *hdr = &pkt->hdr;
	int rc;

	mutex_lock(&ac->cmd_lock);
	ac->result.opcode = 0;
	ac->result.status = 0;
	rc = apr_send_pkt(a->adev, pkt);
	if (rc < 0)
		goto err;

	if (rsp_opcode)
		rc = wait_event_timeout(a->mem_wait,
					(ac->result.opcode == hdr->opcode) ||
					(ac->result.opcode == rsp_opcode),
					5 * HZ);
	else
		rc = wait_event_timeout(a->mem_wait,
					(ac->result.opcode == hdr->opcode),
					5 * HZ);

	if (!rc) {
		dev_err(a->dev, "CMD timeout\n");
		rc = -ETIMEDOUT;
	} else if (ac->result.status > 0) {
		dev_err(a->dev, "DSP returned error[%x]\n",
			ac->result.status);
		rc = -EINVAL;
	}

err:
	mutex_unlock(&ac->cmd_lock);
	return rc;
}

static int __q6asm_memory_unmap(struct audio_client *ac,
				phys_addr_t buf_add, int dir)
{
	struct avs_cmd_shared_mem_unmap_regions *mem_unmap;
	struct q6asm *a = dev_get_drvdata(ac->dev->parent);
	struct apr_pkt *pkt;
	int rc, pkt_size;
	void *p;

	if (ac->port[dir].mem_map_handle == 0) {
		dev_err(ac->dev, "invalid mem handle\n");
		return -EINVAL;
	}

	pkt_size = APR_HDR_SIZE + sizeof(*mem_unmap);
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	mem_unmap = p + APR_HDR_SIZE;

	pkt->hdr.hdr_field = APR_SEQ_CMD_HDR_FIELD;
	pkt->hdr.src_port = 0;
	pkt->hdr.dest_port = 0;
	pkt->hdr.pkt_size = pkt_size;
	pkt->hdr.token = ((ac->session << 8) | dir);

	pkt->hdr.opcode = ASM_CMD_SHARED_MEM_UNMAP_REGIONS;
	mem_unmap->mem_map_handle = ac->port[dir].mem_map_handle;

	rc = q6asm_apr_send_session_pkt(a, ac, pkt, 0);
	if (rc < 0) {
		kfree(pkt);
		return rc;
	}

	ac->port[dir].mem_map_handle = 0;

	kfree(pkt);
	return 0;
}


static void q6asm_audio_client_free_buf(struct audio_client *ac,
					struct audio_port_data *port)
{
	unsigned long flags;

	spin_lock_irqsave(&ac->lock, flags);
	port->num_periods = 0;
	kfree(port->buf);
	port->buf = NULL;
	spin_unlock_irqrestore(&ac->lock, flags);
}

/**
 * q6asm_unmap_memory_regions() - unmap memory regions in the dsp.
 *
 * @dir: direction of audio stream
 * @ac: audio client instanace
 *
 * Return: Will be an negative value on failure or zero on success
 */
int q6asm_unmap_memory_regions(unsigned int dir, struct audio_client *ac)
{
	struct audio_port_data *port;
	int cnt = 0;
	int rc = 0;

	port = &ac->port[dir];
	if (!port->buf) {
		rc = -EINVAL;
		goto err;
	}

	cnt = port->num_periods - 1;
	if (cnt >= 0) {
		rc = __q6asm_memory_unmap(ac, port->buf[dir].phys, dir);
		if (rc < 0) {
			dev_err(ac->dev, "%s: Memory_unmap_regions failed %d\n",
				__func__, rc);
			goto err;
		}
	}

	q6asm_audio_client_free_buf(ac, port);

err:
	return rc;
}
EXPORT_SYMBOL_GPL(q6asm_unmap_memory_regions);

static int __q6asm_memory_map_regions(struct audio_client *ac, int dir,
				      size_t period_sz, unsigned int periods,
				      bool is_contiguous)
{
	struct avs_cmd_shared_mem_map_regions *cmd = NULL;
	struct avs_shared_map_region_payload *mregions = NULL;
	struct q6asm *a = dev_get_drvdata(ac->dev->parent);
	struct audio_port_data *port = NULL;
	struct audio_buffer *ab = NULL;
	struct apr_pkt *pkt;
	void *p;
	unsigned long flags;
	uint32_t num_regions, buf_sz;
	int rc, i, pkt_size;

	if (is_contiguous) {
		num_regions = 1;
		buf_sz = period_sz * periods;
	} else {
		buf_sz = period_sz;
		num_regions = periods;
	}

	/* DSP expects size should be aligned to 4K */
	buf_sz = ALIGN(buf_sz, 4096);

	pkt_size = APR_HDR_SIZE + sizeof(*cmd) +
		   (sizeof(*mregions) * num_regions);

	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	cmd = p + APR_HDR_SIZE;
	mregions = p + APR_HDR_SIZE +  sizeof(*cmd);

	pkt->hdr.hdr_field = APR_SEQ_CMD_HDR_FIELD;
	pkt->hdr.src_port = 0;
	pkt->hdr.dest_port = 0;
	pkt->hdr.pkt_size = pkt_size;
	pkt->hdr.token = ((ac->session << 8) | dir);
	pkt->hdr.opcode = ASM_CMD_SHARED_MEM_MAP_REGIONS;

	cmd->mem_pool_id = ADSP_MEMORY_MAP_SHMEM8_4K_POOL;
	cmd->num_regions = num_regions;
	cmd->property_flag = 0x00;

	spin_lock_irqsave(&ac->lock, flags);
	port = &ac->port[dir];

	for (i = 0; i < num_regions; i++) {
		ab = &port->buf[i];
		mregions->shm_addr_lsw = lower_32_bits(ab->phys);
		mregions->shm_addr_msw = upper_32_bits(ab->phys);
		mregions->mem_size_bytes = buf_sz;
		++mregions;
	}
	spin_unlock_irqrestore(&ac->lock, flags);

	rc = q6asm_apr_send_session_pkt(a, ac, pkt,
					ASM_CMDRSP_SHARED_MEM_MAP_REGIONS);

	kfree(pkt);

	return rc;
}

/**
 * q6asm_map_memory_regions() - map memory regions in the dsp.
 *
 * @dir: direction of audio stream
 * @ac: audio client instanace
 * @phys: physcial address that needs mapping.
 * @period_sz: audio period size
 * @periods: number of periods
 *
 * Return: Will be an negative value on failure or zero on success
 */
int q6asm_map_memory_regions(unsigned int dir, struct audio_client *ac,
			     phys_addr_t phys,
			     size_t period_sz, unsigned int periods)
{
	struct audio_buffer *buf;
	unsigned long flags;
	int cnt;
	int rc;

	spin_lock_irqsave(&ac->lock, flags);
	if (ac->port[dir].buf) {
		dev_err(ac->dev, "Buffer already allocated\n");
		spin_unlock_irqrestore(&ac->lock, flags);
		return 0;
	}

	buf = kzalloc(((sizeof(struct audio_buffer)) * periods), GFP_ATOMIC);
	if (!buf) {
		spin_unlock_irqrestore(&ac->lock, flags);
		return -ENOMEM;
	}


	ac->port[dir].buf = buf;

	buf[0].phys = phys;
	buf[0].size = period_sz;

	for (cnt = 1; cnt < periods; cnt++) {
		if (period_sz > 0) {
			buf[cnt].phys = buf[0].phys + (cnt * period_sz);
			buf[cnt].size = period_sz;
		}
	}
	ac->port[dir].num_periods = periods;

	spin_unlock_irqrestore(&ac->lock, flags);

	rc = __q6asm_memory_map_regions(ac, dir, period_sz, periods, 1);
	if (rc < 0) {
		dev_err(ac->dev, "Memory_map_regions failed\n");
		q6asm_audio_client_free_buf(ac, &ac->port[dir]);
	}

	return rc;
}
EXPORT_SYMBOL_GPL(q6asm_map_memory_regions);

static void q6asm_audio_client_release(struct kref *ref)
{
	struct audio_client *ac;
	struct q6asm *a;
	unsigned long flags;

	ac = container_of(ref, struct audio_client, refcount);
	a = ac->q6asm;

	spin_lock_irqsave(&a->slock, flags);
	a->session[ac->session] = NULL;
	spin_unlock_irqrestore(&a->slock, flags);

	kfree(ac);
}

/**
 * q6asm_audio_client_free() - Freee allocated audio client
 *
 * @ac: audio client to free
 */
void q6asm_audio_client_free(struct audio_client *ac)
{
	kref_put(&ac->refcount, q6asm_audio_client_release);
}
EXPORT_SYMBOL_GPL(q6asm_audio_client_free);

static struct audio_client *q6asm_get_audio_client(struct q6asm *a,
						   int session_id)
{
	struct audio_client *ac = NULL;
	unsigned long flags;

	spin_lock_irqsave(&a->slock, flags);
	if ((session_id <= 0) || (session_id > MAX_SESSIONS)) {
		dev_err(a->dev, "invalid session: %d\n", session_id);
		goto err;
	}

	/* check for valid session */
	if (!a->session[session_id])
		goto err;
	else if (a->session[session_id]->session != session_id)
		goto err;

	ac = a->session[session_id];
	kref_get(&ac->refcount);
err:
	spin_unlock_irqrestore(&a->slock, flags);
	return ac;
}

static int32_t q6asm_stream_callback(struct apr_device *adev,
				     struct apr_resp_pkt *data,
				     int session_id)
{
	struct q6asm *q6asm = dev_get_drvdata(&adev->dev);
	struct aprv2_ibasic_rsp_result_t *result;
	struct apr_hdr *hdr = &data->hdr;
	struct audio_port_data *port;
	struct audio_client *ac;
	uint32_t client_event = 0;
	int ret = 0;

	ac = q6asm_get_audio_client(q6asm, session_id);
	if (!ac)/* Audio client might already be freed by now */
		return 0;

	result = data->payload;

	switch (hdr->opcode) {
	case APR_BASIC_RSP_RESULT:
		switch (result->opcode) {
		case ASM_SESSION_CMD_PAUSE:
			client_event = ASM_CLIENT_EVENT_CMD_PAUSE_DONE;
			break;
		case ASM_SESSION_CMD_SUSPEND:
			client_event = ASM_CLIENT_EVENT_CMD_SUSPEND_DONE;
			break;
		case ASM_DATA_CMD_EOS:
			client_event = ASM_CLIENT_EVENT_CMD_EOS_DONE;
			break;
		case ASM_STREAM_CMD_FLUSH:
			client_event = ASM_CLIENT_EVENT_CMD_FLUSH_DONE;
			break;
		case ASM_SESSION_CMD_RUN_V2:
			client_event = ASM_CLIENT_EVENT_CMD_RUN_DONE;
			break;
		case ASM_STREAM_CMD_CLOSE:
			client_event = ASM_CLIENT_EVENT_CMD_CLOSE_DONE;
			break;
		case ASM_STREAM_CMD_FLUSH_READBUFS:
			client_event = ASM_CLIENT_EVENT_CMD_OUT_FLUSH_DONE;
			break;
		case ASM_STREAM_CMD_OPEN_WRITE_V3:
		case ASM_STREAM_CMD_OPEN_READ_V3:
		case ASM_STREAM_CMD_OPEN_READWRITE_V2:
		case ASM_STREAM_CMD_SET_ENCDEC_PARAM:
		case ASM_DATA_CMD_MEDIA_FMT_UPDATE_V2:
			if (result->status != 0) {
				dev_err(ac->dev,
					"cmd = 0x%x returned error = 0x%x\n",
					result->opcode, result->status);
				ac->result = *result;
				wake_up(&ac->cmd_wait);
				ret = 0;
				goto done;
			}
			break;
		default:
			dev_err(ac->dev, "command[0x%x] not expecting rsp\n",
				result->opcode);
			break;
		}

		ac->result = *result;
		wake_up(&ac->cmd_wait);

		if (ac->cb)
			ac->cb(client_event, hdr->token,
			       data->payload, ac->priv);

		ret = 0;
		goto done;

	case ASM_DATA_EVENT_WRITE_DONE_V2:
		client_event = ASM_CLIENT_EVENT_DATA_WRITE_DONE;
		if (ac->io_mode & ASM_SYNC_IO_MODE) {
			phys_addr_t phys;
			unsigned long flags;

			spin_lock_irqsave(&ac->lock, flags);

			port =  &ac->port[SNDRV_PCM_STREAM_PLAYBACK];

			if (!port->buf) {
				spin_unlock_irqrestore(&ac->lock, flags);
				ret = 0;
				goto done;
			}

			phys = port->buf[hdr->token].phys;

			if (lower_32_bits(phys) != result->opcode ||
			    upper_32_bits(phys) != result->status) {
				dev_err(ac->dev, "Expected addr %pa\n",
					&port->buf[hdr->token].phys);
				spin_unlock_irqrestore(&ac->lock, flags);
				ret = -EINVAL;
				goto done;
			}
			spin_unlock_irqrestore(&ac->lock, flags);
		}
		break;
	case ASM_DATA_EVENT_READ_DONE_V2:
		client_event = ASM_CLIENT_EVENT_DATA_READ_DONE;
		if (ac->io_mode & ASM_SYNC_IO_MODE) {
			struct asm_data_cmd_read_v2_done *done = data->payload;
			unsigned long flags;
			phys_addr_t phys;

			spin_lock_irqsave(&ac->lock, flags);
			port =  &ac->port[SNDRV_PCM_STREAM_CAPTURE];
			if (!port->buf) {
				spin_unlock_irqrestore(&ac->lock, flags);
				ret = 0;
				goto done;
			}

			phys = port->buf[hdr->token].phys;

			if (upper_32_bits(phys) != done->buf_addr_msw ||
			    lower_32_bits(phys) != done->buf_addr_lsw) {
				dev_err(ac->dev, "Expected addr %pa %08x-%08x\n",
					&port->buf[hdr->token].phys,
					done->buf_addr_lsw,
					done->buf_addr_msw);
				spin_unlock_irqrestore(&ac->lock, flags);
				ret = -EINVAL;
				goto done;
			}
			spin_unlock_irqrestore(&ac->lock, flags);
		}

		break;
	}

	if (ac->cb)
		ac->cb(client_event, hdr->token, data->payload, ac->priv);

done:
	kref_put(&ac->refcount, q6asm_audio_client_release);
	return ret;
}

static int q6asm_srvc_callback(struct apr_device *adev,
			       struct apr_resp_pkt *data)
{
	struct q6asm *q6asm = dev_get_drvdata(&adev->dev);
	struct aprv2_ibasic_rsp_result_t *result;
	struct audio_port_data *port;
	struct audio_client *ac = NULL;
	struct apr_hdr *hdr = &data->hdr;
	struct q6asm *a;
	uint32_t sid = 0;
	uint32_t dir = 0;
	int session_id;

	session_id = (hdr->dest_port >> 8) & 0xFF;
	if (session_id)
		return q6asm_stream_callback(adev, data, session_id);

	sid = (hdr->token >> 8) & 0x0F;
	ac = q6asm_get_audio_client(q6asm, sid);
	if (!ac) {
		dev_err(&adev->dev, "Audio Client not active\n");
		return 0;
	}

	a = dev_get_drvdata(ac->dev->parent);
	dir = (hdr->token & 0x0F);
	port = &ac->port[dir];
	result = data->payload;

	switch (hdr->opcode) {
	case APR_BASIC_RSP_RESULT:
		switch (result->opcode) {
		case ASM_CMD_SHARED_MEM_MAP_REGIONS:
		case ASM_CMD_SHARED_MEM_UNMAP_REGIONS:
			ac->result = *result;
			wake_up(&a->mem_wait);
			break;
		default:
			dev_err(&adev->dev, "command[0x%x] not expecting rsp\n",
				 result->opcode);
			break;
		}
		goto done;
	case ASM_CMDRSP_SHARED_MEM_MAP_REGIONS:
		ac->result.status = 0;
		ac->result.opcode = hdr->opcode;
		port->mem_map_handle = result->opcode;
		wake_up(&a->mem_wait);
		break;
	case ASM_CMD_SHARED_MEM_UNMAP_REGIONS:
		ac->result.opcode = hdr->opcode;
		ac->result.status = 0;
		port->mem_map_handle = 0;
		wake_up(&a->mem_wait);
		break;
	default:
		dev_dbg(&adev->dev, "command[0x%x]success [0x%x]\n",
			result->opcode, result->status);
		break;
	}

	if (ac->cb)
		ac->cb(hdr->opcode, hdr->token, data->payload, ac->priv);

done:
	kref_put(&ac->refcount, q6asm_audio_client_release);

	return 0;
}

/**
 * q6asm_get_session_id() - get session id for audio client
 *
 * @c: audio client pointer
 *
 * Return: Will be an session id of the audio client.
 */
int q6asm_get_session_id(struct audio_client *c)
{
	return c->session;
}
EXPORT_SYMBOL_GPL(q6asm_get_session_id);

/**
 * q6asm_audio_client_alloc() - Allocate a new audio client
 *
 * @dev: Pointer to asm child device.
 * @cb: event callback.
 * @priv: private data associated with this client.
 * @stream_id: stream id
 * @perf_mode: performace mode for this client
 *
 * Return: Will be an error pointer on error or a valid audio client
 * on success.
 */
struct audio_client *q6asm_audio_client_alloc(struct device *dev, q6asm_cb cb,
					      void *priv, int stream_id,
					      int perf_mode)
{
	struct q6asm *a = dev_get_drvdata(dev->parent);
	struct audio_client *ac;
	unsigned long flags;

	ac = q6asm_get_audio_client(a, stream_id + 1);
	if (ac) {
		dev_err(dev, "Audio Client already active\n");
		return ac;
	}

	ac = kzalloc(sizeof(*ac), GFP_KERNEL);
	if (!ac)
		return ERR_PTR(-ENOMEM);

	spin_lock_irqsave(&a->slock, flags);
	a->session[stream_id + 1] = ac;
	spin_unlock_irqrestore(&a->slock, flags);
	ac->session = stream_id + 1;
	ac->cb = cb;
	ac->dev = dev;
	ac->q6asm = a;
	ac->priv = priv;
	ac->io_mode = ASM_SYNC_IO_MODE;
	ac->perf_mode = perf_mode;
	/* DSP expects stream id from 1 */
	ac->stream_id = 1;
	ac->adev = a->adev;
	kref_init(&ac->refcount);

	init_waitqueue_head(&ac->cmd_wait);
	mutex_init(&ac->cmd_lock);
	spin_lock_init(&ac->lock);

	return ac;
}
EXPORT_SYMBOL_GPL(q6asm_audio_client_alloc);

static int q6asm_ac_send_cmd_sync(struct audio_client *ac, struct apr_pkt *pkt)
{
	struct apr_hdr *hdr = &pkt->hdr;
	int rc;

	mutex_lock(&ac->cmd_lock);
	ac->result.opcode = 0;
	ac->result.status = 0;

	rc = apr_send_pkt(ac->adev, pkt);
	if (rc < 0)
		goto err;

	rc = wait_event_timeout(ac->cmd_wait,
				(ac->result.opcode == hdr->opcode), 5 * HZ);
	if (!rc) {
		dev_err(ac->dev, "CMD timeout\n");
		rc =  -ETIMEDOUT;
		goto err;
	}

	if (ac->result.status > 0) {
		dev_err(ac->dev, "DSP returned error[%x]\n",
			ac->result.status);
		rc = -EINVAL;
	} else {
		rc = 0;
	}


err:
	mutex_unlock(&ac->cmd_lock);
	return rc;
}

/**
 * q6asm_open_write() - Open audio client for writing
 *
 * @ac: audio client pointer
 * @format: audio sample format
 * @bits_per_sample: bits per sample
 *
 * Return: Will be an negative value on error or zero on success
 */
int q6asm_open_write(struct audio_client *ac, uint32_t format,
		     u32 codec_profile, uint16_t bits_per_sample)
{
	struct asm_stream_cmd_open_write_v3 *open;
	struct apr_pkt *pkt;
	void *p;
	int rc, pkt_size;

	pkt_size = APR_HDR_SIZE + sizeof(*open);

	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	open = p + APR_HDR_SIZE;
	q6asm_add_hdr(ac, &pkt->hdr, pkt_size, true, ac->stream_id);

	pkt->hdr.opcode = ASM_STREAM_CMD_OPEN_WRITE_V3;
	open->mode_flags = 0x00;
	open->mode_flags |= ASM_LEGACY_STREAM_SESSION;

	/* source endpoint : matrix */
	open->sink_endpointype = ASM_END_POINT_DEVICE_MATRIX;
	open->bits_per_sample = bits_per_sample;
	open->postprocopo_id = ASM_NULL_POPP_TOPOLOGY;

	switch (format) {
	case SND_AUDIOCODEC_MP3:
		open->dec_fmt_id = ASM_MEDIA_FMT_MP3;
		break;
	case FORMAT_LINEAR_PCM:
		open->dec_fmt_id = ASM_MEDIA_FMT_MULTI_CHANNEL_PCM_V2;
		break;
	case SND_AUDIOCODEC_FLAC:
		open->dec_fmt_id = ASM_MEDIA_FMT_FLAC;
		break;
	case SND_AUDIOCODEC_WMA:
		switch (codec_profile) {
		case SND_AUDIOPROFILE_WMA9:
			open->dec_fmt_id = ASM_MEDIA_FMT_WMA_V9;
			break;
		case SND_AUDIOPROFILE_WMA10:
		case SND_AUDIOPROFILE_WMA9_PRO:
		case SND_AUDIOPROFILE_WMA9_LOSSLESS:
		case SND_AUDIOPROFILE_WMA10_LOSSLESS:
			open->dec_fmt_id = ASM_MEDIA_FMT_WMA_V10;
			break;
		default:
			dev_err(ac->dev, "Invalid codec profile 0x%x\n",
				codec_profile);
			rc = -EINVAL;
			goto err;
		}
		break;
	case SND_AUDIOCODEC_ALAC:
		open->dec_fmt_id = ASM_MEDIA_FMT_ALAC;
		break;
	case SND_AUDIOCODEC_APE:
		open->dec_fmt_id = ASM_MEDIA_FMT_APE;
		break;
	default:
		dev_err(ac->dev, "Invalid format 0x%x\n", format);
		rc = -EINVAL;
		goto err;
	}

	rc = q6asm_ac_send_cmd_sync(ac, pkt);
	if (rc < 0)
		goto err;

	ac->io_mode |= ASM_TUN_WRITE_IO_MODE;

err:
	kfree(pkt);
	return rc;
}
EXPORT_SYMBOL_GPL(q6asm_open_write);

static int __q6asm_run(struct audio_client *ac, uint32_t flags,
	      uint32_t msw_ts, uint32_t lsw_ts, bool wait)
{
	struct asm_session_cmd_run_v2 *run;
	struct apr_pkt *pkt;
	int pkt_size, rc;
	void *p;

	pkt_size = APR_HDR_SIZE + sizeof(*run);
	p = kzalloc(pkt_size, GFP_ATOMIC);
	if (!p)
		return -ENOMEM;

	pkt = p;
	run = p + APR_HDR_SIZE;

	q6asm_add_hdr(ac, &pkt->hdr, pkt_size, true, ac->stream_id);

	pkt->hdr.opcode = ASM_SESSION_CMD_RUN_V2;
	run->flags = flags;
	run->time_lsw = lsw_ts;
	run->time_msw = msw_ts;
	if (wait) {
		rc = q6asm_ac_send_cmd_sync(ac, pkt);
	} else {
		rc = apr_send_pkt(ac->adev, pkt);
		if (rc == pkt_size)
			rc = 0;
	}

	kfree(pkt);
	return rc;
}

/**
 * q6asm_run() - start the audio client
 *
 * @ac: audio client pointer
 * @flags: flags associated with write
 * @msw_ts: timestamp msw
 * @lsw_ts: timestamp lsw
 *
 * Return: Will be an negative value on error or zero on success
 */
int q6asm_run(struct audio_client *ac, uint32_t flags,
	      uint32_t msw_ts, uint32_t lsw_ts)
{
	return __q6asm_run(ac, flags, msw_ts, lsw_ts, true);
}
EXPORT_SYMBOL_GPL(q6asm_run);

/**
 * q6asm_run_nowait() - start the audio client withou blocking
 *
 * @ac: audio client pointer
 * @flags: flags associated with write
 * @msw_ts: timestamp msw
 * @lsw_ts: timestamp lsw
 *
 * Return: Will be an negative value on error or zero on success
 */
int q6asm_run_nowait(struct audio_client *ac, uint32_t flags,
	      uint32_t msw_ts, uint32_t lsw_ts)
{
	return __q6asm_run(ac, flags, msw_ts, lsw_ts, false);
}
EXPORT_SYMBOL_GPL(q6asm_run_nowait);

/**
 * q6asm_media_format_block_multi_ch_pcm() - setup pcm configuration
 *
 * @ac: audio client pointer
 * @rate: audio sample rate
 * @channels: number of audio channels.
 * @channel_map: channel map pointer
 * @bits_per_sample: bits per sample
 *
 * Return: Will be an negative value on error or zero on success
 */
int q6asm_media_format_block_multi_ch_pcm(struct audio_client *ac,
					  uint32_t rate, uint32_t channels,
					  u8 channel_map[PCM_MAX_NUM_CHANNEL],
					  uint16_t bits_per_sample)
{
	struct asm_multi_channel_pcm_fmt_blk_v2 *fmt;
	struct apr_pkt *pkt;
	u8 *channel_mapping;
	void *p;
	int rc, pkt_size;

	pkt_size = APR_HDR_SIZE + sizeof(*fmt);
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	fmt = p + APR_HDR_SIZE;

	q6asm_add_hdr(ac, &pkt->hdr, pkt_size, true, ac->stream_id);

	pkt->hdr.opcode = ASM_DATA_CMD_MEDIA_FMT_UPDATE_V2;
	fmt->fmt_blk.fmt_blk_size = sizeof(*fmt) - sizeof(fmt->fmt_blk);
	fmt->num_channels = channels;
	fmt->bits_per_sample = bits_per_sample;
	fmt->sample_rate = rate;
	fmt->is_signed = 1;

	channel_mapping = fmt->channel_mapping;

	if (channel_map) {
		memcpy(channel_mapping, channel_map, PCM_MAX_NUM_CHANNEL);
	} else {
		if (q6dsp_map_channels(channel_mapping, channels)) {
			dev_err(ac->dev, " map channels failed %d\n", channels);
			rc = -EINVAL;
			goto err;
		}
	}

	rc = q6asm_ac_send_cmd_sync(ac, pkt);

err:
	kfree(pkt);
	return rc;
}
EXPORT_SYMBOL_GPL(q6asm_media_format_block_multi_ch_pcm);


int q6asm_stream_media_format_block_flac(struct audio_client *ac,
					 struct q6asm_flac_cfg *cfg)
{
	struct asm_flac_fmt_blk_v2 *fmt;
	struct apr_pkt *pkt;
	void *p;
	int rc, pkt_size;

	pkt_size = APR_HDR_SIZE + sizeof(*fmt);
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	fmt = p + APR_HDR_SIZE;

	q6asm_add_hdr(ac, &pkt->hdr, pkt_size, true, ac->stream_id);

	pkt->hdr.opcode = ASM_DATA_CMD_MEDIA_FMT_UPDATE_V2;
	fmt->fmt_blk.fmt_blk_size = sizeof(*fmt) - sizeof(fmt->fmt_blk);
	fmt->is_stream_info_present = cfg->stream_info_present;
	fmt->num_channels = cfg->ch_cfg;
	fmt->min_blk_size = cfg->min_blk_size;
	fmt->max_blk_size = cfg->max_blk_size;
	fmt->sample_rate = cfg->sample_rate;
	fmt->min_frame_size = cfg->min_frame_size;
	fmt->max_frame_size = cfg->max_frame_size;
	fmt->sample_size = cfg->sample_size;

	rc = q6asm_ac_send_cmd_sync(ac, pkt);
	kfree(pkt);

	return rc;
}
EXPORT_SYMBOL_GPL(q6asm_stream_media_format_block_flac);

int q6asm_stream_media_format_block_wma_v9(struct audio_client *ac,
					   struct q6asm_wma_cfg *cfg)
{
	struct asm_wmastdv9_fmt_blk_v2 *fmt;
	struct apr_pkt *pkt;
	void *p;
	int rc, pkt_size;

	pkt_size = APR_HDR_SIZE + sizeof(*fmt);
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	fmt = p + APR_HDR_SIZE;

	q6asm_add_hdr(ac, &pkt->hdr, pkt_size, true, ac->stream_id);

	pkt->hdr.opcode = ASM_DATA_CMD_MEDIA_FMT_UPDATE_V2;
	fmt->fmt_blk.fmt_blk_size = sizeof(*fmt) - sizeof(fmt->fmt_blk);
	fmt->fmtag = cfg->fmtag;
	fmt->num_channels = cfg->num_channels;
	fmt->sample_rate = cfg->sample_rate;
	fmt->bytes_per_sec = cfg->bytes_per_sec;
	fmt->blk_align = cfg->block_align;
	fmt->bits_per_sample = cfg->bits_per_sample;
	fmt->channel_mask = cfg->channel_mask;
	fmt->enc_options = cfg->enc_options;
	fmt->reserved = 0;

	rc = q6asm_ac_send_cmd_sync(ac, pkt);
	kfree(pkt);

	return rc;
}
EXPORT_SYMBOL_GPL(q6asm_stream_media_format_block_wma_v9);

int q6asm_stream_media_format_block_wma_v10(struct audio_client *ac,
					    struct q6asm_wma_cfg *cfg)
{
	struct asm_wmaprov10_fmt_blk_v2 *fmt;
	struct apr_pkt *pkt;
	void *p;
	int rc, pkt_size;

	pkt_size = APR_HDR_SIZE + sizeof(*fmt);
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	fmt = p + APR_HDR_SIZE;

	q6asm_add_hdr(ac, &pkt->hdr, pkt_size, true, ac->stream_id);

	pkt->hdr.opcode = ASM_DATA_CMD_MEDIA_FMT_UPDATE_V2;
	fmt->fmt_blk.fmt_blk_size = sizeof(*fmt) - sizeof(fmt->fmt_blk);
	fmt->fmtag = cfg->fmtag;
	fmt->num_channels = cfg->num_channels;
	fmt->sample_rate = cfg->sample_rate;
	fmt->bytes_per_sec = cfg->bytes_per_sec;
	fmt->blk_align = cfg->block_align;
	fmt->bits_per_sample = cfg->bits_per_sample;
	fmt->channel_mask = cfg->channel_mask;
	fmt->enc_options = cfg->enc_options;
	fmt->advanced_enc_options1 = cfg->adv_enc_options;
	fmt->advanced_enc_options2 = cfg->adv_enc_options2;

	rc = q6asm_ac_send_cmd_sync(ac, pkt);
	kfree(pkt);

	return rc;
}
EXPORT_SYMBOL_GPL(q6asm_stream_media_format_block_wma_v10);

int q6asm_stream_media_format_block_alac(struct audio_client *ac,
					 struct q6asm_alac_cfg *cfg)
{
	struct asm_alac_fmt_blk_v2 *fmt;
	struct apr_pkt *pkt;
	void *p;
	int rc, pkt_size;

	pkt_size = APR_HDR_SIZE + sizeof(*fmt);
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	fmt = p + APR_HDR_SIZE;

	q6asm_add_hdr(ac, &pkt->hdr, pkt_size, true, ac->stream_id);

	pkt->hdr.opcode = ASM_DATA_CMD_MEDIA_FMT_UPDATE_V2;
	fmt->fmt_blk.fmt_blk_size = sizeof(*fmt) - sizeof(fmt->fmt_blk);

	fmt->frame_length = cfg->frame_length;
	fmt->compatible_version = cfg->compatible_version;
	fmt->bit_depth =  cfg->bit_depth;
	fmt->num_channels = cfg->num_channels;
	fmt->max_run = cfg->max_run;
	fmt->max_frame_bytes = cfg->max_frame_bytes;
	fmt->avg_bit_rate = cfg->avg_bit_rate;
	fmt->sample_rate = cfg->sample_rate;
	fmt->channel_layout_tag = cfg->channel_layout_tag;
	fmt->pb = cfg->pb;
	fmt->mb = cfg->mb;
	fmt->kb = cfg->kb;

	rc = q6asm_ac_send_cmd_sync(ac, pkt);
	kfree(pkt);

	return rc;
}
EXPORT_SYMBOL_GPL(q6asm_stream_media_format_block_alac);

int q6asm_stream_media_format_block_ape(struct audio_client *ac,
					struct q6asm_ape_cfg *cfg)
{
	struct asm_ape_fmt_blk_v2 *fmt;
	struct apr_pkt *pkt;
	void *p;
	int rc, pkt_size;

	pkt_size = APR_HDR_SIZE + sizeof(*fmt);
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	fmt = p + APR_HDR_SIZE;

	q6asm_add_hdr(ac, &pkt->hdr, pkt_size, true, ac->stream_id);

	pkt->hdr.opcode = ASM_DATA_CMD_MEDIA_FMT_UPDATE_V2;
	fmt->fmt_blk.fmt_blk_size = sizeof(*fmt) - sizeof(fmt->fmt_blk);

	fmt->compatible_version = cfg->compatible_version;
	fmt->compression_level = cfg->compression_level;
	fmt->format_flags = cfg->format_flags;
	fmt->blocks_per_frame = cfg->blocks_per_frame;
	fmt->final_frame_blocks = cfg->final_frame_blocks;
	fmt->total_frames = cfg->total_frames;
	fmt->bits_per_sample = cfg->bits_per_sample;
	fmt->num_channels = cfg->num_channels;
	fmt->sample_rate = cfg->sample_rate;
	fmt->seek_table_present = cfg->seek_table_present;

	rc = q6asm_ac_send_cmd_sync(ac, pkt);
	kfree(pkt);

	return rc;
}
EXPORT_SYMBOL_GPL(q6asm_stream_media_format_block_ape);

/**
 * q6asm_enc_cfg_blk_pcm_format_support() - setup pcm configuration for capture
 *
 * @ac: audio client pointer
 * @rate: audio sample rate
 * @channels: number of audio channels.
 * @bits_per_sample: bits per sample
 *
 * Return: Will be an negative value on error or zero on success
 */
int q6asm_enc_cfg_blk_pcm_format_support(struct audio_client *ac,
		uint32_t rate, uint32_t channels, uint16_t bits_per_sample)
{
	struct asm_multi_channel_pcm_enc_cfg_v2  *enc_cfg;
	struct apr_pkt *pkt;
	u8 *channel_mapping;
	u32 frames_per_buf = 0;
	int pkt_size, rc;
	void *p;

	pkt_size = APR_HDR_SIZE + sizeof(*enc_cfg);
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	enc_cfg = p + APR_HDR_SIZE;
	q6asm_add_hdr(ac, &pkt->hdr, pkt_size, true, ac->stream_id);

	pkt->hdr.opcode = ASM_STREAM_CMD_SET_ENCDEC_PARAM;
	enc_cfg->encdec.param_id = ASM_PARAM_ID_ENCDEC_ENC_CFG_BLK_V2;
	enc_cfg->encdec.param_size = sizeof(*enc_cfg) - sizeof(enc_cfg->encdec);
	enc_cfg->encblk.frames_per_buf = frames_per_buf;
	enc_cfg->encblk.enc_cfg_blk_size  = enc_cfg->encdec.param_size -
					sizeof(struct asm_enc_cfg_blk_param_v2);

	enc_cfg->num_channels = channels;
	enc_cfg->bits_per_sample = bits_per_sample;
	enc_cfg->sample_rate = rate;
	enc_cfg->is_signed = 1;
	channel_mapping = enc_cfg->channel_mapping;

	if (q6dsp_map_channels(channel_mapping, channels)) {
		rc = -EINVAL;
		goto err;
	}

	rc = q6asm_ac_send_cmd_sync(ac, pkt);
err:
	kfree(pkt);
	return rc;
}
EXPORT_SYMBOL_GPL(q6asm_enc_cfg_blk_pcm_format_support);


/**
 * q6asm_read() - read data of period size from audio client
 *
 * @ac: audio client pointer
 *
 * Return: Will be an negative value on error or zero on success
 */
int q6asm_read(struct audio_client *ac)
{
	struct asm_data_cmd_read_v2 *read;
	struct audio_port_data *port;
	struct audio_buffer *ab;
	struct apr_pkt *pkt;
	unsigned long flags;
	int pkt_size;
	int rc = 0;
	void *p;

	pkt_size = APR_HDR_SIZE + sizeof(*read);
	p = kzalloc(pkt_size, GFP_ATOMIC);
	if (!p)
		return -ENOMEM;

	pkt = p;
	read = p + APR_HDR_SIZE;

	spin_lock_irqsave(&ac->lock, flags);
	port = &ac->port[SNDRV_PCM_STREAM_CAPTURE];
	q6asm_add_hdr(ac, &pkt->hdr, pkt_size, false, ac->stream_id);
	ab = &port->buf[port->dsp_buf];
	pkt->hdr.opcode = ASM_DATA_CMD_READ_V2;
	read->buf_addr_lsw = lower_32_bits(ab->phys);
	read->buf_addr_msw = upper_32_bits(ab->phys);
	read->mem_map_handle = port->mem_map_handle;

	read->buf_size = ab->size;
	read->seq_id = port->dsp_buf;
	pkt->hdr.token = port->dsp_buf;

	port->dsp_buf++;

	if (port->dsp_buf >= port->num_periods)
		port->dsp_buf = 0;

	spin_unlock_irqrestore(&ac->lock, flags);
	rc = apr_send_pkt(ac->adev, pkt);
	if (rc == pkt_size)
		rc = 0;
	else
		pr_err("read op[0x%x]rc[%d]\n", pkt->hdr.opcode, rc);

	kfree(pkt);
	return rc;
}
EXPORT_SYMBOL_GPL(q6asm_read);

static int __q6asm_open_read(struct audio_client *ac,
		uint32_t format, uint16_t bits_per_sample)
{
	struct asm_stream_cmd_open_read_v3 *open;
	struct apr_pkt *pkt;
	int pkt_size, rc;
	void *p;

	pkt_size = APR_HDR_SIZE + sizeof(*open);
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	open = p + APR_HDR_SIZE;

	q6asm_add_hdr(ac, &pkt->hdr,  pkt_size, true, ac->stream_id);
	pkt->hdr.opcode = ASM_STREAM_CMD_OPEN_READ_V3;
	/* Stream prio : High, provide meta info with encoded frames */
	open->src_endpointype = ASM_END_POINT_DEVICE_MATRIX;

	open->preprocopo_id = ASM_STREAM_POSTPROC_TOPO_ID_NONE;
	open->bits_per_sample = bits_per_sample;
	open->mode_flags = 0x0;

	open->mode_flags |= ASM_LEGACY_STREAM_SESSION <<
				ASM_SHIFT_STREAM_PERF_MODE_FLAG_IN_OPEN_READ;

	switch (format) {
	case FORMAT_LINEAR_PCM:
		open->mode_flags |= 0x00;
		open->enc_cfg_id = ASM_MEDIA_FMT_MULTI_CHANNEL_PCM_V2;
		break;
	default:
		pr_err("Invalid format[%d]\n", format);
	}

	rc = q6asm_ac_send_cmd_sync(ac, pkt);

	kfree(pkt);
	return rc;
}

/**
 * q6asm_open_read() - Open audio client for reading
 *
 * @ac: audio client pointer
 * @format: audio sample format
 * @bits_per_sample: bits per sample
 *
 * Return: Will be an negative value on error or zero on success
 */
int q6asm_open_read(struct audio_client *ac, uint32_t format,
			uint16_t bits_per_sample)
{
	return __q6asm_open_read(ac, format, bits_per_sample);
}
EXPORT_SYMBOL_GPL(q6asm_open_read);

/**
 * q6asm_write_async() - non blocking write
 *
 * @ac: audio client pointer
 * @len: length in bytes
 * @msw_ts: timestamp msw
 * @lsw_ts: timestamp lsw
 * @wflags: flags associated with write
 *
 * Return: Will be an negative value on error or zero on success
 */
int q6asm_write_async(struct audio_client *ac, uint32_t len, uint32_t msw_ts,
		       uint32_t lsw_ts, uint32_t wflags)
{
	struct asm_data_cmd_write_v2 *write;
	struct audio_port_data *port;
	struct audio_buffer *ab;
	unsigned long flags;
	struct apr_pkt *pkt;
	int pkt_size;
	int rc = 0;
	void *p;

	pkt_size = APR_HDR_SIZE + sizeof(*write);
	p = kzalloc(pkt_size, GFP_ATOMIC);
	if (!p)
		return -ENOMEM;

	pkt = p;
	write = p + APR_HDR_SIZE;

	spin_lock_irqsave(&ac->lock, flags);
	port = &ac->port[SNDRV_PCM_STREAM_PLAYBACK];
	q6asm_add_hdr(ac, &pkt->hdr, pkt_size, false, ac->stream_id);

	ab = &port->buf[port->dsp_buf];
	pkt->hdr.token = port->dsp_buf;
	pkt->hdr.opcode = ASM_DATA_CMD_WRITE_V2;
	write->buf_addr_lsw = lower_32_bits(ab->phys);
	write->buf_addr_msw = upper_32_bits(ab->phys);
	write->buf_size = len;
	write->seq_id = port->dsp_buf;
	write->timestamp_lsw = lsw_ts;
	write->timestamp_msw = msw_ts;
	write->mem_map_handle =
	    ac->port[SNDRV_PCM_STREAM_PLAYBACK].mem_map_handle;

	if (wflags == NO_TIMESTAMP)
		write->flags = (wflags & 0x800000FF);
	else
		write->flags = (0x80000000 | wflags);

	port->dsp_buf++;

	if (port->dsp_buf >= port->num_periods)
		port->dsp_buf = 0;

	spin_unlock_irqrestore(&ac->lock, flags);
	rc = apr_send_pkt(ac->adev, pkt);
	if (rc == pkt_size)
		rc = 0;

	kfree(pkt);
	return rc;
}
EXPORT_SYMBOL_GPL(q6asm_write_async);

static void q6asm_reset_buf_state(struct audio_client *ac)
{
	struct audio_port_data *port = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ac->lock, flags);
	port = &ac->port[SNDRV_PCM_STREAM_PLAYBACK];
	port->dsp_buf = 0;
	port = &ac->port[SNDRV_PCM_STREAM_CAPTURE];
	port->dsp_buf = 0;
	spin_unlock_irqrestore(&ac->lock, flags);
}

static int __q6asm_cmd(struct audio_client *ac, int cmd, bool wait)
{
	int stream_id = ac->stream_id;
	struct apr_pkt pkt;
	int rc;

	q6asm_add_hdr(ac, &pkt.hdr, APR_HDR_SIZE, true, stream_id);

	switch (cmd) {
	case CMD_PAUSE:
		pkt.hdr.opcode = ASM_SESSION_CMD_PAUSE;
		break;
	case CMD_SUSPEND:
		pkt.hdr.opcode = ASM_SESSION_CMD_SUSPEND;
		break;
	case CMD_FLUSH:
		pkt.hdr.opcode = ASM_STREAM_CMD_FLUSH;
		break;
	case CMD_OUT_FLUSH:
		pkt.hdr.opcode = ASM_STREAM_CMD_FLUSH_READBUFS;
		break;
	case CMD_EOS:
		pkt.hdr.opcode = ASM_DATA_CMD_EOS;
		break;
	case CMD_CLOSE:
		pkt.hdr.opcode = ASM_STREAM_CMD_CLOSE;
		break;
	default:
		return -EINVAL;
	}

	if (wait)
		rc = q6asm_ac_send_cmd_sync(ac, &pkt);
	else
		return apr_send_pkt(ac->adev, &pkt);

	if (rc < 0)
		return rc;

	if (cmd == CMD_FLUSH)
		q6asm_reset_buf_state(ac);

	return 0;
}

/**
 * q6asm_cmd() - run cmd on audio client
 *
 * @ac: audio client pointer
 * @cmd: command to run on audio client.
 *
 * Return: Will be an negative value on error or zero on success
 */
int q6asm_cmd(struct audio_client *ac, int cmd)
{
	return __q6asm_cmd(ac, cmd, true);
}
EXPORT_SYMBOL_GPL(q6asm_cmd);

/**
 * q6asm_cmd_nowait() - non blocking, run cmd on audio client
 *
 * @ac: audio client pointer
 * @cmd: command to run on audio client.
 *
 * Return: Will be an negative value on error or zero on success
 */
int q6asm_cmd_nowait(struct audio_client *ac, int cmd)
{
	return __q6asm_cmd(ac, cmd, false);
}
EXPORT_SYMBOL_GPL(q6asm_cmd_nowait);

static int q6asm_probe(struct apr_device *adev)
{
	struct device *dev = &adev->dev;
	struct q6asm *q6asm;

	q6asm = devm_kzalloc(dev, sizeof(*q6asm), GFP_KERNEL);
	if (!q6asm)
		return -ENOMEM;

	q6core_get_svc_api_info(adev->svc_id, &q6asm->ainfo);

	q6asm->dev = dev;
	q6asm->adev = adev;
	init_waitqueue_head(&q6asm->mem_wait);
	spin_lock_init(&q6asm->slock);
	dev_set_drvdata(dev, q6asm);

	return of_platform_populate(dev->of_node, NULL, NULL, dev);
}

static int q6asm_remove(struct apr_device *adev)
{
	of_platform_depopulate(&adev->dev);

	return 0;
}
static const struct of_device_id q6asm_device_id[]  = {
	{ .compatible = "qcom,q6asm" },
	{},
};
MODULE_DEVICE_TABLE(of, q6asm_device_id);

static struct apr_driver qcom_q6asm_driver = {
	.probe = q6asm_probe,
	.remove = q6asm_remove,
	.callback = q6asm_srvc_callback,
	.driver = {
		.name = "qcom-q6asm",
		.of_match_table = of_match_ptr(q6asm_device_id),
	},
};

module_apr_driver(qcom_q6asm_driver);
MODULE_DESCRIPTION("Q6 Audio Stream Manager driver");
MODULE_LICENSE("GPL v2");
