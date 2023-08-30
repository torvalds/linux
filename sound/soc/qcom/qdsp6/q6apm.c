// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Linaro Limited

#include <dt-bindings/soc/qcom,gpr.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/soc/qcom/apr.h>
#include <linux/wait.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include "audioreach.h"
#include "q6apm.h"

/* Graph Management */
struct apm_graph_mgmt_cmd {
	struct apm_module_param_data param_data;
	uint32_t num_sub_graphs;
	uint32_t sub_graph_id_list[];
} __packed;

#define APM_GRAPH_MGMT_PSIZE(p, n) ALIGN(struct_size(p, sub_graph_id_list, n), 8)

struct q6apm *g_apm;

int q6apm_send_cmd_sync(struct q6apm *apm, struct gpr_pkt *pkt, uint32_t rsp_opcode)
{
	gpr_device_t *gdev = apm->gdev;

	return audioreach_send_cmd_sync(&gdev->dev, gdev, &apm->result, &apm->lock,
					NULL, &apm->wait, pkt, rsp_opcode);
}

static struct audioreach_graph *q6apm_get_audioreach_graph(struct q6apm *apm, uint32_t graph_id)
{
	struct audioreach_graph_info *info;
	struct audioreach_graph *graph;
	int id;

	mutex_lock(&apm->lock);
	graph = idr_find(&apm->graph_idr, graph_id);
	mutex_unlock(&apm->lock);

	if (graph) {
		kref_get(&graph->refcount);
		return graph;
	}

	info = idr_find(&apm->graph_info_idr, graph_id);

	if (!info)
		return ERR_PTR(-ENODEV);

	graph = kzalloc(sizeof(*graph), GFP_KERNEL);
	if (!graph)
		return ERR_PTR(-ENOMEM);

	graph->apm = apm;
	graph->info = info;
	graph->id = graph_id;

	graph->graph = audioreach_alloc_graph_pkt(apm, info);
	if (IS_ERR(graph->graph)) {
		void *err = graph->graph;

		kfree(graph);
		return ERR_CAST(err);
	}

	mutex_lock(&apm->lock);
	id = idr_alloc(&apm->graph_idr, graph, graph_id, graph_id + 1, GFP_KERNEL);
	if (id < 0) {
		dev_err(apm->dev, "Unable to allocate graph id (%d)\n", graph_id);
		kfree(graph->graph);
		kfree(graph);
		mutex_unlock(&apm->lock);
		return ERR_PTR(id);
	}
	mutex_unlock(&apm->lock);

	kref_init(&graph->refcount);

	q6apm_send_cmd_sync(apm, graph->graph, 0);

	return graph;
}

static int audioreach_graph_mgmt_cmd(struct audioreach_graph *graph, uint32_t opcode)
{
	struct audioreach_graph_info *info = graph->info;
	int num_sub_graphs = info->num_sub_graphs;
	struct apm_module_param_data *param_data;
	struct apm_graph_mgmt_cmd *mgmt_cmd;
	struct audioreach_sub_graph *sg;
	struct q6apm *apm = graph->apm;
	int i = 0, rc, payload_size;
	struct gpr_pkt *pkt;

	payload_size = APM_GRAPH_MGMT_PSIZE(mgmt_cmd, num_sub_graphs);

	pkt = audioreach_alloc_apm_cmd_pkt(payload_size, opcode, 0);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	mgmt_cmd = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	mgmt_cmd->num_sub_graphs = num_sub_graphs;

	param_data = &mgmt_cmd->param_data;
	param_data->module_instance_id = APM_MODULE_INSTANCE_ID;
	param_data->param_id = APM_PARAM_ID_SUB_GRAPH_LIST;
	param_data->param_size = payload_size - APM_MODULE_PARAM_DATA_SIZE;

	list_for_each_entry(sg, &info->sg_list, node)
		mgmt_cmd->sub_graph_id_list[i++] = sg->sub_graph_id;

	rc = q6apm_send_cmd_sync(apm, pkt, 0);

	kfree(pkt);

	return rc;
}

