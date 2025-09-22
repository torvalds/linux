/*	$OpenBSD: virtio.c,v 1.127 2025/08/08 13:36:04 dv Exp $	*/

/*
 * Copyright (c) 2015 Mike Larkin <mlarkin@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>	/* PAGE_SIZE */
#include <sys/socket.h>
#include <sys/wait.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>
#include <dev/pv/virtioreg.h>
#include <dev/pci/virtio_pcireg.h>
#include <dev/pv/vioblkreg.h>
#include <dev/vmm/vmm.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <errno.h>
#include <event.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "atomicio.h"
#include "pci.h"
#include "vioscsi.h"
#include "virtio.h"
#include "vmd.h"

#define VIRTIO_DEBUG	0
#ifdef DPRINTF
#undef DPRINTF
#endif
#if VIRTIO_DEBUG
#define DPRINTF		log_debug
#else
#define DPRINTF(x...)	do {} while(0)
#endif	/* VIRTIO_DEBUG */

extern struct vmd *env;
extern char *__progname;

struct virtio_dev viornd;
struct virtio_dev *vioscsi = NULL;
struct virtio_dev vmmci;

/* Devices emulated in subprocesses are inserted into this list. */
SLIST_HEAD(virtio_dev_head, virtio_dev) virtio_devs;

#define MAXPHYS	(64 * 1024)	/* max raw I/O transfer size */

#define VIRTIO_NET_F_MAC	(1<<5)

#define VMMCI_F_TIMESYNC	(1<<0)
#define VMMCI_F_ACK		(1<<1)
#define VMMCI_F_SYNCRTC		(1<<2)

#define RXQ	0
#define TXQ	1

static void virtio_dev_init(struct virtio_dev *, uint8_t, uint16_t, uint16_t,
    uint64_t, uint32_t);
static int virtio_dev_launch(struct vmd_vm *, struct virtio_dev *);
static void virtio_dispatch_dev(int, short, void *);
static int handle_dev_msg(struct viodev_msg *, struct virtio_dev *);
static int virtio_dev_closefds(struct virtio_dev *);
static void virtio_pci_add_cap(uint8_t, uint8_t, uint8_t, uint32_t);
static void vmmci_pipe_dispatch(int, short, void *);

static int virtio_io_dispatch(int, uint16_t, uint32_t *, uint8_t *, void *,
    uint8_t);
static int virtio_io_isr(int, uint16_t, uint32_t *, uint8_t *, void *, uint8_t);
static int virtio_io_notify(int, uint16_t, uint32_t *, uint8_t *, void *,
    uint8_t);
static int viornd_notifyq(struct virtio_dev *, uint16_t);

static void vmmci_ack(struct virtio_dev *, unsigned int);

#if VIRTIO_DEBUG
static const char *
virtio1_reg_name(uint16_t reg)
{
	switch (reg) {
	case VIO1_PCI_DEVICE_FEATURE_SELECT: return "DEVICE_FEATURE_SELECT";
	case VIO1_PCI_DEVICE_FEATURE: return "DEVICE_FEATURE";
	case VIO1_PCI_DRIVER_FEATURE_SELECT: return "DRIVER_FEATURE_SELECT";
	case VIO1_PCI_DRIVER_FEATURE: return "DRIVER_FEATURE";
	case VIO1_PCI_CONFIG_MSIX_VECTOR: return "CONFIG_MSIX_VECTOR";
	case VIO1_PCI_NUM_QUEUES: return "NUM_QUEUES";
	case VIO1_PCI_DEVICE_STATUS: return "DEVICE_STATUS";
	case VIO1_PCI_CONFIG_GENERATION: return "CONFIG_GENERATION";
	case VIO1_PCI_QUEUE_SELECT: return "QUEUE_SELECT";
	case VIO1_PCI_QUEUE_SIZE: return "QUEUE_SIZE";
	case VIO1_PCI_QUEUE_MSIX_VECTOR: return "QUEUE_MSIX_VECTOR";
	case VIO1_PCI_QUEUE_ENABLE: return "QUEUE_ENABLE";
	case VIO1_PCI_QUEUE_NOTIFY_OFF: return "QUEUE_NOTIFY_OFF";
	case VIO1_PCI_QUEUE_DESC: return "QUEUE_DESC";
	case VIO1_PCI_QUEUE_DESC + 4: return "QUEUE_DESC (HIGH)";
	case VIO1_PCI_QUEUE_AVAIL: return "QUEUE_AVAIL";
	case VIO1_PCI_QUEUE_AVAIL + 4: return "QUEUE_AVAIL (HIGH)";
	case VIO1_PCI_QUEUE_USED: return "QUEUE_USED";
	case VIO1_PCI_QUEUE_USED + 4: return "QUEUE_USED (HIGH)";
	default: return "UNKNOWN";
	}
}
#endif	/* VIRTIO_DEBUG */

const char *
virtio_reg_name(uint8_t reg)
{
	switch (reg) {
	case VIRTIO_CONFIG_DEVICE_FEATURES: return "device feature";
	case VIRTIO_CONFIG_GUEST_FEATURES: return "guest feature";
	case VIRTIO_CONFIG_QUEUE_PFN: return "queue address";
	case VIRTIO_CONFIG_QUEUE_SIZE: return "queue size";
	case VIRTIO_CONFIG_QUEUE_SELECT: return "queue select";
	case VIRTIO_CONFIG_QUEUE_NOTIFY: return "queue notify";
	case VIRTIO_CONFIG_DEVICE_STATUS: return "device status";
	case VIRTIO_CONFIG_ISR_STATUS: return "isr status";
	case VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI...VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI + 3:
		return "device config 0";
	case VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI + 4:
	case VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI + 5:
		return "device config 1";
	case VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI + 8: return "device config 2";
	case VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI + 12: return "device config 3";
	case VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI + 16: return "device config 4";
	default: return "unknown";
	}
}

uint32_t
vring_size(uint32_t vq_size)
{
	uint32_t allocsize1, allocsize2;

	/* allocsize1: descriptor table + avail ring + pad */
	allocsize1 = VIRTQUEUE_ALIGN(sizeof(struct vring_desc) * vq_size
	    + sizeof(uint16_t) * (2 + vq_size));
	/* allocsize2: used ring + pad */
	allocsize2 = VIRTQUEUE_ALIGN(sizeof(uint16_t) * 2
	    + sizeof(struct vring_used_elem) * vq_size);

	return allocsize1 + allocsize2;
}

/* Update queue select */
void
virtio_update_qs(struct virtio_dev *dev)
{
	struct virtio_vq_info *vq_info = NULL;

	if (dev->driver_feature & VIRTIO_F_VERSION_1) {
		/* Invalid queue */
		if (dev->pci_cfg.queue_select >= dev->num_queues) {
			dev->pci_cfg.queue_size = 0;
			dev->pci_cfg.queue_enable = 0;
			return;
		}
		vq_info = &dev->vq[dev->pci_cfg.queue_select];
		dev->pci_cfg.queue_size = vq_info->qs;
		dev->pci_cfg.queue_desc = vq_info->q_gpa;
		dev->pci_cfg.queue_avail = vq_info->q_gpa + vq_info->vq_availoffset;
		dev->pci_cfg.queue_used = vq_info->q_gpa + vq_info->vq_usedoffset;
		dev->pci_cfg.queue_enable = vq_info->vq_enabled;
	} else {
		/* Invalid queue? */
		if (dev->cfg.queue_select >= dev->num_queues) {
			dev->cfg.queue_size = 0;
			return;
		}
		vq_info = &dev->vq[dev->cfg.queue_select];
		dev->cfg.queue_pfn = vq_info->q_gpa >> 12;
		dev->cfg.queue_size = vq_info->qs;
	}
}

