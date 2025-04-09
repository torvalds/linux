/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __LINUX_SND_SOC_USB_H
#define __LINUX_SND_SOC_USB_H

#include <sound/soc.h>

enum snd_soc_usb_kctl {
	SND_SOC_USB_KCTL_CARD_ROUTE,
	SND_SOC_USB_KCTL_PCM_ROUTE,
};

/**
 * struct snd_soc_usb_device - SoC USB representation of a USB sound device
 * @card_idx: sound card index associated with USB device
 * @chip_idx: USB sound chip array index
 * @cpcm_idx: capture PCM index array associated with USB device
 * @ppcm_idx: playback PCM index array associated with USB device
 * @num_capture: number of capture streams
 * @num_playback: number of playback streams
 * @list: list head for SoC USB devices
 **/
struct snd_soc_usb_device {
	int card_idx;
	int chip_idx;

	/* PCM index arrays */
	unsigned int *cpcm_idx; /* TODO: capture path is not tested yet */
	unsigned int *ppcm_idx;
	int num_capture; /* TODO: capture path is not tested yet */
	int num_playback;

	struct list_head list;
};

/**
 * struct snd_soc_usb - representation of a SoC USB backend entity
 * @list: list head for SND SOC struct list
 * @component: reference to ASoC component
 * @connection_status_cb: callback to notify connection events
 * @update_offload_route_info: callback to fetch mapped ASoC card and pcm
 *			       device pair.  This is unrelated to the concept
 *			       of DAPM route.  The "route" argument carries
 *			       an array used for a kcontrol output for either
 *			       the card or pcm index.  "path" determines the
 *			       which entry to look for. (ie mapped card or pcm)
 * @priv_data: driver data
 **/
struct snd_soc_usb {
	struct list_head list;
	struct snd_soc_component *component;
	int (*connection_status_cb)(struct snd_soc_usb *usb,
				    struct snd_soc_usb_device *sdev,
				    bool connected);
	int (*update_offload_route_info)(struct snd_soc_component *component,
					 int card, int pcm, int direction,
					 enum snd_soc_usb_kctl path,
					 long *route);
	void *priv_data;
};

#if IS_ENABLED(CONFIG_SND_SOC_USB)
int snd_soc_usb_find_supported_format(int card_idx,
				      struct snd_pcm_hw_params *params,
				      int direction);

int snd_soc_usb_connect(struct device *usbdev, struct snd_soc_usb_device *sdev);
int snd_soc_usb_disconnect(struct device *usbdev, struct snd_soc_usb_device *sdev);
void *snd_soc_usb_find_priv_data(struct device *usbdev);

int snd_soc_usb_setup_offload_jack(struct snd_soc_component *component,
				   struct snd_soc_jack *jack);
int snd_soc_usb_update_offload_route(struct device *dev, int card, int pcm,
				     int direction, enum snd_soc_usb_kctl path,
				     long *route);

struct snd_soc_usb *snd_soc_usb_allocate_port(struct snd_soc_component *component,
					      void *data);
void snd_soc_usb_free_port(struct snd_soc_usb *usb);
void snd_soc_usb_add_port(struct snd_soc_usb *usb);
void snd_soc_usb_remove_port(struct snd_soc_usb *usb);
#else
static inline int
snd_soc_usb_find_supported_format(int card_idx, struct snd_pcm_hw_params *params,
				  int direction)
{
	return -EINVAL;
}

static inline int snd_soc_usb_connect(struct device *usbdev,
				      struct snd_soc_usb_device *sdev)
{
	return -ENODEV;
}

static inline int snd_soc_usb_disconnect(struct device *usbdev,
					 struct snd_soc_usb_device *sdev)
{
	return -EINVAL;
}

static inline void *snd_soc_usb_find_priv_data(struct device *usbdev)
{
	return NULL;
}

static inline int snd_soc_usb_setup_offload_jack(struct snd_soc_component *component,
						 struct snd_soc_jack *jack)
{
	return 0;
}

static int snd_soc_usb_update_offload_route(struct device *dev, int card, int pcm,
					    int direction, enum snd_soc_usb_kctl path,
					    long *route)
{
	return -ENODEV;
}

static inline struct snd_soc_usb *
snd_soc_usb_allocate_port(struct snd_soc_component *component, void *data)
{
	return ERR_PTR(-ENOMEM);
}

static inline void snd_soc_usb_free_port(struct snd_soc_usb *usb)
{ }

static inline void snd_soc_usb_add_port(struct snd_soc_usb *usb)
{ }

static inline void snd_soc_usb_remove_port(struct snd_soc_usb *usb)
{ }
#endif /* IS_ENABLED(CONFIG_SND_SOC_USB) */
#endif /*__LINUX_SND_SOC_USB_H */