static void q6apm_put_audioreach_graph(struct kref *ref)
{
	struct audioreach_graph *graph;
	struct q6apm *apm;

	graph = container_of(ref, struct audioreach_graph, refcount);
	apm = graph->apm;

	audioreach_graph_mgmt_cmd(graph, APM_CMD_GRAPH_CLOSE);

	mutex_lock(&apm->lock);
	graph = idr_remove(&apm->graph_idr, graph->id);
	mutex_unlock(&apm->lock);

	kfree(graph->graph);
	kfree(graph);
}


static int q6apm_get_apm_state(struct q6apm *apm)
{
	struct gpr_pkt *pkt;

	pkt = audioreach_alloc_apm_cmd_pkt(0, APM_CMD_GET_SPF_STATE, 0);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	q6apm_send_cmd_sync(apm, pkt, APM_CMD_RSP_GET_SPF_STATE);

	kfree(pkt);

	return apm->state;
}

bool q6apm_is_adsp_ready(void)
{
	if (g_apm)
		return q6apm_get_apm_state(g_apm);

	return false;
}
EXPORT_SYMBOL_GPL(q6apm_is_adsp_ready);

static struct audioreach_module *__q6apm_find_module_by_mid(struct q6apm *apm,
						    struct audioreach_graph_info *info,
						    uint32_t mid)
{
	struct audioreach_container *container;
	struct audioreach_sub_graph *sgs;
	struct audioreach_module *module;

	list_for_each_entry(sgs, &info->sg_list, node) {
		list_for_each_entry(container, &sgs->container_list, node) {
			list_for_each_entry(module, &container->modules_list, node) {
				if (mid == module->module_id)
					return module;
			}
		}
	}

	return NULL;
}

int q6apm_graph_media_format_shmem(struct q6apm_graph *graph,
				   struct audioreach_module_config *cfg)
{
	struct audioreach_module *module;

	if (cfg->direction == SNDRV_PCM_STREAM_CAPTURE)
		module = q6apm_find_module_by_mid(graph, MODULE_ID_RD_SHARED_MEM_EP);
	else
		module = q6apm_find_module_by_mid(graph, MODULE_ID_WR_SHARED_MEM_EP);

	if (!module)
		return -ENODEV;

	audioreach_set_media_format(graph, module, cfg);

	return 0;

}
EXPORT_SYMBOL_GPL(q6apm_graph_media_format_shmem);

int q6apm_map_memory_regions(struct q6apm_graph *graph, unsigned int dir, phys_addr_t phys,
			     size_t period_sz, unsigned int periods)
{
	struct audioreach_graph_data *data;
	struct audio_buffer *buf;
	int cnt;
	int rc;

	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		data = &graph->rx_data;
	else
		data = &graph->tx_data;

	mutex_lock(&graph->lock);

	if (data->buf) {
		mutex_unlock(&graph->lock);
		return 0;
	}

	buf = kzalloc(((sizeof(struct audio_buffer)) * periods), GFP_KERNEL);
	if (!buf) {
		mutex_unlock(&graph->lock);
		return -ENOMEM;
	}

	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		data = &graph->rx_data;
	else
		data = &graph->tx_data;

	data->buf = buf;

	buf[0].phys = phys;
	buf[0].size = period_sz;

	for (cnt = 1; cnt < periods; cnt++) {
		if (period_sz > 0) {
			buf[cnt].phys = buf[0].phys + (cnt * period_sz);
			buf[cnt].size = period_sz;
		}
	}
	data->num_periods = periods;

	mutex_unlock(&graph->lock);

	rc = audioreach_map_memory_regions(graph, dir, period_sz, periods, 1);
	if (rc < 0) {
		dev_err(graph->dev, "Memory_map_regions failed\n");
		audioreach_graph_free_buf(graph);
	}

	return rc;
}
EXPORT_SYMBOL_GPL(q6apm_map_memory_regions);