/* Update queue address. */
void
virtio_update_qa(struct virtio_dev *dev)
{
	struct virtio_vq_info *vq_info = NULL;
	void *hva = NULL;

	if (dev->driver_feature & VIRTIO_F_VERSION_1) {
		if (dev->pci_cfg.queue_select >= dev->num_queues) {
			log_warnx("%s: invalid queue index", __func__);
			return;
		}
		vq_info = &dev->vq[dev->pci_cfg.queue_select];
		vq_info->q_gpa = dev->pci_cfg.queue_desc;

		/*
		 * Queue size is adjustable by the guest in Virtio 1.x.
		 * We validate the max size at time of write and not here.
		 */
		vq_info->qs = dev->pci_cfg.queue_size;
		vq_info->mask = vq_info->qs - 1;

		if (vq_info->qs > 0 && vq_info->qs % 2 == 0) {
			vq_info->vq_availoffset = dev->pci_cfg.queue_avail -
			    dev->pci_cfg.queue_desc;
			vq_info->vq_usedoffset = dev->pci_cfg.queue_used -
			    dev->pci_cfg.queue_desc;
			vq_info->vq_enabled = (dev->pci_cfg.queue_enable == 1);
		} else {
			vq_info->vq_availoffset = 0;
			vq_info->vq_usedoffset = 0;
			vq_info->vq_enabled = 0;
		}
	} else {
		/* Invalid queue? */
		if (dev->cfg.queue_select >= dev->num_queues) {
			log_warnx("%s: invalid queue index", __func__);
			return;
		}
		vq_info = &dev->vq[dev->cfg.queue_select];
		vq_info->q_gpa = (uint64_t)dev->cfg.queue_pfn *
		    VIRTIO_PAGE_SIZE;

		/* Queue size is immutable in Virtio 0.9. */
		vq_info->vq_availoffset = sizeof(struct vring_desc) *
		    vq_info->qs;
		vq_info->vq_usedoffset = VIRTQUEUE_ALIGN(
			sizeof(struct vring_desc) * vq_info->qs +
			sizeof(uint16_t) * (2 + vq_info->qs));
	}

	/* Update any host va mappings. */
	if (vq_info->q_gpa > 0) {
		hva = hvaddr_mem(vq_info->q_gpa, vring_size(vq_info->qs));
		if (hva == NULL)
			fatalx("%s: failed to translate gpa to hva", __func__);
		vq_info->q_hva = hva;
	} else {
		vq_info->q_hva = NULL;
		vq_info->last_avail = 0;
		vq_info->notified_avail = 0;
	}
}

static int
viornd_notifyq(struct virtio_dev *dev, uint16_t idx)
{
	size_t sz;
	int dxx, ret = 0;
	uint16_t aidx, uidx;
	char *vr, *rnd_data;
	struct vring_desc *desc = NULL;
	struct vring_avail *avail = NULL;
	struct vring_used *used = NULL;
	struct virtio_vq_info *vq_info = NULL;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_ENTROPY)
		fatalx("%s: device is not an entropy device", __func__);

	if (idx >= dev->num_queues) {
		log_warnx("%s: invalid virtqueue index", __func__);
		return (0);
	}
	vq_info = &dev->vq[idx];

	if (!vq_info->vq_enabled) {
		log_warnx("%s: virtqueue not enabled", __func__);
		return (0);
	}

	vr = vq_info->q_hva;
	if (vr == NULL)
		fatalx("%s: null vring", __func__);

	desc = (struct vring_desc *)(vr);
	avail = (struct vring_avail *)(vr + vq_info->vq_availoffset);
	used = (struct vring_used *)(vr + vq_info->vq_usedoffset);

	aidx = avail->idx & vq_info->mask;
	uidx = used->idx & vq_info->mask;

	dxx = avail->ring[aidx] & vq_info->mask;

	sz = desc[dxx].len;
	if (sz > MAXPHYS)
		fatalx("viornd descriptor size too large (%zu)", sz);

	rnd_data = malloc(sz);
	if (rnd_data == NULL)
		fatal("memory allocaiton error for viornd data");

	arc4random_buf(rnd_data, sz);
	if (write_mem(desc[dxx].addr, rnd_data, sz)) {
		log_warnx("viornd: can't write random data @ 0x%llx",
		    desc[dxx].addr);
	} else {
		/* ret == 1 -> interrupt needed */
		/* XXX check VIRTIO_F_NO_INTR */
		ret = 1;
		viornd.isr = 1;
		used->ring[uidx].id = dxx;
		used->ring[uidx].len = sz;
		__sync_synchronize();
		used->idx++;
	}
	free(rnd_data);

	return (ret);
}

static int
virtio_io_dispatch(int dir, uint16_t reg, uint32_t *data, uint8_t *intr,
    void *arg, uint8_t sz)
{
	struct virtio_dev *dev = (struct virtio_dev *)arg;
	uint8_t actual = (uint8_t)reg;

	switch (reg & 0xFF00) {
	case VIO1_CFG_BAR_OFFSET:
		*data = virtio_io_cfg(dev, dir, actual, *data, sz);
		break;
	case VIO1_DEV_BAR_OFFSET:
		if (dev->device_id == PCI_PRODUCT_VIRTIO_SCSI)
			return vioscsi_io(dir, actual, data, intr, arg, sz);
		else if (dir == VEI_DIR_IN) {
			log_debug("%s: no device specific handler", __func__);
			*data = (uint32_t)(-1);
		}
		break;
	case VIO1_NOTIFY_BAR_OFFSET:
		return virtio_io_notify(dir, actual, data, intr, arg, sz);
	case VIO1_ISR_BAR_OFFSET:
		return virtio_io_isr(dir, actual, data, intr, arg, sz);
	default:
		DPRINTF("%s: no handler for reg 0x%04x", __func__, reg);
		if (dir == VEI_DIR_IN)
			*data = (uint32_t)(-1);
	}
	return (0);
}

/*
 * virtio 1.x PCI config register io. If a register is read, returns the value.
 * Otherwise returns 0.
 */
