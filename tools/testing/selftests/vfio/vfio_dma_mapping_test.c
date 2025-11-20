// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include <uapi/linux/types.h>
#include <linux/iommufd.h>
#include <linux/limits.h>
#include <linux/mman.h>
#include <linux/sizes.h>
#include <linux/vfio.h>

#include <vfio_util.h>

#include "../kselftest_harness.h"

static const char *device_bdf;

struct iommu_mapping {
	u64 pgd;
	u64 p4d;
	u64 pud;
	u64 pmd;
	u64 pte;
};

static void parse_next_value(char **line, u64 *value)
{
	char *token;

	token = strtok_r(*line, " \t|\n", line);
	if (!token)
		return;

	/* Caller verifies `value`. No need to check return value. */
	sscanf(token, "0x%lx", value);
}

static int intel_iommu_mapping_get(const char *bdf, u64 iova,
				   struct iommu_mapping *mapping)
{
	char iommu_mapping_path[PATH_MAX], line[PATH_MAX];
	u64 line_iova = -1;
	int ret = -ENOENT;
	FILE *file;
	char *rest;

	snprintf(iommu_mapping_path, sizeof(iommu_mapping_path),
		 "/sys/kernel/debug/iommu/intel/%s/domain_translation_struct",
		 bdf);

	printf("Searching for IOVA 0x%lx in %s\n", iova, iommu_mapping_path);

	file = fopen(iommu_mapping_path, "r");
	VFIO_ASSERT_NOT_NULL(file, "fopen(%s) failed", iommu_mapping_path);

	while (fgets(line, sizeof(line), file)) {
		rest = line;

		parse_next_value(&rest, &line_iova);
		if (line_iova != (iova / getpagesize()))
			continue;

		/*
		 * Ensure each struct field is initialized in case of empty
		 * page table values.
		 */
		memset(mapping, 0, sizeof(*mapping));
		parse_next_value(&rest, &mapping->pgd);
		parse_next_value(&rest, &mapping->p4d);
		parse_next_value(&rest, &mapping->pud);
		parse_next_value(&rest, &mapping->pmd);
		parse_next_value(&rest, &mapping->pte);

		ret = 0;
		break;
	}

	fclose(file);

	if (ret)
		printf("IOVA not found\n");

	return ret;
}

static int iommu_mapping_get(const char *bdf, u64 iova,
			     struct iommu_mapping *mapping)
{
	if (!access("/sys/kernel/debug/iommu/intel", F_OK))
		return intel_iommu_mapping_get(bdf, iova, mapping);

	return -EOPNOTSUPP;
}

FIXTURE(vfio_dma_mapping_test) {
	struct vfio_pci_device *device;
	struct iova_allocator *iova_allocator;
};

FIXTURE_VARIANT(vfio_dma_mapping_test) {
	const char *iommu_mode;
	u64 size;
	int mmap_flags;
};

#define FIXTURE_VARIANT_ADD_IOMMU_MODE(_iommu_mode, _name, _size, _mmap_flags) \
FIXTURE_VARIANT_ADD(vfio_dma_mapping_test, _iommu_mode ## _ ## _name) {	       \
	.iommu_mode = #_iommu_mode,					       \
	.size = (_size),						       \
	.mmap_flags = MAP_ANONYMOUS | MAP_PRIVATE | (_mmap_flags),	       \
}

FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES(anonymous, 0, 0);
FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES(anonymous_hugetlb_2mb, SZ_2M, MAP_HUGETLB | MAP_HUGE_2MB);
FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES(anonymous_hugetlb_1gb, SZ_1G, MAP_HUGETLB | MAP_HUGE_1GB);

#undef FIXTURE_VARIANT_ADD_IOMMU_MODE

FIXTURE_SETUP(vfio_dma_mapping_test)
{
	self->device = vfio_pci_device_init(device_bdf, variant->iommu_mode);
	self->iova_allocator = iova_allocator_init(self->device);
}

FIXTURE_TEARDOWN(vfio_dma_mapping_test)
{
	iova_allocator_cleanup(self->iova_allocator);
	vfio_pci_device_cleanup(self->device);
}

