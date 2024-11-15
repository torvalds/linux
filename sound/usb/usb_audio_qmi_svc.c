// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/uaccess.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/core.h>
#include <sound/asound.h>
#include <linux/usb.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/iommu.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/platform_device.h>
#include <linux/usb/audio-v3.h>
#include <linux/ipc_logging.h>
#include <trace/hooks/audio_usboffload.h>

#include "usbaudio.h"
#include "card.h"
#include "endpoint.h"
#include "helper.h"
#include "pcm.h"
#include "power.h"
#include "usb_audio_qmi_v01.h"

#define BUS_INTERVAL_FULL_SPEED 1000 /* in us */
#define BUS_INTERVAL_HIGHSPEED_AND_ABOVE 125 /* in us */
#define MAX_BINTERVAL_ISOC_EP 16
#define DEV_RELEASE_WAIT_TIMEOUT 10000 /* in ms */

#define SND_PCM_CARD_NUM_MASK 0xffff0000
#define SND_PCM_DEV_NUM_MASK 0xff00
#define SND_PCM_STREAM_DIRECTION 0xff

#define PREPEND_SID_TO_IOVA(iova, sid) ((u64)(((u64)(iova)) | \
					(((u64)sid) << 32)))

/*  event ring iova base address */
#define IOVA_BASE 0x1000

#define IOVA_XFER_RING_BASE (IOVA_BASE + PAGE_SIZE * (SNDRV_CARDS + 1))
#define IOVA_XFER_BUF_BASE (IOVA_XFER_RING_BASE + PAGE_SIZE * SNDRV_CARDS * 32)
#define IOVA_XFER_RING_MAX (IOVA_XFER_BUF_BASE - PAGE_SIZE)
#define IOVA_XFER_BUF_MAX (0xfffff000 - PAGE_SIZE)

#define MAX_XFER_BUFF_LEN (24 * PAGE_SIZE)

struct xhci_ring;

struct xhci_ring *xhci_sec_event_ring_setup(struct usb_device *udev,
		unsigned int intr_num);
int xhci_sec_event_ring_cleanup(struct usb_device *udev, struct xhci_ring *ring);
phys_addr_t xhci_get_sec_event_ring_phys_addr(struct usb_device *udev,
		struct xhci_ring *ring, dma_addr_t *dma);
phys_addr_t xhci_get_xfer_ring_phys_addr(struct usb_device *udev,
		struct usb_host_endpoint *ep, dma_addr_t *dma);
int xhci_stop_endpoint(struct usb_device *udev, struct usb_host_endpoint *ep);

struct iova_info {
	struct list_head list;
	unsigned long start_iova;
	size_t size;
	bool in_use;
};

struct intf_info {
	unsigned long data_xfer_ring_va;
	size_t data_xfer_ring_size;
	unsigned long sync_xfer_ring_va;
	size_t sync_xfer_ring_size;
	unsigned long xfer_buf_va;
	size_t xfer_buf_size;
	phys_addr_t xfer_buf_pa;
	unsigned int data_ep_pipe;
	unsigned int sync_ep_pipe;
	u8 *xfer_buf;
	u8 intf_num;
	u8 pcm_card_num;
	u8 pcm_dev_num;
	u8 direction;
	bool in_use;
};

struct uaudio_dev {
	struct usb_device *udev;
	/* audio control interface */
	struct usb_host_interface *ctrl_intf;
	unsigned int card_num;
	unsigned int usb_core_id;
	atomic_t in_use;
	struct kref kref;
	wait_queue_head_t disconnect_wq;

	/* interface specific */
	int num_intf;
	struct intf_info *info;
	struct snd_usb_audio *chip;
};

static struct uaudio_dev uadev[SNDRV_CARDS];

