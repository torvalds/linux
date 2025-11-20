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
#include <vfio_util.h>

#define PCI_SYSFS_PATH	"/sys/bus/pci/devices"

#define ioctl_assert(_fd, _op, _arg) do {						       \
	void *__arg = (_arg);								       \
	int __ret = ioctl((_fd), (_op), (__arg));					       \
	VFIO_ASSERT_EQ(__ret, 0, "ioctl(%s, %s, %s) returned %d\n", #_fd, #_op, #_arg, __ret); \
} while (0)

static struct vfio_info_cap_header *next_cap_hdr(void *buf, u32 bufsz,
						 u32 *cap_offset)
{
	struct vfio_info_cap_header *hdr;

	if (!*cap_offset)
		return NULL;

	VFIO_ASSERT_LT(*cap_offset, bufsz);
	VFIO_ASSERT_GE(bufsz - *cap_offset, sizeof(*hdr));

	hdr = (struct vfio_info_cap_header *)((u8 *)buf + *cap_offset);
	*cap_offset = hdr->next;

	return hdr;
}

static struct vfio_info_cap_header *vfio_iommu_info_cap_hdr(struct vfio_iommu_type1_info *info,
							    u16 cap_id)
{
	struct vfio_info_cap_header *hdr;
	u32 cap_offset = info->cap_offset;
	u32 max_depth;
	u32 depth = 0;

	if (!(info->flags & VFIO_IOMMU_INFO_CAPS))
		return NULL;

	if (cap_offset)
		VFIO_ASSERT_GE(cap_offset, sizeof(*info));

	max_depth = (info->argsz - sizeof(*info)) / sizeof(*hdr);

	while ((hdr = next_cap_hdr(info, info->argsz, &cap_offset))) {
		depth++;
		VFIO_ASSERT_LE(depth, max_depth, "Capability chain contains a cycle\n");

		if (hdr->id == cap_id)
			return hdr;
	}

	return NULL;
}

/* Return buffer including capability chain, if present. Free with free() */
static struct vfio_iommu_type1_info *vfio_iommu_get_info(struct vfio_pci_device *device)
{
	struct vfio_iommu_type1_info *info;

	info = malloc(sizeof(*info));
	VFIO_ASSERT_NOT_NULL(info);

	*info = (struct vfio_iommu_type1_info) {
		.argsz = sizeof(*info),
	};

	ioctl_assert(device->container_fd, VFIO_IOMMU_GET_INFO, info);
	VFIO_ASSERT_GE(info->argsz, sizeof(*info));

	info = realloc(info, info->argsz);
	VFIO_ASSERT_NOT_NULL(info);

	ioctl_assert(device->container_fd, VFIO_IOMMU_GET_INFO, info);
	VFIO_ASSERT_GE(info->argsz, sizeof(*info));

	return info;
}

/*
 * Return iova ranges for the device's container. Normalize vfio_iommu_type1 to
 * report iommufd's iommu_iova_range. Free with free().
 */
static struct iommu_iova_range *vfio_iommu_iova_ranges(struct vfio_pci_device *device,
						       u32 *nranges)
{
	struct vfio_iommu_type1_info_cap_iova_range *cap_range;
	struct vfio_iommu_type1_info *info;
	struct vfio_info_cap_header *hdr;
	struct iommu_iova_range *ranges = NULL;

	info = vfio_iommu_get_info(device);
	hdr = vfio_iommu_info_cap_hdr(info, VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE);
	VFIO_ASSERT_NOT_NULL(hdr);

	cap_range = container_of(hdr, struct vfio_iommu_type1_info_cap_iova_range, header);
	VFIO_ASSERT_GT(cap_range->nr_iovas, 0);

	ranges = calloc(cap_range->nr_iovas, sizeof(*ranges));
	VFIO_ASSERT_NOT_NULL(ranges);

	for (u32 i = 0; i < cap_range->nr_iovas; i++) {
		ranges[i] = (struct iommu_iova_range){
			.start = cap_range->iova_ranges[i].start,
			.last = cap_range->iova_ranges[i].end,
		};
	}

	*nranges = cap_range->nr_iovas;

	free(info);
	return ranges;
}

