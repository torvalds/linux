// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Author: Libin Yang <libin.yang@intel.com>
 *         Luo Xionghu <xionghu.luo@intel.com>
 *         Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/hw_random.h>
#include <linux/uio.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/device.h>
#include <sound/sof.h>
#include <sound/pcm_params.h>
#include <uapi/sound/sof-ipc.h>
#include <linux/vbs/vq.h>
#include <linux/vbs/vbs.h>
#include <linux/vhm/acrn_common.h>
#include <linux/vhm/acrn_vhm_ioreq.h>
#include <linux/vhm/acrn_vhm_mm.h>
#include <linux/vhm/vhm_vm_mngt.h>
#include "sof-priv.h"
#include "ops.h"
#include "virtio-be.h"
#include "virtio-miscdev.h"

#define IGS(x) (((x) >> SOF_GLB_TYPE_SHIFT) & 0xf)
#define ICS(x) (((x) >> SOF_CMD_TYPE_SHIFT) & 0xfff)

/* find client from client ID */
static struct sof_vbe_client *vbe_client_find(struct snd_sof_dev *sdev,
					      int client_id)
{
	struct sof_vbe_client *client;
	struct sof_vbe *vbe;

	list_for_each_entry(vbe, &sdev->vbe_list, list) {
		list_for_each_entry(client, &vbe->client_list, list) {
			if (client_id == client->vhm_client_id)
				return client;
		}
	}

	return NULL;
}

static struct sof_vbe *sbe_comp_id_to_vbe(struct snd_sof_dev *sdev, int comp_id)
{
	struct sof_vbe *vbe;

	list_for_each_entry(vbe, &sdev->vbe_list, list) {
		if (comp_id < vbe->comp_id_end && comp_id >= vbe->comp_id_begin)
			return vbe;
	}
	return NULL;
}

static int sof_virtio_send_ipc(struct snd_sof_dev *sdev, void *ipc_data,
			       void *reply_data, size_t count,
			       size_t reply_size)
{
	struct snd_sof_ipc *ipc = sdev->ipc;
	struct sof_ipc_hdr *hdr = (struct sof_ipc_hdr *)ipc_data;

	return sof_ipc_tx_message(ipc, hdr->cmd, ipc_data, count,
				  reply_data, reply_size);
}

/*
 * This function will be called when there is a poistion update requirement
 * from vBE. Return true if posn buffer is filled successfully
 */
static bool sbe_fill_posn_vqbuf(struct sof_vbe *vbe,
				struct virtio_vq_info *vq,
				struct sof_ipc_stream_posn *posn,
				bool *endchain)
{
	struct device *dev = vbe->sdev->dev;
	struct iovec iov;
	u16 idx;
	int ret;

	*endchain = false;

	/*
	 * There is a position update requirement, let's get
	 * an empty buffer and fill it.
	 */
	while (virtio_vq_has_descs(vq)) {
		ret = virtio_vq_getchain(vq, &idx, &iov, 1, NULL);
		if (ret <= 0)
			return false;

		/* The buffer is bad. Don't use it. Just release it. */
		if (iov.iov_len < sizeof(struct sof_ipc_stream_posn)) {
			/* we need call endchain() later */
			*endchain = true;
			dev_err(dev, "iov len %lu, expecting len %lu\n",
				iov.iov_len, sizeof(*posn));
			virtio_vq_relchain(vq, idx, iov.iov_len);
			continue;
		}

		/* Get a valid buffer. Let's fill the data and kick back */
		memcpy(iov.iov_base, posn, sizeof(struct sof_ipc_stream_posn));
		*endchain = true;
		virtio_vq_relchain(vq, idx, iov.iov_len);
		return true;
	}

	return false;
}

/*
 * IPC notification reply from vFE to vBE
 * This function is called when vFE has queued an empty buffer to vBE
 */
static void sbe_ipc_fe_not_reply_get(struct sof_vbe *vbe, int vq_idx)
{
	struct virtio_vq_info *vq;
	struct vbs_sof_posn *entry;
	unsigned long flags;
	bool endchain;

	dev_dbg(vbe->sdev->dev,
		"audio BE notification vq kick handling, vq_idx %d\n", vq_idx);

	spin_lock_irqsave(&vbe->posn_lock, flags);

	/*
	 * Now we have gotten an empty buffer from vFE. This buffer is used
	 * to send the position update information to vFE.
	 * Let's check if there is position update requirement from vBE.
	 * If yes, let's kick back the position information to vFE.
	 * If there is no position update requirement from vBE, let's untouch
	 * the buffer and keep the buffer in the vq for later use.
	 */
	if (list_empty(&vbe->posn_list)) {
		/*
		 * No position update requirement. Don't touch the buffer and
		 * Keep the buffer in the vq
		 */
		spin_unlock_irqrestore(&vbe->posn_lock, flags);
		return;
	}

	vq = &vbe->vqs[vq_idx];
	entry = list_first_entry(&vbe->posn_list,
				 struct vbs_sof_posn, list);
	list_del(&entry->list);
	spin_unlock_irqrestore(&vbe->posn_lock, flags);

	/*
	 * There are position update requirements and now we get the new
	 * position buffer entry. Let's try to update the position to vFE.
	 */
	sbe_fill_posn_vqbuf(vbe, vq, &entry->pos, &endchain);

	/*
	 * encchain means: we have already processed this queue
	 * and now let's kick back and release it.
	 */
	if (endchain)
		virtio_vq_endchains(vq, 1);
}