int q6apm_unmap_memory_regions(struct q6apm_graph *graph, unsigned int dir)
{
	struct apm_cmd_shared_mem_unmap_regions *cmd;
	struct audioreach_graph_data *data;
	struct gpr_pkt *pkt;
	int rc;

	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		data = &graph->rx_data;
	else
		data = &graph->tx_data;

	if (!data->mem_map_handle)
		return 0;

	pkt = audioreach_alloc_apm_pkt(sizeof(*cmd), APM_CMD_SHARED_MEM_UNMAP_REGIONS, dir,
				     graph->port->id);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	cmd = (void *)pkt + GPR_HDR_SIZE;
	cmd->mem_map_handle = data->mem_map_handle;

	rc = audioreach_graph_send_cmd_sync(graph, pkt, APM_CMD_SHARED_MEM_UNMAP_REGIONS);
	kfree(pkt);

	audioreach_graph_free_buf(graph);

	return rc;
}
EXPORT_SYMBOL_GPL(q6apm_unmap_memory_regions);

int q6apm_graph_media_format_pcm(struct q6apm_graph *graph, struct audioreach_module_config *cfg)
{
	struct audioreach_graph_info *info = graph->info;
	struct audioreach_sub_graph *sgs;
	struct audioreach_container *container;
	struct audioreach_module *module;

	list_for_each_entry(sgs, &info->sg_list, node) {
		list_for_each_entry(container, &sgs->container_list, node) {
			list_for_each_entry(module, &container->modules_list, node) {
				if ((module->module_id == MODULE_ID_WR_SHARED_MEM_EP) ||
					(module->module_id == MODULE_ID_RD_SHARED_MEM_EP))
					continue;

				audioreach_set_media_format(graph, module, cfg);
			}
		}
	}

	return 0;

}
EXPORT_SYMBOL_GPL(q6apm_graph_media_format_pcm);

static int q6apm_graph_get_tx_shmem_module_iid(struct q6apm_graph *graph)
{
	struct audioreach_module *module;

	module = q6apm_find_module_by_mid(graph, MODULE_ID_RD_SHARED_MEM_EP);
	if (!module)
		return -ENODEV;

	return module->instance_id;

}

int q6apm_graph_get_rx_shmem_module_iid(struct q6apm_graph *graph)
{
	struct audioreach_module *module;

	module = q6apm_find_module_by_mid(graph, MODULE_ID_WR_SHARED_MEM_EP);
	if (!module)
		return -ENODEV;

	return module->instance_id;

}
EXPORT_SYMBOL_GPL(q6apm_graph_get_rx_shmem_module_iid);

int q6apm_write_async(struct q6apm_graph *graph, uint32_t len, uint32_t msw_ts,
		      uint32_t lsw_ts, uint32_t wflags)
{
	struct apm_data_cmd_wr_sh_mem_ep_data_buffer_v2 *write_buffer;
	struct audio_buffer *ab;
	struct gpr_pkt *pkt;
	int rc, iid;

	iid = q6apm_graph_get_rx_shmem_module_iid(graph);
	pkt = audioreach_alloc_pkt(sizeof(*write_buffer), DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2,
				   graph->rx_data.dsp_buf | (len << APM_WRITE_TOKEN_LEN_SHIFT),
				   graph->port->id, iid);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	write_buffer = (void *)pkt + GPR_HDR_SIZE;

	mutex_lock(&graph->lock);
	ab = &graph->rx_data.buf[graph->rx_data.dsp_buf];

	write_buffer->buf_addr_lsw = lower_32_bits(ab->phys);
	write_buffer->buf_addr_msw = upper_32_bits(ab->phys);
	write_buffer->buf_size = len;
	write_buffer->timestamp_lsw = lsw_ts;
	write_buffer->timestamp_msw = msw_ts;
	write_buffer->mem_map_handle = graph->rx_data.mem_map_handle;
	write_buffer->flags = wflags;

	graph->rx_data.dsp_buf++;

	if (graph->rx_data.dsp_buf >= graph->rx_data.num_periods)
		graph->rx_data.dsp_buf = 0;

	mutex_unlock(&graph->lock);

	rc = gpr_send_port_pkt(graph->port, pkt);

	kfree(pkt);

	return rc;
}
EXPORT_SYMBOL_GPL(q6apm_write_async);

