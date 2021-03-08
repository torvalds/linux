/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VIRTIO_PCI_MODERN_H
#define _LINUX_VIRTIO_PCI_MODERN_H

#include <linux/pci.h>
#include <linux/virtio_pci.h>

struct virtio_pci_modern_device {
	struct pci_dev *pci_dev;

	struct virtio_pci_common_cfg __iomem *common;
	/* Device-specific data (non-legacy mode)  */
	void __iomem *device;
	/* Base of vq notifications (non-legacy mode). */
	void __iomem *notify_base;
	/* Where to read and clear interrupt */
	u8 __iomem *isr;

	/* So we can sanity-check accesses. */
	size_t notify_len;
	size_t device_len;

	/* Capability for when we need to map notifications per-vq. */
	int notify_map_cap;

	/* Multiply queue_notify_off by this value. (non-legacy mode). */
	u32 notify_offset_multiplier;

	int modern_bars;

	struct virtio_device_id id;
};

/*
 * Type-safe wrappers for io accesses.
 * Use these to enforce at compile time the following spec requirement:
 *
 * The driver MUST access each field using the “natural” access
 * method, i.e. 32-bit accesses for 32-bit fields, 16-bit accesses
 * for 16-bit fields and 8-bit accesses for 8-bit fields.
 */
static inline u8 vp_ioread8(const u8 __iomem *addr)
{
	return ioread8(addr);
}
static inline u16 vp_ioread16 (const __le16 __iomem *addr)
{
	return ioread16(addr);
}

static inline u32 vp_ioread32(const __le32 __iomem *addr)
{
	return ioread32(addr);
}

static inline void vp_iowrite8(u8 value, u8 __iomem *addr)
{
	iowrite8(value, addr);
}

static inline void vp_iowrite16(u16 value, __le16 __iomem *addr)
{
	iowrite16(value, addr);
}

static inline void vp_iowrite32(u32 value, __le32 __iomem *addr)
{
	iowrite32(value, addr);
}

static inline void vp_iowrite64_twopart(u64 val,
					__le32 __iomem *lo,
					__le32 __iomem *hi)
{
	vp_iowrite32((u32)val, lo);
	vp_iowrite32(val >> 32, hi);
}

u64 vp_modern_get_features(struct virtio_pci_modern_device *mdev);
void vp_modern_set_features(struct virtio_pci_modern_device *mdev,
		     u64 features);
u32 vp_modern_generation(struct virtio_pci_modern_device *mdev);
u8 vp_modern_get_status(struct virtio_pci_modern_device *mdev);
void vp_modern_set_status(struct virtio_pci_modern_device *mdev,
		   u8 status);
u16 vp_modern_queue_vector(struct virtio_pci_modern_device *mdev,
			   u16 idx, u16 vector);
u16 vp_modern_config_vector(struct virtio_pci_modern_device *mdev,
		     u16 vector);
void vp_modern_queue_address(struct virtio_pci_modern_device *mdev,
			     u16 index, u64 desc_addr, u64 driver_addr,
			     u64 device_addr);
void vp_modern_set_queue_enable(struct virtio_pci_modern_device *mdev,
				u16 idx, bool enable);
bool vp_modern_get_queue_enable(struct virtio_pci_modern_device *mdev,
				u16 idx);
void vp_modern_set_queue_size(struct virtio_pci_modern_device *mdev,
			      u16 idx, u16 size);
u16 vp_modern_get_queue_size(struct virtio_pci_modern_device *mdev,
			     u16 idx);
u16 vp_modern_get_num_queues(struct virtio_pci_modern_device *mdev);
u16 vp_modern_get_queue_notify_off(struct virtio_pci_modern_device *mdev,
				   u16 idx);
void __iomem *vp_modern_map_capability(struct virtio_pci_modern_device *mdev, int off,
				       size_t minlen,
				       u32 align,
				       u32 start, u32 size,
				       size_t *len);
int vp_modern_probe(struct virtio_pci_modern_device *mdev);
void vp_modern_remove(struct virtio_pci_modern_device *mdev);
#endif