/* validate component IPC */
static int sbe_ipc_comp(struct snd_sof_dev *sdev, int vm_id,
			struct sof_ipc_hdr *hdr)
{
	/*TODO validate host comp id range based on vm_id */

	/* Nothing to be done */
	return 0;
}

/*
 * This function is to get the BE dai link for the GOS
 * It uses the dai_link name to find the BE dai link.
 * The Current dai_link name "vm_dai_link" is for the GOS,
 * which means only one Virtual Machine is supported.
 * And the VM only support one playback pcm and one capture pcm.
 * After we switch to the new topology, we can support multiple
 * VMs and multiple PCM streams for each VM.
 * This function may be abandoned after switching to the new topology.
 */
static struct snd_pcm_substream *
sbe_get_substream(struct snd_sof_dev *sdev,
		  struct snd_soc_pcm_runtime **rtd, int direction)
{
	struct snd_pcm *pcm;
	struct snd_pcm_str *stream;
	struct snd_pcm_substream *substream = NULL;
	struct snd_soc_dai_link *dai_link;
	struct snd_soc_card *card = sdev->card;
	struct snd_soc_pcm_runtime *r;

	list_for_each_entry(r, &card->rtd_list, list) {
		/*
		 * We need to find a dedicated substream:
		 * pcm->streams[dir].substream which is dedicated
		 * used for vFE.
		 */
		pcm = r->pcm;
		if (!pcm)
			continue;

		stream = &pcm->streams[direction];
		substream = stream->substream;
		if (substream) {
			dai_link = r->dai_link;
			if (strcmp(dai_link->name, "vm_dai_link") == 0) {
				/*
				 * In the current solution, "vm_dai_link" is
				 * for the vFE.
				 */
				if (rtd)
					*rtd = r;
				return substream;
			}
		}
	}

	return NULL;
}

static int sbe_pcm_open(struct snd_sof_dev *sdev,
			void *ipc_data, int vm_id)
{
	/*
	 * TO re-use the sof callback for pcm, we should find a proper
	 * substream and do the correct setting for the substream.
	 * As there is no FE link substream in SOS for GOS (GOS FE link
	 * substreams are created in GOS and SOS will never see it), let's
	 * use BE link substream in SOS for the callbacks.
	 * This is save because the BE link substream is created dedicated for
	 * GOS in machine driver.
	 */
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	struct snd_sof_pcm *spcm;
	struct snd_soc_pcm_runtime *rtd = NULL;
	struct sof_ipc_pcm_params *pcm;
	u32 comp_id;
	size_t size;
	int direction;

	pcm = (struct sof_ipc_pcm_params *)ipc_data;
	comp_id = pcm->comp_id;

	spcm = snd_sof_find_spcm_comp(sdev, comp_id, &direction);
	if (!spcm)
		return -ENODEV;

	mutex_lock(&spcm->mutex);

	substream = sbe_get_substream(sdev, &rtd, direction);
	if (!substream || !rtd)
		return -ENODEV;
	if (substream->ref_count > 0)
		return -EBUSY;
	substream->ref_count++;	/* set it used */

	runtime = kzalloc(sizeof(*runtime), GFP_KERNEL);
	if (!runtime)
		return -ENOMEM;

	size = PAGE_ALIGN(sizeof(struct snd_pcm_mmap_status));
	runtime->status = snd_malloc_pages(size, GFP_KERNEL);
	if (!runtime->status) {
		kfree(runtime);
		return -ENOMEM;
	}
	memset((void *)runtime->status, 0, size);

	size = PAGE_ALIGN(sizeof(struct snd_pcm_mmap_control));
	runtime->control = snd_malloc_pages(size, GFP_KERNEL);
	if (!runtime->control) {
		dev_err(sdev->dev, "fail to alloc pages for runtime->control");
		snd_free_pages((void *)runtime->status,
			       PAGE_ALIGN(sizeof(struct snd_pcm_mmap_status)));
		kfree(runtime);
		return -ENOMEM;
	}
	memset((void *)runtime->control, 0, size);

	init_waitqueue_head(&runtime->sleep);
	init_waitqueue_head(&runtime->tsleep);
	runtime->status->state = SNDRV_PCM_STATE_OPEN;

	substream->runtime = runtime;
	substream->private_data = rtd;
	rtd->sof = spcm;
	substream->stream = direction;

	/* check with spcm exists or not */
	spcm->stream[direction].posn.host_posn = 0;
	spcm->stream[direction].posn.dai_posn = 0;
	spcm->stream[direction].substream = substream;

	/* TODO: codec open */

	snd_sof_pcm_platform_open(sdev, substream);

	mutex_unlock(&spcm->mutex);
	return 0;
}