int q6apm_read(struct q6apm_graph *graph)
{
	struct data_cmd_rd_sh_mem_ep_data_buffer_v2 *read_buffer;
	struct audioreach_graph_data *port;
	struct audio_buffer *ab;
	struct gpr_pkt *pkt;
	int rc, iid;

	iid = q6apm_graph_get_tx_shmem_module_iid(graph);
	pkt = audioreach_alloc_pkt(sizeof(*read_buffer), DATA_CMD_RD_SH_MEM_EP_DATA_BUFFER_V2,
				   graph->tx_data.dsp_buf, graph->port->id, iid);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	read_buffer = (void *)pkt + GPR_HDR_SIZE;

	mutex_lock(&graph->lock);
	port = &graph->tx_data;
	ab = &port->buf[port->dsp_buf];

	read_buffer->buf_addr_lsw = lower_32_bits(ab->phys);
	read_buffer->buf_addr_msw = upper_32_bits(ab->phys);
	read_buffer->mem_map_handle = port->mem_map_handle;
	read_buffer->buf_size = ab->size;

	port->dsp_buf++;

	if (port->dsp_buf >= port->num_periods)
		port->dsp_buf = 0;

	mutex_unlock(&graph->lock);

	rc = gpr_send_port_pkt(graph->port, pkt);
	kfree(pkt);

	return rc;
}
EXPORT_SYMBOL_GPL(q6apm_read);

