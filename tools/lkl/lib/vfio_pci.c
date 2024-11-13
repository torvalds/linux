// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <lkl_host.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/vfio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/vfio.h>
#include <sys/eventfd.h>
#include "iomem.h"

struct lkl_pci_dev {
	struct lkl_sem *thread_init_sem;
	int irq;
	lkl_thread_t int_thread;
	int quit;
	int fd;
	int irq_fd;
	struct vfio_device_info device_info;
	struct vfio_region_info config_reg;
	struct vfio_iommu_type1_dma_map dma_map;
};

/**
 * vfio_pci_add - Create a new pci device
 *
 * The device should be assigned to VFIO by the host in advance.
 *
 * @name - PCI device name (as %x:%x:%x.%x format)
 * @kernel_ram - the start address of kernel memory needed to be mapped for DMA.
 * The address must be aligned to the page size.
 * @ram_size - the size of kernel memory, should be page-aligned as well.
 */

static struct lkl_pci_dev *vfio_pci_add(const char *name, void *kernel_ram,
					unsigned long ram_size)
{
	struct lkl_pci_dev *dev;
	char path[128], link[128];
	int segn, busn, devn, funcn;
	int i;
	int container_fd = 0, group_fd = 0;
	struct vfio_group_status group_status = { .argsz = sizeof(
							  group_status) };
	struct vfio_iommu_type1_info iommu_info = { .argsz = sizeof(
							    iommu_info) };

	dev = malloc(sizeof(*dev));
	if (!dev)
		return NULL;

	memset(dev, 0, sizeof(*dev));

	dev->device_info.argsz = sizeof(struct vfio_device_info);
	dev->config_reg.argsz = sizeof(struct vfio_region_info);
	dev->dma_map.argsz = sizeof(struct vfio_iommu_type1_dma_map);

	container_fd = open("/dev/vfio/vfio", O_RDWR);
	if (container_fd < 0)
		goto error;

	if (ioctl(container_fd, VFIO_GET_API_VERSION) != VFIO_API_VERSION ||
	    ioctl(container_fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU) == 0)
		goto error;

	if (sscanf(name, "vfio%x:%x:%x.%x", &segn, &busn, &devn, &funcn) != 4)
		goto error;

	snprintf(path, sizeof(path),
		 "/sys/bus/pci/devices/%04x:%02x:%02x.%01x/iommu_group", segn,
		 busn, devn, funcn);

	i = readlink(path, link, sizeof(link));
	if (i < 0)
		goto error;

	link[i] = '\0';
	snprintf(path, sizeof(path), "/dev/vfio%s", strrchr(link, '/'));

	group_fd = open(path, O_RDWR);
	if (group_fd < 0)
		goto error;

	if (ioctl(group_fd, VFIO_GROUP_GET_STATUS, &group_status) < 0)
		goto error;

	if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE))
		goto error;

	if (ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd) < 0)
		goto error;

	if (ioctl(container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU) < 0)
		goto error;

	if (ioctl(container_fd, VFIO_IOMMU_GET_INFO, &iommu_info) < 0)
		goto error;

	/* if kernel_ram is null, assume the memory is already initialized
	 * by another device, and skip this step.
	 */
	if (kernel_ram) {
		dev->dma_map.vaddr = (uint64_t)kernel_ram;
		dev->dma_map.size = ram_size;
		dev->dma_map.iova = 0;
		dev->dma_map.flags =
			VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;
		if (ioctl(container_fd, VFIO_IOMMU_MAP_DMA, &dev->dma_map) < 0)
			goto error;
	}

	snprintf(path, sizeof(path), "%04x:%02x:%02x.%01x", segn, busn, devn,
		 funcn);
	dev->fd = ioctl(group_fd, VFIO_GROUP_GET_DEVICE_FD, path);

	if (dev->fd < 0)
		goto error;

	if (ioctl(dev->fd, VFIO_DEVICE_GET_INFO, &dev->device_info) < 0)
		goto error;

	if (dev->device_info.num_regions <= VFIO_PCI_CONFIG_REGION_INDEX)
		goto error;

	dev->config_reg.index = VFIO_PCI_CONFIG_REGION_INDEX;

	if (ioctl(dev->fd, VFIO_DEVICE_GET_REGION_INFO, &dev->config_reg) < 0)
		goto error;

	return dev;

