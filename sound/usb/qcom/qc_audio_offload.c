// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/auxiliary_bus.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/init.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/usb/audio-v3.h>
#include <linux/usb/hcd.h>
#include <linux/usb/quirks.h>
#include <linux/usb/xhci-sideband.h>

#include <sound/control.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/q6usboffload.h>
#include <sound/soc.h>
#include <sound/soc-usb.h>

#include "../usbaudio.h"
#include "../card.h"
#include "../endpoint.h"
#include "../format.h"
#include "../helper.h"
#include "../pcm.h"
#include "../power.h"

#include "mixer_usb_offload.h"
#include "usb_audio_qmi_v01.h"

/* Stream disable request timeout during USB device disconnect */
#define DEV_RELEASE_WAIT_TIMEOUT 10000 /* in ms */

/* Data interval calculation parameters */
#define BUS_INTERVAL_FULL_SPEED 1000 /* in us */
#define BUS_INTERVAL_HIGHSPEED_AND_ABOVE 125 /* in us */
#define MAX_BINTERVAL_ISOC_EP 16

#define QMI_STREAM_REQ_CARD_NUM_MASK 0xffff0000
#define QMI_STREAM_REQ_DEV_NUM_MASK 0xff00
#define QMI_STREAM_REQ_DIRECTION 0xff

/* iommu resource parameters and management */
#define PREPEND_SID_TO_IOVA(iova, sid) ((u64)(((u64)(iova)) | \
					(((u64)sid) << 32)))
#define IOVA_MASK(iova) (((u64)(iova)) & 0xFFFFFFFF)
#define IOVA_BASE 0x1000
#define IOVA_XFER_RING_BASE (IOVA_BASE + PAGE_SIZE * (SNDRV_CARDS + 1))
#define IOVA_XFER_BUF_BASE (IOVA_XFER_RING_BASE + PAGE_SIZE * SNDRV_CARDS * 32)
#define IOVA_XFER_RING_MAX (IOVA_XFER_BUF_BASE - PAGE_SIZE)
#define IOVA_XFER_BUF_MAX (0xfffff000 - PAGE_SIZE)

#define MAX_XFER_BUFF_LEN (24 * PAGE_SIZE)

struct iova_info {
	struct list_head list;
	unsigned long start_iova;
	size_t size;
	bool in_use;
};

struct intf_info {
	/* IOMMU ring/buffer mapping information */
	unsigned long data_xfer_ring_va;
	size_t data_xfer_ring_size;
	unsigned long sync_xfer_ring_va;
	size_t sync_xfer_ring_size;
	dma_addr_t xfer_buf_iova;
	size_t xfer_buf_size;
	dma_addr_t xfer_buf_dma;
	u8 *xfer_buf_cpu;

	/* USB endpoint information */
	unsigned int data_ep_pipe;
	unsigned int sync_ep_pipe;
	unsigned int data_ep_idx;
	unsigned int sync_ep_idx;

	u8 intf_num;
	u8 pcm_card_num;
	u8 pcm_dev_num;
	u8 direction;
	bool in_use;
};

struct uaudio_qmi_dev {
	struct device *dev;
	struct q6usb_offload *data;
	struct auxiliary_device *auxdev;

	/* list to keep track of available iova */
	struct list_head xfer_ring_list;
	size_t xfer_ring_iova_size;
	unsigned long curr_xfer_ring_iova;
	struct list_head xfer_buf_list;
	size_t xfer_buf_iova_size;
	unsigned long curr_xfer_buf_iova;

	/* bit fields representing pcm card enabled */
	unsigned long card_slot;
	/* indicate event ring mapped or not */
	bool er_mapped;
};

struct uaudio_dev {
	struct usb_device *udev;
	/* audio control interface */
	struct usb_host_interface *ctrl_intf;
	unsigned int usb_core_id;
	atomic_t in_use;
	struct kref kref;
	wait_queue_head_t disconnect_wq;

	/* interface specific */
	int num_intf;
	struct intf_info *info;
	struct snd_usb_audio *chip;

	/* xhci sideband */
	struct xhci_sideband *sb;

	/* SoC USB device */
	struct snd_soc_usb_device *sdev;
};

static struct uaudio_dev uadev[SNDRV_CARDS];
static struct uaudio_qmi_dev *uaudio_qdev;
static struct uaudio_qmi_svc *uaudio_svc;
static DEFINE_MUTEX(qdev_mutex);

struct uaudio_qmi_svc {
	struct qmi_handle *uaudio_svc_hdl;
	struct sockaddr_qrtr client_sq;
	bool client_connected;
};

enum mem_type {
	MEM_EVENT_RING,
	MEM_XFER_RING,
	MEM_XFER_BUF,
};

/* Supported audio formats */
enum usb_qmi_audio_format {
	USB_QMI_PCM_FORMAT_S8 = 0,
	USB_QMI_PCM_FORMAT_U8,
	USB_QMI_PCM_FORMAT_S16_LE,
	USB_QMI_PCM_FORMAT_S16_BE,
	USB_QMI_PCM_FORMAT_U16_LE,
	USB_QMI_PCM_FORMAT_U16_BE,
	USB_QMI_PCM_FORMAT_S24_LE,
	USB_QMI_PCM_FORMAT_S24_BE,
	USB_QMI_PCM_FORMAT_U24_LE,
	USB_QMI_PCM_FORMAT_U24_BE,
	USB_QMI_PCM_FORMAT_S24_3LE,
	USB_QMI_PCM_FORMAT_S24_3BE,
	USB_QMI_PCM_FORMAT_U24_3LE,
	USB_QMI_PCM_FORMAT_U24_3BE,
	USB_QMI_PCM_FORMAT_S32_LE,
	USB_QMI_PCM_FORMAT_S32_BE,
	USB_QMI_PCM_FORMAT_U32_LE,
	USB_QMI_PCM_FORMAT_U32_BE,
};

static int usb_qmi_get_pcm_num(struct snd_usb_audio *chip, int direction)
{
	struct snd_usb_substream *subs = NULL;
	struct snd_usb_stream *as;
	int count = 0;

	list_for_each_entry(as, &chip->pcm_list, list) {
		subs = &as->substream[direction];
		if (subs->ep_num)
			count++;
	}

	return count;
}

static enum usb_qmi_audio_device_speed_enum_v01
get_speed_info(enum usb_device_speed udev_speed)
{
	switch (udev_speed) {
	case USB_SPEED_LOW:
		return USB_QMI_DEVICE_SPEED_LOW_V01;
	case USB_SPEED_FULL:
		return USB_QMI_DEVICE_SPEED_FULL_V01;
	case USB_SPEED_HIGH:
		return USB_QMI_DEVICE_SPEED_HIGH_V01;
	case USB_SPEED_SUPER:
		return USB_QMI_DEVICE_SPEED_SUPER_V01;
	case USB_SPEED_SUPER_PLUS:
		return USB_QMI_DEVICE_SPEED_SUPER_PLUS_V01;
	default:
		return USB_QMI_DEVICE_SPEED_INVALID_V01;
	}
}

static struct snd_usb_substream *find_substream(unsigned int card_num,
						unsigned int pcm_idx,
						unsigned int direction)
{
	struct snd_usb_substream *subs = NULL;
	struct snd_usb_audio *chip;
	struct snd_usb_stream *as;

	chip = uadev[card_num].chip;
	if (!chip || atomic_read(&chip->shutdown))
		goto done;

	if (pcm_idx >= chip->pcm_devs)
		goto done;

	if (direction > SNDRV_PCM_STREAM_CAPTURE)
		goto done;

	list_for_each_entry(as, &chip->pcm_list, list) {
		if (as->pcm_index == pcm_idx) {
			subs = &as->substream[direction];
			goto done;
		}
	}

done:
	return subs;
}

static int info_idx_from_ifnum(int card_num, int intf_num, bool enable)
{
	int i;

	/*
	 * default index 0 is used when info is allocated upon
	 * first enable audio stream req for a pcm device
	 */
	if (enable && !uadev[card_num].info)
		return 0;

	for (i = 0; i < uadev[card_num].num_intf; i++) {
		if (enable && !uadev[card_num].info[i].in_use)
			return i;
		else if (!enable &&
			 uadev[card_num].info[i].intf_num == intf_num)
			return i;
	}

	return -EINVAL;
}

static int get_data_interval_from_si(struct snd_usb_substream *subs,
				     u32 service_interval)
{
	unsigned int bus_intval_mult;
	unsigned int bus_intval;
	unsigned int binterval;

	if (subs->dev->speed >= USB_SPEED_HIGH)
		bus_intval = BUS_INTERVAL_HIGHSPEED_AND_ABOVE;
	else
		bus_intval = BUS_INTERVAL_FULL_SPEED;

	if (service_interval % bus_intval)
		return -EINVAL;

	bus_intval_mult = service_interval / bus_intval;
	binterval = ffs(bus_intval_mult);
	if (!binterval || binterval > MAX_BINTERVAL_ISOC_EP)
		return -EINVAL;

	/* check if another bit is set then bail out */
	bus_intval_mult = bus_intval_mult >> binterval;
	if (bus_intval_mult)
		return -EINVAL;

	return (binterval - 1);
}

