/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_VFIO_PCI_DEVICE_H
#define SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_VFIO_PCI_DEVICE_H

#include <fcntl.h>
#include <linux/vfio.h>
#include <linux/pci_regs.h>

#include <libvfio/assert.h>
#include <libvfio/iommu.h>
#include <libvfio/vfio_pci_driver.h>

struct vfio_pci_bar {
	struct vfio_region_info info;
	void *vaddr;
};

struct vfio_pci_device {
	const char *bdf;
	int fd;
	int group_fd;

	struct iommu *iommu;

	struct vfio_device_info info;
	struct vfio_region_info config_space;
	struct vfio_pci_bar bars[PCI_STD_NUM_BARS];

	struct vfio_irq_info msi_info;
	struct vfio_irq_info msix_info;

	/* eventfds for MSI and MSI-x interrupts */
	int msi_eventfds[PCI_MSIX_FLAGS_QSIZE + 1];

	struct vfio_pci_driver driver;
};

#define dev_info(_dev, _fmt, ...) printf("%s: " _fmt, (_dev)->bdf, ##__VA_ARGS__)
#define dev_err(_dev, _fmt, ...) fprintf(stderr, "%s: " _fmt, (_dev)->bdf, ##__VA_ARGS__)

struct vfio_pci_device *vfio_pci_device_init(const char *bdf, struct iommu *iommu);
void vfio_pci_device_cleanup(struct vfio_pci_device *device);

void vfio_pci_device_reset(struct vfio_pci_device *device);

void vfio_pci_config_access(struct vfio_pci_device *device, bool write,
			    size_t config, size_t size, void *data);

#define vfio_pci_config_read(_device, _offset, _type) ({			    \
	_type __data;								    \
	vfio_pci_config_access((_device), false, _offset, sizeof(__data), &__data); \
	__data;									    \
})

#define vfio_pci_config_readb(_d, _o) vfio_pci_config_read(_d, _o, u8)
#define vfio_pci_config_readw(_d, _o) vfio_pci_config_read(_d, _o, u16)
#define vfio_pci_config_readl(_d, _o) vfio_pci_config_read(_d, _o, u32)

#define vfio_pci_config_write(_device, _offset, _value, _type) do {		  \
	_type __data = (_value);						  \
	vfio_pci_config_access((_device), true, _offset, sizeof(_type), &__data); \
} while (0)

#define vfio_pci_config_writeb(_d, _o, _v) vfio_pci_config_write(_d, _o, _v, u8)
#define vfio_pci_config_writew(_d, _o, _v) vfio_pci_config_write(_d, _o, _v, u16)
#define vfio_pci_config_writel(_d, _o, _v) vfio_pci_config_write(_d, _o, _v, u32)

void vfio_pci_irq_enable(struct vfio_pci_device *device, u32 index,
			 u32 vector, int count);
void vfio_pci_irq_disable(struct vfio_pci_device *device, u32 index);
void vfio_pci_irq_trigger(struct vfio_pci_device *device, u32 index, u32 vector);

static inline void fcntl_set_nonblock(int fd)
{
	int r;

	r = fcntl(fd, F_GETFL, 0);
	VFIO_ASSERT_NE(r, -1, "F_GETFL failed for fd %d\n", fd);

	r = fcntl(fd, F_SETFL, r | O_NONBLOCK);
	VFIO_ASSERT_NE(r, -1, "F_SETFL O_NONBLOCK failed for fd %d\n", fd);
}

static inline void vfio_pci_msi_enable(struct vfio_pci_device *device,
				       u32 vector, int count)
{
	vfio_pci_irq_enable(device, VFIO_PCI_MSI_IRQ_INDEX, vector, count);
}

static inline void vfio_pci_msi_disable(struct vfio_pci_device *device)
{
	vfio_pci_irq_disable(device, VFIO_PCI_MSI_IRQ_INDEX);
}

static inline void vfio_pci_msix_enable(struct vfio_pci_device *device,
					u32 vector, int count)
{
	vfio_pci_irq_enable(device, VFIO_PCI_MSIX_IRQ_INDEX, vector, count);
}

static inline void vfio_pci_msix_disable(struct vfio_pci_device *device)
{
	vfio_pci_irq_disable(device, VFIO_PCI_MSIX_IRQ_INDEX);
}

static inline int __to_iova(struct vfio_pci_device *device, void *vaddr, iova_t *iova)
{
	return __iommu_hva2iova(device->iommu, vaddr, iova);
}

static inline iova_t to_iova(struct vfio_pci_device *device, void *vaddr)
{
	return iommu_hva2iova(device->iommu, vaddr);
}

static inline bool vfio_pci_device_match(struct vfio_pci_device *device,
					 u16 vendor_id, u16 device_id)
{
	return (vendor_id == vfio_pci_config_readw(device, PCI_VENDOR_ID)) &&
		(device_id == vfio_pci_config_readw(device, PCI_DEVICE_ID));
}

const char *vfio_pci_get_cdev_path(const char *bdf);

#endif /* SELFTESTS_VFIO_LIB_INCLUDE_LIBVFIO_VFIO_PCI_DEVICE_H */
