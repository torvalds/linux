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
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include "q6asm.h"
#include "q6core.h"
#include "q6dsp-errno.h"
#include "q6dsp-common.h"

#define ASM_CMD_SHARED_MEM_MAP_REGIONS		0x00010D92
#define ASM_CMDRSP_SHARED_MEM_MAP_REGIONS	0x00010D93
#define ASM_CMD_SHARED_MEM_UNMAP_REGIONS	0x00010D94

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
	struct platform_device *pcmdev;
	spinlock_t slock;
	struct audio_client *session[MAX_SESSIONS + 1];
	struct platform_device *pdev_dais;
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


static int q6asm_probe(struct apr_device *adev)
{
	struct device *dev = &adev->dev;
	struct device_node *dais_np;
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

	dais_np = of_get_child_by_name(dev->of_node, "dais");
	if (dais_np) {
		q6asm->pdev_dais = of_platform_device_create(dais_np,
							   "q6asm-dai", dev);
		of_node_put(dais_np);
	}

	return 0;
}

static int q6asm_remove(struct apr_device *adev)
{
	struct q6asm *q6asm = dev_get_drvdata(&adev->dev);

	if (q6asm->pdev_dais)
		of_platform_device_destroy(&q6asm->pdev_dais->dev, NULL);

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
