/*
 * Copyright (c) 2019 Stefan Fritsch <sf@openbsd.org>
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

#ifndef _DEV_PCI_VIRTIO_PCIREG_H_
#define _DEV_PCI_VIRTIO_PCIREG_H_

/* Virtio 0.9 config space */
#define VIRTIO_CONFIG_DEVICE_FEATURES		0 /* 32bit */
#define VIRTIO_CONFIG_GUEST_FEATURES		4 /* 32bit */
#define VIRTIO_CONFIG_QUEUE_ADDRESS		8 /* 32bit */
#define VIRTIO_CONFIG_QUEUE_SIZE		12 /* 16bit */
#define VIRTIO_CONFIG_QUEUE_SELECT		14 /* 16bit */
#define VIRTIO_CONFIG_QUEUE_NOTIFY		16 /* 16bit */
#define VIRTIO_CONFIG_DEVICE_STATUS		18 /* 8bit */
#define VIRTIO_CONFIG_ISR_STATUS		19 /* 8bit */
#define  VIRTIO_CONFIG_ISR_CONFIG_CHANGE	2
#define VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI	20
/* Only if MSIX is enabled: */
#define VIRTIO_MSI_CONFIG_VECTOR		20 /* 16bit, optional */
#define VIRTIO_MSI_QUEUE_VECTOR			22 /* 16bit, optional */
#define VIRTIO_CONFIG_DEVICE_CONFIG_MSI		24

#define VIRTIO_MSI_NO_VECTOR			0xffff

/*
 * Virtio 1.0 specific
 */

struct virtio_pci_cap {
	uint8_t cap_vndr;	/* Generic PCI field: PCI_CAP_ID_VNDR */
	uint8_t cap_next;	/* Generic PCI field: next ptr. */
	uint8_t cap_len;	/* Generic PCI field: capability length */
	uint8_t cfg_type;	/* Identifies the structure. */
	uint8_t bar;		/* Where to find it. */
	uint8_t padding[3];	/* Pad to full dword. */
	uint32_t offset;	/* Offset within bar. */
	uint32_t length;	/* Length of the structure, in bytes. */
} __packed;

/* Common configuration */
#define VIRTIO_PCI_CAP_COMMON_CFG	1
/* Notifications */
#define VIRTIO_PCI_CAP_NOTIFY_CFG	2
/* ISR Status */
#define VIRTIO_PCI_CAP_ISR_CFG		3
/* Device specific configuration */
#define VIRTIO_PCI_CAP_DEVICE_CFG	4
/* PCI configuration access */
#define VIRTIO_PCI_CAP_PCI_CFG		5

struct virtio_pci_notify_cap {
	struct virtio_pci_cap cap;
	uint32_t notify_off_multiplier;	/* Multiplier for queue_notify_off. */
} __packed;

struct virtio_pci_cfg_cap {
	struct virtio_pci_cap cap;
	uint8_t pci_cfg_data[4];	/* Data for BAR access. */
} __packed;

struct virtio_pci_common_cfg {
	/* About the whole device. */
	uint32_t device_feature_select;	/* read-write */
	uint32_t device_feature;	/* read-only for driver */
	uint32_t driver_feature_select;	/* read-write */
	uint32_t driver_feature;	/* read-write */
	uint16_t config_msix_vector;	/* read-write */
	uint16_t num_queues;		/* read-only for driver */
	uint8_t device_status;		/* read-write */
	uint8_t config_generation;	/* read-only for driver */

	/* About a specific virtqueue. */
	uint16_t queue_select;		/* read-write */
	uint16_t queue_size;		/* read-write, power of 2, or 0. */
	uint16_t queue_msix_vector;	/* read-write */
	uint16_t queue_enable;		/* read-write */
	uint16_t queue_notify_off;	/* read-only for driver */
	uint64_t queue_desc;		/* read-write */
	uint64_t queue_avail;		/* read-write */
	uint64_t queue_used;		/* read-write */
} __packed;

#endif /* _DEV_PCI_VIRTIO_PCIREG_H_ */