error:
	lkl_printf("lkl_vfio_pci: failed to create a PCI device for %s\n",
		   name);
	if (container_fd > 0)
		close(container_fd);
	if (group_fd > 0)
		close(group_fd);
	free(dev);
	return NULL;
}

static void vfio_pci_remove(struct lkl_pci_dev *dev)
{
	dev->quit = 1;
	lkl_host_ops.thread_join(dev->int_thread);
	close(dev->fd);
	free(dev);
}

static int check_irq_status(struct lkl_pci_dev *dev)
{
	unsigned short status;

	if (pread(dev->fd, &status, 2, dev->config_reg.offset + 6) != 2)
		return 0;
	return (status & (1 << 3)) ? 1 : 0;
}

/* Currently, we only support INTx. */
static void vfio_int_thread(void *_dev)
{
	eventfd_t icount;
	struct lkl_pci_dev *dev = (struct lkl_pci_dev *)_dev;
	struct timespec req = { 0, 1000 * 1000 };
	struct vfio_irq_info irq = { .argsz = sizeof(irq) };
	struct vfio_irq_set *irq_set;
	char irq_set_buf[sizeof(struct vfio_irq_set) + sizeof(int)];
	fd_set rfds;

	if (dev->device_info.num_irqs <= VFIO_PCI_INTX_IRQ_INDEX)
		goto init_error;

	irq.index = VFIO_PCI_INTX_IRQ_INDEX;

	if (ioctl(dev->fd, VFIO_DEVICE_GET_IRQ_INFO, &irq))
		goto init_error;

	if (irq.count != 1)
		goto init_error;

	irq_set = (struct vfio_irq_set *)irq_set_buf;
	irq_set->argsz = sizeof(irq_set_buf);
	irq_set->count = 1;
	irq_set->flags =
		VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
	irq_set->index = VFIO_PCI_INTX_IRQ_INDEX;
	irq_set->start = 0;
	dev->irq_fd = eventfd(0, EFD_CLOEXEC);
	if (dev->irq_fd < 0)
		goto init_error;
	*(int *)&irq_set->data = dev->irq_fd;

	if (ioctl(dev->fd, VFIO_DEVICE_SET_IRQS, irq_set))
		goto init_error;

	lkl_host_ops.sem_up(dev->thread_init_sem);

	while (1) {
		/* We should wait until the driver actually handles
		 * an interrupt by monitoring the PCI interrupt status bit.
		 */
		while (check_irq_status(dev) && !dev->quit) {
			lkl_trigger_irq(dev->irq);
			nanosleep(&req, NULL);
		}

		if (dev->quit)
			return;

		/* unmask interrupts */
		irq_set->argsz = sizeof(*irq_set);
		irq_set->count = 1;
		irq_set->flags =
			VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_UNMASK;
		irq_set->index = VFIO_PCI_INTX_IRQ_INDEX;
		irq_set->start = 0;
		if (ioctl(dev->fd, VFIO_DEVICE_SET_IRQS, irq_set))
			goto handling_error;

		/* Wait for next interrupt. */
		while (1) {
			struct timeval tv;
			int rc;

			FD_ZERO(&rfds);
			FD_SET(dev->irq_fd, &rfds);
			tv.tv_sec = 0;
			tv.tv_usec = 100 * 1000;
			rc = select(dev->irq_fd + 1, &rfds, NULL, NULL, &tv);
			if (rc == -1)
				goto handling_error;
			else if (rc)
				if (read(dev->irq_fd, &icount, sizeof(icount)) <
				    0)
					goto handling_error;
				else
					break;
			else if (dev->quit)
				return;
		}
	}

init_error:
	lkl_printf("lkl_vfio_pci: failed to setup INTx for a device\n");
	return;
handling_error:
	lkl_printf("lkl_vfio_pci: unknown error in the interrupt handler\n");
}

