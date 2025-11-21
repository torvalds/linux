// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>
#include <stdlib.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/limits.h>
#include <linux/pci_regs.h>
#include <linux/sizes.h>
#include <linux/vfio.h>

#include <vfio_util.h>

#include "../kselftest_harness.h"

static const char *device_bdf;

/*
 * Limit the number of MSIs enabled/disabled by the test regardless of the
 * number of MSIs the device itself supports, e.g. to avoid hitting IRTE limits.
 */
#define MAX_TEST_MSI 16U

FIXTURE(vfio_pci_device_test) {
	struct vfio_pci_device *device;
};

FIXTURE_SETUP(vfio_pci_device_test)
{
	self->device = vfio_pci_device_init(device_bdf, default_iommu_mode);
}

FIXTURE_TEARDOWN(vfio_pci_device_test)
{
	vfio_pci_device_cleanup(self->device);
}

#define read_pci_id_from_sysfs(_file) ({							\
	char __sysfs_path[PATH_MAX];								\
	char __buf[32];										\
	int __fd;										\
												\
	snprintf(__sysfs_path, PATH_MAX, "/sys/bus/pci/devices/%s/%s", device_bdf, _file);	\
	ASSERT_GT((__fd = open(__sysfs_path, O_RDONLY)), 0);					\
	ASSERT_GT(read(__fd, __buf, ARRAY_SIZE(__buf)), 0);					\
	ASSERT_EQ(0, close(__fd));								\
	(u16)strtoul(__buf, NULL, 0);								\
})

TEST_F(vfio_pci_device_test, config_space_read_write)
{
	u16 vendor, device;
	u16 command;

	/* Check that Vendor and Device match what the kernel reports. */
	vendor = read_pci_id_from_sysfs("vendor");
	device = read_pci_id_from_sysfs("device");
	ASSERT_TRUE(vfio_pci_device_match(self->device, vendor, device));

	printf("Vendor: %04x, Device: %04x\n", vendor, device);

	command = vfio_pci_config_readw(self->device, PCI_COMMAND);
	ASSERT_FALSE(command & PCI_COMMAND_MASTER);

	vfio_pci_config_writew(self->device, PCI_COMMAND, command | PCI_COMMAND_MASTER);
	command = vfio_pci_config_readw(self->device, PCI_COMMAND);
	ASSERT_TRUE(command & PCI_COMMAND_MASTER);
	printf("Enabled Bus Mastering (command: %04x)\n", command);

	vfio_pci_config_writew(self->device, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
	command = vfio_pci_config_readw(self->device, PCI_COMMAND);
	ASSERT_FALSE(command & PCI_COMMAND_MASTER);
	printf("Disabled Bus Mastering (command: %04x)\n", command);
}

TEST_F(vfio_pci_device_test, validate_bars)
{
	struct vfio_pci_bar *bar;
	int i;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		bar = &self->device->bars[i];

		if (!(bar->info.flags & VFIO_REGION_INFO_FLAG_MMAP)) {
			printf("BAR %d does not support mmap()\n", i);
			ASSERT_EQ(NULL, bar->vaddr);
			continue;
		}

		/*
		 * BARs that support mmap() should be automatically mapped by
		 * vfio_pci_device_init().
		 */
		ASSERT_NE(NULL, bar->vaddr);
		ASSERT_NE(0, bar->info.size);
		printf("BAR %d mapped at %p (size 0x%llx)\n", i, bar->vaddr, bar->info.size);
	}
}

FIXTURE(vfio_pci_irq_test) {
	struct vfio_pci_device *device;
};

FIXTURE_VARIANT(vfio_pci_irq_test) {
	int irq_index;
};

FIXTURE_VARIANT_ADD(vfio_pci_irq_test, msi) {
	.irq_index = VFIO_PCI_MSI_IRQ_INDEX,
};

FIXTURE_VARIANT_ADD(vfio_pci_irq_test, msix) {
	.irq_index = VFIO_PCI_MSIX_IRQ_INDEX,
};

FIXTURE_SETUP(vfio_pci_irq_test)
{
	self->device = vfio_pci_device_init(device_bdf, default_iommu_mode);
}

FIXTURE_TEARDOWN(vfio_pci_irq_test)
{
	vfio_pci_device_cleanup(self->device);
}

TEST_F(vfio_pci_irq_test, enable_trigger_disable)
{
	bool msix = variant->irq_index == VFIO_PCI_MSIX_IRQ_INDEX;
	int msi_eventfd;
	u32 count;
	u64 value;
	int i;

	if (msix)
		count = self->device->msix_info.count;
	else
		count = self->device->msi_info.count;

	count = min(count, MAX_TEST_MSI);

	if (!count)
		SKIP(return, "MSI%s: not supported\n", msix ? "-x" : "");

	vfio_pci_irq_enable(self->device, variant->irq_index, 0, count);
	printf("MSI%s: enabled %d interrupts\n", msix ? "-x" : "", count);

	for (i = 0; i < count; i++) {
		msi_eventfd = self->device->msi_eventfds[i];

		fcntl_set_nonblock(msi_eventfd);
		ASSERT_EQ(-1, read(msi_eventfd, &value, 8));
		ASSERT_EQ(EAGAIN, errno);

		vfio_pci_irq_trigger(self->device, variant->irq_index, i);

		ASSERT_EQ(8, read(msi_eventfd, &value, 8));
		ASSERT_EQ(1, value);
	}

	vfio_pci_irq_disable(self->device, variant->irq_index);
}

TEST_F(vfio_pci_device_test, reset)
{
	if (!(self->device->info.flags & VFIO_DEVICE_FLAGS_RESET))
		SKIP(return, "Device does not support reset\n");

	vfio_pci_device_reset(self->device);
}

int main(int argc, char *argv[])
{
	device_bdf = vfio_selftests_get_bdf(&argc, argv);
	return test_harness_run(argc, argv);
}