uint32_t
virtio_io_cfg(struct virtio_dev *dev, int dir, uint8_t reg, uint32_t data,
    uint8_t sz)
{
	struct virtio_pci_common_cfg *pci_cfg = &dev->pci_cfg;
	uint32_t res = 0;
	uint16_t i;

	if (dir == VEI_DIR_OUT) {
		switch (reg) {
		case VIO1_PCI_DEVICE_FEATURE_SELECT:
			if (sz != 4)
				log_warnx("%s: unaligned write to device "
				    "feature select (sz=%u)", __func__, sz);
			else
				pci_cfg->device_feature_select = data;
			break;
		case VIO1_PCI_DEVICE_FEATURE:
			log_warnx("%s: illegal write to device feature "
			    "register", __progname);
			break;
		case VIO1_PCI_DRIVER_FEATURE_SELECT:
			if (sz != 4)
				log_warnx("%s: unaligned write to driver "
				    "feature select register (sz=%u)", __func__,
				    sz);
			else
				pci_cfg->driver_feature_select = data;
			break;
		case VIO1_PCI_DRIVER_FEATURE:
			if (sz != 4) {
				log_warnx("%s: unaligned write to driver "
				    "feature register (sz=%u)", __func__, sz);
				break;
			}
			if (pci_cfg->driver_feature_select > 1) {
				/* We only support a 64-bit feature space. */
				DPRINTF("%s: ignoring driver feature write",
				    __func__);
				break;
			}
			pci_cfg->driver_feature = data;
			if (pci_cfg->driver_feature_select == 0)
				dev->driver_feature |= pci_cfg->driver_feature;
			else
				dev->driver_feature |=
				    ((uint64_t)pci_cfg->driver_feature << 32);
			dev->driver_feature &= dev->device_feature;
			DPRINTF("%s: driver features 0x%llx", __func__,
			    dev->driver_feature);
			break;
		case VIO1_PCI_CONFIG_MSIX_VECTOR:
			/* Ignore until we support MSIX. */
			break;
		case VIO1_PCI_NUM_QUEUES:
			log_warnx("%s: illegal write to num queues register",
			    __progname);
			break;
		case VIO1_PCI_DEVICE_STATUS:
			if (sz != 1) {
				log_warnx("%s: unaligned write to device "
				    "status register (sz=%u)", __func__, sz);
				break;
			}
			dev->status = data;
			if (dev->status == 0) {
				/* Reset device and virtqueues (if any). */
				dev->driver_feature = 0;
				dev->isr = 0;

				pci_cfg->queue_select = 0;
				virtio_update_qs(dev);

				if (dev->num_queues > 0) {
					/*
					 * Reset virtqueues to initial state and
					 * set to disabled status. Clear PCI
					 * configuration registers.
					 */
					for (i = 0; i < dev->num_queues; i++)
						virtio_vq_init(dev, i);
				}
			}

			DPRINTF("%s: dev %u status [%s%s%s%s%s%s]", __func__,
			    dev->pci_id,
			    (data & VIRTIO_CONFIG_DEVICE_STATUS_ACK) ?
			    "[ack]" : "",
			    (data & VIRTIO_CONFIG_DEVICE_STATUS_DRIVER) ?
			    "[driver]" : "",
			    (data & VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK) ?
			    "[driver ok]" : "",
			    (data & VIRTIO_CONFIG_DEVICE_STATUS_FEATURES_OK) ?
			    "[features ok]" : "",
			    (data &
				VIRTIO_CONFIG_DEVICE_STATUS_DEVICE_NEEDS_RESET)
			    ? "[needs reset]" : "",
			    (data & VIRTIO_CONFIG_DEVICE_STATUS_FAILED) ?
			    "[failed]" : "");

			break;
		case VIO1_PCI_CONFIG_GENERATION:
			log_warnx("%s: illegal write to config generation "
			    "register", __progname);
			break;
		case VIO1_PCI_QUEUE_SELECT:
			pci_cfg->queue_select = data;
			virtio_update_qs(dev);
			break;
		case VIO1_PCI_QUEUE_SIZE:
			if (data <= VIRTIO_QUEUE_SIZE_MAX)
				pci_cfg->queue_size = data;
			else {
				log_warnx("%s: clamping queue size", __func__);
				pci_cfg->queue_size = VIRTIO_QUEUE_SIZE_MAX;
			}
			virtio_update_qa(dev);
			break;
		case VIO1_PCI_QUEUE_MSIX_VECTOR:
			/* Ignore until we support MSI-X. */
			break;
		case VIO1_PCI_QUEUE_ENABLE:
			pci_cfg->queue_enable = data;
			virtio_update_qa(dev);
			break;
		case VIO1_PCI_QUEUE_NOTIFY_OFF:
			log_warnx("%s: illegal write to queue notify offset "
			    "register", __progname);
			break;
		case VIO1_PCI_QUEUE_DESC:
			if (sz != 4) {
				log_warnx("%s: unaligned write to queue "
				    "desc. register (sz=%u)", __func__, sz);
				break;
			}
			pci_cfg->queue_desc &= 0xffffffff00000000;
			pci_cfg->queue_desc |= (uint64_t)data;
			virtio_update_qa(dev);
			break;
		case VIO1_PCI_QUEUE_DESC + 4:
			if (sz != 4) {
				log_warnx("%s: unaligned write to queue "
				    "desc. register (sz=%u)", __func__, sz);
				break;
			}
			pci_cfg->queue_desc &= 0x00000000ffffffff;
			pci_cfg->queue_desc |= ((uint64_t)data << 32);
			virtio_update_qa(dev);
			break;
		case VIO1_PCI_QUEUE_AVAIL:
			if (sz != 4) {
				log_warnx("%s: unaligned write to queue "
				    "available register (sz=%u)", __func__, sz);
				break;
			}
			pci_cfg->queue_avail &= 0xffffffff00000000;
			pci_cfg->queue_avail |= (uint64_t)data;
			virtio_update_qa(dev);
			break;
		case VIO1_PCI_QUEUE_AVAIL + 4:
			if (sz != 4) {
				log_warnx("%s: unaligned write to queue "
				    "available register (sz=%u)", __func__, sz);
				break;
			}
			pci_cfg->queue_avail &= 0x00000000ffffffff;
			pci_cfg->queue_avail |= ((uint64_t)data << 32);
			virtio_update_qa(dev);
			break;
		case VIO1_PCI_QUEUE_USED:
			if (sz != 4) {
				log_warnx("%s: unaligned write to queue used "
				    "register (sz=%u)", __func__, sz);
				break;
			}
			pci_cfg->queue_used &= 0xffffffff00000000;
			pci_cfg->queue_used |= (uint64_t)data;
			virtio_update_qa(dev);
			break;
		case VIO1_PCI_QUEUE_USED + 4:
			if (sz != 4) {
				log_warnx("%s: unaligned write to queue used "
				    "register (sz=%u)", __func__, sz);
				break;
			}
			pci_cfg->queue_used &= 0x00000000ffffffff;
			pci_cfg->queue_used |= ((uint64_t)data << 32);
			virtio_update_qa(dev);
			break;
		default:
			log_warnx("%s: invalid register 0x%04x", __func__, reg);
		}
	} else {
		switch (reg) {
		case VIO1_PCI_DEVICE_FEATURE_SELECT:
			res = pci_cfg->device_feature_select;
			break;
		case VIO1_PCI_DEVICE_FEATURE:
			if (pci_cfg->device_feature_select == 0)
				res = dev->device_feature & (uint32_t)(-1);
			else if (pci_cfg->device_feature_select == 1)
				res = dev->device_feature >> 32;
			else {
				DPRINTF("%s: ignoring device feature read",
				    __func__);
			}
			break;
		case VIO1_PCI_DRIVER_FEATURE_SELECT:
			res = pci_cfg->driver_feature_select;
			break;
		case VIO1_PCI_DRIVER_FEATURE:
			if (pci_cfg->driver_feature_select == 0)
				res = dev->driver_feature & (uint32_t)(-1);
			else if (pci_cfg->driver_feature_select == 1)
				res = dev->driver_feature >> 32;
			else {
				DPRINTF("%s: ignoring driver feature read",
				    __func__);
			}
			break;
		case VIO1_PCI_CONFIG_MSIX_VECTOR:
			res = VIRTIO_MSI_NO_VECTOR;	/* Unsupported */
			break;
		case VIO1_PCI_NUM_QUEUES:
			res = dev->num_queues;
			break;
		case VIO1_PCI_DEVICE_STATUS:
			res = dev->status;
			break;
		case VIO1_PCI_CONFIG_GENERATION:
			res = pci_cfg->config_generation;
			break;
		case VIO1_PCI_QUEUE_SELECT:
			res = pci_cfg->queue_select;
			break;
		case VIO1_PCI_QUEUE_SIZE:
			res = pci_cfg->queue_size;
			break;
		case VIO1_PCI_QUEUE_MSIX_VECTOR:
			res = VIRTIO_MSI_NO_VECTOR;	/* Unsupported */
			break;
		case VIO1_PCI_QUEUE_ENABLE:
			res = pci_cfg->queue_enable;
			break;
		case VIO1_PCI_QUEUE_NOTIFY_OFF:
			res = pci_cfg->queue_notify_off;
			break;
		case VIO1_PCI_QUEUE_DESC:
			res = (uint32_t)(0xFFFFFFFF & pci_cfg->queue_desc);
			break;
		case VIO1_PCI_QUEUE_DESC + 4:
			res = (uint32_t)(pci_cfg->queue_desc >> 32);
			break;
		case VIO1_PCI_QUEUE_AVAIL:
			res = (uint32_t)(0xFFFFFFFF & pci_cfg->queue_avail);
			break;
		case VIO1_PCI_QUEUE_AVAIL + 4:
			res = (uint32_t)(pci_cfg->queue_avail >> 32);
			break;
		case VIO1_PCI_QUEUE_USED:
			res = (uint32_t)(0xFFFFFFFF & pci_cfg->queue_used);
			break;
		case VIO1_PCI_QUEUE_USED + 4:
			res = (uint32_t)(pci_cfg->queue_used >> 32);
			break;
		default:
			log_warnx("%s: invalid register 0x%04x", __func__, reg);
		}
	}

	DPRINTF("%s: dev=%u %s sz=%u dir=%s data=0x%04x", __func__, dev->pci_id,
	    virtio1_reg_name(reg), sz, (dir == VEI_DIR_OUT) ? "w" : "r",
	    (dir == VEI_DIR_OUT) ? data : res);

	return (res);
}

static int
virtio_io_isr(int dir, uint16_t reg, uint32_t *data, uint8_t *intr,
    void *arg, uint8_t sz)
{
	struct virtio_dev *dev = (struct virtio_dev *)arg;
	*intr = 0xFF;

	DPRINTF("%s: dev=%u, reg=0x%04x, sz=%u, dir=%s", __func__,
	    dev->pci_id, reg, sz,
	    (dir == VEI_DIR_OUT) ? "write" : "read");

	/* Limit to in-process devices. */
	if (dev->device_id == PCI_PRODUCT_VIRTIO_BLOCK ||
	    dev->device_id == PCI_PRODUCT_VIRTIO_NETWORK)
		fatalx("%s: cannot use on multi-process virtio dev", __func__);

	if (dir == VEI_DIR_IN) {
		*data = dev->isr;
		dev->isr = 0;
		vcpu_deassert_irq(dev->vm_id, 0, dev->irq);
	}

	return (0);
}

static int
virtio_io_notify(int dir, uint16_t reg, uint32_t *data, uint8_t *intr,
    void *arg, uint8_t sz)
{
	int raise_intr = 0;
	struct virtio_dev *dev = (struct virtio_dev *)arg;
	uint16_t vq_idx = (uint16_t)(0x0000ffff & *data);

	*intr = 0xFF;

	DPRINTF("%s: reg=0x%04x, sz=%u, vq_idx=%u, dir=%s", __func__, reg, sz,
	    vq_idx, (dir == VEI_DIR_OUT) ? "write" : "read");

	/* Limit this handler to in-process devices */
	if (dev->device_id == PCI_PRODUCT_VIRTIO_BLOCK ||
	    dev->device_id == PCI_PRODUCT_VIRTIO_NETWORK)
		fatalx("%s: cannot use on multi-process virtio dev", __func__);

	if (vq_idx >= dev->num_queues) {
		log_warnx("%s: invalid virtqueue index %u", __func__, vq_idx);
		return (0);
	}

	if (dir == VEI_DIR_IN) {
		/* Behavior is undefined. */
		*data = (uint32_t)(-1);
		return (0);
	}

	switch (dev->device_id) {
	case PCI_PRODUCT_VIRTIO_ENTROPY:
		raise_intr = viornd_notifyq(dev, vq_idx);
		break;
	case PCI_PRODUCT_VIRTIO_SCSI:
		raise_intr = vioscsi_notifyq(dev, vq_idx);
		break;
	case PCI_PRODUCT_VIRTIO_VMMCI:
		/* Does not use a virtqueue. */
		break;
	default:
		log_warnx("%s: invalid device type %u", __func__,
		    dev->device_id);
	}

	if (raise_intr)
		*intr = 1;

	return (0);
}

