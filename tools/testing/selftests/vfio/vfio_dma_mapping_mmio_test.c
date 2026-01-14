// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include <uapi/linux/types.h>
#include <linux/pci_regs.h>
#include <linux/sizes.h>
#include <linux/vfio.h>

#include <libvfio.h>

#include "../kselftest_harness.h"

static const char *device_bdf;

static struct vfio_pci_bar *largest_mapped_bar(struct vfio_pci_device *device)
{
	u32 flags = VFIO_REGION_INFO_FLAG_READ | VFIO_REGION_INFO_FLAG_WRITE;
	struct vfio_pci_bar *largest = NULL;
	u64 bar_size = 0;

	for (int i = 0; i < PCI_STD_NUM_BARS; i++) {
		struct vfio_pci_bar *bar = &device->bars[i];

		if (!bar->vaddr)
			continue;

		/*
		 * iommu_map() maps with READ|WRITE, so require the same
		 * abilities for the underlying VFIO region.
		 */
		if ((bar->info.flags & flags) != flags)
			continue;

		if (bar->info.size > bar_size) {
			bar_size = bar->info.size;
			largest = bar;
		}
	}

	return largest;
}

FIXTURE(vfio_dma_mapping_mmio_test) {
	struct iommu *iommu;
	struct vfio_pci_device *device;
	struct iova_allocator *iova_allocator;
	struct vfio_pci_bar *bar;
};

FIXTURE_VARIANT(vfio_dma_mapping_mmio_test) {
	const char *iommu_mode;
};

#define FIXTURE_VARIANT_ADD_IOMMU_MODE(_iommu_mode)			       \
FIXTURE_VARIANT_ADD(vfio_dma_mapping_mmio_test, _iommu_mode) {		       \
	.iommu_mode = #_iommu_mode,					       \
}

FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES();

#undef FIXTURE_VARIANT_ADD_IOMMU_MODE

FIXTURE_SETUP(vfio_dma_mapping_mmio_test)
{
	self->iommu = iommu_init(variant->iommu_mode);
	self->device = vfio_pci_device_init(device_bdf, self->iommu);
	self->iova_allocator = iova_allocator_init(self->iommu);
	self->bar = largest_mapped_bar(self->device);

	if (!self->bar)
		SKIP(return, "No mappable BAR found on device %s", device_bdf);
}

FIXTURE_TEARDOWN(vfio_dma_mapping_mmio_test)
{
	iova_allocator_cleanup(self->iova_allocator);
	vfio_pci_device_cleanup(self->device);
	iommu_cleanup(self->iommu);
}

static void do_mmio_map_test(struct iommu *iommu,
			     struct iova_allocator *iova_allocator,
			     void *vaddr, size_t size)
{
	struct dma_region region = {
		.vaddr = vaddr,
		.size = size,
		.iova = iova_allocator_alloc(iova_allocator, size),
	};

	/*
	 * NOTE: Check for iommufd compat success once it lands. Native iommufd
	 * will never support this.
	 */
	if (!strcmp(iommu->mode->name, MODE_VFIO_TYPE1V2_IOMMU) ||
	    !strcmp(iommu->mode->name, MODE_VFIO_TYPE1_IOMMU)) {
		iommu_map(iommu, &region);
		iommu_unmap(iommu, &region);
	} else {
		VFIO_ASSERT_NE(__iommu_map(iommu, &region), 0);
		VFIO_ASSERT_NE(__iommu_unmap(iommu, &region, NULL), 0);
	}
}

TEST_F(vfio_dma_mapping_mmio_test, map_full_bar)
{
	do_mmio_map_test(self->iommu, self->iova_allocator,
			 self->bar->vaddr, self->bar->info.size);
}

TEST_F(vfio_dma_mapping_mmio_test, map_partial_bar)
{
	if (self->bar->info.size < 2 * getpagesize())
		SKIP(return, "BAR too small (size=0x%llx)", self->bar->info.size);

	do_mmio_map_test(self->iommu, self->iova_allocator,
			 self->bar->vaddr, getpagesize());
}

/* Test IOMMU mapping of BAR mmap with intentionally poor vaddr alignment. */
TEST_F(vfio_dma_mapping_mmio_test, map_bar_misaligned)
{
	/* Limit size to bound test time for large BARs */
	size_t size = min_t(size_t, self->bar->info.size, SZ_1G);
	void *vaddr;

	vaddr = mmap_reserve(size, SZ_1G, getpagesize());
	vaddr = mmap(vaddr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
		     self->device->fd, self->bar->info.offset);
	VFIO_ASSERT_NE(vaddr, MAP_FAILED);

	do_mmio_map_test(self->iommu, self->iova_allocator, vaddr, size);

	VFIO_ASSERT_EQ(munmap(vaddr, size), 0);
}

int main(int argc, char *argv[])
{
	device_bdf = vfio_selftests_get_bdf(&argc, argv);
	return test_harness_run(argc, argv);
}