/* maps audio format received over QMI to asound.h based pcm format */
static snd_pcm_format_t map_pcm_format(enum usb_qmi_audio_format fmt_received)
{
	switch (fmt_received) {
	case USB_QMI_PCM_FORMAT_S8:
		return SNDRV_PCM_FORMAT_S8;
	case USB_QMI_PCM_FORMAT_U8:
		return SNDRV_PCM_FORMAT_U8;
	case USB_QMI_PCM_FORMAT_S16_LE:
		return SNDRV_PCM_FORMAT_S16_LE;
	case USB_QMI_PCM_FORMAT_S16_BE:
		return SNDRV_PCM_FORMAT_S16_BE;
	case USB_QMI_PCM_FORMAT_U16_LE:
		return SNDRV_PCM_FORMAT_U16_LE;
	case USB_QMI_PCM_FORMAT_U16_BE:
		return SNDRV_PCM_FORMAT_U16_BE;
	case USB_QMI_PCM_FORMAT_S24_LE:
		return SNDRV_PCM_FORMAT_S24_LE;
	case USB_QMI_PCM_FORMAT_S24_BE:
		return SNDRV_PCM_FORMAT_S24_BE;
	case USB_QMI_PCM_FORMAT_U24_LE:
		return SNDRV_PCM_FORMAT_U24_LE;
	case USB_QMI_PCM_FORMAT_U24_BE:
		return SNDRV_PCM_FORMAT_U24_BE;
	case USB_QMI_PCM_FORMAT_S24_3LE:
		return SNDRV_PCM_FORMAT_S24_3LE;
	case USB_QMI_PCM_FORMAT_S24_3BE:
		return SNDRV_PCM_FORMAT_S24_3BE;
	case USB_QMI_PCM_FORMAT_U24_3LE:
		return SNDRV_PCM_FORMAT_U24_3LE;
	case USB_QMI_PCM_FORMAT_U24_3BE:
		return SNDRV_PCM_FORMAT_U24_3BE;
	case USB_QMI_PCM_FORMAT_S32_LE:
		return SNDRV_PCM_FORMAT_S32_LE;
	case USB_QMI_PCM_FORMAT_S32_BE:
		return SNDRV_PCM_FORMAT_S32_BE;
	case USB_QMI_PCM_FORMAT_U32_LE:
		return SNDRV_PCM_FORMAT_U32_LE;
	case USB_QMI_PCM_FORMAT_U32_BE:
		return SNDRV_PCM_FORMAT_U32_BE;
	default:
		/*
		 * We expect the caller to do input validation so we should
		 * never hit this. But we do have to return a proper
		 * snd_pcm_format_t value due to the __bitwise attribute; so
		 * just return the equivalent of 0 in case of bad input.
		 */
		return SNDRV_PCM_FORMAT_S8;
	}
}

/*
 * Sends QMI disconnect indication message, assumes chip->mutex and qdev_mutex
 * lock held by caller.
 */
static int uaudio_send_disconnect_ind(struct snd_usb_audio *chip)
{
	struct qmi_uaudio_stream_ind_msg_v01 disconnect_ind = {0};
	struct uaudio_qmi_svc *svc = uaudio_svc;
	struct uaudio_dev *dev;
	int ret = 0;

	dev = &uadev[chip->card->number];

	if (atomic_read(&dev->in_use)) {
		mutex_unlock(&chip->mutex);
		mutex_unlock(&qdev_mutex);
		dev_dbg(uaudio_qdev->data->dev, "sending qmi indication suspend\n");
		disconnect_ind.dev_event = USB_QMI_DEV_DISCONNECT_V01;
		disconnect_ind.slot_id = dev->udev->slot_id;
		disconnect_ind.controller_num = dev->usb_core_id;
		disconnect_ind.controller_num_valid = 1;
		ret = qmi_send_indication(svc->uaudio_svc_hdl, &svc->client_sq,
					  QMI_UAUDIO_STREAM_IND_V01,
					  QMI_UAUDIO_STREAM_IND_MSG_V01_MAX_MSG_LEN,
					  qmi_uaudio_stream_ind_msg_v01_ei,
					  &disconnect_ind);
		if (ret < 0)
			dev_err(uaudio_qdev->data->dev,
				"qmi send failed with err: %d\n", ret);

		ret = wait_event_interruptible_timeout(dev->disconnect_wq,
				!atomic_read(&dev->in_use),
				msecs_to_jiffies(DEV_RELEASE_WAIT_TIMEOUT));
		if (!ret) {
			dev_err(uaudio_qdev->data->dev,
				"timeout while waiting for dev_release\n");
			atomic_set(&dev->in_use, 0);
		} else if (ret < 0) {
			dev_err(uaudio_qdev->data->dev,
				"failed with ret %d\n", ret);
			atomic_set(&dev->in_use, 0);
		}
		mutex_lock(&qdev_mutex);
		mutex_lock(&chip->mutex);
	}

	return ret;
}

/* Offloading IOMMU management */
static unsigned long uaudio_get_iova(unsigned long *curr_iova,
				     size_t *curr_iova_size,
				     struct list_head *head, size_t size)
{
	struct iova_info *info, *new_info = NULL;
	struct list_head *curr_head;
	size_t tmp_size = size;
	unsigned long iova = 0;

	if (size % PAGE_SIZE)
		goto done;

	if (size > *curr_iova_size)
		goto done;

	if (*curr_iova_size == 0)
		goto done;

	list_for_each_entry(info, head, list) {
		/* exact size iova_info */
		if (!info->in_use && info->size == size) {
			info->in_use = true;
			iova = info->start_iova;
			*curr_iova_size -= size;
			goto done;
		} else if (!info->in_use && tmp_size >= info->size) {
			if (!new_info)
				new_info = info;
			tmp_size -= info->size;
			if (tmp_size)
				continue;

			iova = new_info->start_iova;
			for (curr_head = &new_info->list; curr_head !=
			&info->list; curr_head = curr_head->next) {
				new_info = list_entry(curr_head, struct
						iova_info, list);
				new_info->in_use = true;
			}
			info->in_use = true;
			*curr_iova_size -= size;
			goto done;
		} else {
			/* iova region in use */
			new_info = NULL;
			tmp_size = size;
		}
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		iova = 0;
		goto done;
	}

	iova = *curr_iova;
	info->start_iova = *curr_iova;
	info->size = size;
	info->in_use = true;
	*curr_iova += size;
	*curr_iova_size -= size;
	list_add_tail(&info->list, head);

done:
	return iova;
}

static void uaudio_put_iova(unsigned long iova, size_t size, struct list_head
	*head, size_t *curr_iova_size)
{
	struct iova_info *info;
	size_t tmp_size = size;
	bool found = false;

	list_for_each_entry(info, head, list) {
		if (info->start_iova == iova) {
			if (!info->in_use)
				return;

			found = true;
			info->in_use = false;
			if (info->size == size)
				goto done;
		}

		if (found && tmp_size >= info->size) {
			info->in_use = false;
			tmp_size -= info->size;
			if (!tmp_size)
				goto done;
		}
	}

	if (!found)
		return;

done:
	*curr_iova_size += size;
}

/**
 * uaudio_iommu_unmap() - unmaps iommu memory for adsp
 * @mtype: ring type
 * @iova: virtual address to unmap
 * @iova_size: region size
 * @mapped_iova_size: mapped region size
 *
 * Unmaps the memory region that was previously assigned to the adsp.
 *
 */
static void uaudio_iommu_unmap(enum mem_type mtype, unsigned long iova,
			       size_t iova_size, size_t mapped_iova_size)
{
	size_t umap_size;
	bool unmap = true;

	if (!iova || !iova_size)
		return;

	switch (mtype) {
	case MEM_EVENT_RING:
		if (uaudio_qdev->er_mapped)
			uaudio_qdev->er_mapped = false;
		else
			unmap = false;
		break;

	case MEM_XFER_RING:
		uaudio_put_iova(iova, iova_size, &uaudio_qdev->xfer_ring_list,
				&uaudio_qdev->xfer_ring_iova_size);
		break;
	case MEM_XFER_BUF:
		uaudio_put_iova(iova, iova_size, &uaudio_qdev->xfer_buf_list,
				&uaudio_qdev->xfer_buf_iova_size);
		break;
	default:
		unmap = false;
	}

	if (!unmap || !mapped_iova_size)
		return;

	umap_size = iommu_unmap(uaudio_qdev->data->domain, iova, mapped_iova_size);
	if (umap_size != mapped_iova_size)
		dev_err(uaudio_qdev->data->dev,
			"unmapped size %zu for iova 0x%08lx of mapped size %zu\n",
			umap_size, iova, mapped_iova_size);
}