/*
 * vmmci_ctl
 *
 * Inject a command into the vmmci device, potentially delivering interrupt.
 *
 * Called by the vm process's event(3) loop.
 */
int
vmmci_ctl(struct virtio_dev *dev, unsigned int cmd)
{
	int ret = 0;
	struct timeval tv = { 0, 0 };
	struct vmmci_dev *v = NULL;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_VMMCI)
		fatalx("%s: device is not a vmmci device", __func__);
	v = &dev->vmmci;

	mutex_lock(&v->mutex);

	if ((dev->status & VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK) == 0) {
		ret = -1;
		goto unlock;
	}

	if (cmd == v->cmd)
		goto unlock;

	switch (cmd) {
	case VMMCI_NONE:
		break;
	case VMMCI_SHUTDOWN:
	case VMMCI_REBOOT:
		/* Update command */
		v->cmd = cmd;

		/*
		 * vmm VMs do not support powerdown, send a reboot request
		 * instead and turn it off after the triple fault.
		 */
		if (cmd == VMMCI_SHUTDOWN)
			cmd = VMMCI_REBOOT;

		/* Trigger interrupt */
		dev->isr = VIRTIO_CONFIG_ISR_CONFIG_CHANGE;
		vcpu_assert_irq(dev->vm_id, 0, dev->irq);

		/* Add ACK timeout */
		tv.tv_sec = VMMCI_TIMEOUT_SHORT;
		evtimer_add(&v->timeout, &tv);
		break;
	case VMMCI_SYNCRTC:
		if (vmmci.cfg.guest_feature & VMMCI_F_SYNCRTC) {
			/* RTC updated, request guest VM resync of its RTC */
			v->cmd = cmd;

			dev->isr = VIRTIO_CONFIG_ISR_CONFIG_CHANGE;
			vcpu_assert_irq(dev->vm_id, 0, dev->irq);
		} else {
			log_debug("%s: RTC sync skipped (guest does not "
			    "support RTC sync)\n", __func__);
		}
		break;
	default:
		fatalx("invalid vmmci command: %d", cmd);
	}

unlock:
	mutex_unlock(&v->mutex);

	return (ret);
}

/*
 * vmmci_ack
 *
 * Process a write to the command register.
 *
 * Called by the vcpu thread. Must be called with the mutex held.
 */
static void
vmmci_ack(struct virtio_dev *dev, unsigned int cmd)
{
	struct vmmci_dev *v = NULL;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_VMMCI)
		fatalx("%s: device is not a vmmci device", __func__);
	v = &dev->vmmci;

	switch (cmd) {
	case VMMCI_NONE:
		break;
	case VMMCI_SHUTDOWN:
		/*
		 * The shutdown was requested by the VM if we don't have
		 * a pending shutdown request.  In this case add a short
		 * timeout to give the VM a chance to reboot before the
		 * timer is expired.
		 */
		if (v->cmd == 0) {
			log_debug("%s: vm %u requested shutdown", __func__,
			    dev->vm_id);
			vm_pipe_send(&v->dev_pipe, VMMCI_SET_TIMEOUT_SHORT);
			return;
		}
		/* FALLTHROUGH */
	case VMMCI_REBOOT:
		/*
		 * If the VM acknowledged our shutdown request, give it
		 * enough time to shutdown or reboot gracefully.  This
		 * might take a considerable amount of time (running
		 * rc.shutdown on the VM), so increase the timeout before
		 * killing it forcefully.
		 */
		if (cmd == v->cmd) {
			log_debug("%s: vm %u acknowledged shutdown request",
			    __func__, dev->vm_id);
			vm_pipe_send(&v->dev_pipe, VMMCI_SET_TIMEOUT_LONG);
		}
		break;
	case VMMCI_SYNCRTC:
		log_debug("%s: vm %u acknowledged RTC sync request",
		    __func__, dev->vm_id);
		v->cmd = VMMCI_NONE;
		break;
	default:
		log_warnx("%s: illegal request %u", __func__, cmd);
		break;
	}
}

void
vmmci_timeout(int fd, short type, void *arg)
{
	struct virtio_dev *dev = (struct virtio_dev *)arg;
	struct vmmci_dev *v = NULL;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_VMMCI)
		fatalx("%s: device is not a vmmci device", __func__);
	v = &dev->vmmci;

	log_debug("%s: vm %u shutdown", __progname, dev->vm_id);
	vm_shutdown(v->cmd == VMMCI_REBOOT ? VMMCI_REBOOT : VMMCI_SHUTDOWN);
}

int
vmmci_io(int dir, uint16_t reg, uint32_t *data, uint8_t *intr,
    void *arg, uint8_t sz)
{
	struct virtio_dev	*dev = (struct virtio_dev *)arg;
	struct vmmci_dev	*v = NULL;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_VMMCI)
		fatalx("%s: device is not a vmmci device (%u)",
		    __func__, dev->device_id);
	v = &dev->vmmci;

	*intr = 0xFF;

	mutex_lock(&v->mutex);
	if (dir == 0) {
		switch (reg) {
		case VIRTIO_CONFIG_DEVICE_FEATURES:
		case VIRTIO_CONFIG_QUEUE_SIZE:
		case VIRTIO_CONFIG_ISR_STATUS:
			log_warnx("%s: illegal write %x to %s",
			    __progname, *data, virtio_reg_name(reg));
			break;
		case VIRTIO_CONFIG_GUEST_FEATURES:
			dev->cfg.guest_feature = *data;
			break;
		case VIRTIO_CONFIG_QUEUE_PFN:
			dev->cfg.queue_pfn = *data;
			break;
		case VIRTIO_CONFIG_QUEUE_SELECT:
			dev->cfg.queue_select = *data;
			break;
		case VIRTIO_CONFIG_QUEUE_NOTIFY:
			dev->cfg.queue_notify = *data;
			break;
		case VIRTIO_CONFIG_DEVICE_STATUS:
			dev->status = *data;
			break;
		case VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI:
			vmmci_ack(dev, *data);
			break;
		}
	} else {
		switch (reg) {
		case VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI:
			*data = v->cmd;
			break;
		case VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI + 4:
			/* Update time once when reading the first register */
			gettimeofday(&v->time, NULL);
			*data = (uint64_t)v->time.tv_sec;
			break;
		case VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI + 8:
			*data = (uint64_t)v->time.tv_sec << 32;
			break;
		case VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI + 12:
			*data = (uint64_t)v->time.tv_usec;
			break;
		case VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI + 16:
			*data = (uint64_t)v->time.tv_usec << 32;
			break;
		case VIRTIO_CONFIG_DEVICE_FEATURES:
			*data = dev->cfg.device_feature;
			break;
		case VIRTIO_CONFIG_GUEST_FEATURES:
			*data = dev->cfg.guest_feature;
			break;
		case VIRTIO_CONFIG_QUEUE_PFN:
			*data = dev->cfg.queue_pfn;
			break;
		case VIRTIO_CONFIG_QUEUE_SIZE:
			*data = dev->cfg.queue_size;
			break;
		case VIRTIO_CONFIG_QUEUE_SELECT:
			*data = dev->cfg.queue_select;
			break;
		case VIRTIO_CONFIG_QUEUE_NOTIFY:
			*data = dev->cfg.queue_notify;
			break;
		case VIRTIO_CONFIG_DEVICE_STATUS:
			*data = dev->status;
			break;
		case VIRTIO_CONFIG_ISR_STATUS:
			*data = dev->isr;
			dev->isr = 0;
			vcpu_deassert_irq(dev->vm_id, 0, dev->irq);
			break;
		}
	}
	mutex_unlock(&v->mutex);

	return (0);
}

int
virtio_get_base(int fd, char *path, size_t npath, int type, const char *dpath)
{
	switch (type) {
	case VMDF_RAW:
		return 0;
	case VMDF_QCOW2:
		return virtio_qcow2_get_base(fd, path, npath, dpath);
	}
	log_warnx("%s: invalid disk format", __func__);
	return -1;
}

static void
vmmci_pipe_dispatch(int fd, short event, void *arg)
{
	struct virtio_dev	*dev = (struct virtio_dev *)arg;
	struct vmmci_dev 	*v = &dev->vmmci;
	struct timeval		 tv = { 0, 0 };
	enum pipe_msg_type	 msg;

	msg = vm_pipe_recv(&v->dev_pipe);
	switch (msg) {
	case VMMCI_SET_TIMEOUT_SHORT:
		tv.tv_sec = VMMCI_TIMEOUT_SHORT;
		evtimer_add(&v->timeout, &tv);
		break;
	case VMMCI_SET_TIMEOUT_LONG:
		tv.tv_sec = VMMCI_TIMEOUT_LONG;
		evtimer_add(&v->timeout, &tv);
		break;
	default:
		log_warnx("%s: invalid pipe message type %d", __func__, msg);
	}
}