static int vfio_pci_irq_init(struct lkl_pci_dev *dev, int irq)
{
	dev->thread_init_sem = lkl_host_ops.sem_alloc(0);
	if (!dev->thread_init_sem)
		return -1;

	dev->irq = irq;

	dev->int_thread =
		lkl_host_ops.thread_create(vfio_int_thread, (void *)dev);
	if (!dev->int_thread) {
		lkl_host_ops.sem_free(dev->thread_init_sem);
		return -1;
	}

	/* wait until the interrupt handler thread is ready */
	lkl_host_ops.sem_down(dev->thread_init_sem);
	lkl_host_ops.sem_free(dev->thread_init_sem);
	return 0;
}

static unsigned long long vfio_map_page(struct lkl_pci_dev *dev, void *vaddr,
					unsigned long size)
{
	return (unsigned long long)vaddr - dev->dma_map.vaddr;
}

static void vfio_unmap_page(struct lkl_pci_dev *dev,
			    unsigned long long dma_handle, unsigned long size)
{
}

static int vfio_pci_read(struct lkl_pci_dev *dev, int where, int size,
			 void *val)
{
	return pread(dev->fd, val, size, dev->config_reg.offset + where);
}

static int vfio_pci_write(struct lkl_pci_dev *dev, int where, int size,
			  void *val)
{
	return pwrite(dev->fd, val, size, dev->config_reg.offset + where);
}

static int pci_resource_read(void *data, int offset, void *res, int size)
{
	void *addr = data + offset;

	switch (size) {
	case 8:
		*(uint64_t *)res = *(uint64_t *)addr;
		break;
	case 4:
		*(uint32_t *)res = *(uint32_t *)addr;
		break;
	case 2:
		*(uint16_t *)res = *(uint16_t *)addr;
		break;
	case 1:
		*(uint8_t *)res = *(uint8_t *)addr;
		break;
	default:
		return -LKL_EOPNOTSUPP;
	}
	return 0;
}

static int pci_resource_write(void *data, int offset, void *res, int size)
{
	void *addr = data + offset;

	switch (size) {
	case 8:
		*(uint64_t *)addr = *(uint64_t *)res;
		break;
	case 4:
		*(uint32_t *)addr = *(uint32_t *)res;
		break;
	case 2:
		*(uint16_t *)addr = *(uint16_t *)res;
		break;
	case 1:
		*(uint8_t *)addr = *(uint8_t *)res;
		break;
	default:
		return -LKL_EOPNOTSUPP;
	}
	return 0;
}

static const struct lkl_iomem_ops pci_resource_ops = {
	.read = pci_resource_read,
	.write = pci_resource_write,
};

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static void *vfio_resource_alloc(struct lkl_pci_dev *dev,
				 unsigned long resource_size,
				 int resource_index)
{
	unsigned int region_index_list[] = {
		VFIO_PCI_BAR0_REGION_INDEX, VFIO_PCI_BAR1_REGION_INDEX,
		VFIO_PCI_BAR2_REGION_INDEX, VFIO_PCI_BAR3_REGION_INDEX,
		VFIO_PCI_BAR4_REGION_INDEX, VFIO_PCI_BAR5_REGION_INDEX,
	};
	struct vfio_region_info reg = { .argsz = sizeof(reg) };
	void *mmio_addr;

	if ((unsigned int)resource_index >= ARRAY_SIZE(region_index_list))
		return NULL;

	reg.index = region_index_list[resource_index];

	if (dev->device_info.num_regions <= reg.index)
		return NULL;

	/* We assume the resource is a memory space. */

	if (ioctl(dev->fd, VFIO_DEVICE_GET_REGION_INFO, &reg) < 0)
		return NULL;

	if (reg.size < resource_size)
		return NULL;

	mmio_addr = mmap(NULL, resource_size, PROT_READ | PROT_WRITE,
			 MAP_SHARED, dev->fd, reg.offset);

	if (mmio_addr == MAP_FAILED)
		return NULL;

	return register_iomem(mmio_addr, resource_size, &pci_resource_ops);
}

struct lkl_dev_pci_ops vfio_pci_ops = {
	.add = vfio_pci_add,
	.remove = vfio_pci_remove,
	.irq_init = vfio_pci_irq_init,
	.read = vfio_pci_read,
	.write = vfio_pci_write,
	.resource_alloc = vfio_resource_alloc,
	.map_page = vfio_map_page,
	.unmap_page = vfio_unmap_page,
};
