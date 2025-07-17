// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/of.h>
#include <linux/usb.h>

#include <sound/jack.h>
#include <sound/soc-usb.h>

#include "../usb/card.h"

static DEFINE_MUTEX(ctx_mutex);
static LIST_HEAD(usb_ctx_list);

static struct device_node *snd_soc_find_phandle(struct device *dev)
{
	struct device_node *node;

	node = of_parse_phandle(dev->of_node, "usb-soc-be", 0);
	if (!node)
		return ERR_PTR(-ENODEV);

	return node;
}

static struct snd_soc_usb *snd_soc_usb_ctx_lookup(struct device_node *node)
{
	struct snd_soc_usb *ctx;

	if (!node)
		return NULL;

	list_for_each_entry(ctx, &usb_ctx_list, list) {
		if (ctx->component->dev->of_node == node)
			return ctx;
	}

	return NULL;
}

static struct snd_soc_usb *snd_soc_find_usb_ctx(struct device *dev)
{
	struct snd_soc_usb *ctx;
	struct device_node *node;

	node = snd_soc_find_phandle(dev);
	if (!IS_ERR(node)) {
		ctx = snd_soc_usb_ctx_lookup(node);
		of_node_put(node);
	} else {
		ctx = snd_soc_usb_ctx_lookup(dev->of_node);
	}

	return ctx ? ctx : NULL;
}

/* SOC USB sound kcontrols */
/**
 * snd_soc_usb_setup_offload_jack() - Create USB offloading jack
 * @component: USB DPCM backend DAI component
 * @jack: jack structure to create
 *
 * Creates a jack device for notifying userspace of the availability
 * of an offload capable device.
 *
 * Returns 0 on success, negative on error.
 *
 */
int snd_soc_usb_setup_offload_jack(struct snd_soc_component *component,
				   struct snd_soc_jack *jack)
{
	int ret;

	ret = snd_soc_card_jack_new(component->card, "USB Offload Jack",
				    SND_JACK_USB, jack);
	if (ret < 0) {
		dev_err(component->card->dev, "Unable to add USB offload jack: %d\n",
			ret);
		return ret;
	}