void
virtio_init(struct vmd_vm *vm, int child_cdrom,
    int child_disks[][VM_MAX_BASE_PER_DISK], int *child_taps)
{
	struct vmop_create_params *vmc = &vm->vm_params;
	struct vm_create_params *vcp = &vmc->vmc_params;
	struct virtio_dev *dev;
	uint8_t id, i, j;
	int bar_id, ret = 0;

	SLIST_INIT(&virtio_devs);

	/* Virtio 1.x Entropy Device */
	if (pci_add_device(&id, PCI_VENDOR_QUMRANET,
	    PCI_PRODUCT_QUMRANET_VIO1_RNG, PCI_CLASS_SYSTEM,
	    PCI_SUBCLASS_SYSTEM_MISC, PCI_VENDOR_OPENBSD,
	    PCI_PRODUCT_VIRTIO_ENTROPY, 1, 1, NULL)) {
		log_warnx("%s: can't add PCI virtio rng device",
		    __progname);
		return;
	}
	virtio_dev_init(&viornd, id, VIORND_QUEUE_SIZE_DEFAULT,
	    VIRTIO_RND_QUEUES, VIRTIO_F_VERSION_1, vcp->vcp_id);

	bar_id = pci_add_bar(id, PCI_MAPREG_TYPE_IO, virtio_io_dispatch,
	    &viornd);
	if (bar_id == -1 || bar_id > 0xff) {
		log_warnx("%s: can't add bar for virtio rng device",
		    __progname);
		return;
	}
	virtio_pci_add_cap(id, VIRTIO_PCI_CAP_COMMON_CFG, bar_id, 0);
	virtio_pci_add_cap(id, VIRTIO_PCI_CAP_ISR_CFG, bar_id, 0);
	virtio_pci_add_cap(id, VIRTIO_PCI_CAP_NOTIFY_CFG, bar_id, 0);

	/* Virtio 1.x Network Devices */
	if (vmc->vmc_nnics > 0) {
		for (i = 0; i < vmc->vmc_nnics; i++) {
			dev = malloc(sizeof(struct virtio_dev));
			if (dev == NULL) {
				log_warn("%s: calloc failure allocating vionet",
				    __progname);
				return;
			}
			if (pci_add_device(&id, PCI_VENDOR_QUMRANET,
				PCI_PRODUCT_QUMRANET_VIO1_NET, PCI_CLASS_SYSTEM,
				PCI_SUBCLASS_SYSTEM_MISC, PCI_VENDOR_OPENBSD,
				PCI_PRODUCT_VIRTIO_NETWORK, 1, 1, NULL)) {
				log_warnx("%s: can't add PCI virtio net device",
				    __progname);
				return;
			}
			virtio_dev_init(dev, id, VIONET_QUEUE_SIZE_DEFAULT,
			    VIRTIO_NET_QUEUES,
			    (VIRTIO_NET_F_MAC | VIRTIO_F_VERSION_1),
			    vcp->vcp_id);

			if (pci_add_bar(id, PCI_MAPREG_TYPE_IO, virtio_pci_io,
			    dev) == -1) {
				log_warnx("%s: can't add bar for virtio net "
				    "device", __progname);
				return;
			}
			virtio_pci_add_cap(id, VIRTIO_PCI_CAP_COMMON_CFG,
			    bar_id, 0);
			virtio_pci_add_cap(id, VIRTIO_PCI_CAP_DEVICE_CFG,
			    bar_id, 8);
			virtio_pci_add_cap(id, VIRTIO_PCI_CAP_ISR_CFG, bar_id,
			    0);
			virtio_pci_add_cap(id, VIRTIO_PCI_CAP_NOTIFY_CFG,
			    bar_id, 0);

			/* Device specific initializiation. */
			dev->dev_type = VMD_DEVTYPE_NET;
			dev->vm_vmid = vm->vm_vmid;
			dev->vionet.data_fd = child_taps[i];

			/* MAC address has been assigned by the parent */
			memcpy(&dev->vionet.mac, &vmc->vmc_macs[i], 6);
			dev->vionet.lockedmac =
			    vmc->vmc_ifflags[i] & VMIFF_LOCKED ? 1 : 0;
			dev->vionet.local =
			    vmc->vmc_ifflags[i] & VMIFF_LOCAL ? 1 : 0;
			if (i == 0 && vmc->vmc_bootdevice & VMBOOTDEV_NET)
				dev->vionet.pxeboot = 1;
			memcpy(&dev->vionet.local_prefix,
			    &env->vmd_cfg.cfg_localprefix,
			    sizeof(dev->vionet.local_prefix));
			log_debug("%s: vm \"%s\" vio%u lladdr %s%s%s%s",
			    __func__, vcp->vcp_name, i,
			    ether_ntoa((void *)dev->vionet.mac),
			    dev->vionet.lockedmac ? ", locked" : "",
			    dev->vionet.local ? ", local" : "",
			    dev->vionet.pxeboot ? ", pxeboot" : "");

			/* Add the vionet to our device list. */
			dev->vionet.idx = i;
			SLIST_INSERT_HEAD(&virtio_devs, dev, dev_next);
		}
	}

	/* Virtio 1.x Block Devices */
	if (vmc->vmc_ndisks > 0) {
		for (i = 0; i < vmc->vmc_ndisks; i++) {
			dev = malloc(sizeof(struct virtio_dev));
			if (dev == NULL) {
				log_warn("%s: failure allocating vioblk",
				    __func__);
				return;
			}
			if (pci_add_device(&id, PCI_VENDOR_QUMRANET,
			    PCI_PRODUCT_QUMRANET_VIO1_BLOCK,
			    PCI_CLASS_MASS_STORAGE,
			    PCI_SUBCLASS_MASS_STORAGE_SCSI, PCI_VENDOR_OPENBSD,
			    PCI_PRODUCT_VIRTIO_BLOCK, 1, 1, NULL)) {
				log_warnx("%s: can't add PCI virtio block "
				    "device", __progname);
				return;
			}
			virtio_dev_init(dev, id, VIOBLK_QUEUE_SIZE_DEFAULT,
			    VIRTIO_BLK_QUEUES,
			    (VIRTIO_F_VERSION_1 | VIRTIO_BLK_F_SEG_MAX),
			    vcp->vcp_id);

			bar_id = pci_add_bar(id, PCI_MAPREG_TYPE_IO, virtio_pci_io,
			    dev);
			if (bar_id == -1 || bar_id > 0xff) {
				log_warnx("%s: can't add bar for virtio block "
				    "device", __progname);
				return;
			}
			virtio_pci_add_cap(id, VIRTIO_PCI_CAP_COMMON_CFG,
			    bar_id, 0);
			virtio_pci_add_cap(id, VIRTIO_PCI_CAP_DEVICE_CFG,
			    bar_id, 24);
			virtio_pci_add_cap(id, VIRTIO_PCI_CAP_ISR_CFG, bar_id,
			    0);
			virtio_pci_add_cap(id, VIRTIO_PCI_CAP_NOTIFY_CFG,
			    bar_id, 0);

			/* Device specific initialization. */
			dev->dev_type = VMD_DEVTYPE_DISK;
			dev->vm_vmid = vm->vm_vmid;
			dev->vioblk.seg_max = VIOBLK_SEG_MAX_DEFAULT;

			/*
			 * Initialize disk fds to an invalid fd (-1), then
			 * set any child disk fds.
			 */
			memset(&dev->vioblk.disk_fd, -1,
			    sizeof(dev->vioblk.disk_fd));
			dev->vioblk.ndisk_fd = vmc->vmc_diskbases[i];
			for (j = 0; j < dev->vioblk.ndisk_fd; j++)
				dev->vioblk.disk_fd[j] = child_disks[i][j];

			dev->vioblk.idx = i;
			SLIST_INSERT_HEAD(&virtio_devs, dev, dev_next);
		}
	}

	/*
	 * Launch virtio devices that support subprocess execution.
	 */
	SLIST_FOREACH(dev, &virtio_devs, dev_next) {
		if (virtio_dev_launch(vm, dev) != 0)
			fatalx("failed to launch virtio device");
	}

	/* Virtio 1.x SCSI CD-ROM */
	if (strlen(vmc->vmc_cdrom)) {
		dev = malloc(sizeof(struct virtio_dev));
		if (dev == NULL) {
			log_warn("%s: calloc failure allocating vioscsi",
			    __progname);
			return;
		}
		if (pci_add_device(&id, PCI_VENDOR_QUMRANET,
		    PCI_PRODUCT_QUMRANET_VIO1_SCSI, PCI_CLASS_MASS_STORAGE,
		    PCI_SUBCLASS_MASS_STORAGE_SCSI, PCI_VENDOR_OPENBSD,
		    PCI_PRODUCT_VIRTIO_SCSI, 1, 1, NULL)) {
			log_warnx("%s: can't add PCI vioscsi device",
			    __progname);
			return;
		}
		virtio_dev_init(dev, id, VIOSCSI_QUEUE_SIZE_DEFAULT,
		    VIRTIO_SCSI_QUEUES, VIRTIO_F_VERSION_1, vcp->vcp_id);
		if (pci_add_bar(id, PCI_MAPREG_TYPE_IO, virtio_io_dispatch, dev)
		    == -1) {
			log_warnx("%s: can't add bar for vioscsi device",
			    __progname);
			return;
		}
		virtio_pci_add_cap(id, VIRTIO_PCI_CAP_COMMON_CFG, bar_id, 0);
		virtio_pci_add_cap(id, VIRTIO_PCI_CAP_DEVICE_CFG, bar_id, 36);
		virtio_pci_add_cap(id, VIRTIO_PCI_CAP_ISR_CFG, bar_id, 0);
		virtio_pci_add_cap(id, VIRTIO_PCI_CAP_NOTIFY_CFG, bar_id, 0);

		/* Device specific initialization. */
		if (virtio_raw_init(&dev->vioscsi.file, &dev->vioscsi.sz,
		    &child_cdrom, 1) == -1) {
			log_warnx("%s: unable to determine iso format",
			    __func__);
			return;
		}
		dev->vioscsi.locked = 0;
		dev->vioscsi.lba = 0;
		dev->vioscsi.n_blocks = dev->vioscsi.sz /
		    VIOSCSI_BLOCK_SIZE_CDROM;
		dev->vioscsi.max_xfer = VIOSCSI_BLOCK_SIZE_CDROM;
	}

	/* Virtio 0.9 VMM Control Interface */
	dev = &vmmci;
	if (pci_add_device(&id, PCI_VENDOR_OPENBSD, PCI_PRODUCT_OPENBSD_CONTROL,
	    PCI_CLASS_COMMUNICATIONS, PCI_SUBCLASS_COMMUNICATIONS_MISC,
	    PCI_VENDOR_OPENBSD, PCI_PRODUCT_VIRTIO_VMMCI, 0, 1, NULL)) {
		log_warnx("%s: can't add PCI vmm control device",
		    __progname);
		return;
	}
	virtio_dev_init(dev, id, 0, 0,
	    VMMCI_F_TIMESYNC | VMMCI_F_ACK | VMMCI_F_SYNCRTC, vcp->vcp_id);
	if (pci_add_bar(id, PCI_MAPREG_TYPE_IO, vmmci_io, dev) == -1) {
		log_warnx("%s: can't add bar for vmm control device",
		    __progname);
		return;
	}

	ret = pthread_mutex_init(&dev->vmmci.mutex, NULL);
	if (ret) {
		errno = ret;
		fatal("could not initialize vmmci mutex");
	}
	evtimer_set(&dev->vmmci.timeout, vmmci_timeout, NULL);
	vm_pipe_init2(&dev->vmmci.dev_pipe, vmmci_pipe_dispatch, dev);
	event_add(&dev->vmmci.dev_pipe.read_ev, NULL);
}