static int graph_callback(struct gpr_resp_pkt *data, void *priv, int op)
{
	struct data_cmd_rsp_rd_sh_mem_ep_data_buffer_done_v2 *rd_done;
	struct data_cmd_rsp_wr_sh_mem_ep_data_buffer_done_v2 *done;
	struct apm_cmd_rsp_shared_mem_map_regions *rsp;
	struct gpr_ibasic_rsp_result_t *result;
	struct q6apm_graph *graph = priv;
	struct gpr_hdr *hdr = &data->hdr;
	struct device *dev = graph->dev;
	uint32_t client_event;
	phys_addr_t phys;
	int token;

	result = data->payload;

	switch (hdr->opcode) {
	case DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2:
		client_event = APM_CLIENT_EVENT_DATA_WRITE_DONE;
		mutex_lock(&graph->lock);
		token = hdr->token & APM_WRITE_TOKEN_MASK;

		done = data->payload;
		phys = graph->rx_data.buf[token].phys;
		mutex_unlock(&graph->lock);

		if (lower_32_bits(phys) == done->buf_addr_lsw &&
		    upper_32_bits(phys) == done->buf_addr_msw) {
			graph->result.opcode = hdr->opcode;
			graph->result.status = done->status;
			if (graph->cb)
				graph->cb(client_event, hdr->token, data->payload, graph->priv);
		} else {
			dev_err(dev, "WR BUFF Unexpected addr %08x-%08x\n", done->buf_addr_lsw,
				done->buf_addr_msw);
		}

		break;
	case APM_CMD_RSP_SHARED_MEM_MAP_REGIONS:
		graph->result.opcode = hdr->opcode;
		graph->result.status = 0;
		rsp = data->payload;

		if (hdr->token == SNDRV_PCM_STREAM_PLAYBACK)
			graph->rx_data.mem_map_handle = rsp->mem_map_handle;
		else
			graph->tx_data.mem_map_handle = rsp->mem_map_handle;

		wake_up(&graph->cmd_wait);
		break;
	case DATA_CMD_RSP_RD_SH_MEM_EP_DATA_BUFFER_V2:
		client_event = APM_CLIENT_EVENT_DATA_READ_DONE;
		mutex_lock(&graph->lock);
		rd_done = data->payload;
		phys = graph->tx_data.buf[hdr->token].phys;
		mutex_unlock(&graph->lock);

		if (upper_32_bits(phys) == rd_done->buf_addr_msw &&
		    lower_32_bits(phys) == rd_done->buf_addr_lsw) {
			graph->result.opcode = hdr->opcode;
			graph->result.status = rd_done->status;
			if (graph->cb)
				graph->cb(client_event, hdr->token, data->payload, graph->priv);
		} else {
			dev_err(dev, "RD BUFF Unexpected addr %08x-%08x\n", rd_done->buf_addr_lsw,
				rd_done->buf_addr_msw);
		}
		break;
	case DATA_CMD_WR_SH_MEM_EP_EOS_RENDERED:
		break;
	case GPR_BASIC_RSP_RESULT:
		switch (result->opcode) {
		case APM_CMD_SHARED_MEM_UNMAP_REGIONS:
			graph->result.opcode = result->opcode;
			graph->result.status = 0;
			if (hdr->token == SNDRV_PCM_STREAM_PLAYBACK)
				graph->rx_data.mem_map_handle = 0;
			else
				graph->tx_data.mem_map_handle = 0;

			wake_up(&graph->cmd_wait);
			break;
		case APM_CMD_SHARED_MEM_MAP_REGIONS:
		case DATA_CMD_WR_SH_MEM_EP_MEDIA_FORMAT:
		case APM_CMD_SET_CFG:
			graph->result.opcode = result->opcode;
			graph->result.status = result->status;
			if (result->status)
				dev_err(dev, "Error (%d) Processing 0x%08x cmd\n",
					result->status, result->opcode);
			wake_up(&graph->cmd_wait);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

struct q6apm_graph *q6apm_graph_open(struct device *dev, q6apm_cb cb,
				     void *priv, int graph_id)
{
	struct q6apm *apm = dev_get_drvdata(dev->parent);
	struct audioreach_graph *ar_graph;
	struct q6apm_graph *graph;
	int ret;

	ar_graph = q6apm_get_audioreach_graph(apm, graph_id);
	if (IS_ERR(ar_graph)) {
		dev_err(dev, "No graph found with id %d\n", graph_id);
		return ERR_CAST(ar_graph);
	}

	graph = kzalloc(sizeof(*graph), GFP_KERNEL);
	if (!graph) {
		ret = -ENOMEM;
		goto put_ar_graph;
	}

	graph->apm = apm;
	graph->priv = priv;
	graph->cb = cb;
	graph->info = ar_graph->info;
	graph->ar_graph = ar_graph;
	graph->id = ar_graph->id;
	graph->dev = dev;

	mutex_init(&graph->lock);
	init_waitqueue_head(&graph->cmd_wait);

	graph->port = gpr_alloc_port(apm->gdev, dev, graph_callback, graph);
	if (IS_ERR(graph->port)) {
		ret = PTR_ERR(graph->port);
		goto free_graph;
	}

	return graph;

free_graph:
	kfree(graph);
put_ar_graph:
	kref_put(&ar_graph->refcount, q6apm_put_audioreach_graph);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(q6apm_graph_open);

int q6apm_graph_close(struct q6apm_graph *graph)
{
	struct audioreach_graph *ar_graph = graph->ar_graph;

	gpr_free_port(graph->port);
	kref_put(&ar_graph->refcount, q6apm_put_audioreach_graph);
	kfree(graph);

	return 0;
}
EXPORT_SYMBOL_GPL(q6apm_graph_close);

int q6apm_graph_prepare(struct q6apm_graph *graph)
{
	return audioreach_graph_mgmt_cmd(graph->ar_graph, APM_CMD_GRAPH_PREPARE);
}
EXPORT_SYMBOL_GPL(q6apm_graph_prepare);

int q6apm_graph_start(struct q6apm_graph *graph)
{
	struct audioreach_graph *ar_graph = graph->ar_graph;
	int ret = 0;

	if (ar_graph->start_count == 0)
		ret = audioreach_graph_mgmt_cmd(ar_graph, APM_CMD_GRAPH_START);

	ar_graph->start_count++;

	return ret;
}
EXPORT_SYMBOL_GPL(q6apm_graph_start);

int q6apm_graph_stop(struct q6apm_graph *graph)
{
	struct audioreach_graph *ar_graph = graph->ar_graph;

	if (--ar_graph->start_count > 0)
		return 0;

	return audioreach_graph_mgmt_cmd(ar_graph, APM_CMD_GRAPH_STOP);
}
EXPORT_SYMBOL_GPL(q6apm_graph_stop);

int q6apm_graph_flush(struct q6apm_graph *graph)
{
	return audioreach_graph_mgmt_cmd(graph->ar_graph, APM_CMD_GRAPH_FLUSH);
}
EXPORT_SYMBOL_GPL(q6apm_graph_flush);

static int q6apm_audio_probe(struct snd_soc_component *component)
{
	return audioreach_tplg_init(component);
}

static void q6apm_audio_remove(struct snd_soc_component *component)
{
	/* remove topology */
	snd_soc_tplg_component_remove(component);
}

#define APM_AUDIO_DRV_NAME "q6apm-audio"

static const struct snd_soc_component_driver q6apm_audio_component = {
	.name		= APM_AUDIO_DRV_NAME,
	.probe		= q6apm_audio_probe,
	.remove		= q6apm_audio_remove,
};

static int apm_probe(gpr_device_t *gdev)
{
	struct device *dev = &gdev->dev;
	struct q6apm *apm;
	int ret;

	apm = devm_kzalloc(dev, sizeof(*apm), GFP_KERNEL);
	if (!apm)
		return -ENOMEM;

	dev_set_drvdata(dev, apm);

	mutex_init(&apm->lock);
	apm->dev = dev;
	apm->gdev = gdev;
	init_waitqueue_head(&apm->wait);

	INIT_LIST_HEAD(&apm->widget_list);
	idr_init(&apm->graph_idr);
	idr_init(&apm->graph_info_idr);
	idr_init(&apm->sub_graphs_idr);
	idr_init(&apm->containers_idr);

	idr_init(&apm->modules_idr);

	g_apm = apm;

	q6apm_get_apm_state(apm);

	ret = devm_snd_soc_register_component(dev, &q6apm_audio_component, NULL, 0);
	if (ret < 0) {
		dev_err(dev, "failed to register q6apm: %d\n", ret);
		return ret;
	}

	return of_platform_populate(dev->of_node, NULL, NULL, dev);
}

struct audioreach_module *q6apm_find_module_by_mid(struct q6apm_graph *graph, uint32_t mid)
{
	struct audioreach_graph_info *info = graph->info;
	struct q6apm *apm = graph->apm;

	return __q6apm_find_module_by_mid(apm, info, mid);

}

static int apm_callback(struct gpr_resp_pkt *data, void *priv, int op)
{
	gpr_device_t *gdev = priv;
	struct q6apm *apm = dev_get_drvdata(&gdev->dev);
	struct device *dev = &gdev->dev;
	struct gpr_ibasic_rsp_result_t *result;
	struct gpr_hdr *hdr = &data->hdr;

	result = data->payload;

	switch (hdr->opcode) {
	case APM_CMD_RSP_GET_SPF_STATE:
		apm->result.opcode = hdr->opcode;
		apm->result.status = 0;
		/* First word of result it state */
		apm->state = result->opcode;
		wake_up(&apm->wait);
		break;
	case GPR_BASIC_RSP_RESULT:
		switch (result->opcode) {
		case APM_CMD_GRAPH_START:
		case APM_CMD_GRAPH_OPEN:
		case APM_CMD_GRAPH_PREPARE:
		case APM_CMD_GRAPH_CLOSE:
		case APM_CMD_GRAPH_FLUSH:
		case APM_CMD_GRAPH_STOP:
		case APM_CMD_SET_CFG:
			apm->result.opcode = result->opcode;
			apm->result.status = result->status;
			if (result->status)
				dev_err(dev, "Error (%d) Processing 0x%08x cmd\n", result->status,
					result->opcode);
			wake_up(&apm->wait);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id apm_device_id[]  = {
	{ .compatible = "qcom,q6apm" },
	{},
};
MODULE_DEVICE_TABLE(of, apm_device_id);
#endif

static gpr_driver_t apm_driver = {
	.probe = apm_probe,
	.gpr_callback = apm_callback,
	.driver = {
		.name = "qcom-apm",
		.of_match_table = of_match_ptr(apm_device_id),
	},
};

module_gpr_driver(apm_driver);
MODULE_DESCRIPTION("Audio Process Manager");
MODULE_LICENSE("GPL");
