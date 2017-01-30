/*
 *  intel_hdmi_lpe_audio.c - Intel HDMI LPE audio driver for Atom platforms
 *
 *  Copyright (C) 2016 Intel Corp
 *  Authors:
 *		Jerome Anand <jerome.anand@intel.com>
 *		Aravind Siddappaji <aravindx.siddappaji@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/platform_device.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <drm/intel_lpe_audio.h>
#include "intel_hdmi_lpe_audio.h"
#include "intel_hdmi_audio.h"

/* globals*/
static struct platform_device *hlpe_pdev;
static int hlpe_state;
static union otm_hdmi_eld_t hlpe_eld;

struct hdmi_lpe_audio_ctx {
	int irq;
	void __iomem *mmio_start;
	had_event_call_back had_event_callbacks;
	struct snd_intel_had_interface *had_interface;
	void *had_pvt_data;
	int tmds_clock_speed;
	bool dp_output;
	int link_rate;
	unsigned int had_config_offset;
	int hdmi_audio_interrupt_mask;
	struct work_struct hdmi_audio_wq;
};

static void hdmi_set_eld(void *eld)
{
	int size;

	BUILD_BUG_ON(sizeof(hlpe_eld) > HDMI_MAX_ELD_BYTES);

	size = sizeof(hlpe_eld);
	memcpy((void *)&hlpe_eld, eld, size);
}

static int hdmi_get_eld(void *eld)
{
	u8 *eld_data = (u8 *)&hlpe_eld;

	memcpy(eld, (void *)&hlpe_eld, sizeof(hlpe_eld));

	print_hex_dump_bytes("eld: ", DUMP_PREFIX_NONE, eld_data,
			sizeof(hlpe_eld));
	return 0;
}


static struct hdmi_lpe_audio_ctx *get_hdmi_context(void)
{
	struct hdmi_lpe_audio_ctx *ctx;

	ctx = platform_get_drvdata(hlpe_pdev);
	return ctx;
}

/*
 * return whether HDMI audio device is busy.
 */
bool mid_hdmi_audio_is_busy(void *ddev)
{
	struct hdmi_lpe_audio_ctx *ctx;
	int hdmi_audio_busy = 0;
	struct hdmi_audio_event hdmi_audio_event;

	dev_dbg(&hlpe_pdev->dev, "%s: Enter",  __func__);

	ctx = platform_get_drvdata(hlpe_pdev);

	if (hlpe_state == hdmi_connector_status_disconnected) {
		/* HDMI is not connected, assuming audio device is idle. */
		return false;
	}

	if (ctx->had_interface) {
		hdmi_audio_event.type = HAD_EVENT_QUERY_IS_AUDIO_BUSY;
		hdmi_audio_busy = ctx->had_interface->query(
				ctx->had_pvt_data,
				hdmi_audio_event);
		return hdmi_audio_busy != 0;
	}
	return false;
}

/*
 * return true if HDMI audio device is suspended/ disconnected
 */
bool mid_hdmi_audio_suspend(void *ddev)
{
	struct hdmi_lpe_audio_ctx *ctx;
	struct hdmi_audio_event hdmi_audio_event;
	int ret = 0;

	ctx = platform_get_drvdata(hlpe_pdev);

	if (hlpe_state == hdmi_connector_status_disconnected) {
		/* HDMI is not connected, assuming audio device
		 * is suspended already.
		 */
		return true;
	}

	dev_dbg(&hlpe_pdev->dev, "%s: hlpe_state %d",  __func__,
			hlpe_state);

	if (ctx->had_interface) {
		hdmi_audio_event.type = 0;
		ret = ctx->had_interface->suspend(ctx->had_pvt_data,
				hdmi_audio_event);
		return (ret == 0) ? true : false;
	}
	return true;
}