/*
 * vionet_set_hostmac
 *
 * Sets the hardware address for the host-side tap(4) on a vionet_dev.
 *
 * This should only be called from the event-loop thread
 *
 * vm: pointer to the current vmd_vm instance
 * idx: index into the array of vionet_dev's for the target vionet_dev
 * addr: ethernet address to set
 */
void
vionet_set_hostmac(struct vmd_vm *vm, unsigned int idx, uint8_t *addr)
{
	struct vmop_create_params	*vmc = &vm->vm_params;
	struct virtio_dev		*dev;
	struct vionet_dev		*vionet = NULL;
	int ret;

	if (idx > vmc->vmc_nnics)
		fatalx("%s: invalid vionet index: %u", __func__, idx);

	SLIST_FOREACH(dev, &virtio_devs, dev_next) {
		if (dev->dev_type == VMD_DEVTYPE_NET
		    && dev->vionet.idx == idx) {
			vionet = &dev->vionet;
			break;
		}
	}
	if (vionet == NULL)
		fatalx("%s: dev == NULL, idx = %u", __func__, idx);

	/* Set the local vm process copy. */
	memcpy(vionet->hostmac, addr, sizeof(vionet->hostmac));

	/* Send the information to the device process. */
	ret = imsg_compose_event(&dev->async_iev, IMSG_DEVOP_HOSTMAC, 0, 0, -1,
	    vionet->hostmac, sizeof(vionet->hostmac));
	if (ret == -1) {
		log_warnx("%s: failed to queue hostmac to vionet dev %u",
		    __func__, idx);
		return;
	}
}

void
virtio_shutdown(struct vmd_vm *vm)
{
	int ret, status;
	pid_t pid = 0;
	struct virtio_dev *dev, *tmp;
	struct viodev_msg msg;
	struct imsgbuf *ibuf;

	/* Ensure that our disks are synced. */
	if (vioscsi != NULL)
		vioscsi->vioscsi.file.close(vioscsi->vioscsi.file.p, 0);

	/*
	 * Broadcast shutdown to child devices. We need to do this
	 * synchronously as we have already stopped the async event thread.
	 */
	SLIST_FOREACH(dev, &virtio_devs, dev_next) {
		memset(&msg, 0, sizeof(msg));
		msg.type = VIODEV_MSG_SHUTDOWN;
		ibuf = &dev->sync_iev.ibuf;
		ret = imsg_compose(ibuf, VIODEV_MSG_SHUTDOWN, 0, 0, -1,
		    &msg, sizeof(msg));
		if (ret == -1)
			fatalx("%s: failed to send shutdown to device",
			    __func__);
		if (imsgbuf_flush(ibuf) == -1)
			fatalx("%s: imsgbuf_flush", __func__);
	}

	/*
	 * Wait for all children to shutdown using a simple approach of
	 * iterating over known child devices and waiting for them to die.
	 */
	SLIST_FOREACH_SAFE(dev, &virtio_devs, dev_next, tmp) {
		log_debug("%s: waiting on device pid %d", __func__,
		    dev->dev_pid);
		do {
			pid = waitpid(dev->dev_pid, &status, WNOHANG);
		} while (pid == 0 || (pid == -1 && errno == EINTR));
		if (pid == dev->dev_pid)
			log_debug("%s: device for pid %d is stopped",
			    __func__, pid);
		else
			log_warnx("%s: unexpected pid %d", __func__, pid);
		free(dev);
	}
}

void virtio_broadcast_imsg(struct vmd_vm *vm, uint16_t type, void *data,
    uint16_t datalen)
{
	struct virtio_dev *dev;
	int ret;

	SLIST_FOREACH(dev, &virtio_devs, dev_next) {
		ret = imsg_compose_event(&dev->async_iev, type, 0, 0, -1, data,
		    datalen);
		if (ret == -1) {
			log_warnx("%s: failed to broadcast imsg type %u",
			    __func__, type);
		}
	}

}

void
virtio_stop(struct vmd_vm *vm)
{
	return virtio_broadcast_imsg(vm, IMSG_VMDOP_PAUSE_VM, NULL, 0);
}

void
virtio_start(struct vmd_vm *vm)
{
	return virtio_broadcast_imsg(vm, IMSG_VMDOP_UNPAUSE_VM, NULL, 0);
}

/*
 * Initialize a new virtio device structure.
 */
static void
virtio_dev_init(struct virtio_dev *dev, uint8_t pci_id, uint16_t queue_size,
    uint16_t num_queues, uint64_t features, uint32_t vm_id)
{
	size_t i;
	uint16_t device_id;

	if (num_queues > 0 && num_queues > VIRTIO_MAX_QUEUES)
		fatalx("%s: num_queues too large", __func__);

	device_id = pci_get_subsys_id(pci_id);
	if (!device_id)
		fatalx("%s: invalid pci device id %u", __func__, pci_id);

	memset(dev, 0, sizeof(*dev));

	dev->pci_id = pci_id;
	dev->device_id = device_id;
	dev->irq = pci_get_dev_irq(pci_id);
	dev->isr = 0;
	dev->vm_id = vm_id;

	dev->device_feature = features;

	dev->pci_cfg.config_generation = 0;
	dev->cfg.device_feature = features;

	dev->num_queues = num_queues;
	dev->queue_size = queue_size;
	dev->cfg.queue_size = queue_size;

	dev->async_fd = -1;
	dev->sync_fd = -1;

	if (num_queues > 0) {
		for (i = 0; i < num_queues; i++)
			virtio_vq_init(dev, i);
	}
}