/* Return iova ranges of the device's IOAS. Free with free() */
static struct iommu_iova_range *iommufd_iova_ranges(struct vfio_pci_device *device,
						    u32 *nranges)
{
	struct iommu_iova_range *ranges;
	int ret;

	struct iommu_ioas_iova_ranges query = {
		.size = sizeof(query),
		.ioas_id = device->ioas_id,
	};

	ret = ioctl(device->iommufd, IOMMU_IOAS_IOVA_RANGES, &query);
	VFIO_ASSERT_EQ(ret, -1);
	VFIO_ASSERT_EQ(errno, EMSGSIZE);
	VFIO_ASSERT_GT(query.num_iovas, 0);

	ranges = calloc(query.num_iovas, sizeof(*ranges));
	VFIO_ASSERT_NOT_NULL(ranges);

	query.allowed_iovas = (uintptr_t)ranges;

	ioctl_assert(device->iommufd, IOMMU_IOAS_IOVA_RANGES, &query);
	*nranges = query.num_iovas;

	return ranges;
}

static int iova_range_comp(const void *a, const void *b)
{
	const struct iommu_iova_range *ra = a, *rb = b;

	if (ra->start < rb->start)
		return -1;

	if (ra->start > rb->start)
		return 1;

	return 0;
}

/* Return sorted IOVA ranges of the device. Free with free(). */
struct iommu_iova_range *vfio_pci_iova_ranges(struct vfio_pci_device *device,
					      u32 *nranges)
{
	struct iommu_iova_range *ranges;

	if (device->iommufd)
		ranges = iommufd_iova_ranges(device, nranges);
	else
		ranges = vfio_iommu_iova_ranges(device, nranges);

	if (!ranges)
		return NULL;

	VFIO_ASSERT_GT(*nranges, 0);

	/* Sort and check that ranges are sane and non-overlapping */
	qsort(ranges, *nranges, sizeof(*ranges), iova_range_comp);
	VFIO_ASSERT_LT(ranges[0].start, ranges[0].last);

	for (u32 i = 1; i < *nranges; i++) {
		VFIO_ASSERT_LT(ranges[i].start, ranges[i].last);
		VFIO_ASSERT_LT(ranges[i - 1].last, ranges[i].start);
	}

	return ranges;
}

struct iova_allocator *iova_allocator_init(struct vfio_pci_device *device)
{
	struct iova_allocator *allocator;
	struct iommu_iova_range *ranges;
	u32 nranges;

	ranges = vfio_pci_iova_ranges(device, &nranges);
	VFIO_ASSERT_NOT_NULL(ranges);

	allocator = malloc(sizeof(*allocator));
	VFIO_ASSERT_NOT_NULL(allocator);

	*allocator = (struct iova_allocator){
		.ranges = ranges,
		.nranges = nranges,
		.range_idx = 0,
		.range_offset = 0,
	};

	return allocator;
}

void iova_allocator_cleanup(struct iova_allocator *allocator)
{
	free(allocator->ranges);
	free(allocator);
}

iova_t iova_allocator_alloc(struct iova_allocator *allocator, size_t size)
{
	VFIO_ASSERT_GT(size, 0, "Invalid size arg, zero\n");
	VFIO_ASSERT_EQ(size & (size - 1), 0, "Invalid size arg, non-power-of-2\n");

	for (;;) {
		struct iommu_iova_range *range;
		iova_t iova, last;

		VFIO_ASSERT_LT(allocator->range_idx, allocator->nranges,
			       "IOVA allocator out of space\n");

		range = &allocator->ranges[allocator->range_idx];
		iova = range->start + allocator->range_offset;

		/* Check for sufficient space at the current offset */
		if (check_add_overflow(iova, size - 1, &last) ||
		    last > range->last)
			goto next_range;

		/* Align iova to size */
		iova = last & ~(size - 1);

		/* Check for sufficient space at the aligned iova */
		if (check_add_overflow(iova, size - 1, &last) ||
		    last > range->last)
			goto next_range;

		if (last == range->last) {
			allocator->range_idx++;
			allocator->range_offset = 0;
		} else {
			allocator->range_offset = last - range->start + 1;
		}

		return iova;

next_range:
		allocator->range_idx++;
		allocator->range_offset = 0;
	}
}

iova_t __to_iova(struct vfio_pci_device *device, void *vaddr)
{
	struct vfio_dma_region *region;

	list_for_each_entry(region, &device->dma_regions, link) {
		if (vaddr < region->vaddr)
			continue;

		if (vaddr >= region->vaddr + region->size)
			continue;

		return region->iova + (vaddr - region->vaddr);
	}

	return INVALID_IOVA;
}

iova_t to_iova(struct vfio_pci_device *device, void *vaddr)
{
	iova_t iova;

	iova = __to_iova(device, vaddr);
	VFIO_ASSERT_NE(iova, INVALID_IOVA, "%p is not mapped into device.\n", vaddr);

	return iova;
}

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