void mid_hdmi_audio_resume(void *ddev)
{
	struct hdmi_lpe_audio_ctx *ctx;

	ctx = platform_get_drvdata(hlpe_pdev);

	if (hlpe_state == hdmi_connector_status_disconnected) {
		/* HDMI is not connected, there is no need
		 * to resume audio device.
		 */
		return;
	}

	dev_dbg(&hlpe_pdev->dev, "%s: hlpe_state %d",  __func__, hlpe_state);

	if (ctx->had_interface)
		ctx->had_interface->resume(ctx->had_pvt_data);
}

void mid_hdmi_audio_signal_event(enum had_event_type event)
{
	struct hdmi_lpe_audio_ctx *ctx;

	dev_dbg(&hlpe_pdev->dev, "%s: Enter\n", __func__);

	ctx = platform_get_drvdata(hlpe_pdev);

	/* The handler is protected in the respective
	 * event handlers to avoid races
	 */
	if (ctx->had_event_callbacks)
		(*ctx->had_event_callbacks)(event,
			ctx->had_pvt_data);
}

/*
 * used to write into display controller HDMI audio registers.
 */
int mid_hdmi_audio_write(u32 reg, u32 val)
{
	struct hdmi_lpe_audio_ctx *ctx;

	ctx = platform_get_drvdata(hlpe_pdev);

	dev_dbg(&hlpe_pdev->dev, "%s: reg[0x%x] = 0x%x\n", __func__, reg, val);

	if (ctx->dp_output) {
		if (reg == AUD_CONFIG && (val & AUD_CONFIG_VALID_BIT))
			val |= AUD_CONFIG_DP_MODE | AUD_CONFIG_BLOCK_BIT;
	}
	iowrite32(val, ctx->mmio_start + ctx->had_config_offset + reg);

	return 0;
}

/*
 * used to get the register value read from
 * display controller HDMI audio registers.
 */
int mid_hdmi_audio_read(u32 reg, u32 *val)
{
	struct hdmi_lpe_audio_ctx *ctx;

	ctx = platform_get_drvdata(hlpe_pdev);
	*val = ioread32(ctx->mmio_start + ctx->had_config_offset + reg);
	dev_dbg(&hlpe_pdev->dev, "%s: reg[0x%x] = 0x%x\n", __func__, reg, *val);
	return 0;
}

/*
 * used to update the masked bits in display controller HDMI
 * audio registers.
 */
int mid_hdmi_audio_rmw(u32 reg, u32 val, u32 mask)
{
	struct hdmi_lpe_audio_ctx *ctx;
	u32 val_tmp = 0;

	ctx = platform_get_drvdata(hlpe_pdev);

	val_tmp = ioread32(ctx->mmio_start + ctx->had_config_offset + reg);
	val_tmp &= ~mask;
	val_tmp |= (val & mask);

	if (ctx->dp_output) {
		if (reg == AUD_CONFIG && (val_tmp & AUD_CONFIG_VALID_BIT))
			val_tmp |= AUD_CONFIG_DP_MODE | AUD_CONFIG_BLOCK_BIT;
	}

	iowrite32(val_tmp, ctx->mmio_start + ctx->had_config_offset + reg);
	dev_dbg(&hlpe_pdev->dev, "%s: reg[0x%x] = 0x%x\n", __func__,
				reg, val_tmp);

	return 0;
}

/*
 * used to return the HDMI audio capabilities.
 * e.g. resolution, frame rate.
 */
int mid_hdmi_audio_get_caps(enum had_caps_list get_element,
			    void *capabilities)
{
	struct hdmi_lpe_audio_ctx *ctx;
	int ret = 0;

	ctx = get_hdmi_context();

	dev_dbg(&hlpe_pdev->dev, "%s: Enter\n", __func__);

	switch (get_element) {
	case HAD_GET_ELD:
		ret = hdmi_get_eld(capabilities);
		break;
	case HAD_GET_DISPLAY_RATE:
		/* ToDo: Verify if sampling freq logic is correct */
		*(u32 *)capabilities = ctx->tmds_clock_speed;
		dev_dbg(&hlpe_pdev->dev, "%s: tmds_clock_speed = 0x%x\n",
			__func__, ctx->tmds_clock_speed);
		break;
	case HAD_GET_LINK_RATE:
		/* ToDo: Verify if sampling freq logic is correct */
		*(u32 *)capabilities = ctx->link_rate;
		dev_dbg(&hlpe_pdev->dev, "%s: link rate = 0x%x\n",
			__func__, ctx->link_rate);
		break;
	case HAD_GET_DP_OUTPUT:
		*(u32 *)capabilities = ctx->dp_output;
		dev_dbg(&hlpe_pdev->dev, "%s: dp_output = %d\n",
			__func__, ctx->dp_output);
		break;
	default:
		break;
	}

	return ret;
}