static int sbe_pcm_close(struct snd_sof_dev *sdev,
			 void *ipc_data, int vm_id)
{
	struct snd_pcm_substream *substream;
	struct snd_sof_pcm *spcm;
	struct snd_soc_pcm_runtime *rtd = NULL;
	struct sof_ipc_stream *stream;
	u32 comp_id;
	int direction;

	stream = (struct sof_ipc_stream *)ipc_data;
	comp_id = stream->comp_id;

	spcm = snd_sof_find_spcm_comp(sdev, comp_id, &direction);
	if (!spcm)
		return 0;
	mutex_lock(&spcm->mutex);
	substream = sbe_get_substream(sdev, &rtd, direction);
	if (!substream) {
		mutex_unlock(&spcm->mutex);
		return 0;
	}

	snd_sof_pcm_platform_close(sdev, substream);

	/* TODO: codec close */

	substream->ref_count = 0;
	if (substream->runtime) {
		snd_free_pages((void *)substream->runtime->status,
			       PAGE_ALIGN(sizeof(struct snd_pcm_mmap_status)));
		snd_free_pages((void *)substream->runtime->control,
			       PAGE_ALIGN(sizeof(struct snd_pcm_mmap_control)));
		kfree(substream->runtime);
		substream->runtime = NULL;
		rtd->sof = NULL;
	}
	mutex_unlock(&spcm->mutex);
	return 0;
}

/*
 * FIXME - this function should only convert a compressed GOS PHY page table
 * into a page table of SOS physical pages. It should leave the HDA stream
 * alone for HDA code to manage.
 */
static int sbe_stream_prepare(struct snd_sof_dev *sdev,
			      struct sof_ipc_pcm_params *pcm, int vm_id,
			      struct snd_sg_page *table)
{
	u32 pcm_buffer_gpa = pcm->params.buffer.phy_addr;
	u64 pcm_buffer_hpa = vhm_vm_gpa2hpa(vm_id, (u64)pcm_buffer_gpa);
	u8 *page_table = (uint8_t *)__va(pcm_buffer_hpa);
	int idx, i;
	u32 gpa_parse, pages;
	u64 hpa_parse;

	pages = pcm->params.buffer.pages;
	for (i = 0; i < pages; i++) {
		idx = (((i << 2) + i)) >> 1;
		gpa_parse = page_table[idx] | (page_table[idx + 1] << 8)
			| (page_table[idx + 2] << 16);

		if (i & 0x1)
			gpa_parse <<= 8;
		else
			gpa_parse <<= 12;
		gpa_parse &= 0xfffff000;
		hpa_parse = vhm_vm_gpa2hpa(vm_id, (u64)gpa_parse);

		table[i].addr = hpa_parse;
	}

	return 0;
}