static int vfio_iommu_dma_map(struct vfio_pci_device *device,
			       struct vfio_dma_region *region)
{
	struct vfio_iommu_type1_dma_map args = {
		.argsz = sizeof(args),
		.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
		.vaddr = (u64)region->vaddr,
		.iova = region->iova,
		.size = region->size,
	};

	if (ioctl(device->container_fd, VFIO_IOMMU_MAP_DMA, &args))
		return -errno;

	return 0;
}

static int iommufd_dma_map(struct vfio_pci_device *device,
			    struct vfio_dma_region *region)
{
	struct iommu_ioas_map args = {
		.size = sizeof(args),
		.flags = IOMMU_IOAS_MAP_READABLE |
			 IOMMU_IOAS_MAP_WRITEABLE |
			 IOMMU_IOAS_MAP_FIXED_IOVA,
		.user_va = (u64)region->vaddr,
		.iova = region->iova,
		.length = region->size,
		.ioas_id = device->ioas_id,
	};

	if (ioctl(device->iommufd, IOMMU_IOAS_MAP, &args))
		return -errno;

	return 0;
}

int __vfio_pci_dma_map(struct vfio_pci_device *device,
		      struct vfio_dma_region *region)
{
	int ret;

	if (device->iommufd)
		ret = iommufd_dma_map(device, region);
	else
		ret = vfio_iommu_dma_map(device, region);

	if (ret)
		return ret;

	list_add(&region->link, &device->dma_regions);

	return 0;
}

static int vfio_iommu_dma_unmap(int fd, u64 iova, u64 size, u32 flags,
				u64 *unmapped)
{
	struct vfio_iommu_type1_dma_unmap args = {
		.argsz = sizeof(args),
		.iova = iova,
		.size = size,
		.flags = flags,
	};

	if (ioctl(fd, VFIO_IOMMU_UNMAP_DMA, &args))
		return -errno;

	if (unmapped)
		*unmapped = args.size;

	return 0;
}

static int iommufd_dma_unmap(int fd, u64 iova, u64 length, u32 ioas_id,
			     u64 *unmapped)
{
	struct iommu_ioas_unmap args = {
		.size = sizeof(args),
		.iova = iova,
		.length = length,
		.ioas_id = ioas_id,
	};

	if (ioctl(fd, IOMMU_IOAS_UNMAP, &args))
		return -errno;

	if (unmapped)
		*unmapped = args.length;

	return 0;
}

int __vfio_pci_dma_unmap(struct vfio_pci_device *device,
			 struct vfio_dma_region *region, u64 *unmapped)
{
	int ret;

	if (device->iommufd)
		ret = iommufd_dma_unmap(device->iommufd, region->iova,
					region->size, device->ioas_id,
					unmapped);
	else
		ret = vfio_iommu_dma_unmap(device->container_fd, region->iova,
					   region->size, 0, unmapped);

	if (ret)
		return ret;

	list_del_init(&region->link);

	return 0;
}

int __vfio_pci_dma_unmap_all(struct vfio_pci_device *device, u64 *unmapped)
{
	int ret;
	struct vfio_dma_region *curr, *next;

	if (device->iommufd)
		ret = iommufd_dma_unmap(device->iommufd, 0, UINT64_MAX,
					device->ioas_id, unmapped);
	else
		ret = vfio_iommu_dma_unmap(device->container_fd, 0, 0,
					   VFIO_DMA_UNMAP_FLAG_ALL, unmapped);

	if (ret)
		return ret;

	list_for_each_entry_safe(curr, next, &device->dma_regions, link)
		list_del_init(&curr->link);

	return 0;
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

	ioctl_assert(device->group_fd, VFIO_GROUP_SET_CONTAINER, &device->container_fd);
}

