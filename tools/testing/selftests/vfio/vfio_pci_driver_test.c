// SPDX-License-Identifier: GPL-2.0-only
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/sizes.h>
#include <linux/vfio.h>

#include <vfio_util.h>

#include "../kselftest_harness.h"

static const char *device_bdf;

#define ASSERT_NO_MSI(_eventfd) do {			\
	u64 __value;					\
							\
	ASSERT_EQ(-1, read(_eventfd, &__value, 8));	\
	ASSERT_EQ(EAGAIN, errno);			\
} while (0)

static void region_setup(struct vfio_pci_device *device,
			 struct iova_allocator *iova_allocator,
			 struct vfio_dma_region *region, u64 size)
{
	const int flags = MAP_SHARED | MAP_ANONYMOUS;
	const int prot = PROT_READ | PROT_WRITE;
	void *vaddr;

	vaddr = mmap(NULL, size, prot, flags, -1, 0);
	VFIO_ASSERT_NE(vaddr, MAP_FAILED);

	region->vaddr = vaddr;
	region->iova = iova_allocator_alloc(iova_allocator, size);
	region->size = size;

	vfio_pci_dma_map(device, region);
}

static void region_teardown(struct vfio_pci_device *device,
			    struct vfio_dma_region *region)
{
	vfio_pci_dma_unmap(device, region);
	VFIO_ASSERT_EQ(munmap(region->vaddr, region->size), 0);
}

FIXTURE(vfio_pci_driver_test) {
	struct vfio_pci_device *device;
	struct iova_allocator *iova_allocator;
	struct vfio_dma_region memcpy_region;
	void *vaddr;
	int msi_fd;

	u64 size;
	void *src;
	void *dst;
	iova_t src_iova;
	iova_t dst_iova;
	iova_t unmapped_iova;
};

FIXTURE_VARIANT(vfio_pci_driver_test) {
	const char *iommu_mode;
};

#define FIXTURE_VARIANT_ADD_IOMMU_MODE(_iommu_mode)		\
FIXTURE_VARIANT_ADD(vfio_pci_driver_test, _iommu_mode) {	\
	.iommu_mode = #_iommu_mode,				\
}

FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES();

FIXTURE_SETUP(vfio_pci_driver_test)
{
	struct vfio_pci_driver *driver;

	self->device = vfio_pci_device_init(device_bdf, variant->iommu_mode);
	self->iova_allocator = iova_allocator_init(self->device);

	driver = &self->device->driver;

	region_setup(self->device, self->iova_allocator, &self->memcpy_region, SZ_1G);
	region_setup(self->device, self->iova_allocator, &driver->region, SZ_2M);

	/* Any IOVA that doesn't overlap memcpy_region and driver->region. */
	self->unmapped_iova = iova_allocator_alloc(self->iova_allocator, SZ_1G);

	vfio_pci_driver_init(self->device);
	self->msi_fd = self->device->msi_eventfds[driver->msi];

	/*
	 * Use the maximum size supported by the device for memcpy operations,
	 * slimmed down to fit into the memcpy region (divided by 2 so src and
	 * dst regions do not overlap).
	 */
	self->size = self->device->driver.max_memcpy_size;
	self->size = min(self->size, self->memcpy_region.size / 2);

	self->src = self->memcpy_region.vaddr;
	self->dst = self->src + self->size;

	self->src_iova = to_iova(self->device, self->src);
	self->dst_iova = to_iova(self->device, self->dst);
}

FIXTURE_TEARDOWN(vfio_pci_driver_test)
{
	struct vfio_pci_driver *driver = &self->device->driver;

	vfio_pci_driver_remove(self->device);

	region_teardown(self->device, &self->memcpy_region);
	region_teardown(self->device, &driver->region);

	iova_allocator_cleanup(self->iova_allocator);
	vfio_pci_device_cleanup(self->device);
}