/*
 * used to set the HDMI audio capabilities.
 * e.g. Audio INT.
 */
int mid_hdmi_audio_set_caps(enum had_caps_list set_element,
			    void *capabilties)
{
	struct hdmi_lpe_audio_ctx *ctx;

	ctx = platform_get_drvdata(hlpe_pdev);

	dev_dbg(&hlpe_pdev->dev, "%s: cap_id = 0x%x\n", __func__, set_element);

	switch (set_element) {
	case HAD_SET_ENABLE_AUDIO_INT:
		{
			u32 status_reg;

			mid_hdmi_audio_read(AUD_HDMI_STATUS_v2, &status_reg);
			status_reg |=
				HDMI_AUDIO_BUFFER_DONE | HDMI_AUDIO_UNDERRUN;
			mid_hdmi_audio_write(AUD_HDMI_STATUS_v2, status_reg);
			mid_hdmi_audio_read(AUD_HDMI_STATUS_v2, &status_reg);
		}
		break;
	default:
		break;
	}

	return 0;
}

int mid_hdmi_audio_setup(had_event_call_back audio_callbacks)
{
	struct hdmi_lpe_audio_ctx *ctx;

	ctx = platform_get_drvdata(hlpe_pdev);

	dev_dbg(&hlpe_pdev->dev, "%s: called\n",  __func__);

	ctx->had_event_callbacks = audio_callbacks;

	return 0;
}

static void _had_wq(struct work_struct *work)
{
	mid_hdmi_audio_signal_event(HAD_EVENT_HOT_PLUG);
}

int mid_hdmi_audio_register(struct snd_intel_had_interface *driver,
				void *had_data)
{
	struct hdmi_lpe_audio_ctx *ctx;

	ctx = platform_get_drvdata(hlpe_pdev);

	dev_dbg(&hlpe_pdev->dev, "%s: called\n", __func__);

	ctx->had_pvt_data = had_data;
	ctx->had_interface = driver;

	/* The Audio driver is loading now and we need to notify
	 * it if there is an HDMI device attached
	 */
	INIT_WORK(&ctx->hdmi_audio_wq, _had_wq);
	dev_dbg(&hlpe_pdev->dev, "%s: Scheduling HDMI audio work queue\n",
				__func__);
	schedule_work(&ctx->hdmi_audio_wq);

	return 0;
}

static irqreturn_t display_pipe_interrupt_handler(int irq, void *dev_id)
{
	u32 audio_stat, audio_reg;

	struct hdmi_lpe_audio_ctx *ctx;

	dev_dbg(&hlpe_pdev->dev, "%s: Enter\n", __func__);

	ctx = platform_get_drvdata(hlpe_pdev);

	audio_reg = AUD_HDMI_STATUS_v2;
	mid_hdmi_audio_read(audio_reg, &audio_stat);

	if (audio_stat & HDMI_AUDIO_UNDERRUN) {
		mid_hdmi_audio_write(audio_reg, HDMI_AUDIO_UNDERRUN);
		mid_hdmi_audio_signal_event(
				HAD_EVENT_AUDIO_BUFFER_UNDERRUN);
	}

	if (audio_stat & HDMI_AUDIO_BUFFER_DONE) {
		mid_hdmi_audio_write(audio_reg, HDMI_AUDIO_BUFFER_DONE);
		mid_hdmi_audio_signal_event(
				HAD_EVENT_AUDIO_BUFFER_DONE);
	}

	return IRQ_HANDLED;
}

