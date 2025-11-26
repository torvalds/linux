/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_VFIO_PCI_DRIVER_H
#define SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_VFIO_PCI_DRIVER_H

#include <libvfio/iommu.h>

struct vfio_pci_device;

struct vfio_pci_driver_ops {
	const char *name;

	/**
	 * @probe() - Check if the driver supports the given device.
	 *
	 * Return: 0 on success, non-0 on failure.
	 */
	int (*probe)(struct vfio_pci_device *device);

	/**
	 * @init() - Initialize the driver for @device.
	 *
	 * Must be called after device->driver.region has been initialized.
	 */
	void (*init)(struct vfio_pci_device *device);

	/**
	 * remove() - Deinitialize the driver for @device.
	 */
	void (*remove)(struct vfio_pci_device *device);

	/**
	 * memcpy_start() - Kick off @count repeated memcpy operations from
	 * [@src, @src + @size) to [@dst, @dst + @size).
	 *
	 * Guarantees:
	 *  - The device will attempt DMA reads on [src, src + size).
	 *  - The device will attempt DMA writes on [dst, dst + size).
	 *  - The device will not generate any interrupts.
	 *
	 * memcpy_start() returns immediately, it does not wait for the
	 * copies to complete.
	 */
	void (*memcpy_start)(struct vfio_pci_device *device,
			     iova_t src, iova_t dst, u64 size, u64 count);

	/**
	 * memcpy_wait() - Wait until the memcpy operations started by
	 * memcpy_start() have finished.
	 *
	 * Guarantees:
	 *  - All in-flight DMAs initiated by memcpy_start() are fully complete
	 *    before memcpy_wait() returns.
	 *
	 * Returns non-0 if the driver detects that an error occurred during the
	 * memcpy, 0 otherwise.
	 */
	int (*memcpy_wait)(struct vfio_pci_device *device);

	/**
	 * send_msi() - Make the device send the MSI device->driver.msi.
	 *
	 * Guarantees:
	 *  - The device will send the MSI once.
	 */
	void (*send_msi)(struct vfio_pci_device *device);
};

struct vfio_pci_driver {
	const struct vfio_pci_driver_ops *ops;
	bool initialized;
	bool memcpy_in_progress;

	/* Region to be used by the driver (e.g. for in-memory descriptors) */
	struct dma_region region;

	/* The maximum size that can be passed to memcpy_start(). */
	u64 max_memcpy_size;

	/* The maximum count that can be passed to memcpy_start(). */
	u64 max_memcpy_count;

	/* The MSI vector the device will signal in ops->send_msi(). */
	int msi;
};

void vfio_pci_driver_probe(struct vfio_pci_device *device);
void vfio_pci_driver_init(struct vfio_pci_device *device);
void vfio_pci_driver_remove(struct vfio_pci_device *device);
int vfio_pci_driver_memcpy(struct vfio_pci_device *device,
			   iova_t src, iova_t dst, u64 size);
void vfio_pci_driver_memcpy_start(struct vfio_pci_device *device,
				  iova_t src, iova_t dst, u64 size,
				  u64 count);
int vfio_pci_driver_memcpy_wait(struct vfio_pci_device *device);
void vfio_pci_driver_send_msi(struct vfio_pci_device *device);

#endif /* SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_VFIO_PCI_DRIVER_H */