struct uaudio_qmi_dev {
	struct device *dev;
	u32 sid;
	u32 intr_num;
	struct xhci_ring *sec_ring;
	struct iommu_domain *domain;

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

static struct uaudio_qmi_dev *uaudio_qdev;

struct uaudio_qmi_svc {
	struct qmi_handle *uaudio_svc_hdl;
	struct work_struct qmi_disconnect_work;
	struct workqueue_struct *uaudio_wq;
	struct sockaddr_qrtr client_sq;
	bool client_connected;
	void *uaudio_ipc_log;
};

static struct uaudio_qmi_svc *uaudio_svc;
static void handle_uaudio_stream_req(struct qmi_handle *handle,
			struct sockaddr_qrtr *sq,
			struct qmi_txn *txn,
			const void *decoded_msg);

static struct qmi_msg_handler uaudio_stream_req_handlers = {
	.type = QMI_REQUEST,
	.msg_id = QMI_UAUDIO_STREAM_REQ_V01,
	.ei = qmi_uaudio_stream_req_msg_v01_ei,
	.decoded_size = QMI_UAUDIO_STREAM_REQ_MSG_V01_MAX_MSG_LEN,
	.fn = handle_uaudio_stream_req,
};

enum mem_type {
	MEM_EVENT_RING,
	MEM_XFER_RING,
	MEM_XFER_BUF,
};

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

#define uaudio_print(level, fmt, ...) do { \
	ipc_log_string(uaudio_svc->uaudio_ipc_log, "%s%s: " fmt, "", __func__,\
			##__VA_ARGS__); \
	printk("%s%s: " fmt, level, __func__, ##__VA_ARGS__); \
	} while (0)

#ifdef CONFIG_DYNAMIC_DEBUG
#define uaudio_dbg(fmt, ...) do { \
	ipc_log_string(uaudio_svc->uaudio_ipc_log, "%s: " fmt, __func__,\
			##__VA_ARGS__); \
	dynamic_pr_debug("%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)
#else
#define uaudio_dbg(fmt, ...) uaudio_print(KERN_DEBUG, fmt, ##__VA_ARGS__)
#endif

#define uaudio_info(fmt, ...) uaudio_print(KERN_INFO, fmt, ##__VA_ARGS__)
#define uaudio_err(fmt, ...) uaudio_print(KERN_ERR, fmt, ##__VA_ARGS__)

#define NUM_LOG_PAGES		10

static int uaudio_snd_usb_pcm_change_state(struct snd_usb_substream *subs, int state)
{
	int ret;

	if (!subs->str_pd)
		return 0;

	ret = snd_usb_power_domain_set(subs->stream->chip, subs->str_pd, state);
	if (ret < 0) {
		dev_err(&subs->dev->dev,
			"Cannot change Power Domain ID: %d to state: %d. Err: %d\n",
			subs->str_pd->pd_id, state, ret);
		return ret;
	}

	return 0;
}

static void uaudio_iommu_unmap(enum mem_type mtype, unsigned long va,
	size_t iova_size, size_t mapped_iova_size);

static enum usb_audio_device_speed_enum_v01
get_speed_info(enum usb_device_speed udev_speed)
{
	switch (udev_speed) {
	case USB_SPEED_LOW:
		return USB_AUDIO_DEVICE_SPEED_LOW_V01;
	case USB_SPEED_FULL:
		return USB_AUDIO_DEVICE_SPEED_FULL_V01;
	case USB_SPEED_HIGH:
		return USB_AUDIO_DEVICE_SPEED_HIGH_V01;
	case USB_SPEED_SUPER:
		return USB_AUDIO_DEVICE_SPEED_SUPER_V01;
	case USB_SPEED_SUPER_PLUS:
		return USB_AUDIO_DEVICE_SPEED_SUPER_PLUS_V01;
	default:
		uaudio_err("udev speed %d\n", udev_speed);
		return USB_AUDIO_DEVICE_SPEED_INVALID_V01;
	}
}

static unsigned long uaudio_get_iova(unsigned long *curr_iova,
	size_t *curr_iova_size, struct list_head *head, size_t size)
{
	struct iova_info *info, *new_info = NULL;
	struct list_head *curr_head;
	unsigned long va = 0;
	size_t tmp_size = size;
	bool found = false;

	if (size % PAGE_SIZE) {
		uaudio_err("size %zu is not page size multiple\n", size);
		goto done;
	}

	if (size > *curr_iova_size) {
		uaudio_err("size %zu > curr size %zu\n", size, *curr_iova_size);
		goto done;
	}
	if (*curr_iova_size == 0) {
		uaudio_err("iova mapping is full\n");
		goto done;
	}

	list_for_each_entry(info, head, list) {
		/* exact size iova_info */
		if (!info->in_use && info->size == size) {
			info->in_use = true;
			va = info->start_iova;
			*curr_iova_size -= size;
			found = true;
			uaudio_dbg("exact size: %zu found\n", size);
			goto done;
		} else if (!info->in_use && tmp_size >= info->size) {
			if (!new_info)
				new_info = info;
			uaudio_dbg("partial size: %zu found\n", info->size);
			tmp_size -= info->size;
			if (tmp_size)
				continue;

			va = new_info->start_iova;
			for (curr_head = &new_info->list; curr_head !=
			&info->list; curr_head = curr_head->next) {
				new_info = list_entry(curr_head, struct
						iova_info, list);
				new_info->in_use = true;
			}
			info->in_use = true;
			*curr_iova_size -= size;
			found = true;
			goto done;
		} else {
			/* iova region in use */
			new_info = NULL;
			tmp_size = size;
		}
	}

	info = kzalloc(sizeof(struct iova_info), GFP_KERNEL);
	if (!info) {
		va = 0;
		goto done;
	}

	va = info->start_iova = *curr_iova;
	info->size = size;
	info->in_use = true;
	*curr_iova += size;
	*curr_iova_size -= size;
	found = true;
	list_add_tail(&info->list, head);

done:
	if (!found)
		uaudio_err("unable to find %zu size iova\n", size);
	else
		uaudio_dbg("va:0x%08lx curr_iova:0x%08lx curr_iova_size:%zu\n",
				va, *curr_iova, *curr_iova_size);

	return va;
}

static unsigned long uaudio_iommu_map(enum mem_type mtype, bool dma_coherent,
		phys_addr_t pa, size_t size, struct sg_table *sgt)
{
	unsigned long va_sg, va = 0;
	bool map = true;
	int i, ret;
	size_t sg_len, total_len = 0;
	struct scatterlist *sg;
	phys_addr_t pa_sg;
	int prot = IOMMU_READ | IOMMU_WRITE;

	if (dma_coherent)
		prot |= IOMMU_CACHE;

	switch (mtype) {
	case MEM_EVENT_RING:
		va = IOVA_BASE;
		/* er already mapped */
		if (uaudio_qdev->er_mapped)
			map = false;
		break;
	case MEM_XFER_RING:
		va = uaudio_get_iova(&uaudio_qdev->curr_xfer_ring_iova,
		&uaudio_qdev->xfer_ring_iova_size, &uaudio_qdev->xfer_ring_list,
		size);
		break;
	case MEM_XFER_BUF:
		va = uaudio_get_iova(&uaudio_qdev->curr_xfer_buf_iova,
		&uaudio_qdev->xfer_buf_iova_size, &uaudio_qdev->xfer_buf_list,
		size);
		break;
	default:
		uaudio_err("unknown mem type %d\n", mtype);
	}

	if (!va || !map)
		goto done;

	if (!sgt)
		goto skip_sgt_map;

	va_sg = va;
	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		sg_len = PAGE_ALIGN(sg->offset + sg->length);
		pa_sg = page_to_phys(sg_page(sg));
		ret = iommu_map(uaudio_qdev->domain, va_sg, pa_sg, sg_len,
								prot);
		if (ret) {
			uaudio_err("mapping failed ret%d\n", ret);
			uaudio_err("type:%d, pa:%pa iova:0x%08lx sg_len:%zu\n",
				mtype, &pa_sg, va_sg, sg_len);
			uaudio_iommu_unmap(MEM_XFER_BUF, va, size, total_len);
			va = 0;
			goto done;
		}
		uaudio_dbg("type:%d map pa:%pa to iova:0x%08lx len:%zu offset:%u\n",
				mtype, &pa_sg, va_sg, sg_len, sg->offset);
		va_sg += sg_len;
		total_len += sg_len;
	}

	if (size != total_len) {
		uaudio_err("iova size %zu != mapped iova size %zu\n", size,
				total_len);
		uaudio_iommu_unmap(MEM_XFER_BUF, va, size, total_len);
		va = 0;
	}
	return va;

skip_sgt_map:
	uaudio_dbg("type:%d map pa:%pa to iova:0x%08lx size:%zu\n", mtype, &pa,
			va, size);

	ret = iommu_map(uaudio_qdev->domain, va, pa, size, prot);
	if (ret)
		uaudio_err("failed to map pa:%pa iova:0x%lx type:%d ret:%d\n",
				&pa, va, mtype, ret);
done:
	return va;
}

static void uaudio_put_iova(unsigned long va, size_t size, struct list_head
	*head, size_t *curr_iova_size)
{
	struct iova_info *info;
	size_t tmp_size = size;
	bool found = false;

	list_for_each_entry(info, head, list) {
		if (info->start_iova == va) {
			if (!info->in_use) {
				uaudio_err("va %lu is not in use\n", va);
				return;
			}
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

	if (!found) {
		uaudio_err("unable to find the va %lu\n", va);
		return;
	}
done:
	*curr_iova_size += size;
	uaudio_dbg("curr_iova_size %zu\n", *curr_iova_size);
}

static void uaudio_iommu_unmap(enum mem_type mtype, unsigned long va,
	size_t iova_size, size_t mapped_iova_size)
{
	size_t umap_size;
	bool unmap = true;

	if (!va || !iova_size)
		return;

	switch (mtype) {
	case MEM_EVENT_RING:
		if (uaudio_qdev->er_mapped)
			uaudio_qdev->er_mapped = false;
		else
			unmap = false;
		break;

	case MEM_XFER_RING:
		uaudio_put_iova(va, iova_size, &uaudio_qdev->xfer_ring_list,
		&uaudio_qdev->xfer_ring_iova_size);
		break;
	case MEM_XFER_BUF:
		uaudio_put_iova(va, iova_size, &uaudio_qdev->xfer_buf_list,
		&uaudio_qdev->xfer_buf_iova_size);
		break;
	default:
		uaudio_err("unknown mem type %d\n", mtype);
		unmap = false;
	}

	if (!unmap || !mapped_iova_size)
		return;

	uaudio_dbg("type %d: unmap iova 0x%08lx size %zu\n", mtype, va,
			mapped_iova_size);

	umap_size = iommu_unmap(uaudio_qdev->domain, va, mapped_iova_size);
	if (umap_size != mapped_iova_size)
		uaudio_err("unmapped size %zu for iova 0x%08lx of mapped size %zu\n",
				umap_size, va, mapped_iova_size);
}

/* looks up alias, if any, for controller DT node and returns the index */
static int usb_get_controller_id(struct usb_device *udev)
{
	if (udev->bus->sysdev && udev->bus->sysdev->of_node)
		return of_alias_get_id(udev->bus->sysdev->of_node, "usb");

	return -ENODEV;
}

static void *find_csint_desc(unsigned char *descstart, int desclen, u8 dsubtype)
{
	u8 *p, *end, *next;

	p = descstart;
	end = p + desclen;
	while (p < end) {
		if (p[0] < 2)
			return NULL;
		next = p + p[0];
		if (next > end)
			return NULL;
		if (p[1] == USB_DT_CS_INTERFACE && p[2] == dsubtype)
			return p;
		p = next;
	}
	return NULL;
}

static int prepare_qmi_response(struct snd_usb_substream *subs,
		struct qmi_uaudio_stream_req_msg_v01 *req_msg,
		struct qmi_uaudio_stream_resp_msg_v01 *resp, int info_idx)
{
	struct usb_interface *iface;
	struct usb_host_interface *alts;
	struct usb_interface_descriptor *altsd;
	struct usb_interface_assoc_descriptor *assoc;
	struct usb_host_endpoint *ep;
	struct uac_format_type_i_continuous_descriptor *fmt;
	struct uac_format_type_i_discrete_descriptor *fmt_v1;
	struct uac_format_type_i_ext_descriptor *fmt_v2;
	struct uac1_as_header_descriptor *as;
	int ret;
	int protocol, card_num, pcm_dev_num;
	void *hdr_ptr;
	u8 *xfer_buf;
	unsigned int data_ep_pipe = 0, sync_ep_pipe = 0;
	u32 len, mult, remainder, xfer_buf_len;
	unsigned long va, tr_data_va = 0, tr_sync_va = 0;
	phys_addr_t xhci_pa, xfer_buf_pa, tr_data_pa = 0, tr_sync_pa = 0;
	dma_addr_t dma;
	struct sg_table sgt;
	bool dma_coherent;

	iface = usb_ifnum_to_if(subs->dev, subs->cur_audiofmt->iface);
	if (!iface) {
		uaudio_err("interface # %d does not exist\n", subs->cur_audiofmt->iface);
		ret = -ENODEV;
		goto err;
	}

	assoc = iface->intf_assoc;
	pcm_dev_num = (req_msg->usb_token & SND_PCM_DEV_NUM_MASK) >> 8;
	card_num = (req_msg->usb_token & SND_PCM_CARD_NUM_MASK) >> 16;
	xfer_buf_len = req_msg->xfer_buff_size;

	alts = &iface->altsetting[subs->cur_audiofmt->altset_idx];
	altsd = get_iface_desc(alts);
	protocol = altsd->bInterfaceProtocol;

	/* get format type */
	if (protocol != UAC_VERSION_3) {
		fmt = find_csint_desc(alts->extra, alts->extralen,
				UAC_FORMAT_TYPE);
		if (!fmt) {
			uaudio_err("%u:%d : no UAC_FORMAT_TYPE desc\n",
					subs->cur_audiofmt->iface,
					subs->cur_audiofmt->altset_idx);
			ret = -ENODEV;
			goto err;
		}
	}

	if (!uadev[card_num].ctrl_intf) {
		uaudio_err("audio ctrl intf info not cached\n");
		ret = -ENODEV;
		goto err;
	}

	if (protocol != UAC_VERSION_3) {
		hdr_ptr = find_csint_desc(uadev[card_num].ctrl_intf->extra,
				uadev[card_num].ctrl_intf->extralen,
				UAC_HEADER);
		if (!hdr_ptr) {
			uaudio_err("no UAC_HEADER desc\n");
			ret = -ENODEV;
			goto err;
		}
	}

	if (protocol == UAC_VERSION_1) {
		struct uac1_ac_header_descriptor *uac1_hdr = hdr_ptr;

		as = find_csint_desc(alts->extra, alts->extralen,
			UAC_AS_GENERAL);
		if (!as) {
			uaudio_err("%u:%d : no UAC_AS_GENERAL desc\n",
					subs->cur_audiofmt->iface,
					subs->cur_audiofmt->altset_idx);
			ret = -ENODEV;
			goto err;
		}
		resp->data_path_delay = as->bDelay;
		resp->data_path_delay_valid = 1;
		fmt_v1 = (struct uac_format_type_i_discrete_descriptor *)fmt;
		resp->usb_audio_subslot_size = fmt_v1->bSubframeSize;
		resp->usb_audio_subslot_size_valid = 1;

		resp->usb_audio_spec_revision = le16_to_cpu(uac1_hdr->bcdADC);
		resp->usb_audio_spec_revision_valid = 1;
	} else if (protocol == UAC_VERSION_2) {
		struct uac2_ac_header_descriptor *uac2_hdr = hdr_ptr;

		fmt_v2 = (struct uac_format_type_i_ext_descriptor *)fmt;
		resp->usb_audio_subslot_size = fmt_v2->bSubslotSize;
		resp->usb_audio_subslot_size_valid = 1;

		resp->usb_audio_spec_revision = le16_to_cpu(uac2_hdr->bcdADC);
		resp->usb_audio_spec_revision_valid = 1;
	} else if (protocol == UAC_VERSION_3) {
		if (assoc->bFunctionSubClass ==
					UAC3_FUNCTION_SUBCLASS_FULL_ADC_3_0) {
			uaudio_err("full adc is not supported\n");
			ret = -EINVAL;
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
			uaudio_err("%d: %u: Invalid wMaxPacketSize\n",
					subs->cur_audiofmt->iface,
					subs->cur_audiofmt->altset_idx);
			ret = -EINVAL;
			goto err;
		}
		resp->usb_audio_subslot_size_valid = 1;
	} else {
		uaudio_err("unknown protocol version %x\n", protocol);
		ret = -ENODEV;
		goto err;
	}

	resp->slot_id = subs->dev->slot_id;
	resp->slot_id_valid = 1;

	memcpy(&resp->std_as_opr_intf_desc, &alts->desc, sizeof(alts->desc));
	resp->std_as_opr_intf_desc_valid = 1;

	ep = usb_pipe_endpoint(subs->dev, subs->data_endpoint->pipe);
	if (!ep) {
		uaudio_err("data ep # %d context is null\n",
				subs->data_endpoint->ep_num);
		ret = -ENODEV;
		goto err;
	}
	data_ep_pipe = subs->data_endpoint->pipe;
	memcpy(&resp->std_as_data_ep_desc, &ep->desc, sizeof(ep->desc));
	resp->std_as_data_ep_desc_valid = 1;

	tr_data_pa = xhci_get_xfer_ring_phys_addr(subs->dev, ep, &dma);
	if (!tr_data_pa) {
		uaudio_err("failed to get data ep ring dma address\n");
		ret = -ENODEV;
		goto err;
	}

	resp->xhci_mem_info.tr_data.pa = dma;

	if (subs->sync_endpoint) {
		ep = usb_pipe_endpoint(subs->dev, subs->sync_endpoint->pipe);
		if (!ep) {
			uaudio_dbg("implicit fb on data ep\n");
			goto skip_sync_ep;
		}
		sync_ep_pipe = subs->sync_endpoint->pipe;
		memcpy(&resp->std_as_sync_ep_desc, &ep->desc, sizeof(ep->desc));
		resp->std_as_sync_ep_desc_valid = 1;

		tr_sync_pa = xhci_get_xfer_ring_phys_addr(subs->dev, ep, &dma);
		if (!tr_sync_pa) {
			uaudio_err("failed to get sync ep ring dma address\n");
			ret = -ENODEV;
			goto err;
		}
		resp->xhci_mem_info.tr_sync.pa = dma;
	}

skip_sync_ep:
	resp->interrupter_num = uaudio_qdev->intr_num;
	resp->interrupter_num_valid = 1;
	resp->controller_num_valid = 0;
	ret = usb_get_controller_id(subs->dev);
	if (ret >= 0) {
		resp->controller_num = ret;
		resp->controller_num_valid = 1;
	}

	/* map xhci data structures PA memory to iova */
	dma_coherent = dev_is_dma_coherent(subs->dev->bus->sysdev);

	/* event ring */
	uaudio_qdev->sec_ring = xhci_sec_event_ring_setup(subs->dev, resp->interrupter_num);
	if (IS_ERR(uaudio_qdev->sec_ring)) {
		ret = PTR_ERR(uaudio_qdev->sec_ring);
		uaudio_err("failed to setup sec event ring ret %d\n", ret);
		goto err;
	}

	xhci_pa = xhci_get_sec_event_ring_phys_addr(subs->dev,
			uaudio_qdev->sec_ring, &dma);
	if (!xhci_pa) {
		uaudio_err("failed to get sec event ring dma address\n");
		ret = -ENODEV;
		goto free_sec_ring;
	}

	va = uaudio_iommu_map(MEM_EVENT_RING, dma_coherent, xhci_pa, PAGE_SIZE,
			NULL);
	if (!va) {
		ret = -ENOMEM;
		goto free_sec_ring;
	}

	resp->xhci_mem_info.evt_ring.va = PREPEND_SID_TO_IOVA(va,
						uaudio_qdev->sid);
	resp->xhci_mem_info.evt_ring.pa = dma;
	resp->xhci_mem_info.evt_ring.size = PAGE_SIZE;
	uaudio_qdev->er_mapped = true;

	resp->speed_info = get_speed_info(subs->dev->speed);
	if (resp->speed_info == USB_AUDIO_DEVICE_SPEED_INVALID_V01) {
		ret = -ENODEV;
		goto unmap_er;
	}

	resp->speed_info_valid = 1;

	/* data transfer ring */
	va = uaudio_iommu_map(MEM_XFER_RING, dma_coherent, tr_data_pa,
			PAGE_SIZE, NULL);
	if (!va) {
		ret = -ENOMEM;
		goto unmap_er;
	}

	tr_data_va = va;
	resp->xhci_mem_info.tr_data.va = PREPEND_SID_TO_IOVA(va,
						uaudio_qdev->sid);
	resp->xhci_mem_info.tr_data.size = PAGE_SIZE;

	/* sync transfer ring */
	if (!resp->xhci_mem_info.tr_sync.pa)
		goto skip_sync;

	xhci_pa = resp->xhci_mem_info.tr_sync.pa;
	va = uaudio_iommu_map(MEM_XFER_RING, dma_coherent, tr_sync_pa,
			PAGE_SIZE, NULL);
	if (!va) {
		ret = -ENOMEM;
		goto unmap_data;
	}

	tr_sync_va = va;
	resp->xhci_mem_info.tr_sync.va = PREPEND_SID_TO_IOVA(va,
						uaudio_qdev->sid);
	resp->xhci_mem_info.tr_sync.size = PAGE_SIZE;

skip_sync:
	/* xfer buffer, multiple of 4K only */
	if (!xfer_buf_len)
		xfer_buf_len = PAGE_SIZE;

	mult = xfer_buf_len / PAGE_SIZE;
	remainder = xfer_buf_len % PAGE_SIZE;
	len = mult * PAGE_SIZE;
	len += remainder ? PAGE_SIZE : 0;

	if (len > MAX_XFER_BUFF_LEN) {
		uaudio_err("req buf len %d > max buf len %lu, setting %lu\n",
				len, MAX_XFER_BUFF_LEN, MAX_XFER_BUFF_LEN);
		len = MAX_XFER_BUFF_LEN;
	}

	xfer_buf = usb_alloc_coherent(subs->dev, len, GFP_KERNEL, &xfer_buf_pa);
	if (!xfer_buf) {
		ret = -ENOMEM;
		goto unmap_sync;
	}

	dma_get_sgtable(subs->dev->bus->sysdev, &sgt, xfer_buf, xfer_buf_pa,
			len);
	va = uaudio_iommu_map(MEM_XFER_BUF, dma_coherent, xfer_buf_pa, len,
			&sgt);
	if (!va) {
		ret = -ENOMEM;
		goto unmap_sync;
	}

	resp->xhci_mem_info.xfer_buff.pa = xfer_buf_pa;
	resp->xhci_mem_info.xfer_buff.size = len;

	resp->xhci_mem_info.xfer_buff.va = PREPEND_SID_TO_IOVA(va,
						uaudio_qdev->sid);

	resp->xhci_mem_info_valid = 1;

	sg_free_table(&sgt);

	if (!atomic_read(&uadev[card_num].in_use)) {
		kref_init(&uadev[card_num].kref);
		init_waitqueue_head(&uadev[card_num].disconnect_wq);
		uadev[card_num].num_intf =
			subs->dev->config->desc.bNumInterfaces;
		uadev[card_num].info = kcalloc(uadev[card_num].num_intf,
			sizeof(struct intf_info), GFP_KERNEL);
		if (!uadev[card_num].info) {
			ret = -ENOMEM;
			goto unmap_sync;
		}
		uadev[card_num].udev = subs->dev;
		atomic_set(&uadev[card_num].in_use, 1);
	} else {
		kref_get(&uadev[card_num].kref);
	}

	uadev[card_num].card_num = card_num;
	uadev[card_num].usb_core_id = resp->controller_num;

	/* cache intf specific info to use it for unmap and free xfer buf */
	uadev[card_num].info[info_idx].data_xfer_ring_va = tr_data_va;
	uadev[card_num].info[info_idx].data_xfer_ring_size = PAGE_SIZE;
	uadev[card_num].info[info_idx].sync_xfer_ring_va = tr_sync_va;
	uadev[card_num].info[info_idx].sync_xfer_ring_size = PAGE_SIZE;
	uadev[card_num].info[info_idx].xfer_buf_va = va;
	uadev[card_num].info[info_idx].xfer_buf_pa = xfer_buf_pa;
	uadev[card_num].info[info_idx].xfer_buf_size = len;
	uadev[card_num].info[info_idx].data_ep_pipe = data_ep_pipe;
	uadev[card_num].info[info_idx].sync_ep_pipe = sync_ep_pipe;
	uadev[card_num].info[info_idx].xfer_buf = xfer_buf;
	uadev[card_num].info[info_idx].pcm_card_num = card_num;
	uadev[card_num].info[info_idx].pcm_dev_num = pcm_dev_num;
	uadev[card_num].info[info_idx].direction = subs->direction;
	uadev[card_num].info[info_idx].intf_num = subs->cur_audiofmt->iface;
	uadev[card_num].info[info_idx].in_use = true;

	set_bit(card_num, &uaudio_qdev->card_slot);

	return 0;

unmap_sync:
	usb_free_coherent(subs->dev, len, xfer_buf, xfer_buf_pa);
	uaudio_iommu_unmap(MEM_XFER_RING, tr_sync_va, PAGE_SIZE, PAGE_SIZE);
unmap_data:
	uaudio_iommu_unmap(MEM_XFER_RING, tr_data_va, PAGE_SIZE, PAGE_SIZE);
unmap_er:
	uaudio_iommu_unmap(MEM_EVENT_RING, IOVA_BASE, PAGE_SIZE, PAGE_SIZE);
free_sec_ring:
	xhci_sec_event_ring_cleanup(subs->dev, uaudio_qdev->sec_ring);
err:
	return ret;
}

static void uaudio_dev_intf_cleanup(struct usb_device *udev,
	struct intf_info *info)
{

	if (!info) {
		uaudio_err("info is NULL\n");
		return;
	}

	uaudio_iommu_unmap(MEM_XFER_RING, info->data_xfer_ring_va,
		info->data_xfer_ring_size, info->data_xfer_ring_size);
	info->data_xfer_ring_va = 0;
	info->data_xfer_ring_size = 0;

	uaudio_iommu_unmap(MEM_XFER_RING, info->sync_xfer_ring_va,
		info->sync_xfer_ring_size, info->sync_xfer_ring_size);
	info->sync_xfer_ring_va = 0;
	info->sync_xfer_ring_size = 0;

	uaudio_iommu_unmap(MEM_XFER_BUF, info->xfer_buf_va,
		info->xfer_buf_size, info->xfer_buf_size);
	info->xfer_buf_va = 0;

	usb_free_coherent(udev, info->xfer_buf_size,
		info->xfer_buf, info->xfer_buf_pa);
	info->xfer_buf_size = 0;
	info->xfer_buf = NULL;
	info->xfer_buf_pa = 0;

	info->in_use = false;
}

static void uaudio_event_ring_cleanup_free(struct uaudio_dev *dev)
{
	clear_bit(dev->card_num, &uaudio_qdev->card_slot);
	/* all audio devices are disconnected */
	if (!uaudio_qdev->card_slot) {
		uaudio_iommu_unmap(MEM_EVENT_RING, IOVA_BASE, PAGE_SIZE,
			PAGE_SIZE);
		xhci_sec_event_ring_cleanup(dev->udev, uaudio_qdev->sec_ring);
		uaudio_dbg("all audio devices disconnected\n");
	}
}

static void uaudio_dev_cleanup(struct uaudio_dev *dev)
{
	int if_idx;

	if (!dev->udev) {
		uaudio_dbg("USB audio device memory is already freed.\n");
		return;
	}

	/* free xfer buffer and unmap xfer ring and buf per interface */
	for (if_idx = 0; if_idx < dev->num_intf; if_idx++) {
		if (!dev->info[if_idx].in_use)
			continue;
		uaudio_dev_intf_cleanup(dev->udev, &dev->info[if_idx]);
		uaudio_dbg("release resources: intf# %d card# %d\n",
				dev->info[if_idx].intf_num, dev->card_num);
	}

	dev->num_intf = 0;

	/* free interface info */
	kfree(dev->info);
	dev->info = NULL;
	uaudio_event_ring_cleanup_free(dev);
	dev->udev = NULL;
}


static void uaudio_connect(void *unused, struct usb_interface *intf,
		struct snd_usb_audio *chip)
{
	uaudio_dbg("intf: %s: %p chip: %p card_number:%d\n",
		dev_name(&intf->dev), intf, chip, chip->card->number);

	if (chip->card->number >= SNDRV_CARDS) {
		uaudio_err("Invalid card number\n");
		return;
	}

	uadev[chip->card->number].chip = chip;
}

static void uaudio_disconnect(void *unused, struct usb_interface *intf)
{
	int ret;
	struct snd_usb_audio *chip = usb_get_intfdata(intf);
	struct uaudio_dev *dev;
	int card_num;
	struct uaudio_qmi_svc *svc = uaudio_svc;
	struct qmi_uaudio_stream_ind_msg_v01 disconnect_ind = {0};

	if (!chip) {
		uaudio_err("chip is NULL\n");
		return;
	}

	card_num = chip->card->number;
	uaudio_dbg("intf: %s: %p chip: %p card: %d\n", dev_name(&intf->dev),
			intf, chip, card_num);

	if (card_num >= SNDRV_CARDS) {
		uaudio_err("invalid card number\n");
		return;
	}

	mutex_lock(&chip->mutex);
	dev = &uadev[card_num];

	/* clean up */
	if (!dev->udev) {
		uaudio_dbg("no clean up required\n");
		goto done;
	}

	if (atomic_read(&dev->in_use)) {
		mutex_unlock(&chip->mutex);
		uaudio_dbg("sending qmi indication disconnect\n");
		uaudio_dbg("sq->sq_family:%x sq->sq_node:%x sq->sq_port:%x\n",
				svc->client_sq.sq_family,
				svc->client_sq.sq_node, svc->client_sq.sq_port);
		disconnect_ind.dev_event = USB_AUDIO_DEV_DISCONNECT_V01;
		disconnect_ind.slot_id = dev->udev->slot_id;
		disconnect_ind.controller_num = dev->usb_core_id;
		disconnect_ind.controller_num_valid = 1;
		ret = qmi_send_indication(svc->uaudio_svc_hdl, &svc->client_sq,
				QMI_UAUDIO_STREAM_IND_V01,
				QMI_UAUDIO_STREAM_IND_MSG_V01_MAX_MSG_LEN,
				qmi_uaudio_stream_ind_msg_v01_ei,
				&disconnect_ind);
		if (ret < 0)
			uaudio_err("qmi send failed with err: %d\n", ret);

		ret = wait_event_interruptible_timeout(dev->disconnect_wq,
				!atomic_read(&dev->in_use),
				msecs_to_jiffies(DEV_RELEASE_WAIT_TIMEOUT));
		if (!ret) {
			uaudio_err("timeout while waiting for dev_release\n");
			atomic_set(&dev->in_use, 0);
		} else if (ret < 0) {
			uaudio_err("failed with ret %d\n", ret);
			atomic_set(&dev->in_use, 0);
		}

		mutex_lock(&chip->mutex);
	}

	uaudio_dev_cleanup(dev);
done:
	mutex_unlock(&chip->mutex);
	uadev[card_num].chip = NULL;
}

static void uaudio_dev_release(struct kref *kref)
{
	struct uaudio_dev *dev = container_of(kref, struct uaudio_dev, kref);

	uaudio_dbg("for dev %pK\n", dev);

	uaudio_event_ring_cleanup_free(dev);
	atomic_set(&dev->in_use, 0);
	wake_up(&dev->disconnect_wq);
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
	unsigned int bus_intval, bus_intval_mult, binterval;

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

static struct snd_usb_substream *find_substream(unsigned int card_num,
	unsigned int pcm_idx, unsigned int direction)
{
	struct snd_usb_stream *as;
	struct snd_usb_substream *subs = NULL;
	struct snd_usb_audio *chip;

	chip = uadev[card_num].chip;
	if (!chip || atomic_read(&chip->shutdown)) {
		pr_debug("%s: instance of usb card # %d does not exist\n",
			__func__, card_num);
		goto err;
	}

	if (pcm_idx >= chip->pcm_devs) {
		pr_err("%s: invalid pcm dev number %u > %d\n", __func__,
			pcm_idx, chip->pcm_devs);
		goto err;
	}

	if (direction > SNDRV_PCM_STREAM_CAPTURE) {
		pr_err("%s: invalid direction %u\n", __func__, direction);
		goto err;
	}

	list_for_each_entry(as, &chip->pcm_list, list) {
		if (as->pcm_index == pcm_idx) {
			subs = &as->substream[direction];
			goto done;
		}
	}

done:
err:
	if (!subs)
		pr_debug("%s: substream instance not found\n", __func__);
	return subs;
}

static const struct audioformat *
find_format_and_si(struct list_head *fmt_list_head, snd_pcm_format_t format,
	    unsigned int rate, unsigned int channels, unsigned int datainterval,
	    struct snd_usb_substream *subs)
{
	const struct audioformat *fp;
	const struct audioformat *found = NULL;
	int cur_attr = 0, attr;

	list_for_each_entry(fp, fmt_list_head, list) {
		if (datainterval != -EINVAL && datainterval != fp->datainterval)
			continue;
		if (!(fp->formats & pcm_format_to_bits(format)))
			continue;
		if (fp->channels != channels)
			continue;
		if (rate < fp->rate_min || rate > fp->rate_max)
			continue;
		if (!(fp->rates & SNDRV_PCM_RATE_CONTINUOUS)) {
			unsigned int i;

			for (i = 0; i < fp->nr_rates; i++)
				if (fp->rate_table[i] == rate)
					break;
			if (i >= fp->nr_rates)
				continue;
		}
		attr = fp->ep_attr & USB_ENDPOINT_SYNCTYPE;
		if (!found) {
			found = fp;
			cur_attr = attr;
			continue;
		}
		/* avoid async out and adaptive in if the other method
		 * supports the same format.
		 * this is a workaround for the case like
		 * M-audio audiophile USB.
		 */
		if (subs && attr != cur_attr) {
			if ((attr == USB_ENDPOINT_SYNC_ASYNC &&
			     subs->direction == SNDRV_PCM_STREAM_PLAYBACK) ||
			    (attr == USB_ENDPOINT_SYNC_ADAPTIVE &&
			     subs->direction == SNDRV_PCM_STREAM_CAPTURE))
				continue;
			if ((cur_attr == USB_ENDPOINT_SYNC_ASYNC &&
			     subs->direction == SNDRV_PCM_STREAM_PLAYBACK) ||
			    (cur_attr == USB_ENDPOINT_SYNC_ADAPTIVE &&
			     subs->direction == SNDRV_PCM_STREAM_CAPTURE)) {
				found = fp;
				cur_attr = attr;
				continue;
			}
		}
		/* find the format with the largest max. packet size */
		if (fp->maxpacksize > found->maxpacksize) {
			found = fp;
			cur_attr = attr;
		}
	}
	return found;
}

static void close_endpoints(struct snd_usb_audio *chip,
			    struct snd_usb_substream *subs)
{
	mutex_lock(&chip->mutex);
	if (subs->data_endpoint) {
		subs->data_endpoint->sync_source = NULL;
		mutex_unlock(&chip->mutex);
		snd_usb_endpoint_close(chip, subs->data_endpoint);
		mutex_lock(&chip->mutex);
		subs->data_endpoint = NULL;
	}

	if (subs->sync_endpoint) {
		mutex_unlock(&chip->mutex);
		snd_usb_endpoint_close(chip, subs->sync_endpoint);
		mutex_lock(&chip->mutex);
		subs->sync_endpoint = NULL;
	}
	mutex_unlock(&chip->mutex);
}

static int configure_endpoints(struct snd_usb_audio *chip,
			       struct snd_usb_substream *subs)
{
	int err;

	if (subs->data_endpoint->need_setup) {
		err = snd_usb_endpoint_prepare(chip, subs->data_endpoint);
		if (err < 0) {
			uaudio_err("failed to configure data endpoint\n");
			return err;
		}
	}

	if (subs->sync_endpoint) {
		err = snd_usb_endpoint_prepare(chip, subs->sync_endpoint);
		if (err < 0) {
			uaudio_err("failed to configure endpoint\n");
			return err;
		}
	}

	return 0;
}

static int snd_interval_refine_set(struct snd_interval *i, unsigned int val)
{
	struct snd_interval t;

	t.empty = 0;
	t.min = t.max = val;
	t.openmin = t.openmax = 0;
	t.integer = 1;
	return snd_interval_refine(i, &t);
}

static int _snd_pcm_hw_param_set(struct snd_pcm_hw_params *params,
				 snd_pcm_hw_param_t var, unsigned int val,
				 int dir)
{
	int changed;

	if (hw_is_mask(var)) {
		struct snd_mask *m = hw_param_mask(params, var);

		if (val == 0 && dir < 0) {
			changed = -EINVAL;
			snd_mask_none(m);
		} else {
			if (dir > 0)
				val++;
			else if (dir < 0)
				val--;
			changed = snd_mask_refine_set(
					hw_param_mask(params, var), val);
		}
	} else if (hw_is_interval(var)) {
		struct snd_interval *i = hw_param_interval(params, var);

		if (val == 0 && dir < 0) {
			changed = -EINVAL;
			snd_interval_none(i);
		} else if (dir == 0)
			changed = snd_interval_refine_set(i, val);
		else {
			struct snd_interval t;

			t.openmin = 1;
			t.openmax = 1;
			t.empty = 0;
			t.integer = 0;
			if (dir < 0) {
				t.min = val - 1;
				t.max = val;
			} else {
				t.min = val;
				t.max = val+1;
			}
			changed = snd_interval_refine(i, &t);
		}
	} else
		return -EINVAL;
	if (changed) {
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	}
	return changed;
}

bool _snd_usb_pcm_has_fixed_rate(struct snd_usb_substream *subs)
{
	const struct audioformat *fp;
	struct snd_usb_audio *chip;
	int rate = -1;

	if (!subs)
		return false;
	chip = subs->stream->chip;
	if (!(chip->quirk_flags & QUIRK_FLAG_FIXED_RATE))
		return false;
	list_for_each_entry(fp, &subs->fmt_list, list) {
		if (fp->rates & SNDRV_PCM_RATE_CONTINUOUS)
			return false;
		if (fp->nr_rates < 1)
			continue;
		if (fp->nr_rates > 1)
			return false;
		if (rate < 0) {
			rate = fp->rate_table[0];
			continue;
		}
		if (rate != fp->rate_table[0])
			return false;
	}
	return true;
}

static void disable_audio_stream(struct snd_usb_substream *subs)
{
	struct snd_usb_audio *chip = subs->stream->chip;

	if (subs->data_endpoint || subs->sync_endpoint) {
		close_endpoints(chip, subs);

		mutex_lock(&chip->mutex);
		subs->cur_audiofmt = NULL;
		mutex_unlock(&chip->mutex);
	}

	snd_usb_autosuspend(chip);
}

static int enable_audio_stream(struct snd_usb_substream *subs,
				snd_pcm_format_t pcm_format,
				unsigned int channels, unsigned int cur_rate,
				int datainterval)
{
	struct snd_usb_audio *chip = subs->stream->chip;
	struct snd_pcm_hw_params params;
	const struct audioformat *fmt;
	int ret;
	bool fixed_rate;

	_snd_pcm_hw_params_any(&params);
	_snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_FORMAT,
			pcm_format, 0);
	_snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_CHANNELS,
			channels, 0);
	_snd_pcm_hw_param_set(&params, SNDRV_PCM_HW_PARAM_RATE,
			cur_rate, 0);

	if (!chip->intf[0])
		return -ENODEV;

	pm_runtime_barrier(&chip->intf[0]->dev);
	snd_usb_autoresume(chip);

	ret = uaudio_snd_usb_pcm_change_state(subs, UAC3_PD_STATE_D0);
	if (ret < 0)
		return ret;

	fmt = find_format_and_si(&subs->fmt_list, pcm_format, cur_rate,
			channels, datainterval, subs);
	if (!fmt) {
		dev_err(&subs->dev->dev, "cannot find format: format = %#x, rate = %d, channels = %d\n",
			   pcm_format, cur_rate, channels);
		return -EINVAL;
	}

	if (atomic_read(&chip->shutdown)) {
		uaudio_err("chip already shutdown\n");
		ret = -ENODEV;
	} else {
		if (subs->data_endpoint)
			close_endpoints(chip, subs);

		fixed_rate = _snd_usb_pcm_has_fixed_rate(subs);
		subs->data_endpoint = snd_usb_endpoint_open(chip, fmt,
				&params, false, fixed_rate);
		if (!subs->data_endpoint) {
			uaudio_err("failed to open data endpoint\n");
			return -EINVAL;
		}

		if (fmt->sync_ep) {
			subs->sync_endpoint = snd_usb_endpoint_open(chip,
					fmt, &params, true, fixed_rate);
			if (!subs->sync_endpoint) {
				uaudio_err("failed to open sync endpoint\n");
				return -EINVAL;
			}

			subs->data_endpoint->sync_source = subs->sync_endpoint;
		}

		mutex_lock(&chip->mutex);
		subs->cur_audiofmt = fmt;
		mutex_unlock(&chip->mutex);

		ret = configure_endpoints(chip, subs);
		if (ret < 0) {
			uaudio_err("%d:%d: usb_set_interface failed (%d)\n",
					fmt->iface, fmt->altsetting, ret);
			return ret;
		}

		uaudio_info("selected %s iface:%d altsetting:%d datainterval:%dus\n",
				subs->direction ? "capture" : "playback",
				fmt->iface, fmt->altsetting,
				(1 << fmt->datainterval) *
				(subs->dev->speed >= USB_SPEED_HIGH ?
				 BUS_INTERVAL_HIGHSPEED_AND_ABOVE :
				 BUS_INTERVAL_FULL_SPEED));
	}

	return ret;
}

static int __handle_uaudio_stream_req(struct qmi_uaudio_stream_req_msg_v01 *req_msg,
					int *info_idx)
{
	struct snd_usb_substream *subs;
	struct snd_usb_audio *chip;
	int ifnum;
	u8 pcm_card_num, pcm_dev_num, direction;

	direction = req_msg->usb_token & SND_PCM_STREAM_DIRECTION;
	pcm_dev_num = (req_msg->usb_token & SND_PCM_DEV_NUM_MASK) >> 8;
	pcm_card_num = (req_msg->usb_token & SND_PCM_CARD_NUM_MASK) >> 16;

	uaudio_info("card#:%d dev#:%d dir:%d en:%d fmt:%d rate:%d #ch:%d\n",
			pcm_card_num, pcm_dev_num, direction, req_msg->enable,
			req_msg->audio_format, req_msg->bit_rate,
			req_msg->number_of_ch);

	if (pcm_card_num >= SNDRV_CARDS) {
		uaudio_err("invalid card # %u", pcm_card_num);
		return -EINVAL;
	}

	if (req_msg->audio_format > USB_QMI_PCM_FORMAT_U32_BE) {
		uaudio_err("unsupported pcm format received %d\n",
				req_msg->audio_format);
		return -EINVAL;
	}

	subs = find_substream(pcm_card_num, pcm_dev_num, direction);
	chip = uadev[pcm_card_num].chip;
	if (!subs || !chip || atomic_read(&chip->shutdown)) {
		uaudio_err("can't find substream for card# %u, dev# %u dir%u\n",
				pcm_card_num, pcm_dev_num, direction);
		return -ENODEV;
	}

	ifnum = subs->cur_audiofmt ? subs->cur_audiofmt->iface : -1;
	*info_idx = info_idx_from_ifnum(pcm_card_num, ifnum, req_msg->enable);

	if (atomic_read(&chip->shutdown) || !subs->stream || !subs->stream->pcm
			|| !subs->stream->chip) {
		uaudio_err("chip or sub not available: shutdown:%d stream:%p\n",
				atomic_read(&chip->shutdown), subs->stream);

		if (subs->stream)
			uaudio_err("pcm:%p chip:%p\n", subs->stream->pcm, subs->stream->chip);

		return -ENODEV;
	}

	if (req_msg->enable && (*info_idx < 0)) {
		uaudio_err("interface# %d already in use card# %d\n",
				ifnum, pcm_card_num);
		return -EBUSY;
	}

	return 0;
}

static void handle_uaudio_stream_req(struct qmi_handle *handle,
			struct sockaddr_qrtr *sq,
			struct qmi_txn *txn,
			const void *decoded_msg)
{
	struct qmi_uaudio_stream_req_msg_v01 *req_msg;
	struct qmi_uaudio_stream_resp_msg_v01 resp = {{0}, 0};
	struct snd_usb_substream *subs = NULL;
	struct uaudio_qmi_svc *svc = uaudio_svc;
	struct intf_info *info;
	struct usb_host_endpoint *ep;
	ktime_t t_request_recvd = ktime_get();
	struct snd_usb_audio *chip = NULL;

	u8 pcm_card_num, pcm_dev_num, direction;
	int info_idx = -EINVAL, datainterval = -EINVAL, ret = 0;

	uaudio_dbg("sq_node:%x sq_port:%x sq_family:%x\n", sq->sq_node,
			sq->sq_port, sq->sq_family);
	if (!svc->client_connected) {
		svc->client_sq = *sq;
		svc->client_connected = true;
	}

	req_msg = (struct qmi_uaudio_stream_req_msg_v01 *)decoded_msg;
	if (!req_msg->audio_format_valid || !req_msg->bit_rate_valid ||
	!req_msg->number_of_ch_valid || !req_msg->xfer_buff_size_valid) {
		uaudio_err("invalid request msg\n");
		ret = -EINVAL;
		goto response;
	}

	direction = req_msg->usb_token & SND_PCM_STREAM_DIRECTION;
	pcm_dev_num = (req_msg->usb_token & SND_PCM_DEV_NUM_MASK) >> 8;
	pcm_card_num = (req_msg->usb_token & SND_PCM_CARD_NUM_MASK) >> 16;

	subs = find_substream(pcm_card_num, pcm_dev_num, direction);

	if (!subs) {
		uaudio_err("invalid substream\n");
		ret = -EINVAL;
		goto response;
	}

	chip = uadev[pcm_card_num].chip;

	ret = __handle_uaudio_stream_req(req_msg, &info_idx);
	if (ret)
		goto response;

	if (req_msg->service_interval_valid) {
		ret = get_data_interval_from_si(subs,
						req_msg->service_interval);
		if (ret == -EINVAL) {
			uaudio_err("invalid service interval %u\n",
					req_msg->service_interval);
			goto response;
		}

		datainterval = ret;
		uaudio_dbg("data interval %u\n", ret);
	}

	uadev[pcm_card_num].ctrl_intf = chip->ctrl_intf;
	atomic_inc(&chip->usage_count);
	if (req_msg->enable) {
		ret = enable_audio_stream(subs,
				map_pcm_format(req_msg->audio_format),
				req_msg->number_of_ch, req_msg->bit_rate,
				datainterval);
		if (!ret)
			ret = prepare_qmi_response(subs, req_msg, &resp,
					info_idx);
	} else {
		info = &uadev[pcm_card_num].info[info_idx];
		if (info->data_ep_pipe) {
			ep = usb_pipe_endpoint(uadev[pcm_card_num].udev,
						info->data_ep_pipe);
			if (!ep)
				uaudio_dbg("no data ep\n");
			else
				xhci_stop_endpoint(uadev[pcm_card_num].udev,
						ep);
			info->data_ep_pipe = 0;
		}

		if (info->sync_ep_pipe) {
			ep = usb_pipe_endpoint(uadev[pcm_card_num].udev,
						info->sync_ep_pipe);
			if (!ep)
				uaudio_dbg("no sync ep\n");
			else
				xhci_stop_endpoint(uadev[pcm_card_num].udev,
						ep);
			info->sync_ep_pipe = 0;
		}

		disable_audio_stream(subs);
	}

	if (atomic_dec_and_test(&chip->usage_count) && atomic_read(&chip->shutdown))
		wake_up(&chip->shutdown_wait);

response:
	if (!req_msg->enable && ret != -EINVAL && ret != -ENODEV) {
		mutex_lock(&chip->mutex);
		if (info_idx >= 0) {
			info = &uadev[pcm_card_num].info[info_idx];
			uaudio_dev_intf_cleanup(
					uadev[pcm_card_num].udev,
					info);
			uaudio_dbg("release resources: intf# %d card# %d\n",
					info->intf_num, pcm_card_num);
		}
		if (atomic_read(&uadev[pcm_card_num].in_use))
			kref_put(&uadev[pcm_card_num].kref,
					uaudio_dev_release);
		mutex_unlock(&chip->mutex);
	}

	resp.usb_token = req_msg->usb_token;
	resp.usb_token_valid = 1;
	resp.internal_status = ret;
	resp.internal_status_valid = 1;
	resp.status = ret ? USB_AUDIO_STREAM_REQ_FAILURE_V01 : ret;
	resp.status_valid = 1;
	ret = qmi_send_response(svc->uaudio_svc_hdl, sq, txn,
			QMI_UAUDIO_STREAM_RESP_V01,
			QMI_UAUDIO_STREAM_RESP_MSG_V01_MAX_MSG_LEN,
			qmi_uaudio_stream_resp_msg_v01_ei, &resp);

	uaudio_dbg("ret %d: qmi response latency %lld ms\n", ret,
		ktime_to_ms(ktime_sub(ktime_get(), t_request_recvd)));
}

static void uaudio_qmi_disconnect_work(struct work_struct *w)
{
	struct intf_info *info;
	int idx, if_idx;
	struct snd_usb_substream *subs;
	struct snd_usb_audio *chip;

	/* find all active intf for set alt 0 and cleanup usb audio dev */
	for (idx = 0; idx < SNDRV_CARDS; idx++) {
		if (!atomic_read(&uadev[idx].in_use))
			continue;

		for (if_idx = 0; if_idx < uadev[idx].num_intf; if_idx++) {
			if (!uadev[idx].info || !uadev[idx].info[if_idx].in_use)
				continue;
			info = &uadev[idx].info[if_idx];
			subs = find_substream(info->pcm_card_num,
						info->pcm_dev_num,
						info->direction);
			chip = uadev[idx].chip;
			if (!subs || !chip || atomic_read(&chip->shutdown)) {
				uaudio_dbg("no subs for c#%u, dev#%u dir%u\n",
						info->pcm_card_num,
						info->pcm_dev_num,
						info->direction);
				continue;
			}
			disable_audio_stream(subs);
		}
		atomic_set(&uadev[idx].in_use, 0);
		uaudio_dev_cleanup(&uadev[idx]);
	}
}

static void uaudio_qmi_bye_cb(struct qmi_handle *handle, unsigned int node)
{
	struct uaudio_qmi_svc *svc = uaudio_svc;

	if (svc->uaudio_svc_hdl != handle) {
		uaudio_err("handle mismatch\n");
		return;
	}

	if (svc->client_connected && svc->client_sq.sq_node == node) {
		uaudio_dbg("node: %d\n", node);
		queue_work(svc->uaudio_wq, &svc->qmi_disconnect_work);
		svc->client_sq.sq_node = 0;
		svc->client_sq.sq_port = 0;
		svc->client_sq.sq_family = 0;
		svc->client_connected = false;
	}
}

static void uaudio_qmi_svc_disconnect_cb(struct qmi_handle *handle,
				  unsigned int node, unsigned int port)
{
	struct uaudio_qmi_svc *svc;

	if (uaudio_svc == NULL)
		return;

	svc = uaudio_svc;
	if (svc->uaudio_svc_hdl != handle) {
		uaudio_err("handle mismatch\n");
		return;
	}

	if (svc->client_connected && svc->client_sq.sq_node == node &&
			svc->client_sq.sq_port == port) {
		uaudio_dbg("client node:%x port:%x\n", node, port);
		queue_work(svc->uaudio_wq, &svc->qmi_disconnect_work);
		svc->client_sq.sq_node = 0;
		svc->client_sq.sq_port = 0;
		svc->client_sq.sq_family = 0;
		svc->client_connected = false;
	}
}

static struct qmi_ops uaudio_svc_ops_options = {
	.bye = uaudio_qmi_bye_cb,
	.del_client = uaudio_qmi_svc_disconnect_cb,
};

static int uaudio_qmi_svc_init(void);

static int uaudio_qmi_plat_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *node = pdev->dev.of_node;

	if (!uaudio_svc) {
		ret = uaudio_qmi_svc_init();
		if (ret)
			return ret;
	}

	uaudio_qdev = devm_kzalloc(&pdev->dev, sizeof(struct uaudio_qmi_dev),
		GFP_KERNEL);
	if (!uaudio_qdev)
		return -ENOMEM;

	uaudio_qdev->dev = &pdev->dev;

	ret = of_property_read_u32(node, "qcom,usb-audio-stream-id",
				&uaudio_qdev->sid);
	if (ret) {
		dev_err(&pdev->dev, "failed to read sid.\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(node, "qcom,usb-audio-intr-num",
				&uaudio_qdev->intr_num);
	if (ret) {
		dev_err(&pdev->dev, "failed to read intr num.\n");
		return -ENODEV;
	}

	uaudio_qdev->domain = iommu_domain_alloc(pdev->dev.bus);
	if (!uaudio_qdev->domain) {
		dev_err(&pdev->dev, "failed to allocate iommu domain\n");
		return -ENODEV;
	}

	/* attach to external processor iommu */
	ret = iommu_attach_device(uaudio_qdev->domain, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to attach device ret = %d\n", ret);
		goto free_domain;
	}

	/* initialize xfer ring and xfer buf iova list */
	INIT_LIST_HEAD(&uaudio_qdev->xfer_ring_list);
	uaudio_qdev->curr_xfer_ring_iova = IOVA_XFER_RING_BASE;
	uaudio_qdev->xfer_ring_iova_size =
			IOVA_XFER_RING_MAX - IOVA_XFER_RING_BASE;

	INIT_LIST_HEAD(&uaudio_qdev->xfer_buf_list);
	uaudio_qdev->curr_xfer_buf_iova = IOVA_XFER_BUF_BASE;
	uaudio_qdev->xfer_buf_iova_size =
		IOVA_XFER_BUF_MAX - IOVA_XFER_BUF_BASE;

	ret = register_trace_android_vh_audio_usb_offload_connect(uaudio_connect, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to register connect callback ret = %d\n", ret);
		goto detach_device;
	}

	ret = register_trace_android_rvh_audio_usb_offload_disconnect(uaudio_disconnect, NULL);
		dev_err(&pdev->dev, "failed to register disconnect callback ret = %d\n", ret);

	return 0;

detach_device:
	iommu_detach_device(uaudio_qdev->domain, &pdev->dev);
free_domain:
	iommu_domain_free(uaudio_qdev->domain);
	return ret;
}

static int uaudio_qmi_plat_remove(struct platform_device *pdev)
{
	unregister_trace_android_vh_audio_usb_offload_connect(uaudio_connect, NULL);
	iommu_detach_device(uaudio_qdev->domain, &pdev->dev);
	iommu_domain_free(uaudio_qdev->domain);
	uaudio_qdev->domain = NULL;

	return 0;
}

static const struct of_device_id of_uaudio_matach[] = {
	{
		.compatible = "qcom,usb-audio-qmi-dev",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, of_uaudio_matach);

static struct platform_driver uaudio_qmi_driver = {
	.probe		= uaudio_qmi_plat_probe,
	.remove		= uaudio_qmi_plat_remove,
	.driver		= {
		.name	= "uaudio-qmi",
		.of_match_table	= of_uaudio_matach,
	},
};

static int uaudio_qmi_svc_init(void)
{
	int ret;
	struct uaudio_qmi_svc *svc;

	svc = kzalloc(sizeof(struct uaudio_qmi_svc), GFP_KERNEL);
	if (!svc)
		return -ENOMEM;

	svc->uaudio_wq = create_singlethread_workqueue("uaudio_svc");
	if (!svc->uaudio_wq) {
		ret = -ENOMEM;
		goto free_svc;
	}

	svc->uaudio_svc_hdl = kzalloc(sizeof(struct qmi_handle), GFP_KERNEL);
	if (!svc->uaudio_svc_hdl) {
		ret = -ENOMEM;
		goto destroy_uaudio_wq;
	}

	ret = qmi_handle_init(svc->uaudio_svc_hdl,
				QMI_UAUDIO_STREAM_REQ_MSG_V01_MAX_MSG_LEN,
				&uaudio_svc_ops_options,
				&uaudio_stream_req_handlers);
	if (ret < 0) {
		pr_err("%s:Error registering uaudio svc %d\n", __func__, ret);
		goto free_svc_hdl;
	}

	uaudio_svc = svc;
	INIT_WORK(&svc->qmi_disconnect_work, uaudio_qmi_disconnect_work);
	ret = qmi_add_server(svc->uaudio_svc_hdl, UAUDIO_STREAM_SERVICE_ID_V01,
					UAUDIO_STREAM_SERVICE_VERS_V01, 0);
	if (ret < 0) {
		pr_err("%s: failed to add uaudio svc server :%d\n",
							__func__, ret);
		goto release_uaudio_svs_hdl;
	}

	svc->uaudio_ipc_log = ipc_log_context_create(NUM_LOG_PAGES, "usb_audio",
			0);

	return 0;

release_uaudio_svs_hdl:
	qmi_handle_release(svc->uaudio_svc_hdl);
	uaudio_svc = NULL;
free_svc_hdl:
	kfree(svc->uaudio_svc_hdl);
destroy_uaudio_wq:
	destroy_workqueue(svc->uaudio_wq);
free_svc:
	kfree(svc);
	return ret;
}

static void uaudio_qmi_svc_exit(void)
{
	struct uaudio_qmi_svc *svc = uaudio_svc;

	qmi_handle_release(svc->uaudio_svc_hdl);
	flush_workqueue(svc->uaudio_wq);
	destroy_workqueue(svc->uaudio_wq);
	kfree(svc->uaudio_svc_hdl);
	ipc_log_context_destroy(svc->uaudio_ipc_log);
	kfree(svc);
	uaudio_svc = NULL;
}

static int __init uaudio_qmi_plat_init(void)
{
	return platform_driver_register(&uaudio_qmi_driver);
}

static void __exit uaudio_qmi_plat_exit(void)
{
	uaudio_qmi_svc_exit();
	platform_driver_unregister(&uaudio_qmi_driver);
}

module_init(uaudio_qmi_plat_init);
module_exit(uaudio_qmi_plat_exit);

MODULE_DESCRIPTION("USB AUDIO QMI Service Driver");
MODULE_LICENSE("GPL");
