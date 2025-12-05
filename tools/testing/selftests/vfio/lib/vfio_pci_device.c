// SPDX-License-Identifier: GPL-2.0-only
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <uapi/linux/types.h>
#include <linux/iommufd.h>
#include <linux/limits.h>
#include <linux/mman.h>
#include <linux/overflow.h>
#include <linux/types.h>
#include <linux/vfio.h>

#include "../../../kselftest.h"
#include <libvfio.h>

#define PCI_SYSFS_PATH	"/sys/bus/pci/devices"

static void vfio_pci_irq_set(struct vfio_pci_device *device,
			     u32 index, u32 vector, u32 count, int *fds)
{
	u8 buf[sizeof(struct vfio_irq_set) + sizeof(int) * count] = {};
	struct vfio_irq_set *irq = (void *)&buf;
	int *irq_fds = (void *)&irq->data;

	irq->argsz = sizeof(buf);
	irq->flags = VFIO_IRQ_SET_ACTION_TRIGGER;
	irq->index = index;
	irq->start = vector;
	irq->count = count;

	if (count) {
		irq->flags |= VFIO_IRQ_SET_DATA_EVENTFD;
		memcpy(irq_fds, fds, sizeof(int) * count);
	} else {
		irq->flags |= VFIO_IRQ_SET_DATA_NONE;
	}

	ioctl_assert(device->fd, VFIO_DEVICE_SET_IRQS, irq);
}

void vfio_pci_irq_trigger(struct vfio_pci_device *device, u32 index, u32 vector)
{
	struct vfio_irq_set irq = {
		.argsz = sizeof(irq),
		.flags = VFIO_IRQ_SET_ACTION_TRIGGER | VFIO_IRQ_SET_DATA_NONE,
		.index = index,
		.start = vector,
		.count = 1,
	};

	ioctl_assert(device->fd, VFIO_DEVICE_SET_IRQS, &irq);
}

static void check_supported_irq_index(u32 index)
{
	/* VFIO selftests only supports MSI and MSI-x for now. */
	VFIO_ASSERT_TRUE(index == VFIO_PCI_MSI_IRQ_INDEX ||
			 index == VFIO_PCI_MSIX_IRQ_INDEX,
			 "Unsupported IRQ index: %u\n", index);
}

void vfio_pci_irq_enable(struct vfio_pci_device *device, u32 index, u32 vector,
			 int count)
{
	int i;

	check_supported_irq_index(index);

	for (i = vector; i < vector + count; i++) {
		VFIO_ASSERT_LT(device->msi_eventfds[i], 0);
		device->msi_eventfds[i] = eventfd(0, 0);
		VFIO_ASSERT_GE(device->msi_eventfds[i], 0);
	}

	vfio_pci_irq_set(device, index, vector, count, device->msi_eventfds + vector);
}

void vfio_pci_irq_disable(struct vfio_pci_device *device, u32 index)
{
	int i;

	check_supported_irq_index(index);

	for (i = 0; i < ARRAY_SIZE(device->msi_eventfds); i++) {
		if (device->msi_eventfds[i] < 0)
			continue;

		VFIO_ASSERT_EQ(close(device->msi_eventfds[i]), 0);
		device->msi_eventfds[i] = -1;
	}

	vfio_pci_irq_set(device, index, 0, 0, NULL);
}

static void vfio_pci_irq_get(struct vfio_pci_device *device, u32 index,
			     struct vfio_irq_info *irq_info)
{
	irq_info->argsz = sizeof(*irq_info);
	irq_info->index = index;

	ioctl_assert(device->fd, VFIO_DEVICE_GET_IRQ_INFO, irq_info);
}

static void vfio_pci_region_get(struct vfio_pci_device *device, int index,
				struct vfio_region_info *info)
{
	memset(info, 0, sizeof(*info));

	info->argsz = sizeof(*info);
	info->index = index;

	ioctl_assert(device->fd, VFIO_DEVICE_GET_REGION_INFO, info);
}

static void vfio_pci_bar_map(struct vfio_pci_device *device, int index)
{
	struct vfio_pci_bar *bar = &device->bars[index];
	int prot = 0;

	VFIO_ASSERT_LT(index, PCI_STD_NUM_BARS);
	VFIO_ASSERT_NULL(bar->vaddr);
	VFIO_ASSERT_TRUE(bar->info.flags & VFIO_REGION_INFO_FLAG_MMAP);

	if (bar->info.flags & VFIO_REGION_INFO_FLAG_READ)
		prot |= PROT_READ;
	if (bar->info.flags & VFIO_REGION_INFO_FLAG_WRITE)
		prot |= PROT_WRITE;

	bar->vaddr = mmap(NULL, bar->info.size, prot, MAP_FILE | MAP_SHARED,
			  device->fd, bar->info.offset);
	VFIO_ASSERT_NE(bar->vaddr, MAP_FAILED);
}

