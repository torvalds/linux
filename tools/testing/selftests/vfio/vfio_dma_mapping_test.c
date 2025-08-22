// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

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

FIXTURE_SETUP(vfio_dma_mapping_test)
{
	self->device = vfio_pci_device_init(device_bdf, variant->iommu_mode);
}

FIXTURE_TEARDOWN(vfio_dma_mapping_test)
{
	vfio_pci_device_cleanup(self->device);
}

TEST_F(vfio_dma_mapping_test, dma_map_unmap)
{
	const u64 size = variant->size ?: getpagesize();
	const int flags = variant->mmap_flags;
	struct vfio_dma_region region;
	struct iommu_mapping mapping;
	u64 mapping_size = size;
	int rc;

	region.vaddr = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, -1, 0);

	/* Skip the test if there aren't enough HugeTLB pages available. */
	if (flags & MAP_HUGETLB && region.vaddr == MAP_FAILED)
		SKIP(return, "mmap() failed: %s (%d)\n", strerror(errno), errno);
	else
		ASSERT_NE(region.vaddr, MAP_FAILED);

	region.iova = (u64)region.vaddr;
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
	vfio_pci_dma_unmap(self->device, &region);
	printf("Unmapped IOVA 0x%lx\n", region.iova);
	ASSERT_EQ(INVALID_IOVA, __to_iova(self->device, region.vaddr));
	ASSERT_NE(0, iommu_mapping_get(device_bdf, region.iova, &mapping));

	ASSERT_TRUE(!munmap(region.vaddr, size));
}

int main(int argc, char *argv[])
{
	device_bdf = vfio_selftests_get_bdf(&argc, argv);
	return test_harness_run(argc, argv);
}