static int sbe_assemble_params(struct sof_ipc_pcm_params *pcm,
			       struct snd_pcm_hw_params *params)
{
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS)->min =
		pcm->params.channels;

	hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE)->min =
		pcm->params.rate;

	hw_param_interval(params, SNDRV_PCM_HW_PARAM_PERIOD_BYTES)->min =
		pcm->params.host_period_bytes;

	hw_param_interval(params, SNDRV_PCM_HW_PARAM_BUFFER_BYTES)->min =
		pcm->params.buffer.size;

	snd_mask_none(fmt);
	switch (pcm->params.frame_fmt) {
	case SOF_IPC_FRAME_S16_LE:
		snd_mask_set(fmt, SNDRV_PCM_FORMAT_S16);
		break;
	case SOF_IPC_FRAME_S24_4LE:
		snd_mask_set(fmt, SNDRV_PCM_FORMAT_S24);
		break;
	case SOF_IPC_FRAME_S32_LE:
		snd_mask_set(fmt, SNDRV_PCM_FORMAT_S32);
		break;
	case SOF_IPC_FRAME_FLOAT:
		snd_mask_set(fmt, SNDRV_PCM_FORMAT_FLOAT);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sbe_stream_hw_params(struct snd_sof_dev *sdev,
				struct sof_ipc_pcm_params *pcm, int vm_id)
{
	struct snd_sg_page *table;
	struct snd_sg_buf sgbuf; /* FIXME alloc at topology load */
	struct snd_dma_buffer dmab; /* FIXME alloc at topology load */
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	struct snd_pcm_hw_params params;
	int direction = pcm->params.direction;
	u32 pages;
	int ret;

	/* find the proper substream */
	substream = sbe_get_substream(sdev, NULL, direction);
	if (!substream)
		return -ENODEV;

	runtime = substream->runtime;
	if (!runtime) {
		dev_err(sdev->dev, "no runtime is available for hw_params\n");
		return -ENODEV;
	}

	/* setup hw */
	pages = pcm->params.buffer.pages;
	table = kcalloc(pages, sizeof(*table), GFP_KERNEL);
	sgbuf.table = table;
	dmab.private_data = &sgbuf;
	runtime->dma_buffer_p = &dmab; /* use the audiobuf from FE */

	/* TODO: codec hw_params */

	/* convert buffer GPA to HPA */
	ret = sbe_stream_prepare(sdev, pcm, vm_id, table);

	/* Use different stream_tag from FE. This is the real tag */
	sbe_assemble_params(pcm, &params);
	pcm->params.stream_tag =
		snd_sof_pcm_platform_hw_params(sdev, substream, &params);
	dev_dbg(sdev->dev, "stream_tag %d",
		pcm->params.stream_tag);

	kfree(table);
	return ret;
}

/* handle the stream ipc */
static int sbe_ipc_stream_codec(struct snd_sof_dev *sdev, int vm_id,
				struct sof_ipc_hdr *hdr)
{
	struct sof_ipc_pcm_params *pcm;
	struct sof_ipc_stream *stream;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_pcm_substream *substream;
	struct snd_soc_dai *codec_dai;
	const struct snd_soc_dai_ops *ops;
	int ret, direction, comp_id;
	int i;
	u32 cmd = (hdr->cmd & SOF_CMD_TYPE_MASK) >> SOF_CMD_TYPE_SHIFT;

	/* TODO validate host comp id range based on vm_id */

	switch (cmd) {
	case ICS(SOF_IPC_STREAM_TRIG_START):
		stream = (struct sof_ipc_stream *)hdr;
		comp_id = stream->comp_id;
		snd_sof_find_spcm_comp(sdev, comp_id, &direction);
		substream = sbe_get_substream(sdev, &rtd, direction);

		for (i = 0; i < rtd->num_codecs; i++) {
			/*
			 * Now we are ready to trigger start.
			 * Let's unmute the codec firstly
			 */
			codec_dai = rtd->codec_dais[i];
			snd_soc_dai_digital_mute(codec_dai, 0, direction);
			ops = codec_dai->driver->ops;
			if (ops->trigger) {
				ret = ops->trigger(substream,
						   SNDRV_PCM_TRIGGER_START,
						   codec_dai);
				if (ret < 0)
					return ret;
			}
		}
		break;
	default:
		dev_dbg(sdev->dev, "0x%x!\n", cmd);
		break;
	}

	return 0;
}

/* handle the stream ipc */
static int sbe_ipc_stream(struct snd_sof_dev *sdev, int vm_id,
			  struct sof_ipc_hdr *hdr)
{
	struct sof_ipc_pcm_params *pcm;
	struct sof_ipc_stream *stream;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_pcm_substream *substream;
	struct snd_soc_dai *codec_dai;
	const struct snd_soc_dai_ops *ops;
	int ret, direction, comp_id, i;
	u32 cmd = (hdr->cmd & SOF_CMD_TYPE_MASK) >> SOF_CMD_TYPE_SHIFT;

	/* TODO validate host comp id range based on vm_id */

	switch (cmd) {
	case ICS(SOF_IPC_STREAM_PCM_PARAMS):
		sbe_pcm_open(sdev, hdr, vm_id);
		pcm = (struct sof_ipc_pcm_params *)hdr;
		ret = sbe_stream_hw_params(sdev, pcm, vm_id);
		break;
	case ICS(SOF_IPC_STREAM_TRIG_START):
		stream = (struct sof_ipc_stream *)hdr;
		comp_id = stream->comp_id;
		snd_sof_find_spcm_comp(sdev, comp_id, &direction);
		substream = sbe_get_substream(sdev, &rtd, direction);
		snd_sof_pcm_platform_trigger(sdev, substream,
					     SNDRV_PCM_TRIGGER_START);
		break;
	case ICS(SOF_IPC_STREAM_TRIG_STOP):
		stream = (struct sof_ipc_stream *)hdr;
		comp_id = stream->comp_id;
		snd_sof_find_spcm_comp(sdev, comp_id, &direction);
		substream = sbe_get_substream(sdev, &rtd, direction);
		for (i = 0; i < rtd->num_codecs; i++) {
			codec_dai = rtd->codec_dais[i];
			ops = codec_dai->driver->ops;
			if (ops->trigger) {
				ret = ops->trigger(substream,
						   SNDRV_PCM_TRIGGER_STOP,
						   codec_dai);
				if (ret < 0) {
					dev_err(sdev->dev,
						"trigger stop fails\n");
					return ret;
				}
			}
		}
		snd_sof_pcm_platform_trigger(sdev, substream,
					     SNDRV_PCM_TRIGGER_STOP);
		break;
	case ICS(SOF_IPC_STREAM_PCM_FREE):
		sbe_pcm_close(sdev, hdr, vm_id);
		break;
	case ICS(SOF_IPC_STREAM_POSITION):
		/*
		 * TODO: this is special case, we do not send this IPC to DSP
		 * but read back position directly from memory (like SOS) and
		 * then reply to FE.
		 * Use stream ID to get correct stream data
		 */
		break;
	default:
		dev_dbg(sdev->dev, "0x%x!\n", cmd);
		break;
	}

	return 0;
}

static int sbe_ipc_tplg_comp_new(struct snd_sof_dev *sdev, int vm_id,
				 struct sof_ipc_hdr *hdr)
{
	struct snd_sof_pcm *spcm;
	struct sof_ipc_comp *comp = (struct sof_ipc_comp *)hdr;
	struct sof_ipc_comp_host *host;

	switch (comp->type) {
	case SOF_COMP_HOST:
		/*
		 * TODO: below is a temporary solution. next step is
		 * to create a whole pcm staff incluing substream
		 * based on Liam's suggestion.
		 */

		/*
		 * let's create spcm in HOST ipc
		 * spcm should be created in pcm load, but there is no such ipc
		 * so let create it here
		 */
		host = (struct sof_ipc_comp_host *)hdr;
		spcm = kzalloc(sizeof(*spcm), GFP_KERNEL);
		if (!spcm)
			return -ENOMEM;

		spcm->sdev = sdev;
		spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].comp_id =
			SOF_VIRTIO_COMP_ID_UNASSIGNED;
		spcm->stream[SNDRV_PCM_STREAM_CAPTURE].comp_id =
			SOF_VIRTIO_COMP_ID_UNASSIGNED;
		mutex_init(&spcm->mutex);
		spcm->stream[host->direction].comp_id = host->comp.id;
		list_add(&spcm->list, &sdev->pcm_list);
		break;
	default:
		break;
	}
	return 0;
}