/**
 * uaudio_iommu_map() - maps iommu memory for adsp
 * @mtype: ring type
 * @dma_coherent: dma coherent
 * @pa: physical address for ring/buffer
 * @size: size of memory region
 * @sgt: sg table for memory region
 *
 * Maps the XHCI related resources to a memory region that is assigned to be
 * used by the adsp.  This will be mapped to the domain, which is created by
 * the ASoC USB backend driver.
 *
 */
static unsigned long uaudio_iommu_map(enum mem_type mtype, bool dma_coherent,
				      phys_addr_t pa, size_t size,
				      struct sg_table *sgt)
{
	struct scatterlist *sg;
	unsigned long iova = 0;
	size_t total_len = 0;
	unsigned long iova_sg;
	phys_addr_t pa_sg;
	bool map = true;
	size_t sg_len;
	int prot;
	int ret;
	int i;

	prot = IOMMU_READ | IOMMU_WRITE;

	if (dma_coherent)
		prot |= IOMMU_CACHE;

	switch (mtype) {
	case MEM_EVENT_RING:
		iova = IOVA_BASE;
		/* er already mapped */
		if (uaudio_qdev->er_mapped)
			map = false;
		break;
	case MEM_XFER_RING:
		iova = uaudio_get_iova(&uaudio_qdev->curr_xfer_ring_iova,
				     &uaudio_qdev->xfer_ring_iova_size,
				     &uaudio_qdev->xfer_ring_list, size);
		break;
	case MEM_XFER_BUF:
		iova = uaudio_get_iova(&uaudio_qdev->curr_xfer_buf_iova,
				     &uaudio_qdev->xfer_buf_iova_size,
				     &uaudio_qdev->xfer_buf_list, size);
		break;
	default:
		dev_err(uaudio_qdev->data->dev, "unknown mem type %d\n", mtype);
	}

	if (!iova || !map)
		goto done;

	if (!sgt)
		goto skip_sgt_map;

	iova_sg = iova;
	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		sg_len = PAGE_ALIGN(sg->offset + sg->length);
		pa_sg = page_to_phys(sg_page(sg));
		ret = iommu_map(uaudio_qdev->data->domain, iova_sg, pa_sg, sg_len,
				prot, GFP_KERNEL);
		if (ret) {
			uaudio_iommu_unmap(MEM_XFER_BUF, iova, size, total_len);
			iova = 0;
			goto done;
		}

		iova_sg += sg_len;
		total_len += sg_len;
	}

	if (size != total_len) {
		uaudio_iommu_unmap(MEM_XFER_BUF, iova, size, total_len);
		iova = 0;
	}
	return iova;

skip_sgt_map:
	iommu_map(uaudio_qdev->data->domain, iova, pa, size, prot, GFP_KERNEL);

done:
	return iova;
}

/* looks up alias, if any, for controller DT node and returns the index */
static int usb_get_controller_id(struct usb_device *udev)
{
	if (udev->bus->sysdev && udev->bus->sysdev->of_node)
		return of_alias_get_id(udev->bus->sysdev->of_node, "usb");

	return -ENODEV;
}

/**
 * uaudio_dev_intf_cleanup() - cleanup transfer resources
 * @udev: usb device
 * @info: usb offloading interface
 *
 * Cleans up the transfer ring related resources which are assigned per
 * endpoint from XHCI.  This is invoked when the USB endpoints are no
 * longer in use by the adsp.
 *
 */
static void uaudio_dev_intf_cleanup(struct usb_device *udev, struct intf_info *info)
{
	uaudio_iommu_unmap(MEM_XFER_RING, info->data_xfer_ring_va,
			   info->data_xfer_ring_size, info->data_xfer_ring_size);
	info->data_xfer_ring_va = 0;
	info->data_xfer_ring_size = 0;

	uaudio_iommu_unmap(MEM_XFER_RING, info->sync_xfer_ring_va,
			   info->sync_xfer_ring_size, info->sync_xfer_ring_size);
	info->sync_xfer_ring_va = 0;
	info->sync_xfer_ring_size = 0;

	uaudio_iommu_unmap(MEM_XFER_BUF, info->xfer_buf_iova, info->xfer_buf_size,
			   info->xfer_buf_size);
	info->xfer_buf_iova = 0;

	usb_free_coherent(udev, info->xfer_buf_size, info->xfer_buf_cpu,
			  info->xfer_buf_dma);
	info->xfer_buf_size = 0;
	info->xfer_buf_cpu = NULL;
	info->xfer_buf_dma = 0;

	info->in_use = false;
}

/**
 * uaudio_event_ring_cleanup_free() - cleanup secondary event ring
 * @dev: usb offload device
 *
 * Cleans up the secondary event ring that was requested.  This will
 * occur when the adsp is no longer transferring data on the USB bus
 * across all endpoints.
 *
 */
static void uaudio_event_ring_cleanup_free(struct uaudio_dev *dev)
{
	clear_bit(dev->chip->card->number, &uaudio_qdev->card_slot);
	/* all audio devices are disconnected */
	if (!uaudio_qdev->card_slot) {
		uaudio_iommu_unmap(MEM_EVENT_RING, IOVA_BASE, PAGE_SIZE,
				   PAGE_SIZE);
		xhci_sideband_remove_interrupter(uadev[dev->chip->card->number].sb);
	}
}

static void uaudio_dev_cleanup(struct uaudio_dev *dev)
{
	int if_idx;

	if (!dev->udev)
		return;

	/* free xfer buffer and unmap xfer ring and buf per interface */
	for (if_idx = 0; if_idx < dev->num_intf; if_idx++) {
		if (!dev->info[if_idx].in_use)
			continue;
		uaudio_dev_intf_cleanup(dev->udev, &dev->info[if_idx]);
		dev_dbg(uaudio_qdev->data->dev,
			"release resources: intf# %d card# %d\n",
			dev->info[if_idx].intf_num, dev->chip->card->number);
	}

	dev->num_intf = 0;

	/* free interface info */
	kfree(dev->info);
	dev->info = NULL;
	uaudio_event_ring_cleanup_free(dev);
	dev->udev = NULL;
}

/**
 * disable_audio_stream() - disable usb snd endpoints
 * @subs: usb substream
 *
 * Closes the USB SND endpoints associated with the current audio stream
 * used.  This will decrement the USB SND endpoint opened reference count.
 *
 */
static void disable_audio_stream(struct snd_usb_substream *subs)
{
	struct snd_usb_audio *chip = subs->stream->chip;

	snd_usb_hw_free(subs);
	snd_usb_autosuspend(chip);
}

/* QMI service disconnect handlers */
static void qmi_stop_session(void)
{
	struct snd_usb_substream *subs;
	struct usb_host_endpoint *ep;
	struct snd_usb_audio *chip;
	struct intf_info *info;
	int pcm_card_num;
	int if_idx;
	int idx;

	mutex_lock(&qdev_mutex);
	/* find all active intf for set alt 0 and cleanup usb audio dev */
	for (idx = 0; idx < SNDRV_CARDS; idx++) {
		if (!atomic_read(&uadev[idx].in_use))
			continue;

		chip = uadev[idx].chip;
		for (if_idx = 0; if_idx < uadev[idx].num_intf; if_idx++) {
			if (!uadev[idx].info || !uadev[idx].info[if_idx].in_use)
				continue;
			info = &uadev[idx].info[if_idx];
			pcm_card_num = info->pcm_card_num;
			subs = find_substream(pcm_card_num, info->pcm_dev_num,
					      info->direction);
			if (!subs || !chip || atomic_read(&chip->shutdown)) {
				dev_err(&uadev[idx].udev->dev,
					"no sub for c#%u dev#%u dir%u\n",
					info->pcm_card_num,
					info->pcm_dev_num,
					info->direction);
				continue;
			}
			/* Release XHCI endpoints */
			if (info->data_ep_pipe)
				ep = usb_pipe_endpoint(uadev[pcm_card_num].udev,
						       info->data_ep_pipe);
			xhci_sideband_remove_endpoint(uadev[pcm_card_num].sb, ep);

			if (info->sync_ep_pipe)
				ep = usb_pipe_endpoint(uadev[pcm_card_num].udev,
						       info->sync_ep_pipe);
			xhci_sideband_remove_endpoint(uadev[pcm_card_num].sb, ep);

			disable_audio_stream(subs);
		}
		atomic_set(&uadev[idx].in_use, 0);
		mutex_lock(&chip->mutex);
		uaudio_dev_cleanup(&uadev[idx]);
		mutex_unlock(&chip->mutex);
	}
	mutex_unlock(&qdev_mutex);
}