static void vfio_pci_bar_unmap(struct vfio_pci_device *device, int index)
{
	struct vfio_pci_bar *bar = &device->bars[index];

	VFIO_ASSERT_LT(index, PCI_STD_NUM_BARS);
	VFIO_ASSERT_NOT_NULL(bar->vaddr);

	VFIO_ASSERT_EQ(munmap(bar->vaddr, bar->info.size), 0);
	bar->vaddr = NULL;
}

static void vfio_pci_bar_unmap_all(struct vfio_pci_device *device)
{
	int i;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (device->bars[i].vaddr)
			vfio_pci_bar_unmap(device, i);
	}
}

void vfio_pci_config_access(struct vfio_pci_device *device, bool write,
			    size_t config, size_t size, void *data)
{
	struct vfio_region_info *config_space = &device->config_space;
	int ret;

	if (write)
		ret = pwrite(device->fd, data, size, config_space->offset + config);
	else
		ret = pread(device->fd, data, size, config_space->offset + config);

	VFIO_ASSERT_EQ(ret, size, "Failed to %s PCI config space: 0x%lx\n",
		       write ? "write to" : "read from", config);
}

void vfio_pci_device_reset(struct vfio_pci_device *device)
{
	ioctl_assert(device->fd, VFIO_DEVICE_RESET, NULL);
}

static unsigned int vfio_pci_get_group_from_dev(const char *bdf)
{
	char dev_iommu_group_path[PATH_MAX] = {0};
	char sysfs_path[PATH_MAX] = {0};
	unsigned int group;
	int ret;

	snprintf(sysfs_path, PATH_MAX, "%s/%s/iommu_group", PCI_SYSFS_PATH, bdf);

	ret = readlink(sysfs_path, dev_iommu_group_path, sizeof(dev_iommu_group_path));
	VFIO_ASSERT_NE(ret, -1, "Failed to get the IOMMU group for device: %s\n", bdf);

	ret = sscanf(basename(dev_iommu_group_path), "%u", &group);
	VFIO_ASSERT_EQ(ret, 1, "Failed to get the IOMMU group for device: %s\n", bdf);

	return group;
}

static void vfio_pci_group_setup(struct vfio_pci_device *device, const char *bdf)
{
	struct vfio_group_status group_status = {
		.argsz = sizeof(group_status),
	};
	char group_path[32];
	int group;

	group = vfio_pci_get_group_from_dev(bdf);
	snprintf(group_path, sizeof(group_path), "/dev/vfio/%d", group);

	device->group_fd = open(group_path, O_RDWR);
	VFIO_ASSERT_GE(device->group_fd, 0, "open(%s) failed\n", group_path);

	ioctl_assert(device->group_fd, VFIO_GROUP_GET_STATUS, &group_status);
	VFIO_ASSERT_TRUE(group_status.flags & VFIO_GROUP_FLAGS_VIABLE);

	ioctl_assert(device->group_fd, VFIO_GROUP_SET_CONTAINER, &device->iommu->container_fd);
}

static void vfio_pci_container_setup(struct vfio_pci_device *device, const char *bdf)
{
	struct iommu *iommu = device->iommu;
	unsigned long iommu_type = iommu->mode->iommu_type;
	int ret;

	vfio_pci_group_setup(device, bdf);

	ret = ioctl(iommu->container_fd, VFIO_CHECK_EXTENSION, iommu_type);
	VFIO_ASSERT_GT(ret, 0, "VFIO IOMMU type %lu not supported\n", iommu_type);

	/*
	 * Allow multiple threads to race to set the IOMMU type on the
	 * container. The first will succeed and the rest should fail
	 * because the IOMMU type is already set.
	 */
	(void)ioctl(iommu->container_fd, VFIO_SET_IOMMU, (void *)iommu_type);

	device->fd = ioctl(device->group_fd, VFIO_GROUP_GET_DEVICE_FD, bdf);
	VFIO_ASSERT_GE(device->fd, 0);
}

