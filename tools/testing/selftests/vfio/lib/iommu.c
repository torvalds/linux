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

#include <linux/limits.h>
#include <linux/mman.h>
#include <linux/types.h>
#include <linux/vfio.h>
#include <linux/iommufd.h>

#include "../../../kselftest.h"
#include <libvfio.h>

const char *default_iommu_mode = MODE_IOMMUFD;

/* Reminder: Keep in sync with FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES(). */
static const struct iommu_mode iommu_modes[] = {
	{
		.name = MODE_VFIO_TYPE1_IOMMU,
		.container_path = "/dev/vfio/vfio",
		.iommu_type = VFIO_TYPE1_IOMMU,
	},
	{
		.name = MODE_VFIO_TYPE1V2_IOMMU,
		.container_path = "/dev/vfio/vfio",
		.iommu_type = VFIO_TYPE1v2_IOMMU,
	},
	{
		.name = MODE_IOMMUFD_COMPAT_TYPE1,
		.container_path = "/dev/iommu",
		.iommu_type = VFIO_TYPE1_IOMMU,
	},
	{
		.name = MODE_IOMMUFD_COMPAT_TYPE1V2,
		.container_path = "/dev/iommu",
		.iommu_type = VFIO_TYPE1v2_IOMMU,
	},
	{
		.name = MODE_IOMMUFD,
	},
};

static const struct iommu_mode *lookup_iommu_mode(const char *iommu_mode)
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

int __iommu_hva2iova(struct iommu *iommu, void *vaddr, iova_t *iova)
{
	struct dma_region *region;

	list_for_each_entry(region, &iommu->dma_regions, link) {
		if (vaddr < region->vaddr)
			continue;

		if (vaddr >= region->vaddr + region->size)
			continue;

		if (iova)
			*iova = region->iova + (vaddr - region->vaddr);

		return 0;
	}

	return -ENOENT;
}

iova_t iommu_hva2iova(struct iommu *iommu, void *vaddr)
{
	iova_t iova;
	int ret;

	ret = __iommu_hva2iova(iommu, vaddr, &iova);
	VFIO_ASSERT_EQ(ret, 0, "%p is not mapped into the iommu\n", vaddr);

	return iova;
}

static int vfio_iommu_map(struct iommu *iommu, struct dma_region *region)
{
	struct vfio_iommu_type1_dma_map args = {
		.argsz = sizeof(args),
		.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
		.vaddr = (u64)region->vaddr,
		.iova = region->iova,
		.size = region->size,
	};

	if (ioctl(iommu->container_fd, VFIO_IOMMU_MAP_DMA, &args))
		return -errno;

	return 0;
}

static int iommufd_map(struct iommu *iommu, struct dma_region *region)
{
	struct iommu_ioas_map args = {
		.size = sizeof(args),
		.flags = IOMMU_IOAS_MAP_READABLE |
			 IOMMU_IOAS_MAP_WRITEABLE |
			 IOMMU_IOAS_MAP_FIXED_IOVA,
		.user_va = (u64)region->vaddr,
		.iova = region->iova,
		.length = region->size,
		.ioas_id = iommu->ioas_id,
	};

	if (ioctl(iommu->iommufd, IOMMU_IOAS_MAP, &args))
		return -errno;

	return 0;
}

int __iommu_map(struct iommu *iommu, struct dma_region *region)
{
	int ret;

	if (iommu->iommufd)
		ret = iommufd_map(iommu, region);
	else
		ret = vfio_iommu_map(iommu, region);

	if (ret)
		return ret;

	list_add(&region->link, &iommu->dma_regions);

	return 0;
}

static int __vfio_iommu_unmap(int fd, u64 iova, u64 size, u32 flags, u64 *unmapped)
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

static int vfio_iommu_unmap(struct iommu *iommu, struct dma_region *region,
			    u64 *unmapped)
{
	return __vfio_iommu_unmap(iommu->container_fd, region->iova,
				  region->size, 0, unmapped);
}

static int __iommufd_unmap(int fd, u64 iova, u64 length, u32 ioas_id, u64 *unmapped)
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

static int iommufd_unmap(struct iommu *iommu, struct dma_region *region,
			 u64 *unmapped)
{
	return __iommufd_unmap(iommu->iommufd, region->iova, region->size,
			       iommu->ioas_id, unmapped);
}

int __iommu_unmap(struct iommu *iommu, struct dma_region *region, u64 *unmapped)
{
	int ret;

	if (iommu->iommufd)
		ret = iommufd_unmap(iommu, region, unmapped);
	else
		ret = vfio_iommu_unmap(iommu, region, unmapped);

	if (ret)
		return ret;

	list_del_init(&region->link);

	return 0;
}