	ret = snd_soc_component_set_jack(component, jack, NULL);
	if (ret) {
		dev_err(component->card->dev, "Failed to set jack: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_usb_setup_offload_jack);

/**
 * snd_soc_usb_update_offload_route - Find active USB offload path
 * @dev: USB device to get offload status
 * @card: USB card index
 * @pcm: USB PCM device index
 * @direction: playback or capture direction
 * @path: pcm or card index
 * @route: pointer to route output array
 *
 * Fetch the current status for the USB SND card and PCM device indexes
 * specified.  The "route" argument should be an array of integers being
 * used for a kcontrol output.  The first element should have the selected
 * card index, and the second element should have the selected pcm device
 * index.
 */
int snd_soc_usb_update_offload_route(struct device *dev, int card, int pcm,
				     int direction, enum snd_soc_usb_kctl path,
				     long *route)
{
	struct snd_soc_usb *ctx;
	int ret = -ENODEV;

	mutex_lock(&ctx_mutex);
	ctx = snd_soc_find_usb_ctx(dev);
	if (!ctx)
		goto exit;

	if (ctx->update_offload_route_info)
		ret = ctx->update_offload_route_info(ctx->component, card, pcm,
						     direction, path, route);
exit:
	mutex_unlock(&ctx_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_usb_update_offload_route);

/**
 * snd_soc_usb_find_priv_data() - Retrieve private data stored
 * @usbdev: device reference
 *
 * Fetch the private data stored in the USB SND SoC structure.
 *
 */
void *snd_soc_usb_find_priv_data(struct device *usbdev)
{
	struct snd_soc_usb *ctx;

	mutex_lock(&ctx_mutex);
	ctx = snd_soc_find_usb_ctx(usbdev);
	mutex_unlock(&ctx_mutex);

	return ctx ? ctx->priv_data : NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_usb_find_priv_data);

/**
 * snd_soc_usb_find_supported_format() - Check if audio format is supported
 * @card_idx: USB sound chip array index
 * @params: PCM parameters
 * @direction: capture or playback
 *
 * Ensure that a requested audio profile from the ASoC side is able to be
 * supported by the USB device.
 *
 * Return 0 on success, negative on error.
 *
 */
int snd_soc_usb_find_supported_format(int card_idx,
				      struct snd_pcm_hw_params *params,
				      int direction)
{
	struct snd_usb_stream *as;

	as = snd_usb_find_suppported_substream(card_idx, params, direction);
	if (!as)
		return -EOPNOTSUPP;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_usb_find_supported_format);

/**
 * snd_soc_usb_allocate_port() - allocate a SoC USB port for offloading support
 * @component: USB DPCM backend DAI component
 * @data: private data
 *
 * Allocate and initialize a SoC USB port.  The SoC USB port is used to communicate
 * different USB audio devices attached, in order to start audio offloading handled
 * by an ASoC entity.  USB device plug in/out events are signaled with a
 * notification, but don't directly impact the memory allocated for the SoC USB
 * port.
 *
 */
struct snd_soc_usb *snd_soc_usb_allocate_port(struct snd_soc_component *component,
					      void *data)
{
	struct snd_soc_usb *usb;

	usb = kzalloc(sizeof(*usb), GFP_KERNEL);
	if (!usb)
		return ERR_PTR(-ENOMEM);

	usb->component = component;
	usb->priv_data = data;

	return usb;
}
EXPORT_SYMBOL_GPL(snd_soc_usb_allocate_port);

/**
 * snd_soc_usb_free_port() - free a SoC USB port used for offloading support
 * @usb: allocated SoC USB port
 *
 * Free and remove the SoC USB port from the available list of ports.  This will
 * ensure that the communication between USB SND and ASoC is halted.
 *
 */
void snd_soc_usb_free_port(struct snd_soc_usb *usb)
{
	snd_soc_usb_remove_port(usb);
	kfree(usb);
}
EXPORT_SYMBOL_GPL(snd_soc_usb_free_port);

/**
 * snd_soc_usb_add_port() - Add a USB backend port
 * @usb: soc usb port to add
 *
 * Register a USB backend DAI link to the USB SoC framework.  Memory is allocated
 * as part of the USB backend DAI link.
 *
 */
void snd_soc_usb_add_port(struct snd_soc_usb *usb)
{
	mutex_lock(&ctx_mutex);
	list_add_tail(&usb->list, &usb_ctx_list);
	mutex_unlock(&ctx_mutex);

	snd_usb_rediscover_devices();
}
EXPORT_SYMBOL_GPL(snd_soc_usb_add_port);

/**
 * snd_soc_usb_remove_port() - Remove a USB backend port
 * @usb: soc usb port to remove
 *
 * Remove a USB backend DAI link from USB SoC.  Memory is freed when USB backend
 * DAI is removed, or when snd_soc_usb_free_port() is called.
 *
 */
void snd_soc_usb_remove_port(struct snd_soc_usb *usb)
{
	struct snd_soc_usb *ctx, *tmp;

	mutex_lock(&ctx_mutex);
	list_for_each_entry_safe(ctx, tmp, &usb_ctx_list, list) {
		if (ctx == usb) {
			list_del(&ctx->list);
			break;
		}
	}
	mutex_unlock(&ctx_mutex);
}
EXPORT_SYMBOL_GPL(snd_soc_usb_remove_port);

/**
 * snd_soc_usb_connect() - Notification of USB device connection
 * @usbdev: USB bus device
 * @sdev: USB SND device to add
 *
 * Notify of a new USB SND device connection.  The sdev->card_idx can be used to
 * handle how the DPCM backend selects, which device to enable USB offloading
 * on.
 *
 */
int snd_soc_usb_connect(struct device *usbdev, struct snd_soc_usb_device *sdev)
{
	struct snd_soc_usb *ctx;

	if (!usbdev)
		return -ENODEV;

	mutex_lock(&ctx_mutex);
	ctx = snd_soc_find_usb_ctx(usbdev);
	if (!ctx)
		goto exit;

	if (ctx->connection_status_cb)
		ctx->connection_status_cb(ctx, sdev, true);

exit:
	mutex_unlock(&ctx_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_usb_connect);

/**
 * snd_soc_usb_disconnect() - Notification of USB device disconnection
 * @usbdev: USB bus device
 * @sdev: USB SND device to remove
 *
 * Notify of a new USB SND device disconnection to the USB backend.
 *
 */
int snd_soc_usb_disconnect(struct device *usbdev, struct snd_soc_usb_device *sdev)
{
	struct snd_soc_usb *ctx;

	if (!usbdev)
		return -ENODEV;

	mutex_lock(&ctx_mutex);
	ctx = snd_soc_find_usb_ctx(usbdev);
	if (!ctx)
		goto exit;

	if (ctx->connection_status_cb)
		ctx->connection_status_cb(ctx, sdev, false);

exit:
	mutex_unlock(&ctx_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_usb_disconnect);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SoC USB driver for offloading");