static void vfio_pci_container_setup(struct vfio_pci_device *device, const char *bdf)
{
	unsigned long iommu_type = device->iommu_mode->iommu_type;
	const char *path = device->iommu_mode->container_path;
	int version;
	int ret;

	device->container_fd = open(path, O_RDWR);
	VFIO_ASSERT_GE(device->container_fd, 0, "open(%s) failed\n", path);

	version = ioctl(device->container_fd, VFIO_GET_API_VERSION);
	VFIO_ASSERT_EQ(version, VFIO_API_VERSION, "Unsupported version: %d\n", version);

	vfio_pci_group_setup(device, bdf);

	ret = ioctl(device->container_fd, VFIO_CHECK_EXTENSION, iommu_type);
	VFIO_ASSERT_GT(ret, 0, "VFIO IOMMU type %lu not supported\n", iommu_type);

	ioctl_assert(device->container_fd, VFIO_SET_IOMMU, (void *)iommu_type);

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

/* Reminder: Keep in sync with FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES(). */
static const struct vfio_iommu_mode iommu_modes[] = {
	{
		.name = "vfio_type1_iommu",
		.container_path = "/dev/vfio/vfio",
		.iommu_type = VFIO_TYPE1_IOMMU,
	},
	{
		.name = "vfio_type1v2_iommu",
		.container_path = "/dev/vfio/vfio",
		.iommu_type = VFIO_TYPE1v2_IOMMU,
	},
	{
		.name = "iommufd_compat_type1",
		.container_path = "/dev/iommu",
		.iommu_type = VFIO_TYPE1_IOMMU,
	},
	{
		.name = "iommufd_compat_type1v2",
		.container_path = "/dev/iommu",
		.iommu_type = VFIO_TYPE1v2_IOMMU,
	},
	{
		.name = "iommufd",
	},
};

const char *default_iommu_mode = "iommufd";

static const struct vfio_iommu_mode *lookup_iommu_mode(const char *iommu_mode)
{
	int i;

	if (!iommu_mode)
		iommu_mode = default_iommu_mode;

	for (i = 0; i < ARRAY_SIZE(iommu_modes); i++) {
		if (strcmp(iommu_mode, iommu_modes[i].name))
			continue;

		return &iommu_modes[i];
	}

	VFIO_FAIL("Unrecognized IOMMU mode: %s\n", iommu_mode);
}

static void vfio_device_bind_iommufd(int device_fd, int iommufd)
{
	struct vfio_device_bind_iommufd args = {
		.argsz = sizeof(args),
		.iommufd = iommufd,
	};

	ioctl_assert(device_fd, VFIO_DEVICE_BIND_IOMMUFD, &args);
}

static u32 iommufd_ioas_alloc(int iommufd)
{
	struct iommu_ioas_alloc args = {
		.size = sizeof(args),
	};

	ioctl_assert(iommufd, IOMMU_IOAS_ALLOC, &args);
	return args.out_ioas_id;
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

	/*
	 * Require device->iommufd to be >0 so that a simple non-0 check can be
	 * used to check if iommufd is enabled. In practice open() will never
	 * return 0 unless stdin is closed.
	 */
	device->iommufd = open("/dev/iommu", O_RDWR);
	VFIO_ASSERT_GT(device->iommufd, 0);

	vfio_device_bind_iommufd(device->fd, device->iommufd);
	device->ioas_id = iommufd_ioas_alloc(device->iommufd);
	vfio_device_attach_iommufd_pt(device->fd, device->ioas_id);
}

struct vfio_pci_device *vfio_pci_device_init(const char *bdf, const char *iommu_mode)
{
	struct vfio_pci_device *device;

	device = calloc(1, sizeof(*device));
	VFIO_ASSERT_NOT_NULL(device);

	INIT_LIST_HEAD(&device->dma_regions);

	device->iommu_mode = lookup_iommu_mode(iommu_mode);

	if (device->iommu_mode->container_path)
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

	if (device->iommufd) {
		VFIO_ASSERT_EQ(close(device->iommufd), 0);
	} else {
		VFIO_ASSERT_EQ(close(device->group_fd), 0);
		VFIO_ASSERT_EQ(close(device->container_fd), 0);
	}

	free(device);
}

static bool is_bdf(const char *str)
{
	unsigned int s, b, d, f;
	int length, count;

	count = sscanf(str, "%4x:%2x:%2x.%2x%n", &s, &b, &d, &f, &length);
	return count == 4 && length == strlen(str);
}

const char *vfio_selftests_get_bdf(int *argc, char *argv[])
{
	char *bdf;

	if (*argc > 1 && is_bdf(argv[*argc - 1]))
		return argv[--(*argc)];

	bdf = getenv("VFIO_SELFTESTS_BDF");
	if (bdf) {
		VFIO_ASSERT_TRUE(is_bdf(bdf), "Invalid BDF: %s\n", bdf);
		return bdf;
	}

	fprintf(stderr, "Unable to determine which device to use, skipping test.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "To pass the device address via environment variable:\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    export VFIO_SELFTESTS_BDF=segment:bus:device.function\n");
	fprintf(stderr, "    %s [options]\n", argv[0]);
	fprintf(stderr, "\n");
	fprintf(stderr, "To pass the device address via argv:\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    %s [options] segment:bus:device.function\n", argv[0]);
	fprintf(stderr, "\n");
	exit(KSFT_SKIP);
}