TEST_F(vfio_pci_driver_test, init_remove)
{
	int i;

	for (i = 0; i < 10; i++) {
		vfio_pci_driver_remove(self->device);
		vfio_pci_driver_init(self->device);
	}
}

TEST_F(vfio_pci_driver_test, memcpy_success)
{
	fcntl_set_nonblock(self->msi_fd);

	memset(self->src, 'x', self->size);
	memset(self->dst, 'y', self->size);

	ASSERT_EQ(0, vfio_pci_driver_memcpy(self->device,
					    self->src_iova,
					    self->dst_iova,
					    self->size));

	ASSERT_EQ(0, memcmp(self->src, self->dst, self->size));
	ASSERT_NO_MSI(self->msi_fd);
}

TEST_F(vfio_pci_driver_test, memcpy_from_unmapped_iova)
{
	fcntl_set_nonblock(self->msi_fd);

	/*
	 * Ignore the return value since not all devices will detect and report
	 * accesses to unmapped IOVAs as errors.
	 */
	vfio_pci_driver_memcpy(self->device, self->unmapped_iova,
			       self->dst_iova, self->size);

	ASSERT_NO_MSI(self->msi_fd);
}

TEST_F(vfio_pci_driver_test, memcpy_to_unmapped_iova)
{
	fcntl_set_nonblock(self->msi_fd);

	/*
	 * Ignore the return value since not all devices will detect and report
	 * accesses to unmapped IOVAs as errors.
	 */
	vfio_pci_driver_memcpy(self->device, self->src_iova,
			       self->unmapped_iova, self->size);

	ASSERT_NO_MSI(self->msi_fd);
}

TEST_F(vfio_pci_driver_test, send_msi)
{
	u64 value;

	vfio_pci_driver_send_msi(self->device);
	ASSERT_EQ(8, read(self->msi_fd, &value, 8));
	ASSERT_EQ(1, value);
}

TEST_F(vfio_pci_driver_test, mix_and_match)
{
	u64 value;
	int i;

	for (i = 0; i < 10; i++) {
		memset(self->src, 'x', self->size);
		memset(self->dst, 'y', self->size);

		ASSERT_EQ(0, vfio_pci_driver_memcpy(self->device,
						    self->src_iova,
						    self->dst_iova,
						    self->size));

		ASSERT_EQ(0, memcmp(self->src, self->dst, self->size));

		vfio_pci_driver_memcpy(self->device,
				       self->unmapped_iova,
				       self->dst_iova,
				       self->size);

		vfio_pci_driver_send_msi(self->device);
		ASSERT_EQ(8, read(self->msi_fd, &value, 8));
		ASSERT_EQ(1, value);
	}
}

TEST_F_TIMEOUT(vfio_pci_driver_test, memcpy_storm, 60)
{
	struct vfio_pci_driver *driver = &self->device->driver;
	u64 total_size;
	u64 count;

	fcntl_set_nonblock(self->msi_fd);

	/*
	 * Perform up to 250GiB worth of DMA reads and writes across several
	 * memcpy operations. Some devices can support even more but the test
	 * will take too long.
	 */
	total_size = 250UL * SZ_1G;
	count = min(total_size / self->size, driver->max_memcpy_count);

	printf("Kicking off %lu memcpys of size 0x%lx\n", count, self->size);
	vfio_pci_driver_memcpy_start(self->device,
				     self->src_iova,
				     self->dst_iova,
				     self->size, count);

	ASSERT_EQ(0, vfio_pci_driver_memcpy_wait(self->device));
	ASSERT_NO_MSI(self->msi_fd);
}

int main(int argc, char *argv[])
{
	struct vfio_pci_device *device;

	device_bdf = vfio_selftests_get_bdf(&argc, argv);

	device = vfio_pci_device_init(device_bdf, default_iommu_mode);
	if (!device->driver.ops) {
		fprintf(stderr, "No driver found for device %s\n", device_bdf);
		return KSFT_SKIP;
	}
	vfio_pci_device_cleanup(device);

	return test_harness_run(argc, argv);
}