static void notify_audio_lpe(struct platform_device *pdev)
{
	struct hdmi_lpe_audio_ctx *ctx = platform_get_drvdata(pdev);
	struct intel_hdmi_lpe_audio_pdata *pdata = pdev->dev.platform_data;

	if (pdata->hdmi_connected != true) {

		dev_dbg(&pdev->dev, "%s: Event: HAD_NOTIFY_HOT_UNPLUG\n",
			__func__);

		if (hlpe_state == hdmi_connector_status_connected) {

			hlpe_state =
				hdmi_connector_status_disconnected;

			mid_hdmi_audio_signal_event(
				HAD_EVENT_HOT_UNPLUG);
		} else
			dev_dbg(&pdev->dev, "%s: Already Unplugged!\n",
				__func__);

	} else {
		struct intel_hdmi_lpe_audio_eld *eld = &pdata->eld;

		switch (eld->pipe_id) {
		case 0:
			ctx->had_config_offset = AUDIO_HDMI_CONFIG_A;
			break;
		case 1:
			ctx->had_config_offset = AUDIO_HDMI_CONFIG_B;
			break;
		case 2:
			ctx->had_config_offset = AUDIO_HDMI_CONFIG_C;
			break;
		default:
			dev_dbg(&pdev->dev, "Invalid pipe %d\n",
				eld->pipe_id);
			break;
		}

		hdmi_set_eld(eld->eld_data);

		mid_hdmi_audio_signal_event(HAD_EVENT_HOT_PLUG);

		hlpe_state = hdmi_connector_status_connected;

		dev_dbg(&pdev->dev, "%s: HAD_NOTIFY_ELD : port = %d, tmds = %d\n",
			__func__, eld->port_id,	pdata->tmds_clock_speed);

		if (pdata->tmds_clock_speed) {
			ctx->tmds_clock_speed = pdata->tmds_clock_speed;
			ctx->dp_output = pdata->dp_output;
			ctx->link_rate = pdata->link_rate;
			mid_hdmi_audio_signal_event(HAD_EVENT_MODE_CHANGING);
		}
	}
}

/**
 * hdmi_lpe_audio_probe - start bridge with i915
 *
 * This function is called when the i915 driver creates the
 * hdmi-lpe-audio platform device. Card creation is deferred until a
 * hot plug event is received
 */