/* validate topology IPC */
static int sbe_ipc_tplg(struct snd_sof_dev *sdev, int vm_id,
			struct sof_ipc_hdr *hdr)
{
	/* TODO validate host comp id range based on vm_id */

	u32 cmd;
	int ret = 0;

	cmd = (hdr->cmd & SOF_CMD_TYPE_MASK) >> SOF_CMD_TYPE_SHIFT;

	switch (cmd) {
	case ICS(SOF_IPC_TPLG_COMP_NEW):
		ret = sbe_ipc_tplg_comp_new(sdev, vm_id, hdr);
		break;
	default:
		break;
	}

	return ret;
}

static int sbe_ipc_stream_param_post(struct snd_sof_dev *sdev,
				     void *ipc_buf, void *reply_buf)
{
	struct sof_ipc_pcm_params_reply *ipc_params_reply;
	struct snd_sof_pcm *spcm;
	int direction;
	u32 comp_id;
	int posn_offset;

	ipc_params_reply = (struct sof_ipc_pcm_params_reply *)reply_buf;
	comp_id = ipc_params_reply->comp_id;
	posn_offset = ipc_params_reply->posn_offset;

	spcm = snd_sof_find_spcm_comp(sdev, comp_id, &direction);
	if (!spcm)
		return -ENODEV;

	spcm->posn_offset[direction] =
		sdev->stream_box.offset + posn_offset;
	return 0;
}

/*
 * For some IPCs, the reply needs to be handled.
 * This function is used to handle the replies of these IPCs.
 */
