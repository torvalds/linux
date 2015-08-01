/*
 *  skl-message.c - HDA DSP interface for FW registration, Pipe and Module
 *  configurations
 *
 *  Copyright (C) 2015 Intel Corp
 *  Author:Rafal Redzimski <rafal.f.redzimski@intel.com>
 *	   Jeeja KP <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "skl-sst-dsp.h"
#include "skl-sst-ipc.h"
#include "skl.h"
#include "../common/sst-dsp.h"
#include "../common/sst-dsp-priv.h"
#include "skl-topology.h"
#include "skl-tplg-interface.h"

static int skl_alloc_dma_buf(struct device *dev,
		struct snd_dma_buffer *dmab, size_t size)
{
	struct hdac_ext_bus *ebus = dev_get_drvdata(dev);
	struct hdac_bus *bus = ebus_to_hbus(ebus);

	if (!bus)
		return -ENODEV;

	return  bus->io_ops->dma_alloc_pages(bus, SNDRV_DMA_TYPE_DEV, size, dmab);
}

static int skl_free_dma_buf(struct device *dev, struct snd_dma_buffer *dmab)
{
	struct hdac_ext_bus *ebus = dev_get_drvdata(dev);
	struct hdac_bus *bus = ebus_to_hbus(ebus);

	if (!bus)
		return -ENODEV;

	bus->io_ops->dma_free_pages(bus, dmab);

	return 0;
}

int skl_init_dsp(struct skl *skl)
{
	void __iomem *mmio_base;
	struct hdac_ext_bus *ebus = &skl->ebus;
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	int irq = bus->irq;
	struct skl_dsp_loader_ops loader_ops;
	int ret;

	loader_ops.alloc_dma_buf = skl_alloc_dma_buf;
	loader_ops.free_dma_buf = skl_free_dma_buf;

	/* enable ppcap interrupt */
	snd_hdac_ext_bus_ppcap_enable(&skl->ebus, true);
	snd_hdac_ext_bus_ppcap_int_enable(&skl->ebus, true);

	/* read the BAR of the ADSP MMIO */
	mmio_base = pci_ioremap_bar(skl->pci, 4);
	if (mmio_base == NULL) {
		dev_err(bus->dev, "ioremap error\n");
		return -ENXIO;
	}

	ret = skl_sst_dsp_init(bus->dev, mmio_base, irq,
			loader_ops, &skl->skl_sst);

	dev_dbg(bus->dev, "dsp registration status=%d\n", ret);

	return ret;
}

void skl_free_dsp(struct skl *skl)
{
	struct hdac_ext_bus *ebus = &skl->ebus;
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	struct skl_sst *ctx =  skl->skl_sst;

	/* disable  ppcap interrupt */
	snd_hdac_ext_bus_ppcap_int_enable(&skl->ebus, false);

	skl_sst_dsp_cleanup(bus->dev, ctx);
	if (ctx->dsp->addr.lpe)
		iounmap(ctx->dsp->addr.lpe);
}

int skl_suspend_dsp(struct skl *skl)
{
	struct skl_sst *ctx = skl->skl_sst;
	int ret;

	/* if ppcap is not supported return 0 */
	if (!skl->ebus.ppcap)
		return 0;

	ret = skl_dsp_sleep(ctx->dsp);
	if (ret < 0)
		return ret;

	/* disable ppcap interrupt */
	snd_hdac_ext_bus_ppcap_int_enable(&skl->ebus, false);
	snd_hdac_ext_bus_ppcap_enable(&skl->ebus, false);

	return 0;
}

int skl_resume_dsp(struct skl *skl)
{
	struct skl_sst *ctx = skl->skl_sst;

	/* if ppcap is not supported return 0 */
	if (!skl->ebus.ppcap)
		return 0;

	/* enable ppcap interrupt */
	snd_hdac_ext_bus_ppcap_enable(&skl->ebus, true);
	snd_hdac_ext_bus_ppcap_int_enable(&skl->ebus, true);

	return skl_dsp_wake(ctx->dsp);
}

enum skl_bitdepth skl_get_bit_depth(int params)
{
	switch (params) {
	case 8:
		return SKL_DEPTH_8BIT;

	case 16:
		return SKL_DEPTH_16BIT;

	case 24:
		return SKL_DEPTH_24BIT;

	case 32:
		return SKL_DEPTH_32BIT;

	default:
		return SKL_DEPTH_INVALID;

	}
}