/**
 * uaudio_sideband_notifier() - xHCI sideband event handler
 * @intf: USB interface handle
 * @evt: xHCI sideband event type
 *
 * This callback is executed when the xHCI sideband encounters a sequence
 * that requires the sideband clients to take action.  An example, is when
 * xHCI frees the transfer ring, so the client has to ensure that the
 * offload path is halted.
 *
 */
static int uaudio_sideband_notifier(struct usb_interface *intf,
				    struct xhci_sideband_event *evt)
{
	struct snd_usb_audio *chip;
	struct uaudio_dev *dev;
	int if_idx;

	if (!intf || !evt)
		return 0;

	chip = usb_get_intfdata(intf);

	mutex_lock(&qdev_mutex);
	mutex_lock(&chip->mutex);

	dev = &uadev[chip->card->number];

	if (evt->type == XHCI_SIDEBAND_XFER_RING_FREE) {
		unsigned int *ep = (unsigned int *) evt->evt_data;

		for (if_idx = 0; if_idx < dev->num_intf; if_idx++) {
			if (dev->info[if_idx].data_ep_idx == *ep ||
			    dev->info[if_idx].sync_ep_idx == *ep)
				uaudio_send_disconnect_ind(chip);
		}
	}

	mutex_unlock(&qdev_mutex);
	mutex_unlock(&chip->mutex);

	return 0;
}

/**
 * qmi_bye_cb() - qmi bye message callback
 * @handle: QMI handle
 * @node: id of the dying node
 *
 * This callback is invoked when the QMI bye control message is received
 * from the QMI client.  Handle the message accordingly by ensuring that
 * the USB offload path is disabled and cleaned up.  At this point, ADSP
 * is not utilizing the USB bus.
 *
 */
static void qmi_bye_cb(struct qmi_handle *handle, unsigned int node)
{
	struct uaudio_qmi_svc *svc = uaudio_svc;

	if (svc->uaudio_svc_hdl != handle)
		return;

	if (svc->client_connected && svc->client_sq.sq_node == node) {
		qmi_stop_session();

		/* clear QMI client parameters to block further QMI messages */
		svc->client_sq.sq_node = 0;
		svc->client_sq.sq_port = 0;
		svc->client_sq.sq_family = 0;
		svc->client_connected = false;
	}
}

/**
 * qmi_svc_disconnect_cb() - qmi client disconnected
 * @handle: QMI handle
 * @node: id of the dying node
 * @port: port of the dying client
 *
 * Invoked when the remote QMI client is disconnected.  Handle this event
 * the same way as when the QMI bye message is received.  This will ensure
 * the USB offloading path is disabled and cleaned up.
 *
 */
static void qmi_svc_disconnect_cb(struct qmi_handle *handle,
				  unsigned int node, unsigned int port)
{
	struct uaudio_qmi_svc *svc;

	if (!uaudio_svc)
		return;

	svc = uaudio_svc;
	if (svc->uaudio_svc_hdl != handle)
		return;

	if (svc->client_connected && svc->client_sq.sq_node == node &&
	    svc->client_sq.sq_port == port) {
		qmi_stop_session();

		/* clear QMI client parameters to block further QMI messages */
		svc->client_sq.sq_node = 0;
		svc->client_sq.sq_port = 0;
		svc->client_sq.sq_family = 0;
		svc->client_connected = false;
	}
}

/* QMI client callback handlers from QMI interface */
static struct qmi_ops uaudio_svc_ops_options = {
	.bye = qmi_bye_cb,
	.del_client = qmi_svc_disconnect_cb,
};

/* kref release callback when all streams are disabled */
static void uaudio_dev_release(struct kref *kref)
{
	struct uaudio_dev *dev = container_of(kref, struct uaudio_dev, kref);

	uaudio_event_ring_cleanup_free(dev);
	atomic_set(&dev->in_use, 0);
	wake_up(&dev->disconnect_wq);
}

/**
 * enable_audio_stream() - enable usb snd endpoints
 * @subs: usb substream
 * @pcm_format: pcm format requested
 * @channels: number of channels
 * @cur_rate: sample rate
 * @datainterval: interval
 *
 * Opens all USB SND endpoints used for the data interface.  This will increment
 * the USB SND endpoint's opened count.  Requests to keep the interface resumed
 * until the audio stream is stopped.  Will issue the USB set interface control
 * message to enable the data interface.
 *
 */
static int enable_audio_stream(struct snd_usb_substream *subs,
			       snd_pcm_format_t pcm_format,
			       unsigned int channels, unsigned int cur_rate,
			       int datainterval)
{
	struct snd_pcm_hw_params params;
	struct snd_usb_audio *chip;
	struct snd_interval *i;
	struct snd_mask *m;
	int ret;

	chip = subs->stream->chip;

	_snd_pcm_hw_params_any(&params);

	m = hw_param_mask(&params, SNDRV_PCM_HW_PARAM_FORMAT);
	snd_mask_leave(m, pcm_format);

	i = hw_param_interval(&params, SNDRV_PCM_HW_PARAM_CHANNELS);
	snd_interval_setinteger(i);
	i->min = channels;
	i->max = channels;

	i = hw_param_interval(&params, SNDRV_PCM_HW_PARAM_RATE);
	snd_interval_setinteger(i);
	i->min = cur_rate;
	i->max = cur_rate;

	pm_runtime_barrier(&chip->intf[0]->dev);
	snd_usb_autoresume(chip);

	ret = snd_usb_hw_params(subs, &params);
	if (ret < 0)
		goto put_suspend;

	if (!atomic_read(&chip->shutdown)) {
		ret = snd_usb_lock_shutdown(chip);
		if (ret < 0)
			goto detach_ep;

		if (subs->sync_endpoint) {
			ret = snd_usb_endpoint_prepare(chip, subs->sync_endpoint);
			if (ret < 0)
				goto unlock;
		}

		ret = snd_usb_endpoint_prepare(chip, subs->data_endpoint);
		if (ret < 0)
			goto unlock;

		snd_usb_unlock_shutdown(chip);

		dev_dbg(uaudio_qdev->data->dev,
			"selected %s iface:%d altsetting:%d datainterval:%dus\n",
			subs->direction ? "capture" : "playback",
			subs->cur_audiofmt->iface, subs->cur_audiofmt->altsetting,
			(1 << subs->cur_audiofmt->datainterval) *
			(subs->dev->speed >= USB_SPEED_HIGH ?
			BUS_INTERVAL_HIGHSPEED_AND_ABOVE :
			BUS_INTERVAL_FULL_SPEED));
	}

	return 0;

unlock:
	snd_usb_unlock_shutdown(chip);

detach_ep:
	snd_usb_hw_free(subs);

put_suspend:
	snd_usb_autosuspend(chip);

	return ret;
}

/**
 * uaudio_transfer_buffer_setup() - fetch and populate xfer buffer params
 * @subs: usb substream
 * @xfer_buf: xfer buf to be allocated
 * @xfer_buf_len: size of allocation
 * @mem_info: QMI response info
 *
 * Allocates and maps the transfer buffers that will be utilized by the
 * audio DSP.  Will populate the information in the QMI response that is
 * sent back to the stream enable request.
 *
 */
static int uaudio_transfer_buffer_setup(struct snd_usb_substream *subs,
					void **xfer_buf_cpu, u32 xfer_buf_len,
					struct mem_info_v01 *mem_info)
{
	struct sg_table xfer_buf_sgt;
	dma_addr_t xfer_buf_dma;
	void *xfer_buf;
	phys_addr_t xfer_buf_pa;
	u32 len = xfer_buf_len;
	bool dma_coherent;
	dma_addr_t xfer_buf_dma_sysdev;
	u32 remainder;
	u32 mult;
	int ret;

	dma_coherent = dev_is_dma_coherent(subs->dev->bus->sysdev);

	/* xfer buffer, multiple of 4K only */
	if (!len)
		len = PAGE_SIZE;

	mult = len / PAGE_SIZE;
	remainder = len % PAGE_SIZE;
	len = mult * PAGE_SIZE;
	len += remainder ? PAGE_SIZE : 0;

	if (len > MAX_XFER_BUFF_LEN) {
		dev_err(uaudio_qdev->data->dev,
			"req buf len %d > max buf len %lu, setting %lu\n",
			len, MAX_XFER_BUFF_LEN, MAX_XFER_BUFF_LEN);
		len = MAX_XFER_BUFF_LEN;
	}

	/* get buffer mapped into subs->dev */
	xfer_buf = usb_alloc_coherent(subs->dev, len, GFP_KERNEL, &xfer_buf_dma);
	if (!xfer_buf)
		return -ENOMEM;