static int sbe_ipc_post(struct snd_sof_dev *sdev,
			void *ipc_buf, void *reply_buf)
{
	struct sof_ipc_hdr *hdr;
	u32 type, cmd;
	int ret = 0;

	hdr = (struct sof_ipc_hdr *)ipc_buf;
	type = (hdr->cmd & SOF_GLB_TYPE_MASK) >> SOF_GLB_TYPE_SHIFT;
	cmd = (hdr->cmd & SOF_CMD_TYPE_MASK) >> SOF_CMD_TYPE_SHIFT;

	switch (type) {
	case IGS(SOF_IPC_GLB_STREAM_MSG):
		switch (cmd) {
		case ICS(SOF_IPC_STREAM_PCM_PARAMS):
			ret = sbe_ipc_stream_param_post(sdev,
							ipc_buf, reply_buf);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return ret;
}

/*
 * TODO: The guest base ID is passed to guest at boot.
 * TODO rename function name, not submit but consume
 * TODO add topology ipc support and manage the multiple pcm and vms
 */
static int sbe_ipc_fwd(struct snd_sof_dev *sdev, int vm_id,
		       void *ipc_buf, void *reply_buf,
		       size_t count, size_t reply_sz)
{
	struct sof_ipc_hdr *hdr;
	u32 type;
	int ret = 0;

	/* validate IPC */
	if (!count) {
		dev_err(sdev->dev, "error: guest IPC size is 0\n");
		return -EINVAL;
	}

	hdr = (struct sof_ipc_hdr *)ipc_buf;
	type = (hdr->cmd & SOF_GLB_TYPE_MASK) >> SOF_GLB_TYPE_SHIFT;

	/* validate the ipc */
	switch (type) {
	case IGS(SOF_IPC_GLB_COMP_MSG):
		ret = sbe_ipc_comp(sdev, vm_id, hdr);
		if (ret < 0)
			return ret;
		break;
	case IGS(SOF_IPC_GLB_STREAM_MSG):
		ret = sbe_ipc_stream(sdev, vm_id, hdr);
		if (ret < 0)
			return ret;
		break;
	case IGS(SOF_IPC_GLB_DAI_MSG):
		/*
		 * After we use the new topology solution for FE,
		 * we will not touch DAI anymore.
		 */
		break;
	case IGS(SOF_IPC_GLB_TPLG_MSG):
		ret = sbe_ipc_tplg(sdev, vm_id, hdr);
		if (ret < 0)
			return ret;
		break;
	case IGS(SOF_IPC_GLB_TRACE_MSG):
		/* Trace should be initialized in SOS, skip FE requirement */
		return 0;
	default:
		dev_info(sdev->dev, "unhandled IPC 0x%x!\n", type);
		break;
	}

	/* now send the IPC */
	ret = sof_virtio_send_ipc(sdev, ipc_buf, reply_buf, count, reply_sz);
	if (ret < 0) {
		dev_err(sdev->dev, "err: failed to send virtio IPC %d\n", ret);
		return ret;
	}

	/* For some IPCs, the reply needs to be handled */
	ret = sbe_ipc_post(sdev, ipc_buf, reply_buf);

	switch (type) {
	case IGS(SOF_IPC_GLB_STREAM_MSG):
		/* setup the codec */
		ret = sbe_ipc_stream_codec(sdev, vm_id, hdr);
		if (ret < 0)
			return ret;
		break;
	default:
			break;
	}

	return ret;
}

/* IPC commands coming from FE to BE */
static void sbe_ipc_fe_cmd_get(struct sof_vbe *vbe, int vq_idx)
{
	struct virtio_vq_info *vq = &vbe->vqs[vq_idx];
	struct device *dev = vbe->sdev->dev;
	struct iovec iov[2];
	u16 idx;
	void *ipc_buf;
	void *reply_buf;
	size_t len1, len2;
	int vm_id;
	int ret, i;

	vm_id = vbe->vm_id;
	memset(iov, 0, sizeof(iov));

	/* while there are mesages in virtio queue */
	while (virtio_vq_has_descs(vq)) {
		/* FE uses items, first is command second is reply data */
		ret = virtio_vq_getchain(vq, &idx, iov, 2, NULL);
		if (ret < 2) {
			/* something wrong in vq, no item is fetched */
			if (ret < 0) {
				/*
				 * This should never happen.
				 * FE should be aware this situation already
				 */
				virtio_vq_endchains(vq, 1);
				return;
			}

			dev_err(dev, "ipc buf and reply buf not paired\n");

			/* no enough items, let drop this kick */
			for (i = 0; i <= ret; i++) {
				virtio_vq_relchain(vq, idx + i,
						   iov[i].iov_len);
			}
			virtio_vq_endchains(vq, 1);
			return;
		}

		/*
		 * let's check the ipc message and reply buffer's
		 * length is valid or not
		 */
		len1 = iov[SOF_VIRTIO_IPC_MSG].iov_len;
		len2 = iov[SOF_VIRTIO_IPC_REPLY].iov_len;
		if (!len1 || !len2) {
			if (len1)
				virtio_vq_relchain(vq, idx, len1);
			if (len2)
				virtio_vq_relchain(vq, idx + 1, len2);
		} else {
			/* OK, the buffer is valid. let's handle the ipc */
			ipc_buf = iov[SOF_VIRTIO_IPC_MSG].iov_base;
			reply_buf = iov[SOF_VIRTIO_IPC_REPLY].iov_base;

			/* send IPC to HW */
			ret = sbe_ipc_fwd(vbe->sdev, vm_id, ipc_buf, reply_buf,
					  len1, len2);
			if (ret < 0)
				dev_err(dev, "submit guest ipc command fail\n");

			virtio_vq_relchain(vq, idx, len1);
			virtio_vq_relchain(vq, idx + 1, len2);
		}
	}

	/* BE has finished the operations, now let's kick back */
	virtio_vq_endchains(vq, 1);
}

static void handle_vq_kick(struct sof_vbe *vbe, int vq_idx)
{
	dev_dbg(vbe->sdev->dev, "vq_idx %d\n", vq_idx);

	switch (vq_idx) {
	case SOF_VIRTIO_IPC_CMD_TX_VQ:
		/* IPC command from FE to DSP */
		return sbe_ipc_fe_cmd_get(vbe, vq_idx);
	case SOF_VIRTIO_IPC_CMD_RX_VQ:
		/* IPC command reply from DSP to FE - NOT kick */
		break;
	case SOF_VIRTIO_IPC_NOT_TX_VQ:
		/* IPC notification reply from FE to DSP */
		return sbe_ipc_fe_not_reply_get(vbe, vq_idx);
	case SOF_VIRTIO_IPC_NOT_RX_VQ:
		/* IPC notification from DSP to FE - NOT kick */
		break;
	default:
		dev_err(vbe->sdev->dev, "idx %d is invalid\n", vq_idx);
		break;
	}
}

/*
 * handle_kick() is used to handle the event that vFE kick a queue
 * entry to vBE. First, check this event is valid or not. If it is
 * valid and needs to be handled, let's call hanle_vq_kick()
 */
static int handle_kick(int client_id, unsigned long *ioreqs_map)
{
	struct vhm_request *req;
	struct sof_vbe_client *client;
	struct sof_vbe *vbe;
	struct snd_sof_dev *sdev = sof_virtio_get_sof();
	int i, handle;

	if (!sdev) {
		pr_err("error: no BE registered for SOF!\n");
		return -EINVAL;
	}

	dev_dbg(sdev->dev, "virtio audio kick handling!\n");

	/* get the client this notification is for/from? */
	client = vbe_client_find(sdev, client_id);
	if (!client) {
		dev_err(sdev->dev, "Ooops! client %d not found!\n", client_id);
		return -EINVAL;
	}
	vbe = client->vbe;

	/* go through all vcpu for the valid request buffer */
	for (i = 0; i < client->max_vcpu; i++) {
		req = &client->req_buf[i];
		handle = 0;

		/* is request valid and for this client */
		if (!req->valid)
			continue;
		if (req->client != client->vhm_client_id)
			continue;

		/* ignore if not processing state */
		if (req->processed != REQ_STATE_PROCESSING)
			continue;

		dev_dbg(sdev->dev,
			"ioreq type %d, direction %d, addr 0x%llx, size 0x%llx, value 0x%x\n",
			 req->type,
			 req->reqs.pio_request.direction,
			 req->reqs.pio_request.address,
			 req->reqs.pio_request.size,
			 req->reqs.pio_request.value);

		if (req->reqs.pio_request.direction == REQUEST_READ) {
			/*
			 * currently we handle kick only,
			 * so read will return 0
			 */
			req->reqs.pio_request.value = 0;
		} else {
			req->reqs.pio_request.value >= 0 ?
				(handle = 1) : (handle = 0);
		}

		req->processed = REQ_STATE_SUCCESS;

		/*
		 * notify hypervisor this event request is finished.
		 * virtio driver is ready to handle this event.
		 */
		acrn_ioreq_complete_request(client->vhm_client_id, i);

		/* handle VQ kick if needed */
		if (handle)
			handle_vq_kick(vbe, req->reqs.pio_request.value);
	}

	return 0;
}

/*
 * register vhm client with virtio.
 * vhm use the client to handle the io access from FE
 */
int sof_vbe_register_client(struct sof_vbe *vbe)
{
	struct virtio_dev_info *dev_info = &vbe->dev_info;
	struct snd_sof_dev *sdev = vbe->sdev;
	struct vm_info info;
	struct sof_vbe_client *client;
	unsigned int vmid;
	int ret;

	/*
	 * vbs core has mechanism to manage the client
	 * there is no need to handle this in the special BE driver
	 * let's use the vbs core client management later
	 */
	client = devm_kzalloc(sdev->dev, sizeof(*client), GFP_KERNEL);
	if (!client)
		return -EINVAL;

	client->vbe = vbe;

	vmid = dev_info->_ctx.vmid;
	client->vhm_client_id = acrn_ioreq_create_client(vmid, handle_kick,
							 "sof_vbe kick init\n");
	if (client->vhm_client_id < 0) {
		dev_err(sdev->dev, "failed to create client of acrn ioreq!\n");
		return client->vhm_client_id;
	}

	ret = acrn_ioreq_add_iorange(client->vhm_client_id, REQ_PORTIO,
				     dev_info->io_range_start,
				     dev_info->io_range_start +
				     dev_info->io_range_len - 1);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to add iorange to acrn ioreq!\n");
		goto err;
	}

	/*
	 * setup the vm information, such as max_vcpu and max_gfn
	 * BE need this information to handle the vqs
	 */
	ret = vhm_get_vm_info(vmid, &info);
	if (ret < 0) {
		dev_err(sdev->dev, "failed in vhm_get_vm_info!\n");
		goto err;
	}
	client->max_vcpu = info.max_vcpu;

	/*
	 * Setup the reqbuf for this client. The reqbuf is ready in
	 * acrn system now.
	 */
	client->req_buf = acrn_ioreq_get_reqbuf(client->vhm_client_id);
	if (!client->req_buf) {
		dev_err(sdev->dev, "failed in acrn_ioreq_get_reqbuf!\n");
		goto err;
	}

	/* just attach once as vhm will kick kthread */
	acrn_ioreq_attach_client(client->vhm_client_id, 0);

	/* complete client init and add to list */
	list_add(&client->list, &vbe->client_list);

	return 0;
err:
	acrn_ioreq_destroy_client(client->vhm_client_id);
	return -EINVAL;
}

/* register SOF audio BE with virtio/acrn */
int sof_vbe_register(struct snd_sof_dev *sdev, struct sof_vbe **svbe)
{
	struct sof_vbe *vbe;
	struct virtio_vq_info *vqs;
	int i;

	vbe = devm_kzalloc(sdev->dev, sizeof(*vbe), GFP_KERNEL);
	if (!vbe)
		return -ENOMEM;

	INIT_LIST_HEAD(&vbe->client_list);
	INIT_LIST_HEAD(&vbe->posn_list);
	spin_lock_init(&vbe->posn_lock);
	vbe->sdev = sdev;

	/*
	 * We currently only support one VM. The comp_id range will be
	 * dynamically assigned when multiple VMs are supported.
	 */
	vbe->comp_id_begin = SOF_VIRTIO_MAX_GOS_COMPS;
	vbe->comp_id_end = vbe->comp_id_begin + SOF_VIRTIO_MAX_GOS_COMPS;

	vqs = vbe->vqs;
	for (i = 0; i < SOF_VIRTIO_NUM_OF_VQS; i++) {
		vqs[i].dev = &vbe->dev_info;

		/*
		 * currently relies on VHM to kick us,
		 * thus vq_notify not used
		 */
		vqs[i].vq_notify = NULL;
	}

	/* link dev and vqs */
	vbe->dev_info.vqs = vqs;

	virtio_dev_init(&vbe->dev_info, vqs, SOF_VIRTIO_NUM_OF_VQS);

	*svbe = vbe;

	return 0;
}

int sof_vbe_update_guest_posn(struct snd_sof_dev *sdev,
			      struct sof_ipc_stream_posn *posn)
{
	struct sof_vbe *vbe = sbe_comp_id_to_vbe(sdev, posn->comp_id);
	struct virtio_vq_info *vq = &vbe->vqs[SOF_VIRTIO_IPC_NOT_RX_VQ];
	struct vbs_sof_posn *entry;
	unsigned long flags;
	bool ret, endchain;

	/* posn update for SOS */
	if (!vbe)
		return 0;

	/*
	 * let's try to get a notification RX vq available buffer
	 * If there is an available buffer, let's notify immediately
	 */
	ret = sbe_fill_posn_vqbuf(vbe, vq, posn, &endchain);
	if (ret) {
		if (endchain)
			virtio_vq_endchains(vq, 1);
		return 0;
	}

	spin_lock_irqsave(&vbe->posn_lock, flags);

	/*
	 * Notification RX vq buffer is not available. Let's save the posn
	 * update msg. And send the msg when vq buffer is available.
	 */
	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry) {
		spin_unlock_irqrestore(&vbe->posn_lock, flags);
		return -ENOMEM;
	}

	memcpy(&entry->pos, posn, sizeof(struct vbs_sof_posn));
	list_add_tail(&entry->list, &vbe->posn_list);
	spin_unlock_irqrestore(&vbe->posn_lock, flags);

	if (endchain)
		virtio_vq_endchains(vq, 1);

	return 0;
}