static u32 skl_create_channel_map(enum skl_ch_cfg ch_cfg)
{
	u32 config;

	switch (ch_cfg) {
	case SKL_CH_CFG_MONO:
		config =  (0xFFFFFFF0 | SKL_CHANNEL_LEFT);
		break;

	case SKL_CH_CFG_STEREO:
		config = (0xFFFFFF00 | SKL_CHANNEL_LEFT
			| (SKL_CHANNEL_RIGHT << 4));
		break;

	case SKL_CH_CFG_2_1:
		config = (0xFFFFF000 | SKL_CHANNEL_LEFT
			| (SKL_CHANNEL_RIGHT << 4)
			| (SKL_CHANNEL_LFE << 8));
		break;

	case SKL_CH_CFG_3_0:
		config =  (0xFFFFF000 | SKL_CHANNEL_LEFT
			| (SKL_CHANNEL_CENTER << 4)
			| (SKL_CHANNEL_RIGHT << 8));
		break;

	case SKL_CH_CFG_3_1:
		config = (0xFFFF0000 | SKL_CHANNEL_LEFT
			| (SKL_CHANNEL_CENTER << 4)
			| (SKL_CHANNEL_RIGHT << 8)
			| (SKL_CHANNEL_LFE << 12));
		break;

	case SKL_CH_CFG_QUATRO:
		config = (0xFFFF0000 | SKL_CHANNEL_LEFT
			| (SKL_CHANNEL_RIGHT << 4)
			| (SKL_CHANNEL_LEFT_SURROUND << 8)
			| (SKL_CHANNEL_RIGHT_SURROUND << 12));
		break;

	case SKL_CH_CFG_4_0:
		config = (0xFFFF0000 | SKL_CHANNEL_LEFT
			| (SKL_CHANNEL_CENTER << 4)
			| (SKL_CHANNEL_RIGHT << 8)
			| (SKL_CHANNEL_CENTER_SURROUND << 12));
		break;

	case SKL_CH_CFG_5_0:
		config = (0xFFF00000 | SKL_CHANNEL_LEFT
			| (SKL_CHANNEL_CENTER << 4)
			| (SKL_CHANNEL_RIGHT << 8)
			| (SKL_CHANNEL_LEFT_SURROUND << 12)
			| (SKL_CHANNEL_RIGHT_SURROUND << 16));
		break;

	case SKL_CH_CFG_5_1:
		config = (0xFF000000 | SKL_CHANNEL_CENTER
			| (SKL_CHANNEL_LEFT << 4)
			| (SKL_CHANNEL_RIGHT << 8)
			| (SKL_CHANNEL_LEFT_SURROUND << 12)
			| (SKL_CHANNEL_RIGHT_SURROUND << 16)
			| (SKL_CHANNEL_LFE << 20));
		break;

	case SKL_CH_CFG_DUAL_MONO:
		config = (0xFFFFFF00 | SKL_CHANNEL_LEFT
			| (SKL_CHANNEL_LEFT << 4));
		break;

	case SKL_CH_CFG_I2S_DUAL_STEREO_0:
		config = (0xFFFFFF00 | SKL_CHANNEL_LEFT
			| (SKL_CHANNEL_RIGHT << 4));
		break;

	case SKL_CH_CFG_I2S_DUAL_STEREO_1:
		config = (0xFFFF00FF | (SKL_CHANNEL_LEFT << 8)
			| (SKL_CHANNEL_RIGHT << 12));
		break;

	default:
		config =  0xFFFFFFFF;
		break;

	}

	return config;
}

/*
 * Each module in DSP expects a base module configuration, which consists of
 * PCM format information, which we calculate in driver and resource values
 * which are read from widget information passed through topology binary
 * This is send when we create a module with INIT_INSTANCE IPC msg
 */
static void skl_set_base_module_format(struct skl_sst *ctx,
			struct skl_module_cfg *mconfig,
			struct skl_base_cfg *base_cfg)
{
	struct skl_module_fmt *format = &mconfig->in_fmt;

	base_cfg->audio_fmt.number_of_channels = (u8)format->channels;

	base_cfg->audio_fmt.s_freq = format->s_freq;
	base_cfg->audio_fmt.bit_depth = format->bit_depth;
	base_cfg->audio_fmt.valid_bit_depth = format->valid_bit_depth;
	base_cfg->audio_fmt.ch_cfg = format->ch_cfg;

	dev_dbg(ctx->dev, "bit_depth=%x valid_bd=%x ch_config=%x\n",
			format->bit_depth, format->valid_bit_depth,
			format->ch_cfg);