int __iommu_unmap_all(struct iommu *iommu, u64 *unmapped)
{
	int ret;
	struct dma_region *curr, *next;

	if (iommu->iommufd)
		ret = __iommufd_unmap(iommu->iommufd, 0, UINT64_MAX,
				      iommu->ioas_id, unmapped);
	else
		ret = __vfio_iommu_unmap(iommu->container_fd, 0, 0,
					 VFIO_DMA_UNMAP_FLAG_ALL, unmapped);

	if (ret)
		return ret;

	list_for_each_entry_safe(curr, next, &iommu->dma_regions, link)
		list_del_init(&curr->link);

	return 0;
}

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
static struct vfio_iommu_type1_info *vfio_iommu_get_info(int container_fd)
{
	struct vfio_iommu_type1_info *info;

	info = malloc(sizeof(*info));
	VFIO_ASSERT_NOT_NULL(info);

	*info = (struct vfio_iommu_type1_info) {
		.argsz = sizeof(*info),
	};

	ioctl_assert(container_fd, VFIO_IOMMU_GET_INFO, info);
	VFIO_ASSERT_GE(info->argsz, sizeof(*info));

	info = realloc(info, info->argsz);
	VFIO_ASSERT_NOT_NULL(info);

	ioctl_assert(container_fd, VFIO_IOMMU_GET_INFO, info);
	VFIO_ASSERT_GE(info->argsz, sizeof(*info));

	return info;
}

/*
 * Return iova ranges for the device's container. Normalize vfio_iommu_type1 to
 * report iommufd's iommu_iova_range. Free with free().
 */
static struct iommu_iova_range *vfio_iommu_iova_ranges(struct iommu *iommu,
						       u32 *nranges)
{
	struct vfio_iommu_type1_info_cap_iova_range *cap_range;
	struct vfio_iommu_type1_info *info;
	struct vfio_info_cap_header *hdr;
	struct iommu_iova_range *ranges = NULL;

	info = vfio_iommu_get_info(iommu->container_fd);
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
static struct iommu_iova_range *iommufd_iova_ranges(struct iommu *iommu,
						    u32 *nranges)
{
	struct iommu_iova_range *ranges;
	int ret;

	struct iommu_ioas_iova_ranges query = {
		.size = sizeof(query),
		.ioas_id = iommu->ioas_id,
	};

	ret = ioctl(iommu->iommufd, IOMMU_IOAS_IOVA_RANGES, &query);
	VFIO_ASSERT_EQ(ret, -1);
	VFIO_ASSERT_EQ(errno, EMSGSIZE);
	VFIO_ASSERT_GT(query.num_iovas, 0);

	ranges = calloc(query.num_iovas, sizeof(*ranges));
	VFIO_ASSERT_NOT_NULL(ranges);

	query.allowed_iovas = (uintptr_t)ranges;

	ioctl_assert(iommu->iommufd, IOMMU_IOAS_IOVA_RANGES, &query);
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
struct iommu_iova_range *iommu_iova_ranges(struct iommu *iommu, u32 *nranges)
{
	struct iommu_iova_range *ranges;

	if (iommu->iommufd)
		ranges = iommufd_iova_ranges(iommu, nranges);
	else
		ranges = vfio_iommu_iova_ranges(iommu, nranges);

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

static u32 iommufd_ioas_alloc(int iommufd)
{
	struct iommu_ioas_alloc args = {
		.size = sizeof(args),
	};

	ioctl_assert(iommufd, IOMMU_IOAS_ALLOC, &args);
	return args.out_ioas_id;
}

struct iommu *iommu_init(const char *iommu_mode)
{
	const char *container_path;
	struct iommu *iommu;
	int version;

	iommu = calloc(1, sizeof(*iommu));
	VFIO_ASSERT_NOT_NULL(iommu);

	INIT_LIST_HEAD(&iommu->dma_regions);

	iommu->mode = lookup_iommu_mode(iommu_mode);

	container_path = iommu->mode->container_path;
	if (container_path) {
		iommu->container_fd = open(container_path, O_RDWR);
		VFIO_ASSERT_GE(iommu->container_fd, 0, "open(%s) failed\n", container_path);

		version = ioctl(iommu->container_fd, VFIO_GET_API_VERSION);
		VFIO_ASSERT_EQ(version, VFIO_API_VERSION, "Unsupported version: %d\n", version);
	} else {
		/*
		 * Require device->iommufd to be >0 so that a simple non-0 check can be
		 * used to check if iommufd is enabled. In practice open() will never
		 * return 0 unless stdin is closed.
		 */
		iommu->iommufd = open("/dev/iommu", O_RDWR);
		VFIO_ASSERT_GT(iommu->iommufd, 0);

		iommu->ioas_id = iommufd_ioas_alloc(iommu->iommufd);
	}

	return iommu;
}

void iommu_cleanup(struct iommu *iommu)
{
	if (iommu->iommufd)
		VFIO_ASSERT_EQ(close(iommu->iommufd), 0);
	else
		VFIO_ASSERT_EQ(close(iommu->container_fd), 0);

	free(iommu);
}