	/* Remapping is not possible if xfer_buf is outside of linear map */
	xfer_buf_pa = virt_to_phys(xfer_buf);
	if (WARN_ON(!page_is_ram(PFN_DOWN(xfer_buf_pa)))) {
		ret = -ENXIO;
		goto unmap_sync;
	}
	dma_get_sgtable(subs->dev->bus->sysdev, &xfer_buf_sgt, xfer_buf,
			xfer_buf_dma, len);

	/* map the physical buffer into sysdev as well */
	xfer_buf_dma_sysdev = uaudio_iommu_map(MEM_XFER_BUF, dma_coherent,
					       xfer_buf_pa, len, &xfer_buf_sgt);
	if (!xfer_buf_dma_sysdev) {
		ret = -ENOMEM;
		goto unmap_sync;
	}

	mem_info->dma = xfer_buf_dma;
	mem_info->size = len;
	mem_info->iova = PREPEND_SID_TO_IOVA(xfer_buf_dma_sysdev, uaudio_qdev->data->sid);
	*xfer_buf_cpu = xfer_buf;
	sg_free_table(&xfer_buf_sgt);

	return 0;

unmap_sync:
	usb_free_coherent(subs->dev, len, xfer_buf, xfer_buf_dma);

	return ret;
}

/**
 * uaudio_endpoint_setup() - fetch and populate endpoint params
 * @subs: usb substream
 * @endpoint: usb endpoint to add
 * @card_num: uadev index
 * @mem_info: QMI response info
 * @ep_desc: QMI ep desc response field
 *
 * Initialize the USB endpoint being used for a particular USB
 * stream.  Will request XHCI sec intr to reserve the EP for
 * offloading as well as populating the QMI response with the
 * transfer ring parameters.
 *
 */
static phys_addr_t
uaudio_endpoint_setup(struct snd_usb_substream *subs,
		      struct snd_usb_endpoint *endpoint, int card_num,
		      struct mem_info_v01 *mem_info,
		      struct usb_endpoint_descriptor_v01 *ep_desc)
{
	struct usb_host_endpoint *ep;
	phys_addr_t tr_pa = 0;
	struct sg_table *sgt;
	bool dma_coherent;
	unsigned long iova;
	struct page *pg;
	int ret = -ENODEV;

	dma_coherent = dev_is_dma_coherent(subs->dev->bus->sysdev);

	ep = usb_pipe_endpoint(subs->dev, endpoint->pipe);
	if (!ep) {
		dev_err(uaudio_qdev->data->dev, "data ep # %d context is null\n",
			subs->data_endpoint->ep_num);
		goto exit;
	}

	memcpy(ep_desc, &ep->desc, sizeof(ep->desc));

	ret = xhci_sideband_add_endpoint(uadev[card_num].sb, ep);
	if (ret < 0) {
		dev_err(&subs->dev->dev,
			"failed to add data ep to sec intr\n");
		ret = -ENODEV;
		goto exit;
	}

	sgt = xhci_sideband_get_endpoint_buffer(uadev[card_num].sb, ep);
	if (!sgt) {
		dev_err(&subs->dev->dev,
			"failed to get data ep ring address\n");
		ret = -ENODEV;
		goto remove_ep;
	}

	pg = sg_page(sgt->sgl);
	tr_pa = page_to_phys(pg);
	mem_info->dma = sg_dma_address(sgt->sgl);
	sg_free_table(sgt);

	/* data transfer ring */
	iova = uaudio_iommu_map(MEM_XFER_RING, dma_coherent, tr_pa,
			      PAGE_SIZE, NULL);
	if (!iova) {
		ret = -ENOMEM;
		goto clear_pa;
	}

	mem_info->iova = PREPEND_SID_TO_IOVA(iova, uaudio_qdev->data->sid);
	mem_info->size = PAGE_SIZE;

	return 0;

clear_pa:
	mem_info->dma = 0;
remove_ep:
	xhci_sideband_remove_endpoint(uadev[card_num].sb, ep);
exit:
	return ret;
}

/**
 * uaudio_event_ring_setup() - fetch and populate event ring params
 * @subs: usb substream
 * @card_num: uadev index
 * @mem_info: QMI response info
 *
 * Register secondary interrupter to XHCI and fetch the event buffer info
 * and populate the information into the QMI response.
 *
 */
static int uaudio_event_ring_setup(struct snd_usb_substream *subs,
				   int card_num, struct mem_info_v01 *mem_info)
{
	struct sg_table *sgt;
	phys_addr_t er_pa;
	bool dma_coherent;
	unsigned long iova;
	struct page *pg;
	int ret;

	dma_coherent = dev_is_dma_coherent(subs->dev->bus->sysdev);
	er_pa = 0;

	/* event ring */
	ret = xhci_sideband_create_interrupter(uadev[card_num].sb, 1, false,
					       0, uaudio_qdev->data->intr_num);
	if (ret < 0) {
		dev_err(&subs->dev->dev, "failed to fetch interrupter\n");
		goto exit;
	}

	sgt = xhci_sideband_get_event_buffer(uadev[card_num].sb);
	if (!sgt) {
		dev_err(&subs->dev->dev,
			"failed to get event ring address\n");
		ret = -ENODEV;
		goto remove_interrupter;
	}

	pg = sg_page(sgt->sgl);
	er_pa = page_to_phys(pg);
	mem_info->dma = sg_dma_address(sgt->sgl);
	sg_free_table(sgt);

	iova = uaudio_iommu_map(MEM_EVENT_RING, dma_coherent, er_pa,
			      PAGE_SIZE, NULL);
	if (!iova) {
		ret = -ENOMEM;
		goto clear_pa;
	}

	mem_info->iova = PREPEND_SID_TO_IOVA(iova, uaudio_qdev->data->sid);
	mem_info->size = PAGE_SIZE;

	return 0;

clear_pa:
	mem_info->dma = 0;
remove_interrupter:
	xhci_sideband_remove_interrupter(uadev[card_num].sb);
exit:
	return ret;
}

/**
 * uaudio_populate_uac_desc() - parse UAC parameters and populate QMI resp
 * @subs: usb substream
 * @resp: QMI response buffer
 *
 * Parses information specified within UAC descriptors which explain the
 * sample parameters that the device expects.  This information is populated
 * to the QMI response sent back to the audio DSP.
 *
 */
static int uaudio_populate_uac_desc(struct snd_usb_substream *subs,
				    struct qmi_uaudio_stream_resp_msg_v01 *resp)
{
	struct usb_interface_descriptor *altsd;
	struct usb_host_interface *alts;
	struct usb_interface *iface;
	int protocol;

	iface = usb_ifnum_to_if(subs->dev, subs->cur_audiofmt->iface);
	if (!iface) {
		dev_err(&subs->dev->dev, "interface # %d does not exist\n",
			subs->cur_audiofmt->iface);
		return -ENODEV;
	}

	alts = &iface->altsetting[subs->cur_audiofmt->altset_idx];
	altsd = get_iface_desc(alts);
	protocol = altsd->bInterfaceProtocol;

	if (protocol == UAC_VERSION_1) {
		struct uac1_as_header_descriptor *as;

		as = snd_usb_find_csint_desc(alts->extra, alts->extralen, NULL,
					     UAC_AS_GENERAL);
		if (!as) {
			dev_err(&subs->dev->dev,
				"%u:%d : no UAC_AS_GENERAL desc\n",
				subs->cur_audiofmt->iface,
				subs->cur_audiofmt->altset_idx);
			return -ENODEV;
		}

		resp->data_path_delay = as->bDelay;
		resp->data_path_delay_valid = 1;

		resp->usb_audio_subslot_size = subs->cur_audiofmt->fmt_sz;
		resp->usb_audio_subslot_size_valid = 1;

		resp->usb_audio_spec_revision = le16_to_cpu((__force __le16)0x0100);
		resp->usb_audio_spec_revision_valid = 1;
	} else if (protocol == UAC_VERSION_2) {
		resp->usb_audio_subslot_size = subs->cur_audiofmt->fmt_sz;
		resp->usb_audio_subslot_size_valid = 1;

		resp->usb_audio_spec_revision = le16_to_cpu((__force __le16)0x0200);
		resp->usb_audio_spec_revision_valid = 1;
	} else if (protocol == UAC_VERSION_3) {
		if (iface->intf_assoc->bFunctionSubClass ==
					UAC3_FUNCTION_SUBCLASS_FULL_ADC_3_0) {
			dev_err(&subs->dev->dev,
				"full adc is not supported\n");
			return -EINVAL;
		}

		switch (le16_to_cpu(get_endpoint(alts, 0)->wMaxPacketSize)) {
		case UAC3_BADD_EP_MAXPSIZE_SYNC_MONO_16:
		case UAC3_BADD_EP_MAXPSIZE_SYNC_STEREO_16:
		case UAC3_BADD_EP_MAXPSIZE_ASYNC_MONO_16:
		case UAC3_BADD_EP_MAXPSIZE_ASYNC_STEREO_16: {
			resp->usb_audio_subslot_size = 0x2;
			break;
		}

		case UAC3_BADD_EP_MAXPSIZE_SYNC_MONO_24:
		case UAC3_BADD_EP_MAXPSIZE_SYNC_STEREO_24:
		case UAC3_BADD_EP_MAXPSIZE_ASYNC_MONO_24:
		case UAC3_BADD_EP_MAXPSIZE_ASYNC_STEREO_24: {
			resp->usb_audio_subslot_size = 0x3;
			break;
		}

		default:
			dev_err(&subs->dev->dev,
				"%d: %u: Invalid wMaxPacketSize\n",
				subs->cur_audiofmt->iface,
				subs->cur_audiofmt->altset_idx);
			return -EINVAL;
		}
		resp->usb_audio_subslot_size_valid = 1;
	} else {
		dev_err(&subs->dev->dev, "unknown protocol version %x\n",
			protocol);
		return -ENODEV;
	}

