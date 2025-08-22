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

FIXTURE(vfio_dma_mapping_test) {
	struct vfio_pci_device *device;
};

FIXTURE_VARIANT(vfio_dma_mapping_test) {
	u64 size;
	int mmap_flags;
};

FIXTURE_VARIANT_ADD(vfio_dma_mapping_test, anonymous) {
	.mmap_flags = MAP_ANONYMOUS | MAP_PRIVATE,
};

FIXTURE_VARIANT_ADD(vfio_dma_mapping_test, anonymous_hugetlb_2mb) {
	.size = SZ_2M,
	.mmap_flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | MAP_HUGE_2MB,
};

FIXTURE_VARIANT_ADD(vfio_dma_mapping_test, anonymous_hugetlb_1gb) {
	.size = SZ_1G,
	.mmap_flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | MAP_HUGE_1GB,
};

FIXTURE_SETUP(vfio_dma_mapping_test)
{
	self->device = vfio_pci_device_init(device_bdf, VFIO_TYPE1_IOMMU);
}

FIXTURE_TEARDOWN(vfio_dma_mapping_test)
{
	vfio_pci_device_cleanup(self->device);
}

TEST_F(vfio_dma_mapping_test, dma_map_unmap)
{
	const u64 size = variant->size ?: getpagesize();
	const int flags = variant->mmap_flags;
	void *mem;
	u64 iova;

	mem = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, -1, 0);

	/* Skip the test if there aren't enough HugeTLB pages available. */
	if (flags & MAP_HUGETLB && mem == MAP_FAILED)
		SKIP(return, "mmap() failed: %s (%d)\n", strerror(errno), errno);
	else
		ASSERT_NE(mem, MAP_FAILED);

	iova = (u64)mem;

	vfio_pci_dma_map(self->device, iova, size, mem);
	printf("Mapped HVA %p (size 0x%lx) at IOVA 0x%lx\n", mem, size, iova);

	vfio_pci_dma_unmap(self->device, iova, size);

	ASSERT_TRUE(!munmap(mem, size));
}

int main(int argc, char *argv[])
{
	device_bdf = vfio_selftests_get_bdf(&argc, argv);
	return test_harness_run(argc, argv);
}