void
virtio_vq_init(struct virtio_dev *dev, size_t idx)
{
	struct virtio_vq_info *vq_info = NULL;
	int v1 = (dev->device_feature & VIRTIO_F_VERSION_1) ? 1 : 0;

	if (idx >= dev->num_queues)
		fatalx("%s: invalid virtqueue index", __func__);
	vq_info = &dev->vq[idx];

	vq_info->q_gpa = 0;
	vq_info->qs = dev->queue_size;
	vq_info->mask = dev->queue_size - 1;

	if (v1) {
		vq_info->vq_enabled = 0;
		vq_info->vq_availoffset = 0;
		vq_info->vq_usedoffset = 0;
	} else {
		/* Always enable on pre-1.0 virtio devices. */
		vq_info->vq_enabled = 1;
		vq_info->vq_availoffset =
		    sizeof(struct vring_desc) * vq_info->qs;
		vq_info->vq_usedoffset = VIRTQUEUE_ALIGN(
		    sizeof(struct vring_desc) * vq_info->qs +
		    sizeof(uint16_t) * (2 + vq_info->qs));
	}

	vq_info->last_avail = 0;
	vq_info->notified_avail = 0;
}


static void
virtio_pci_add_cap(uint8_t pci_id, uint8_t cfg_type, uint8_t bar_id,
    uint32_t dev_cfg_len)
{
	struct virtio_pci_common_cap cap;

	memset(&cap, 0, sizeof(cap));

	cap.virtio.cap_vndr = PCI_CAP_VENDSPEC;
	cap.virtio.cap_len = sizeof(struct virtio_pci_cap);
	cap.virtio.bar = bar_id;
	cap.virtio.cfg_type = cfg_type;

	switch (cfg_type) {
	case VIRTIO_PCI_CAP_COMMON_CFG:
		cap.virtio.offset = VIO1_CFG_BAR_OFFSET;
		cap.virtio.length = sizeof(struct virtio_pci_common_cfg);
		break;
	case VIRTIO_PCI_CAP_DEVICE_CFG:
		/* XXX maybe inspect the virtio device and lookup the len. */
		cap.virtio.offset = VIO1_DEV_BAR_OFFSET;
		cap.virtio.length = dev_cfg_len;
		break;
	case VIRTIO_PCI_CAP_ISR_CFG:
		cap.virtio.offset = VIO1_ISR_BAR_OFFSET;
		cap.virtio.length = sizeof(uint8_t);
		break;
	case VIRTIO_PCI_CAP_NOTIFY_CFG:
		cap.virtio.offset = VIO1_NOTIFY_BAR_OFFSET;
		cap.virtio.length = sizeof(uint16_t);
		cap.notify.notify_off_multiplier = 0;
		break;
	default:
		fatalx("%s: invalid pci capability config type %u", __func__,
		    cfg_type);
	}

	if (pci_add_capability(pci_id, &cap.pci) == -1) {
		fatalx("%s: can't add capability for virtio pci device %u",
		    __func__, pci_id);
	}
}

/*
 * Fork+exec a child virtio device. Returns 0 on success.
 */
static int
virtio_dev_launch(struct vmd_vm *vm, struct virtio_dev *dev)
{
	char *nargv[12], num[32], vmm_fd[32], vm_name[VM_NAME_MAX], t[2];
	pid_t dev_pid;
	int sync_fds[2], async_fds[2], ret = 0;
	size_t i, sz = 0;
	struct viodev_msg msg;
	struct virtio_dev *dev_entry;
	struct imsg imsg;
	struct imsgev *iev = &dev->sync_iev;

	switch (dev->dev_type) {
	case VMD_DEVTYPE_NET:
		log_debug("%s: launching vionet%d",
		    vm->vm_params.vmc_params.vcp_name, dev->vionet.idx);
		break;
	case VMD_DEVTYPE_DISK:
		log_debug("%s: launching vioblk%d",
		    vm->vm_params.vmc_params.vcp_name, dev->vioblk.idx);
		break;
		/* NOTREACHED */
	default:
		log_warn("%s: invalid device type", __func__);
		return (EINVAL);
	}

	/* We need two channels: one synchronous (IO reads) and one async. */
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, PF_UNSPEC,
	    sync_fds) == -1) {
		log_warn("failed to create socketpair");
		return (errno);
	}
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, PF_UNSPEC,
	    async_fds) == -1) {
		log_warn("failed to create async socketpair");
		return (errno);
	}

	/* Fork... */
	dev_pid = fork();
	if (dev_pid == -1) {
		ret = errno;
		log_warn("%s: fork failed", __func__);
		goto err;
	}

	if (dev_pid > 0) {
		/* Parent */
		close_fd(sync_fds[1]);
		close_fd(async_fds[1]);

		/* Save the child's pid to help with cleanup. */
		dev->dev_pid = dev_pid;

		/* Set the channel fds to the child's before sending. */
		dev->sync_fd = sync_fds[1];
		dev->async_fd = async_fds[1];

		/* 1. Send over our configured device. */
		log_debug("%s: sending '%c' type device struct", __func__,
			dev->dev_type);
		sz = atomicio(vwrite, sync_fds[0], dev, sizeof(*dev));
		if (sz != sizeof(*dev)) {
			log_warnx("%s: failed to send device", __func__);
			ret = EIO;
			goto err;
		}

		/* Close data fds. Only the child device needs them now. */
		if (virtio_dev_closefds(dev) == -1) {
			log_warnx("%s: failed to close device data fds",
			    __func__);
			goto err;
		}

		/* 2. Send over details on the VM (including memory fds). */
		log_debug("%s: sending vm message for '%s'", __func__,
			vm->vm_params.vmc_params.vcp_name);
		sz = atomicio(vwrite, sync_fds[0], vm, sizeof(*vm));
		if (sz != sizeof(*vm)) {
			log_warnx("%s: failed to send vm details", __func__);
			ret = EIO;
			goto err;
		}

		/*
		 * Initialize our imsg channel to the child device. The initial
		 * communication will be synchronous. We expect the child to
		 * report itself "ready" to confirm the launch was a success.
		 */
		if (imsgbuf_init(&iev->ibuf, sync_fds[0]) == -1) {
			log_warn("%s: failed to init imsgbuf", __func__);
			goto err;
		}
		imsgbuf_allow_fdpass(&iev->ibuf);
		ret = imsgbuf_read_one(&iev->ibuf, &imsg);
		if (ret == 0 || ret == -1) {
			log_warnx("%s: failed to receive ready message from "
			    "'%c' type device", __func__, dev->dev_type);
			ret = EIO;
			goto err;
		}
		ret = 0;

		viodev_msg_read(&imsg, &msg);
		imsg_free(&imsg);

		if (msg.type != VIODEV_MSG_READY) {
			log_warnx("%s: expected ready message, got type %d",
			    __func__, msg.type);
			ret = EINVAL;
			goto err;
		}
		log_debug("%s: device reports ready via sync channel",
		    __func__);

		/*
		 * Wire in the async event handling, but after reverting back
		 * to the parent's fd's.
		 */
		dev->sync_fd = sync_fds[0];
		dev->async_fd = async_fds[0];
		vm_device_pipe(dev, virtio_dispatch_dev, NULL);
	} else {
		/* Child */
		close_fd(async_fds[0]);
		close_fd(sync_fds[0]);

		/* Close pty. Virtio devices do not need it. */
		close_fd(vm->vm_tty);
		vm->vm_tty = -1;

		if (vm->vm_cdrom != -1) {
			close_fd(vm->vm_cdrom);
			vm->vm_cdrom = -1;
		}

		/* Keep data file descriptors open after exec. */
		SLIST_FOREACH(dev_entry, &virtio_devs, dev_next) {
			if (dev_entry == dev)
				continue;
			if (virtio_dev_closefds(dev_entry) == -1)
				fatalx("unable to close other virtio devs");
		}

		memset(num, 0, sizeof(num));
		snprintf(num, sizeof(num), "%d", sync_fds[1]);
		memset(vmm_fd, 0, sizeof(vmm_fd));
		snprintf(vmm_fd, sizeof(vmm_fd), "%d", env->vmd_fd);
		memset(vm_name, 0, sizeof(vm_name));
		snprintf(vm_name, sizeof(vm_name), "%s",
		    vm->vm_params.vmc_params.vcp_name);

		t[0] = dev->dev_type;
		t[1] = '\0';

		i = 0;
		nargv[i++] = env->argv0;
		nargv[i++] = "-X";
		nargv[i++] = num;
		nargv[i++] = "-t";
		nargv[i++] = t;
		nargv[i++] = "-i";
		nargv[i++] = vmm_fd;
		nargv[i++] = "-p";
		nargv[i++] = vm_name;
		if (env->vmd_debug)
			nargv[i++] = "-d";
		if (env->vmd_verbose == 1)
			nargv[i++] = "-v";
		else if (env->vmd_verbose > 1)
			nargv[i++] = "-vv";
		nargv[i++] = NULL;
		if (i > sizeof(nargv) / sizeof(nargv[0]))
			fatalx("%s: nargv overflow", __func__);

		/* Control resumes in vmd.c:main(). */
		execvp(nargv[0], nargv);

		ret = errno;
		log_warn("%s: failed to exec device", __func__);
		_exit(ret);
		/* NOTREACHED */
	}

	return (ret);

