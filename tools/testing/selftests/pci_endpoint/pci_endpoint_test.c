// SPDX-License-Identifier: GPL-2.0
/*
 * Kselftest for PCI Endpoint Subsystem
 *
 * Copyright (c) 2022 Samsung Electronics Co., Ltd.
 *             https://www.samsung.com
 * Author: Aman Gupta <aman1.gupta@samsung.com>
 *
 * Copyright (c) 2024, Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../../../../include/uapi/linux/pcitest.h"

#include "../kselftest_harness.h"

#define pci_ep_ioctl(cmd, arg)			\
({						\
	ret = ioctl(self->fd, cmd, arg);	\
	ret = ret < 0 ? -errno : 0;		\
})

static const char *test_device = "/dev/pci-endpoint-test.0";
static const unsigned long test_size[5] = { 1, 1024, 1025, 1024000, 1024001 };

FIXTURE(pci_ep_bar)
{
	int fd;
};

FIXTURE_SETUP(pci_ep_bar)
{
	self->fd = open(test_device, O_RDWR);

	ASSERT_NE(-1, self->fd) TH_LOG("Can't open PCI Endpoint Test device");
}

FIXTURE_TEARDOWN(pci_ep_bar)
{
	close(self->fd);
}

FIXTURE_VARIANT(pci_ep_bar)
{
	int barno;
};

FIXTURE_VARIANT_ADD(pci_ep_bar, BAR0) { .barno = 0 };
FIXTURE_VARIANT_ADD(pci_ep_bar, BAR1) { .barno = 1 };
FIXTURE_VARIANT_ADD(pci_ep_bar, BAR2) { .barno = 2 };
FIXTURE_VARIANT_ADD(pci_ep_bar, BAR3) { .barno = 3 };
FIXTURE_VARIANT_ADD(pci_ep_bar, BAR4) { .barno = 4 };
FIXTURE_VARIANT_ADD(pci_ep_bar, BAR5) { .barno = 5 };

TEST_F(pci_ep_bar, BAR_TEST)
{
	int ret;

	pci_ep_ioctl(PCITEST_BAR, variant->barno);
	EXPECT_FALSE(ret) TH_LOG("Test failed for BAR%d", variant->barno);
}

FIXTURE(pci_ep_basic)
{
	int fd;
};

FIXTURE_SETUP(pci_ep_basic)
{
	self->fd = open(test_device, O_RDWR);

	ASSERT_NE(-1, self->fd) TH_LOG("Can't open PCI Endpoint Test device");
}

FIXTURE_TEARDOWN(pci_ep_basic)
{
	close(self->fd);
}

TEST_F(pci_ep_basic, CONSECUTIVE_BAR_TEST)
{
	int ret;

	pci_ep_ioctl(PCITEST_BARS, 0);
	EXPECT_FALSE(ret) TH_LOG("Consecutive BAR test failed");
}

TEST_F(pci_ep_basic, LEGACY_IRQ_TEST)
{
	int ret;

	pci_ep_ioctl(PCITEST_SET_IRQTYPE, 0);
	ASSERT_EQ(0, ret) TH_LOG("Can't set Legacy IRQ type");

	pci_ep_ioctl(PCITEST_LEGACY_IRQ, 0);
	EXPECT_FALSE(ret) TH_LOG("Test failed for Legacy IRQ");
}

TEST_F(pci_ep_basic, MSI_TEST)
{
	int ret, i;

	pci_ep_ioctl(PCITEST_SET_IRQTYPE, 1);
	ASSERT_EQ(0, ret) TH_LOG("Can't set MSI IRQ type");

	for (i = 1; i <= 32; i++) {
		pci_ep_ioctl(PCITEST_MSI, i);
		EXPECT_FALSE(ret) TH_LOG("Test failed for MSI%d", i);
	}
}

TEST_F(pci_ep_basic, MSIX_TEST)
{
	int ret, i;

	pci_ep_ioctl(PCITEST_SET_IRQTYPE, 2);
	ASSERT_EQ(0, ret) TH_LOG("Can't set MSI-X IRQ type");

	for (i = 1; i <= 2048; i++) {
		pci_ep_ioctl(PCITEST_MSIX, i);
		EXPECT_FALSE(ret) TH_LOG("Test failed for MSI-X%d", i);
	}
}

FIXTURE(pci_ep_data_transfer)
{
	int fd;
};

FIXTURE_SETUP(pci_ep_data_transfer)
{
	self->fd = open(test_device, O_RDWR);

	ASSERT_NE(-1, self->fd) TH_LOG("Can't open PCI Endpoint Test device");
}

FIXTURE_TEARDOWN(pci_ep_data_transfer)
{
	close(self->fd);
}

FIXTURE_VARIANT(pci_ep_data_transfer)
{
	bool use_dma;
};

FIXTURE_VARIANT_ADD(pci_ep_data_transfer, memcpy)
{
	.use_dma = false,
};

FIXTURE_VARIANT_ADD(pci_ep_data_transfer, dma)
{
	.use_dma = true,
};

TEST_F(pci_ep_data_transfer, READ_TEST)
{
	struct pci_endpoint_test_xfer_param param = {};
	int ret, i;

	if (variant->use_dma)
		param.flags = PCITEST_FLAGS_USE_DMA;

	pci_ep_ioctl(PCITEST_SET_IRQTYPE, 1);
	ASSERT_EQ(0, ret) TH_LOG("Can't set MSI IRQ type");

	for (i = 0; i < ARRAY_SIZE(test_size); i++) {
		param.size = test_size[i];
		pci_ep_ioctl(PCITEST_READ, &param);
		EXPECT_FALSE(ret) TH_LOG("Test failed for size (%ld)",
					 test_size[i]);
	}
}

TEST_F(pci_ep_data_transfer, WRITE_TEST)
{
	struct pci_endpoint_test_xfer_param param = {};
	int ret, i;

	if (variant->use_dma)
		param.flags = PCITEST_FLAGS_USE_DMA;

	pci_ep_ioctl(PCITEST_SET_IRQTYPE, 1);
	ASSERT_EQ(0, ret) TH_LOG("Can't set MSI IRQ type");

	for (i = 0; i < ARRAY_SIZE(test_size); i++) {
		param.size = test_size[i];
		pci_ep_ioctl(PCITEST_WRITE, &param);
		EXPECT_FALSE(ret) TH_LOG("Test failed for size (%ld)",
					 test_size[i]);
	}
}

TEST_F(pci_ep_data_transfer, COPY_TEST)
{
	struct pci_endpoint_test_xfer_param param = {};
	int ret, i;

	if (variant->use_dma)
		param.flags = PCITEST_FLAGS_USE_DMA;

	pci_ep_ioctl(PCITEST_SET_IRQTYPE, 1);
	ASSERT_EQ(0, ret) TH_LOG("Can't set MSI IRQ type");

	for (i = 0; i < ARRAY_SIZE(test_size); i++) {
		param.size = test_size[i];
		pci_ep_ioctl(PCITEST_COPY, &param);
		EXPECT_FALSE(ret) TH_LOG("Test failed for size (%ld)",
					 test_size[i]);
	}
}
TEST_HARNESS_MAIN