TEST_F(vfio_dma_mapping_test, dma_map_unmap)
{
	const u64 size = variant->size ?: getpagesize();
	const int flags = variant->mmap_flags;
	struct vfio_dma_region region;
	struct iommu_mapping mapping;
	u64 mapping_size = size;
	u64 unmapped;
	int rc;

	region.vaddr = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, -1, 0);

	/* Skip the test if there aren't enough HugeTLB pages available. */
	if (flags & MAP_HUGETLB && region.vaddr == MAP_FAILED)
		SKIP(return, "mmap() failed: %s (%d)\n", strerror(errno), errno);
	else
		ASSERT_NE(region.vaddr, MAP_FAILED);

	region.iova = iova_allocator_alloc(self->iova_allocator, size);
	region.size = size;

	vfio_pci_dma_map(self->device, &region);
	printf("Mapped HVA %p (size 0x%lx) at IOVA 0x%lx\n", region.vaddr, size, region.iova);

	ASSERT_EQ(region.iova, to_iova(self->device, region.vaddr));

	rc = iommu_mapping_get(device_bdf, region.iova, &mapping);
	if (rc == -EOPNOTSUPP)
		goto unmap;

	/*
	 * IOMMUFD compatibility-mode does not support huge mappings when
	 * using VFIO_TYPE1_IOMMU.
	 */
	if (!strcmp(variant->iommu_mode, "iommufd_compat_type1"))
		mapping_size = SZ_4K;

	ASSERT_EQ(0, rc);
	printf("Found IOMMU mappings for IOVA 0x%lx:\n", region.iova);
	printf("PGD: 0x%016lx\n", mapping.pgd);
	printf("P4D: 0x%016lx\n", mapping.p4d);
	printf("PUD: 0x%016lx\n", mapping.pud);
	printf("PMD: 0x%016lx\n", mapping.pmd);
	printf("PTE: 0x%016lx\n", mapping.pte);

	switch (mapping_size) {
	case SZ_4K:
		ASSERT_NE(0, mapping.pte);
		break;
	case SZ_2M:
		ASSERT_EQ(0, mapping.pte);
		ASSERT_NE(0, mapping.pmd);
		break;
	case SZ_1G:
		ASSERT_EQ(0, mapping.pte);
		ASSERT_EQ(0, mapping.pmd);
		ASSERT_NE(0, mapping.pud);
		break;
	default:
		VFIO_FAIL("Unrecognized size: 0x%lx\n", mapping_size);
	}

unmap:
	rc = __vfio_pci_dma_unmap(self->device, &region, &unmapped);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(unmapped, region.size);
	printf("Unmapped IOVA 0x%lx\n", region.iova);
	ASSERT_EQ(INVALID_IOVA, __to_iova(self->device, region.vaddr));
	ASSERT_NE(0, iommu_mapping_get(device_bdf, region.iova, &mapping));

	ASSERT_TRUE(!munmap(region.vaddr, size));
}

FIXTURE(vfio_dma_map_limit_test) {
	struct vfio_pci_device *device;
	struct vfio_dma_region region;
	size_t mmap_size;
};

FIXTURE_VARIANT(vfio_dma_map_limit_test) {
	const char *iommu_mode;
};

#define FIXTURE_VARIANT_ADD_IOMMU_MODE(_iommu_mode)			       \
FIXTURE_VARIANT_ADD(vfio_dma_map_limit_test, _iommu_mode) {		       \
	.iommu_mode = #_iommu_mode,					       \
}

FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES();

#undef FIXTURE_VARIANT_ADD_IOMMU_MODE

FIXTURE_SETUP(vfio_dma_map_limit_test)
{
	struct vfio_dma_region *region = &self->region;
	struct iommu_iova_range *ranges;
	u64 region_size = getpagesize();
	iova_t last_iova;
	u32 nranges;

	/*
	 * Over-allocate mmap by double the size to provide enough backing vaddr
	 * for overflow tests
	 */
	self->mmap_size = 2 * region_size;

	self->device = vfio_pci_device_init(device_bdf, variant->iommu_mode);
	region->vaddr = mmap(NULL, self->mmap_size, PROT_READ | PROT_WRITE,
			     MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	ASSERT_NE(region->vaddr, MAP_FAILED);

	ranges = vfio_pci_iova_ranges(self->device, &nranges);
	VFIO_ASSERT_NOT_NULL(ranges);
	last_iova = ranges[nranges - 1].last;
	free(ranges);

	/* One page prior to the last iova */
	region->iova = last_iova & ~(region_size - 1);
	region->size = region_size;
}

FIXTURE_TEARDOWN(vfio_dma_map_limit_test)
{
	vfio_pci_device_cleanup(self->device);
	ASSERT_EQ(munmap(self->region.vaddr, self->mmap_size), 0);
}

TEST_F(vfio_dma_map_limit_test, unmap_range)
{
	struct vfio_dma_region *region = &self->region;
	u64 unmapped;
	int rc;

	vfio_pci_dma_map(self->device, region);
	ASSERT_EQ(region->iova, to_iova(self->device, region->vaddr));

	rc = __vfio_pci_dma_unmap(self->device, region, &unmapped);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(unmapped, region->size);
}

TEST_F(vfio_dma_map_limit_test, unmap_all)
{
	struct vfio_dma_region *region = &self->region;
	u64 unmapped;
	int rc;

	vfio_pci_dma_map(self->device, region);
	ASSERT_EQ(region->iova, to_iova(self->device, region->vaddr));

	rc = __vfio_pci_dma_unmap_all(self->device, &unmapped);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(unmapped, region->size);
}

TEST_F(vfio_dma_map_limit_test, overflow)
{
	struct vfio_dma_region *region = &self->region;
	int rc;

	region->iova = ~(iova_t)0 & ~(region->size - 1);
	region->size = self->mmap_size;

	rc = __vfio_pci_dma_map(self->device, region);
	ASSERT_EQ(rc, -EOVERFLOW);

	rc = __vfio_pci_dma_unmap(self->device, region, NULL);
	ASSERT_EQ(rc, -EOVERFLOW);
}

int main(int argc, char *argv[])
{
	device_bdf = vfio_selftests_get_bdf(&argc, argv);
	return test_harness_run(argc, argv);
}