static int hdmi_lpe_audio_probe(struct platform_device *pdev)
{
	struct hdmi_lpe_audio_ctx *ctx;
	struct intel_hdmi_lpe_audio_pdata *pdata;
	int irq;
	struct resource *res_mmio;
	void __iomem *mmio_start;
	int ret = 0;
	unsigned long flag_irq;
	static const struct pci_device_id cherryview_ids[] = {
		{PCI_DEVICE(0x8086, 0x22b0)},
		{PCI_DEVICE(0x8086, 0x22b1)},
		{PCI_DEVICE(0x8086, 0x22b2)},
		{PCI_DEVICE(0x8086, 0x22b3)},
		{}
	};

	dev_dbg(&hlpe_pdev->dev, "Enter %s\n", __func__);

	/*TBD:remove globals*/
	hlpe_pdev = pdev;
	hlpe_state = hdmi_connector_status_disconnected;

	/* get resources */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&hlpe_pdev->dev, "Could not get irq resource\n");
		return -ENODEV;
	}

	res_mmio = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mmio) {
		dev_err(&hlpe_pdev->dev, "Could not get IO_MEM resources\n");
		return -ENXIO;
	}

	dev_dbg(&hlpe_pdev->dev, "%s: mmio_start = 0x%x, mmio_end = 0x%x\n",
		__func__, (unsigned int)res_mmio->start,
		(unsigned int)res_mmio->end);

	mmio_start = ioremap_nocache(res_mmio->start,
				     (size_t)(resource_size(res_mmio)));
	if (!mmio_start) {
		dev_err(&hlpe_pdev->dev, "Could not get ioremap\n");
		return -EACCES;
	}

	/* setup interrupt handler */
	ret = request_irq(irq, display_pipe_interrupt_handler,
			0,
			pdev->name,
			NULL);
	if (ret < 0) {
		dev_err(&hlpe_pdev->dev, "request_irq failed\n");
		iounmap(mmio_start);
		return -ENODEV;
	}

	/* alloc and save context */
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL) {
		free_irq(irq, NULL);
		iounmap(mmio_start);
		return -ENOMEM;
	}

	ctx->irq = irq;
	dev_dbg(&hlpe_pdev->dev, "hdmi lpe audio: irq num = %d\n", irq);
	ctx->mmio_start = mmio_start;
	ctx->tmds_clock_speed = DIS_SAMPLE_RATE_148_5;

	if (pci_dev_present(cherryview_ids))
		dev_dbg(&hlpe_pdev->dev, "%s: Cherrytrail LPE - Detected\n",
				__func__);
	else
		dev_dbg(&hlpe_pdev->dev, "%s: Baytrail LPE - Assume\n",
				__func__);

	/* assume pipe A as default */
	ctx->had_config_offset = AUDIO_HDMI_CONFIG_A;

	pdata = pdev->dev.platform_data;

	if (pdata == NULL) {
		dev_err(&hlpe_pdev->dev, "%s: quit: pdata not allocated by i915!!\n", __func__);
		kfree(ctx);
		free_irq(irq, NULL);
		iounmap(mmio_start);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, ctx);

	ret = hdmi_audio_probe((void *)pdev);
	dev_dbg(&hlpe_pdev->dev, "hdmi lpe audio: setting pin eld notify callback\n");

	spin_lock_irqsave(&pdata->lpe_audio_slock, flag_irq);
	pdata->notify_audio_lpe = notify_audio_lpe;
	if (pdata->notify_pending) {

		dev_dbg(&hlpe_pdev->dev, "%s: handle pending notification\n", __func__);
		notify_audio_lpe(pdev);
		pdata->notify_pending = false;
	}
	spin_unlock_irqrestore(&pdata->lpe_audio_slock, flag_irq);

	return ret;
}

/**
 * hdmi_lpe_audio_remove - stop bridge with i915
 *
 * This function is called when the platform device is destroyed. The sound
 * card should have been removed on hot plug event.
 */
static int hdmi_lpe_audio_remove(struct platform_device *pdev)
{
	struct hdmi_lpe_audio_ctx *ctx;

	dev_dbg(&hlpe_pdev->dev, "Enter %s\n", __func__);

	hdmi_audio_remove(pdev);

	/* get context, release resources */
	ctx = platform_get_drvdata(pdev);
	iounmap(ctx->mmio_start);
	free_irq(ctx->irq, NULL);
	kfree(ctx);
	return 0;
}

static int hdmi_lpe_audio_suspend(struct platform_device *pt_dev,
				pm_message_t state)
{
	dev_dbg(&hlpe_pdev->dev, "Enter %s\n", __func__);
	mid_hdmi_audio_suspend(NULL);
	return 0;
}

static int hdmi_lpe_audio_resume(struct platform_device *pt_dev)
{
	dev_dbg(&hlpe_pdev->dev, "Enter %s\n", __func__);
	mid_hdmi_audio_resume(NULL);
	return 0;
}

static struct platform_driver hdmi_lpe_audio_driver = {
	.driver		= {
		.name  = "hdmi-lpe-audio",
	},
	.probe          = hdmi_lpe_audio_probe,
	.remove		= hdmi_lpe_audio_remove,
	.suspend	= hdmi_lpe_audio_suspend,
	.resume		= hdmi_lpe_audio_resume
};

module_platform_driver(hdmi_lpe_audio_driver);
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:hdmi_lpe_audio");