err:
	close_fd(sync_fds[0]);
	close_fd(sync_fds[1]);
	close_fd(async_fds[0]);
	close_fd(async_fds[1]);
	return (ret);
}

/*
 * Initialize an async imsg channel for a virtio device.
 */
int
vm_device_pipe(struct virtio_dev *dev, void (*cb)(int, short, void *),
    struct event_base *ev_base)
{
	struct imsgev *iev = &dev->async_iev;
	int fd = dev->async_fd;

	log_debug("%s: initializing '%c' device pipe (fd=%d)", __func__,
	    dev->dev_type, fd);

	if (imsgbuf_init(&iev->ibuf, fd) == -1)
		fatal("imsgbuf_init");
	imsgbuf_allow_fdpass(&iev->ibuf);
	iev->handler = cb;
	iev->data = dev;
	iev->events = EV_READ;
	imsg_event_add2(iev, ev_base);

	return (0);
}

void
virtio_dispatch_dev(int fd, short event, void *arg)
{
	struct virtio_dev	*dev = (struct virtio_dev*)arg;
	struct imsgev		*iev = &dev->async_iev;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct imsg		 imsg;
	struct viodev_msg	 msg;
	ssize_t			 n = 0;
	uint32_t		 type;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("%s: imsgbuf_read", __func__);
		if (n == 0) {
			/* this pipe is dead, so remove the event handler */
			log_debug("%s: pipe dead (EV_READ)", __func__);
			event_del(&iev->ev);
			event_loopexit(NULL);
			return;
		}
	}

	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE) {
				/* this pipe is dead, remove the handler */
				log_debug("%s: pipe dead (EV_WRITE)", __func__);
				event_del(&iev->ev);
				event_loopexit(NULL);
				return;
			}
			fatal("%s: imsgbuf_write", __func__);
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("%s: imsg_get", __func__);
		if (n == 0)
			break;

		type = imsg_get_type(&imsg);
		switch (type) {
		case IMSG_DEVOP_MSG:
			viodev_msg_read(&imsg, &msg);
			handle_dev_msg(&msg, dev);
			break;
		default:
			log_warnx("%s: got non devop imsg %d", __func__, type);
			break;
		}
		imsg_free(&imsg);
	}
	imsg_event_add(iev);
}


static int
handle_dev_msg(struct viodev_msg *msg, struct virtio_dev *gdev)
{
	uint32_t vm_id = gdev->vm_id;

	switch (msg->type) {
	case VIODEV_MSG_KICK:
		if (msg->state == INTR_STATE_ASSERT)
			vcpu_assert_irq(vm_id, msg->vcpu, msg->irq);
		else if (msg->state == INTR_STATE_DEASSERT)
			vcpu_deassert_irq(vm_id, msg->vcpu, msg->irq);
		break;
	case VIODEV_MSG_READY:
		log_debug("%s: device reports ready", __func__);
		break;
	case VIODEV_MSG_ERROR:
		log_warnx("%s: device reported error", __func__);
		break;
	case VIODEV_MSG_INVALID:
	case VIODEV_MSG_IO_READ:
	case VIODEV_MSG_IO_WRITE:
		/* FALLTHROUGH */
	default:
		log_warnx("%s: unsupported device message type %d", __func__,
		    msg->type);
		return (1);
	}

	return (0);
};

/*
 * Called by the VM process while processing IO from the VCPU thread.
 *
 * N.b. Since the VCPU thread calls this function, we cannot mutate the event
 * system. All ipc messages must be sent manually and cannot be queued for
 * the event loop to push them. (We need to perform a synchronous read, so
 * this isn't really a big deal.)
 */
int
virtio_pci_io(int dir, uint16_t reg, uint32_t *data, uint8_t *intr,
    void *cookie, uint8_t sz)
{
	struct virtio_dev *dev = (struct virtio_dev *)cookie;
	struct imsgbuf *ibuf = &dev->sync_iev.ibuf;
	struct imsg imsg;
	struct viodev_msg msg;
	int ret = 0;

	memset(&msg, 0, sizeof(msg));
	msg.reg = reg;
	msg.io_sz = sz;

	if (dir == 0) {
		msg.type = VIODEV_MSG_IO_WRITE;
		msg.data = *data;
		msg.data_valid = 1;
	} else
		msg.type = VIODEV_MSG_IO_READ;

	if (msg.type == VIODEV_MSG_IO_WRITE) {
		/*
		 * Write request. No reply expected.
		 */
		ret = imsg_compose(ibuf, IMSG_DEVOP_MSG, 0, 0, -1, &msg,
		    sizeof(msg));
		if (ret == -1) {
			log_warn("%s: failed to send async io event to virtio"
			    " device", __func__);
			return (ret);
		}
		if (imsgbuf_flush(ibuf) == -1) {
			log_warnx("%s: imsgbuf_flush (write)", __func__);
			return (-1);
		}
	} else {
		/*
		 * Read request. Requires waiting for a reply.
		 */
		ret = imsg_compose(ibuf, IMSG_DEVOP_MSG, 0, 0, -1, &msg,
		    sizeof(msg));
		if (ret == -1) {
			log_warnx("%s: failed to send sync io event to virtio"
			    " device", __func__);
			return (ret);
		}
		if (imsgbuf_flush(ibuf) == -1) {
			log_warnx("%s: imsgbuf_flush (read)", __func__);
			return (-1);
		}

		/* Read our reply. */
		ret = imsgbuf_read_one(ibuf, &imsg);
		if (ret == 0 || ret == -1) {
			log_warn("%s: imsgbuf_read (n=%d)", __func__, ret);
			return (-1);
		}
		viodev_msg_read(&imsg, &msg);
		imsg_free(&imsg);

		if (msg.type == VIODEV_MSG_IO_READ && msg.data_valid) {
			DPRINTF("%s: got sync read response (reg=%s)", __func__,
			    virtio_reg_name(msg.reg));
			*data = msg.data;
			/*
			 * It's possible we're asked to {de,}assert after the
			 * device performs a register read.
			 */
			if (msg.state == INTR_STATE_ASSERT)
				vcpu_assert_irq(dev->vm_id, msg.vcpu, msg.irq);
			else if (msg.state == INTR_STATE_DEASSERT)
				vcpu_deassert_irq(dev->vm_id, msg.vcpu, msg.irq);
		} else {
			log_warnx("%s: expected IO_READ, got %d", __func__,
			    msg.type);
			return (-1);
		}
	}

	return (0);
}

void
virtio_assert_irq(struct virtio_dev *dev, int vcpu)
{
	struct viodev_msg msg;
	int ret;

	memset(&msg, 0, sizeof(msg));
	msg.irq = dev->irq;
	msg.vcpu = vcpu;
	msg.type = VIODEV_MSG_KICK;
	msg.state = INTR_STATE_ASSERT;

	ret = imsg_compose_event(&dev->async_iev, IMSG_DEVOP_MSG, 0, 0, -1,
	    &msg, sizeof(msg));
	if (ret == -1)
		log_warnx("%s: failed to assert irq %d", __func__, dev->irq);
}

void
virtio_deassert_irq(struct virtio_dev *dev, int vcpu)
{
	struct viodev_msg msg;
	int ret;

	memset(&msg, 0, sizeof(msg));
	msg.irq = dev->irq;
	msg.vcpu = vcpu;
	msg.type = VIODEV_MSG_KICK;
	msg.state = INTR_STATE_DEASSERT;

	ret = imsg_compose_event(&dev->async_iev, IMSG_DEVOP_MSG, 0, 0, -1,
	    &msg, sizeof(msg));
	if (ret == -1)
		log_warnx("%s: failed to deassert irq %d", __func__, dev->irq);
}

/*
 * Close all underlying file descriptors for a given virtio device.
 */
static int
virtio_dev_closefds(struct virtio_dev *dev)
{
	size_t i;

	switch (dev->dev_type) {
		case VMD_DEVTYPE_DISK:
			for (i = 0; i < dev->vioblk.ndisk_fd; i++) {
				close_fd(dev->vioblk.disk_fd[i]);
				dev->vioblk.disk_fd[i] = -1;
			}
			break;
		case VMD_DEVTYPE_NET:
			close_fd(dev->vionet.data_fd);
			dev->vionet.data_fd = -1;
			break;
	default:
		log_warnx("%s: invalid device type", __func__);
		return (-1);
	}

	close_fd(dev->async_fd);
	dev->async_fd = -1;
	close_fd(dev->sync_fd);
	dev->sync_fd = -1;

	return (0);
}

void
viodev_msg_read(struct imsg *imsg, struct viodev_msg *msg)
{
	if (imsg_get_data(imsg, msg, sizeof(*msg)))
		fatal("%s", __func__);
}

void
vionet_hostmac_read(struct imsg *imsg, struct vionet_dev *dev)
{
	if (imsg_get_data(imsg, dev->hostmac, sizeof(dev->hostmac)))
		fatal("%s", __func__);
}