	base_cfg->audio_fmt.channel_map = skl_create_channel_map(
					base_cfg->audio_fmt.ch_cfg);

	base_cfg->audio_fmt.interleaving = SKL_INTERLEAVING_PER_CHANNEL;

	base_cfg->cps = mconfig->mcps;
	base_cfg->ibs = mconfig->ibs;
	base_cfg->obs = mconfig->obs;
}

/*
 * Copies copier capabilities into copier module and updates copier module
 * config size.
 */
static void skl_copy_copier_caps(struct skl_module_cfg *mconfig,
				struct skl_cpr_cfg *cpr_mconfig)
{
	if (mconfig->formats_config.caps_size == 0)
		return;

	memcpy(cpr_mconfig->gtw_cfg.config_data,
			mconfig->formats_config.caps,
			mconfig->formats_config.caps_size);

	cpr_mconfig->gtw_cfg.config_length =
			(mconfig->formats_config.caps_size) / 4;
}

/*
 * Calculate the gatewat settings required for copier module, type of
 * gateway and index of gateway to use
 */
static void skl_setup_cpr_gateway_cfg(struct skl_sst *ctx,
			struct skl_module_cfg *mconfig,
			struct skl_cpr_cfg *cpr_mconfig)
{
	union skl_connector_node_id node_id = {0};
	struct skl_pipe_params *params = mconfig->pipe->p_params;

	switch (mconfig->dev_type) {
	case SKL_DEVICE_BT:
		node_id.node.dma_type =
			(SKL_CONN_SOURCE == mconfig->hw_conn_type) ?
			SKL_DMA_I2S_LINK_OUTPUT_CLASS :
			SKL_DMA_I2S_LINK_INPUT_CLASS;
		node_id.node.vindex = params->host_dma_id +
					(mconfig->vbus_id << 3);
		break;

	case SKL_DEVICE_I2S:
		node_id.node.dma_type =
			(SKL_CONN_SOURCE == mconfig->hw_conn_type) ?
			SKL_DMA_I2S_LINK_OUTPUT_CLASS :
			SKL_DMA_I2S_LINK_INPUT_CLASS;
		node_id.node.vindex = params->host_dma_id +
					 (mconfig->time_slot << 1) +
					 (mconfig->vbus_id << 3);
		break;

	case SKL_DEVICE_DMIC:
		node_id.node.dma_type = SKL_DMA_DMIC_LINK_INPUT_CLASS;
		node_id.node.vindex = mconfig->vbus_id +
					 (mconfig->time_slot);
		break;

	case SKL_DEVICE_HDALINK:
		node_id.node.dma_type =
			(SKL_CONN_SOURCE == mconfig->hw_conn_type) ?
			SKL_DMA_HDA_LINK_OUTPUT_CLASS :
			SKL_DMA_HDA_LINK_INPUT_CLASS;
		node_id.node.vindex = params->link_dma_id;
		break;

	default:
		node_id.node.dma_type =
			(SKL_CONN_SOURCE == mconfig->hw_conn_type) ?
			SKL_DMA_HDA_HOST_OUTPUT_CLASS :
			SKL_DMA_HDA_HOST_INPUT_CLASS;
		node_id.node.vindex = params->host_dma_id;
		break;
	}

	cpr_mconfig->gtw_cfg.node_id = node_id.val;

	if (SKL_CONN_SOURCE == mconfig->hw_conn_type)
		cpr_mconfig->gtw_cfg.dma_buffer_size = 2 * mconfig->obs;
	else
		cpr_mconfig->gtw_cfg.dma_buffer_size = 2 * mconfig->ibs;

	cpr_mconfig->cpr_feature_mask = 0;
	cpr_mconfig->gtw_cfg.config_length  = 0;

	skl_copy_copier_caps(mconfig, cpr_mconfig);
}

static void skl_setup_out_format(struct skl_sst *ctx,
			struct skl_module_cfg *mconfig,
			struct skl_audio_data_format *out_fmt)
{
	struct skl_module_fmt *format = &mconfig->out_fmt;

	out_fmt->number_of_channels = (u8)format->channels;
	out_fmt->s_freq = format->s_freq;
	out_fmt->bit_depth = format->bit_depth;
	out_fmt->valid_bit_depth = format->valid_bit_depth;
	out_fmt->ch_cfg = format->ch_cfg;

	out_fmt->channel_map = skl_create_channel_map(out_fmt->ch_cfg);
	out_fmt->interleaving = SKL_INTERLEAVING_PER_CHANNEL;