	memcpy(&resp->std_as_opr_intf_desc, &alts->desc, sizeof(alts->desc));

	return 0;
}

/**
 * prepare_qmi_response() - prepare stream enable response
 * @subs: usb substream
 * @req_msg: QMI request message
 * @resp: QMI response buffer
 * @info_idx: usb interface array index
 *
 * Prepares the QMI response for a USB QMI stream enable request.  Will parse
 * out the parameters within the stream enable request, in order to match
 * requested audio profile to the ones exposed by the USB device connected.
 *
 * In addition, will fetch the XHCI transfer resources needed for the handoff to
 * happen.  This includes, transfer ring and buffer addresses and secondary event
 * ring address.  These parameters will be communicated as part of the USB QMI
 * stream enable response.
 *
 */
static int prepare_qmi_response(struct snd_usb_substream *subs,
				struct qmi_uaudio_stream_req_msg_v01 *req_msg,
				struct qmi_uaudio_stream_resp_msg_v01 *resp,
				int info_idx)
{
	struct q6usb_offload *data;
	int pcm_dev_num;
	int card_num;
	void *xfer_buf_cpu;
	int ret;

	pcm_dev_num = (req_msg->usb_token & QMI_STREAM_REQ_DEV_NUM_MASK) >> 8;
	card_num = (req_msg->usb_token & QMI_STREAM_REQ_CARD_NUM_MASK) >> 16;

	if (!uadev[card_num].ctrl_intf) {
		dev_err(&subs->dev->dev, "audio ctrl intf info not cached\n");
		return -ENODEV;
	}

	ret = uaudio_populate_uac_desc(subs, resp);
	if (ret < 0)
		return ret;

	resp->slot_id = subs->dev->slot_id;
	resp->slot_id_valid = 1;

	data = snd_soc_usb_find_priv_data(uaudio_qdev->auxdev->dev.parent);
	if (!data) {
		dev_err(&subs->dev->dev, "No private data found\n");
		return -ENODEV;
	}

	uaudio_qdev->data = data;

	resp->std_as_opr_intf_desc_valid = 1;
	ret = uaudio_endpoint_setup(subs, subs->data_endpoint, card_num,
				    &resp->xhci_mem_info.tr_data,
				    &resp->std_as_data_ep_desc);
	if (ret < 0)
		return ret;

	resp->std_as_data_ep_desc_valid = 1;

	if (subs->sync_endpoint) {
		ret = uaudio_endpoint_setup(subs, subs->sync_endpoint, card_num,
					    &resp->xhci_mem_info.tr_sync,
					    &resp->std_as_sync_ep_desc);
		if (ret < 0)
			goto drop_data_ep;

		resp->std_as_sync_ep_desc_valid = 1;
	}

	resp->interrupter_num_valid = 1;
	resp->controller_num_valid = 0;
	ret = usb_get_controller_id(subs->dev);
	if (ret >= 0) {
		resp->controller_num = ret;
		resp->controller_num_valid = 1;
	}

	/* event ring */
	ret = uaudio_event_ring_setup(subs, card_num,
				      &resp->xhci_mem_info.evt_ring);
	if (ret < 0)
		goto drop_sync_ep;

	uaudio_qdev->er_mapped = true;
	resp->interrupter_num = xhci_sideband_interrupter_id(uadev[card_num].sb);

	resp->speed_info = get_speed_info(subs->dev->speed);
	if (resp->speed_info == USB_QMI_DEVICE_SPEED_INVALID_V01) {
		ret = -ENODEV;
		goto free_sec_ring;
	}

	resp->speed_info_valid = 1;

	ret = uaudio_transfer_buffer_setup(subs, &xfer_buf_cpu, req_msg->xfer_buff_size,
					   &resp->xhci_mem_info.xfer_buff);
	if (ret < 0) {
		ret = -ENOMEM;
		goto free_sec_ring;
	}

	resp->xhci_mem_info_valid = 1;

	if (!atomic_read(&uadev[card_num].in_use)) {
		kref_init(&uadev[card_num].kref);
		init_waitqueue_head(&uadev[card_num].disconnect_wq);
		uadev[card_num].num_intf =
			subs->dev->config->desc.bNumInterfaces;
		uadev[card_num].info = kcalloc(uadev[card_num].num_intf,
					       sizeof(struct intf_info),
					       GFP_KERNEL);
		if (!uadev[card_num].info) {
			ret = -ENOMEM;
			goto unmap_er;
		}
		uadev[card_num].udev = subs->dev;
		atomic_set(&uadev[card_num].in_use, 1);
	} else {
		kref_get(&uadev[card_num].kref);
	}

	uadev[card_num].usb_core_id = resp->controller_num;

	/* cache intf specific info to use it for unmap and free xfer buf */
	uadev[card_num].info[info_idx].data_xfer_ring_va =
					IOVA_MASK(resp->xhci_mem_info.tr_data.iova);
	uadev[card_num].info[info_idx].data_xfer_ring_size = PAGE_SIZE;
	uadev[card_num].info[info_idx].sync_xfer_ring_va =
					IOVA_MASK(resp->xhci_mem_info.tr_sync.iova);
	uadev[card_num].info[info_idx].sync_xfer_ring_size = PAGE_SIZE;
	uadev[card_num].info[info_idx].xfer_buf_iova =
					IOVA_MASK(resp->xhci_mem_info.xfer_buff.iova);
	uadev[card_num].info[info_idx].xfer_buf_dma =
					resp->xhci_mem_info.xfer_buff.dma;
	uadev[card_num].info[info_idx].xfer_buf_size =
					resp->xhci_mem_info.xfer_buff.size;
	uadev[card_num].info[info_idx].data_ep_pipe = subs->data_endpoint ?
						subs->data_endpoint->pipe : 0;
	uadev[card_num].info[info_idx].sync_ep_pipe = subs->sync_endpoint ?
						subs->sync_endpoint->pipe : 0;
	uadev[card_num].info[info_idx].data_ep_idx = subs->data_endpoint ?
						subs->data_endpoint->ep_num : 0;
	uadev[card_num].info[info_idx].sync_ep_idx = subs->sync_endpoint ?
						subs->sync_endpoint->ep_num : 0;
	uadev[card_num].info[info_idx].xfer_buf_cpu = xfer_buf_cpu;
	uadev[card_num].info[info_idx].pcm_card_num = card_num;
	uadev[card_num].info[info_idx].pcm_dev_num = pcm_dev_num;
	uadev[card_num].info[info_idx].direction = subs->direction;
	uadev[card_num].info[info_idx].intf_num = subs->cur_audiofmt->iface;
	uadev[card_num].info[info_idx].in_use = true;

	set_bit(card_num, &uaudio_qdev->card_slot);

	return 0;

unmap_er:
	uaudio_iommu_unmap(MEM_EVENT_RING, IOVA_BASE, PAGE_SIZE, PAGE_SIZE);
free_sec_ring:
	xhci_sideband_remove_interrupter(uadev[card_num].sb);
drop_sync_ep:
	if (subs->sync_endpoint) {
		uaudio_iommu_unmap(MEM_XFER_RING,
				   IOVA_MASK(resp->xhci_mem_info.tr_sync.iova),
				   PAGE_SIZE, PAGE_SIZE);
		xhci_sideband_remove_endpoint(uadev[card_num].sb,
			usb_pipe_endpoint(subs->dev, subs->sync_endpoint->pipe));
	}
drop_data_ep:
	uaudio_iommu_unmap(MEM_XFER_RING, IOVA_MASK(resp->xhci_mem_info.tr_data.iova),
			   PAGE_SIZE, PAGE_SIZE);
	xhci_sideband_remove_endpoint(uadev[card_num].sb,
			usb_pipe_endpoint(subs->dev, subs->data_endpoint->pipe));

	return ret;
}

