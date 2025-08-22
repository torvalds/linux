// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>

#include <sys/mman.h>

#include <linux/sizes.h>
#include <linux/vfio.h>

#include <vfio_util.h>

#include "../kselftest_harness.h"

static const char *device_bdf;

FIXTURE(vfio_dma_mapping_test) {
	struct vfio_pci_device *device;
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
	const u64 size = SZ_2M;
	void *mem;
	u64 iova;

	mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
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