	dev_dbg(ctx->dev, "copier out format chan=%d fre=%d bitdepth=%d\n",
		out_fmt->number_of_channels, format->s_freq, format->bit_depth);
}

/*
 * 'copier' is DSP internal module which copies data from Host DMA (HDA host
 * dma) or link (hda link, SSP, PDM)
 * Here we calculate the copier module parameters, like PCM format, output
 * format, gateway settings
 * copier_module_config is sent as input buffer with INIT_INSTANCE IPC msg
 */
static void skl_set_copier_format(struct skl_sst *ctx,
			struct skl_module_cfg *mconfig,
			struct skl_cpr_cfg *cpr_mconfig)
{
	struct skl_audio_data_format *out_fmt = &cpr_mconfig->out_fmt;
	struct skl_base_cfg *base_cfg = (struct skl_base_cfg *)cpr_mconfig;

	skl_set_base_module_format(ctx, mconfig, base_cfg);

	skl_setup_out_format(ctx, mconfig, out_fmt);
	skl_setup_cpr_gateway_cfg(ctx, mconfig, cpr_mconfig);
}

static u16 skl_get_module_param_size(struct skl_sst *ctx,
			struct skl_module_cfg *mconfig)
{
	u16 param_size;

	switch (mconfig->m_type) {
	case SKL_MODULE_TYPE_COPIER:
		param_size = sizeof(struct skl_cpr_cfg);
		param_size += mconfig->formats_config.caps_size;
		return param_size;

	default:
		/*
		 * return only base cfg when no specific module type is
		 * specified
		 */
		return sizeof(struct skl_base_cfg);
	}

	return 0;
}

/*
 * DSP firmware supports various modules like copier etc. These modules
 * required various parameters to be calculated and sent for the module
 * initialization to DSP. By default a generic module needs only base module
 * format configuration
 */
static int skl_set_module_format(struct skl_sst *ctx,
			struct skl_module_cfg *module_config,
			u16 *module_config_size,
			void **param_data)
{
	u16 param_size;

	param_size  = skl_get_module_param_size(ctx, module_config);

	*param_data = kzalloc(param_size, GFP_KERNEL);
	if (NULL == *param_data)
		return -ENOMEM;

	*module_config_size = param_size;

	switch (module_config->m_type) {
	case SKL_MODULE_TYPE_COPIER:
		skl_set_copier_format(ctx, module_config, *param_data);
		break;

	default:
		skl_set_base_module_format(ctx, module_config, *param_data);
		break;

	}

	dev_dbg(ctx->dev, "Module type=%d config size: %d bytes\n",
			module_config->id.module_id, param_size);
	print_hex_dump(KERN_DEBUG, "Module params:", DUMP_PREFIX_OFFSET, 8, 4,
			*param_data, param_size, false);
	return 0;
}

static int skl_get_queue_index(struct skl_module_pin *mpin,
				struct skl_module_inst_id id, int max)
{
	int i;

	for (i = 0; i < max; i++)  {
		if (mpin[i].id.module_id == id.module_id &&
			mpin[i].id.instance_id == id.instance_id)
			return i;
	}

	return -EINVAL;
}

/*
 * Allocates queue for each module.
 * if dynamic, the pin_index is allocated 0 to max_pin.
 * In static, the pin_index is fixed based on module_id and instance id
 */
static int skl_alloc_queue(struct skl_module_pin *mpin,
			struct skl_module_inst_id id, int max)
{
	int i;

	/*
	 * if pin in dynamic, find first free pin
	 * otherwise find match module and instance id pin as topology will
	 * ensure a unique pin is assigned to this so no need to
	 * allocate/free
	 */
	for (i = 0; i < max; i++)  {
		if (mpin[i].is_dynamic) {
			if (!mpin[i].in_use) {
				mpin[i].in_use = true;
				mpin[i].id.module_id = id.module_id;
				mpin[i].id.instance_id = id.instance_id;
				return i;
			}
		} else {
			if (mpin[i].id.module_id == id.module_id &&
				mpin[i].id.instance_id == id.instance_id)
				return i;
		}
	}

	return -EINVAL;
}

static void skl_free_queue(struct skl_module_pin *mpin, int q_index)
{
	if (mpin[q_index].is_dynamic) {
		mpin[q_index].in_use = false;
		mpin[q_index].id.module_id = 0;
		mpin[q_index].id.instance_id = 0;
	}
}
