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

#define ASM_SYNC_IO_MODE		0x0001
#define ASM_ASYNC_IO_MODE		0x0002
#define ASM_TUN_READ_IO_MODE		0x0004	/* tunnel read write mode */
#define ASM_TUN_WRITE_IO_MODE		0x0008	/* tunnel read write mode */

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
	wait_queue_head_t cmd_wait;
	struct aprv2_ibasic_rsp_result_t result;
	int perf_mode;
	int stream_id;
	struct q6asm *q6asm;
	struct device *dev;
};

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
	struct audio_client *ac = NULL;
	struct apr_hdr *hdr = &data->hdr;
	uint32_t sid = 0;

	sid = (hdr->token >> 8) & 0x0F;
	ac = q6asm_get_audio_client(q6asm, sid);
	if (!ac) {
		dev_err(&adev->dev, "Audio Client not active\n");
		return 0;
	}

	if (ac->cb)
		ac->cb(hdr->opcode, hdr->token, data->payload, ac->priv);

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