/**
 * handle_uaudio_stream_req() - handle stream enable/disable request
 * @handle: QMI client handle
 * @sq: qrtr socket
 * @txn: QMI transaction context
 * @decoded_msg: decoded QMI message
 *
 * Main handler for the QMI stream enable/disable requests.  This executes the
 * corresponding enable/disable stream apis, respectively.
 *
 */
static void handle_uaudio_stream_req(struct qmi_handle *handle,
				     struct sockaddr_qrtr *sq,
				     struct qmi_txn *txn,
				     const void *decoded_msg)
{
	struct qmi_uaudio_stream_req_msg_v01 *req_msg;
	struct qmi_uaudio_stream_resp_msg_v01 resp = {{0}, 0};
	struct uaudio_qmi_svc *svc = uaudio_svc;
	struct snd_usb_audio *chip = NULL;
	struct snd_usb_substream *subs;
	struct usb_host_endpoint *ep;
	int datainterval = -EINVAL;
	int info_idx = -EINVAL;
	struct intf_info *info;
	u8 pcm_card_num;
	u8 pcm_dev_num;
	u8 direction;
	int ret = 0;

	if (!svc->client_connected) {
		svc->client_sq = *sq;
		svc->client_connected = true;
	}

	mutex_lock(&qdev_mutex);
	req_msg = (struct qmi_uaudio_stream_req_msg_v01 *)decoded_msg;
	if (!req_msg->audio_format_valid || !req_msg->bit_rate_valid ||
	    !req_msg->number_of_ch_valid || !req_msg->xfer_buff_size_valid) {
		ret = -EINVAL;
		goto response;
	}

	if (!uaudio_qdev) {
		ret = -EINVAL;
		goto response;
	}

	direction = (req_msg->usb_token & QMI_STREAM_REQ_DIRECTION);
	pcm_dev_num = (req_msg->usb_token & QMI_STREAM_REQ_DEV_NUM_MASK) >> 8;
	pcm_card_num = (req_msg->usb_token & QMI_STREAM_REQ_CARD_NUM_MASK) >> 16;
	if (pcm_card_num >= SNDRV_CARDS) {
		ret = -EINVAL;
		goto response;
	}

	if (req_msg->audio_format > USB_QMI_PCM_FORMAT_U32_BE) {
		ret = -EINVAL;
		goto response;
	}

	subs = find_substream(pcm_card_num, pcm_dev_num, direction);
	chip = uadev[pcm_card_num].chip;
	if (!subs || !chip || atomic_read(&chip->shutdown)) {
		ret = -ENODEV;
		goto response;
	}

	info_idx = info_idx_from_ifnum(pcm_card_num, subs->cur_audiofmt ?
			subs->cur_audiofmt->iface : -1, req_msg->enable);
	if (atomic_read(&chip->shutdown) || !subs->stream || !subs->stream->pcm ||
	    !subs->stream->chip) {
		ret = -ENODEV;
		goto response;
	}

	mutex_lock(&chip->mutex);
	if (req_msg->enable) {
		if (info_idx < 0 || chip->system_suspend || subs->opened) {
			ret = -EBUSY;
			mutex_unlock(&chip->mutex);

			goto response;
		}
		subs->opened = 1;
	}
	mutex_unlock(&chip->mutex);

	if (req_msg->service_interval_valid) {
		ret = get_data_interval_from_si(subs,
						req_msg->service_interval);
		if (ret == -EINVAL)
			goto response;

		datainterval = ret;
	}

	uadev[pcm_card_num].ctrl_intf = chip->ctrl_intf;

	if (req_msg->enable) {
		ret = enable_audio_stream(subs,
					  map_pcm_format(req_msg->audio_format),
					  req_msg->number_of_ch, req_msg->bit_rate,
					  datainterval);

		if (!ret)
			ret = prepare_qmi_response(subs, req_msg, &resp,
						   info_idx);
		if (ret < 0) {
			mutex_lock(&chip->mutex);
			subs->opened = 0;
			mutex_unlock(&chip->mutex);
		}
	} else {
		info = &uadev[pcm_card_num].info[info_idx];
		if (info->data_ep_pipe) {
			ep = usb_pipe_endpoint(uadev[pcm_card_num].udev,
					       info->data_ep_pipe);
			if (ep) {
				xhci_sideband_stop_endpoint(uadev[pcm_card_num].sb,
							    ep);
				xhci_sideband_remove_endpoint(uadev[pcm_card_num].sb,
							      ep);
			}

			info->data_ep_pipe = 0;
		}

		if (info->sync_ep_pipe) {
			ep = usb_pipe_endpoint(uadev[pcm_card_num].udev,
					       info->sync_ep_pipe);
			if (ep) {
				xhci_sideband_stop_endpoint(uadev[pcm_card_num].sb,
							    ep);
				xhci_sideband_remove_endpoint(uadev[pcm_card_num].sb,
							      ep);
			}

			info->sync_ep_pipe = 0;
		}

		disable_audio_stream(subs);
		mutex_lock(&chip->mutex);
		subs->opened = 0;
		mutex_unlock(&chip->mutex);
	}

response:
	if (!req_msg->enable && ret != -EINVAL && ret != -ENODEV) {
		mutex_lock(&chip->mutex);
		if (info_idx >= 0) {
			info = &uadev[pcm_card_num].info[info_idx];
			uaudio_dev_intf_cleanup(uadev[pcm_card_num].udev,
						info);
		}
		if (atomic_read(&uadev[pcm_card_num].in_use))
			kref_put(&uadev[pcm_card_num].kref,
				 uaudio_dev_release);
		mutex_unlock(&chip->mutex);
	}
	mutex_unlock(&qdev_mutex);

	resp.usb_token = req_msg->usb_token;
	resp.usb_token_valid = 1;
	resp.internal_status = ret;
	resp.internal_status_valid = 1;
	resp.status = ret ? USB_QMI_STREAM_REQ_FAILURE_V01 : ret;
	resp.status_valid = 1;
	ret = qmi_send_response(svc->uaudio_svc_hdl, sq, txn,
				QMI_UAUDIO_STREAM_RESP_V01,
				QMI_UAUDIO_STREAM_RESP_MSG_V01_MAX_MSG_LEN,
				qmi_uaudio_stream_resp_msg_v01_ei, &resp);
}

static struct qmi_msg_handler uaudio_stream_req_handlers = {
	.type = QMI_REQUEST,
	.msg_id = QMI_UAUDIO_STREAM_REQ_V01,
	.ei = qmi_uaudio_stream_req_msg_v01_ei,
	.decoded_size = QMI_UAUDIO_STREAM_REQ_MSG_V01_MAX_MSG_LEN,
	.fn = handle_uaudio_stream_req,
};

/**
 * qc_usb_audio_offload_init_qmi_dev() - initializes qmi dev
 *
 * Initializes the USB qdev, which is used to carry information pertaining to
 * the offloading resources.  This device is freed only when there are no longer
 * any offloading candidates. (i.e, when all audio devices are disconnected)
 *
 */
static int qc_usb_audio_offload_init_qmi_dev(void)
{
	uaudio_qdev = kzalloc(sizeof(*uaudio_qdev), GFP_KERNEL);
	if (!uaudio_qdev)
		return -ENOMEM;

	/* initialize xfer ring and xfer buf iova list */
	INIT_LIST_HEAD(&uaudio_qdev->xfer_ring_list);
	uaudio_qdev->curr_xfer_ring_iova = IOVA_XFER_RING_BASE;
	uaudio_qdev->xfer_ring_iova_size =
			IOVA_XFER_RING_MAX - IOVA_XFER_RING_BASE;

	INIT_LIST_HEAD(&uaudio_qdev->xfer_buf_list);
	uaudio_qdev->curr_xfer_buf_iova = IOVA_XFER_BUF_BASE;
	uaudio_qdev->xfer_buf_iova_size =
		IOVA_XFER_BUF_MAX - IOVA_XFER_BUF_BASE;

	return 0;
}

/* Populates ppcm_idx array with supported PCM indexes */
static int qc_usb_audio_offload_fill_avail_pcms(struct snd_usb_audio *chip,
						struct snd_soc_usb_device *sdev)
{
	struct snd_usb_stream *as;
	struct snd_usb_substream *subs;
	int idx = 0;

	list_for_each_entry(as, &chip->pcm_list, list) {
		subs = &as->substream[SNDRV_PCM_STREAM_PLAYBACK];
		if (subs->ep_num) {
			sdev->ppcm_idx[idx] = as->pcm->device;
			idx++;
		}
		/*
		 * Break if the current index exceeds the number of possible
		 * playback streams counted from the UAC descriptors.
		 */
		if (idx >= sdev->num_playback)
			break;
	}

	return -1;
}

/**
 * qc_usb_audio_offload_probe() - platform op connect handler
 * @chip: USB SND device
 *
 * Platform connect handler when a USB SND device is detected. Will
 * notify SOC USB about the connection to enable the USB ASoC backend
 * and populate internal USB chip array.
 *
 */