static void vfio_pci_device_setup(struct vfio_pci_device *device)
{
	int i;

	device->info.argsz = sizeof(device->info);
	ioctl_assert(device->fd, VFIO_DEVICE_GET_INFO, &device->info);

	vfio_pci_region_get(device, VFIO_PCI_CONFIG_REGION_INDEX, &device->config_space);

	/* Sanity check VFIO does not advertise mmap for config space */
	VFIO_ASSERT_TRUE(!(device->config_space.flags & VFIO_REGION_INFO_FLAG_MMAP),
			 "PCI config space should not support mmap()\n");

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		struct vfio_pci_bar *bar = device->bars + i;

		vfio_pci_region_get(device, i, &bar->info);
		if (bar->info.flags & VFIO_REGION_INFO_FLAG_MMAP)
			vfio_pci_bar_map(device, i);
	}

	vfio_pci_irq_get(device, VFIO_PCI_MSI_IRQ_INDEX, &device->msi_info);
	vfio_pci_irq_get(device, VFIO_PCI_MSIX_IRQ_INDEX, &device->msix_info);

	for (i = 0; i < ARRAY_SIZE(device->msi_eventfds); i++)
		device->msi_eventfds[i] = -1;
}

const char *vfio_pci_get_cdev_path(const char *bdf)
{
	char dir_path[PATH_MAX];
	struct dirent *entry;
	char *cdev_path;
	DIR *dir;

	cdev_path = calloc(PATH_MAX, 1);
	VFIO_ASSERT_NOT_NULL(cdev_path);

	snprintf(dir_path, sizeof(dir_path), "/sys/bus/pci/devices/%s/vfio-dev/", bdf);

	dir = opendir(dir_path);
	VFIO_ASSERT_NOT_NULL(dir, "Failed to open directory %s\n", dir_path);

	while ((entry = readdir(dir)) != NULL) {
		/* Find the file that starts with "vfio" */
		if (strncmp("vfio", entry->d_name, 4))
			continue;

		snprintf(cdev_path, PATH_MAX, "/dev/vfio/devices/%s", entry->d_name);
		break;
	}

	VFIO_ASSERT_NE(cdev_path[0], 0, "Failed to find vfio cdev file.\n");
	VFIO_ASSERT_EQ(closedir(dir), 0);

	return cdev_path;
}

static void vfio_device_bind_iommufd(int device_fd, int iommufd)
{
	struct vfio_device_bind_iommufd args = {
		.argsz = sizeof(args),
		.iommufd = iommufd,
	};

	ioctl_assert(device_fd, VFIO_DEVICE_BIND_IOMMUFD, &args);
}

static void vfio_device_attach_iommufd_pt(int device_fd, u32 pt_id)
{
	struct vfio_device_attach_iommufd_pt args = {
		.argsz = sizeof(args),
		.pt_id = pt_id,
	};

	ioctl_assert(device_fd, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &args);
}

static void vfio_pci_iommufd_setup(struct vfio_pci_device *device, const char *bdf)
{
	const char *cdev_path = vfio_pci_get_cdev_path(bdf);

	device->fd = open(cdev_path, O_RDWR);
	VFIO_ASSERT_GE(device->fd, 0);
	free((void *)cdev_path);

	vfio_device_bind_iommufd(device->fd, device->iommu->iommufd);
	vfio_device_attach_iommufd_pt(device->fd, device->iommu->ioas_id);
}

struct vfio_pci_device *vfio_pci_device_init(const char *bdf, struct iommu *iommu)
{
	struct vfio_pci_device *device;

	device = calloc(1, sizeof(*device));
	VFIO_ASSERT_NOT_NULL(device);

	VFIO_ASSERT_NOT_NULL(iommu);
	device->iommu = iommu;
	device->bdf = bdf;

	if (iommu->mode->container_path)
		vfio_pci_container_setup(device, bdf);
	else
		vfio_pci_iommufd_setup(device, bdf);

	vfio_pci_device_setup(device);
	vfio_pci_driver_probe(device);

	return device;
}

void vfio_pci_device_cleanup(struct vfio_pci_device *device)
{
	int i;

	if (device->driver.initialized)
		vfio_pci_driver_remove(device);

	vfio_pci_bar_unmap_all(device);

	VFIO_ASSERT_EQ(close(device->fd), 0);

	for (i = 0; i < ARRAY_SIZE(device->msi_eventfds); i++) {
		if (device->msi_eventfds[i] < 0)
			continue;

		VFIO_ASSERT_EQ(close(device->msi_eventfds[i]), 0);
	}

	if (device->group_fd)
		VFIO_ASSERT_EQ(close(device->group_fd), 0);

	free(device);
}