static void qc_usb_audio_offload_probe(struct snd_usb_audio *chip)
{
	struct usb_interface *intf = chip->intf[chip->num_interfaces - 1];
	struct usb_interface_descriptor *altsd;
	struct usb_host_interface *alts;
	struct snd_soc_usb_device *sdev;
	struct xhci_sideband *sb;

	/*
	 * If there is no priv_data, or no playback paths, the connected
	 * device doesn't support offloading.  Avoid populating entries for
	 * this device.
	 */
	if (!snd_soc_usb_find_priv_data(uaudio_qdev->auxdev->dev.parent) ||
	    !usb_qmi_get_pcm_num(chip, 0))
		return;

	mutex_lock(&qdev_mutex);
	mutex_lock(&chip->mutex);
	if (!uadev[chip->card->number].chip) {
		sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
		if (!sdev)
			goto exit;

		sb = xhci_sideband_register(intf, XHCI_SIDEBAND_VENDOR,
					    uaudio_sideband_notifier);
		if (!sb)
			goto free_sdev;
	} else {
		sb = uadev[chip->card->number].sb;
		sdev = uadev[chip->card->number].sdev;
	}

	uadev[chip->card->number].sb = sb;
	uadev[chip->card->number].chip = chip;
	uadev[chip->card->number].sdev = sdev;

	alts = &intf->altsetting[0];
	altsd = get_iface_desc(alts);

	/* Wait until all PCM devices are populated before notifying soc-usb */
	if (altsd->bInterfaceNumber == chip->last_iface) {
		sdev->num_playback = usb_qmi_get_pcm_num(chip, 0);

		/*
		 * Allocate playback pcm index array based on number of possible
		 * playback paths within the UAC descriptors.
		 */
		sdev->ppcm_idx = kcalloc(sdev->num_playback, sizeof(unsigned int),
					 GFP_KERNEL);
		if (!sdev->ppcm_idx)
			goto unreg_xhci;

		qc_usb_audio_offload_fill_avail_pcms(chip, sdev);
		sdev->card_idx = chip->card->number;
		sdev->chip_idx = chip->index;

		snd_usb_offload_create_ctl(chip, uaudio_qdev->auxdev->dev.parent);
		snd_soc_usb_connect(uaudio_qdev->auxdev->dev.parent, sdev);
	}

	mutex_unlock(&chip->mutex);
	mutex_unlock(&qdev_mutex);

	return;

unreg_xhci:
	xhci_sideband_unregister(sb);
	uadev[chip->card->number].sb = NULL;
free_sdev:
	kfree(sdev);
	uadev[chip->card->number].sdev = NULL;
	uadev[chip->card->number].chip = NULL;
exit:
	mutex_unlock(&chip->mutex);
	mutex_unlock(&qdev_mutex);
}

/**
 * qc_usb_audio_cleanup_qmi_dev() - release qmi device
 *
 * Frees the USB qdev.  Only occurs when there are no longer any potential
 * devices that can utilize USB audio offloading.
 *
 */
static void qc_usb_audio_cleanup_qmi_dev(void)
{
	kfree(uaudio_qdev);
	uaudio_qdev = NULL;
}

/**
 * qc_usb_audio_offload_disconnect() - platform op disconnect handler
 * @chip: USB SND device
 *
 * Platform disconnect handler.  Will ensure that any pending stream is
 * halted by issuing a QMI disconnect indication packet to the adsp.
 *
 */
static void qc_usb_audio_offload_disconnect(struct snd_usb_audio *chip)
{
	struct uaudio_dev *dev;
	int card_num;

	if (!chip)
		return;

	card_num = chip->card->number;
	if (card_num >= SNDRV_CARDS)
		return;

	mutex_lock(&qdev_mutex);
	mutex_lock(&chip->mutex);
	dev = &uadev[card_num];

	/* Device has already been cleaned up, or never populated */
	if (!dev->chip) {
		mutex_unlock(&qdev_mutex);
		mutex_unlock(&chip->mutex);
		return;
	}

	/* cleaned up already */
	if (!dev->udev)
		goto done;

	uaudio_send_disconnect_ind(chip);
	uaudio_dev_cleanup(dev);
done:
	/*
	 * If num_interfaces == 1, the last USB SND interface is being removed.
	 * This is to accommodate for devices w/ multiple UAC functions.
	 */
	if (chip->num_interfaces == 1) {
		snd_soc_usb_disconnect(uaudio_qdev->auxdev->dev.parent, dev->sdev);
		xhci_sideband_unregister(dev->sb);
		dev->chip = NULL;
		kfree(dev->sdev->ppcm_idx);
		kfree(dev->sdev);
		dev->sdev = NULL;
	}
	mutex_unlock(&chip->mutex);

	mutex_unlock(&qdev_mutex);
}

/**
 * qc_usb_audio_offload_suspend() - USB offload PM suspend handler
 * @intf: USB interface
 * @message: suspend type
 *
 * PM suspend handler to ensure that the USB offloading driver is able to stop
 * any pending traffic, so that the bus can be suspended.
 *
 */
static void qc_usb_audio_offload_suspend(struct usb_interface *intf,
					 pm_message_t message)
{
	struct snd_usb_audio *chip = usb_get_intfdata(intf);
	int card_num;

	if (!chip)
		return;

	card_num = chip->card->number;
	if (card_num >= SNDRV_CARDS)
		return;

	mutex_lock(&qdev_mutex);
	mutex_lock(&chip->mutex);

	uaudio_send_disconnect_ind(chip);

	mutex_unlock(&qdev_mutex);
	mutex_unlock(&chip->mutex);
}

static struct snd_usb_platform_ops offload_ops = {
	.connect_cb = qc_usb_audio_offload_probe,
	.disconnect_cb = qc_usb_audio_offload_disconnect,
	.suspend_cb = qc_usb_audio_offload_suspend,
};

static int qc_usb_audio_probe(struct auxiliary_device *auxdev,
			  const struct auxiliary_device_id *id)

{
	struct uaudio_qmi_svc *svc;
	int ret;

	svc = kzalloc(sizeof(*svc), GFP_KERNEL);
	if (!svc)
		return -ENOMEM;

	svc->uaudio_svc_hdl = kzalloc(sizeof(*svc->uaudio_svc_hdl), GFP_KERNEL);
	if (!svc->uaudio_svc_hdl) {
		ret = -ENOMEM;
		goto free_svc;
	}

	ret = qmi_handle_init(svc->uaudio_svc_hdl,
			      QMI_UAUDIO_STREAM_REQ_MSG_V01_MAX_MSG_LEN,
			      &uaudio_svc_ops_options,
			      &uaudio_stream_req_handlers);
	ret = qmi_add_server(svc->uaudio_svc_hdl, UAUDIO_STREAM_SERVICE_ID_V01,
			     UAUDIO_STREAM_SERVICE_VERS_V01, 0);

	uaudio_svc = svc;

	qc_usb_audio_offload_init_qmi_dev();
	uaudio_qdev->auxdev = auxdev;

	ret = snd_usb_register_platform_ops(&offload_ops);
	if (ret < 0)
		goto release_qmi;

	snd_usb_rediscover_devices();

	return 0;

release_qmi:
	qc_usb_audio_cleanup_qmi_dev();
	qmi_handle_release(svc->uaudio_svc_hdl);
free_svc:
	kfree(svc);

	return ret;
}

static void qc_usb_audio_remove(struct auxiliary_device *auxdev)
{
	struct uaudio_qmi_svc *svc = uaudio_svc;
	int idx;

	/*
	 * Remove all connected devices after unregistering ops, to ensure
	 * that no further connect events will occur.  The disconnect routine
	 * will issue the QMI disconnect indication, which results in the
	 * external DSP to stop issuing transfers.
	 */
	snd_usb_unregister_platform_ops();
	for (idx = 0; idx < SNDRV_CARDS; idx++)
		qc_usb_audio_offload_disconnect(uadev[idx].chip);

	qc_usb_audio_cleanup_qmi_dev();

	qmi_handle_release(svc->uaudio_svc_hdl);
	kfree(svc);
	uaudio_svc = NULL;
}

static const struct auxiliary_device_id qc_usb_audio_table[] = {
	{ .name = "q6usb.qc-usb-audio-offload" },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, qc_usb_audio_table);

static struct auxiliary_driver qc_usb_audio_offload_drv = {
	.name = "qc-usb-audio-offload",
	.id_table = qc_usb_audio_table,
	.probe = qc_usb_audio_probe,
	.remove = qc_usb_audio_remove,
};
module_auxiliary_driver(qc_usb_audio_offload_drv);

MODULE_DESCRIPTION("QC USB Audio Offloading");
MODULE_LICENSE("GPL");
