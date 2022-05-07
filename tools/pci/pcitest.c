// SPDX-License-Identifier: GPL-2.0-only
/**
 * Userspace PCI Endpoint Test Module
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/pcitest.h>

#define BILLION 1E9

static char *result[] = { "NOT OKAY", "OKAY" };
static char *irq[] = { "LEGACY", "MSI", "MSI-X" };

struct pci_test {
	char		*device;
	char		barnum;
	bool		legacyirq;
	unsigned int	msinum;
	unsigned int	msixnum;
	int		irqtype;
	bool		set_irqtype;
	bool		get_irqtype;
	bool		clear_irq;
	bool		read;
	bool		write;
	bool		copy;
	unsigned long	size;
	bool		use_dma;
};

static int run_test(struct pci_test *test)
{
	struct pci_endpoint_test_xfer_param param;
	int ret = -EINVAL;
	int fd;

	fd = open(test->device, O_RDWR);
	if (fd < 0) {
		perror("can't open PCI Endpoint Test device");
		return -ENODEV;
	}

	if (test->barnum >= 0 && test->barnum <= 5) {
		ret = ioctl(fd, PCITEST_BAR, test->barnum);
		fprintf(stdout, "BAR%d:\t\t", test->barnum);
		if (ret < 0)
			fprintf(stdout, "TEST FAILED\n");
		else
			fprintf(stdout, "%s\n", result[ret]);
	}

	if (test->set_irqtype) {
		ret = ioctl(fd, PCITEST_SET_IRQTYPE, test->irqtype);
		fprintf(stdout, "SET IRQ TYPE TO %s:\t\t", irq[test->irqtype]);
		if (ret < 0)
			fprintf(stdout, "FAILED\n");
		else
			fprintf(stdout, "%s\n", result[ret]);
	}

	if (test->get_irqtype) {
		ret = ioctl(fd, PCITEST_GET_IRQTYPE);
		fprintf(stdout, "GET IRQ TYPE:\t\t");
		if (ret < 0)
			fprintf(stdout, "FAILED\n");
		else
			fprintf(stdout, "%s\n", irq[ret]);
	}

	if (test->clear_irq) {
		ret = ioctl(fd, PCITEST_CLEAR_IRQ);
		fprintf(stdout, "CLEAR IRQ:\t\t");
		if (ret < 0)
			fprintf(stdout, "FAILED\n");
		else
			fprintf(stdout, "%s\n", result[ret]);
	}

	if (test->legacyirq) {
		ret = ioctl(fd, PCITEST_LEGACY_IRQ, 0);
		fprintf(stdout, "LEGACY IRQ:\t");
		if (ret < 0)
			fprintf(stdout, "TEST FAILED\n");
		else
			fprintf(stdout, "%s\n", result[ret]);
	}

	if (test->msinum > 0 && test->msinum <= 32) {
		ret = ioctl(fd, PCITEST_MSI, test->msinum);
		fprintf(stdout, "MSI%d:\t\t", test->msinum);
		if (ret < 0)
			fprintf(stdout, "TEST FAILED\n");
		else
			fprintf(stdout, "%s\n", result[ret]);
	}

	if (test->msixnum > 0 && test->msixnum <= 2048) {
		ret = ioctl(fd, PCITEST_MSIX, test->msixnum);
		fprintf(stdout, "MSI-X%d:\t\t", test->msixnum);
		if (ret < 0)
			fprintf(stdout, "TEST FAILED\n");
		else
			fprintf(stdout, "%s\n", result[ret]);
	}

	if (test->write) {
		param.size = test->size;
		if (test->use_dma)
			param.flags = PCITEST_FLAGS_USE_DMA;
		ret = ioctl(fd, PCITEST_WRITE, &param);
		fprintf(stdout, "WRITE (%7ld bytes):\t\t", test->size);
		if (ret < 0)
			fprintf(stdout, "TEST FAILED\n");
		else
			fprintf(stdout, "%s\n", result[ret]);
	}

	if (test->read) {
		param.size = test->size;
		if (test->use_dma)
			param.flags = PCITEST_FLAGS_USE_DMA;
		ret = ioctl(fd, PCITEST_READ, &param);
		fprintf(stdout, "READ (%7ld bytes):\t\t", test->size);
		if (ret < 0)
			fprintf(stdout, "TEST FAILED\n");
		else
			fprintf(stdout, "%s\n", result[ret]);
	}

	if (test->copy) {
		param.size = test->size;
		if (test->use_dma)
			param.flags = PCITEST_FLAGS_USE_DMA;
		ret = ioctl(fd, PCITEST_COPY, &param);
		fprintf(stdout, "COPY (%7ld bytes):\t\t", test->size);
		if (ret < 0)
			fprintf(stdout, "TEST FAILED\n");
		else
			fprintf(stdout, "%s\n", result[ret]);
	}

	fflush(stdout);
	close(fd);
	return (ret < 0) ? ret : 1 - ret; /* return 0 if test succeeded */
}

int main(int argc, char **argv)
{
	int c;
	struct pci_test *test;

	test = calloc(1, sizeof(*test));
	if (!test) {
		perror("Fail to allocate memory for pci_test\n");
		return -ENOMEM;
	}

	/* since '0' is a valid BAR number, initialize it to -1 */
	test->barnum = -1;

	/* set default size as 100KB */
	test->size = 0x19000;

	/* set default endpoint device */
	test->device = "/dev/pci-endpoint-test.0";

	while ((c = getopt(argc, argv, "D:b:m:x:i:deIlhrwcs:")) != EOF)
	switch (c) {
	case 'D':
		test->device = optarg;
		continue;
	case 'b':
		test->barnum = atoi(optarg);
		if (test->barnum < 0 || test->barnum > 5)
			goto usage;
		continue;
	case 'l':
		test->legacyirq = true;
		continue;
	case 'm':
		test->msinum = atoi(optarg);
		if (test->msinum < 1 || test->msinum > 32)
			goto usage;
		continue;
	case 'x':
		test->msixnum = atoi(optarg);
		if (test->msixnum < 1 || test->msixnum > 2048)
			goto usage;
		continue;
	case 'i':
		test->irqtype = atoi(optarg);
		if (test->irqtype < 0 || test->irqtype > 2)
			goto usage;
		test->set_irqtype = true;
		continue;
	case 'I':
		test->get_irqtype = true;
		continue;
	case 'r':
		test->read = true;
		continue;
	case 'w':
		test->write = true;
		continue;
	case 'c':
		test->copy = true;
		continue;
	case 'e':
		test->clear_irq = true;
		continue;
	case 's':
		test->size = strtoul(optarg, NULL, 0);
		continue;
	case 'd':
		test->use_dma = true;
		continue;
	case 'h':
	default:
usage:
		fprintf(stderr,
			"usage: %s [options]\n"
			"Options:\n"
			"\t-D <dev>		PCI endpoint test device {default: /dev/pci-endpoint-test.0}\n"
			"\t-b <bar num>		BAR test (bar number between 0..5)\n"
			"\t-m <msi num>		MSI test (msi number between 1..32)\n"
			"\t-x <msix num>	\tMSI-X test (msix number between 1..2048)\n"
			"\t-i <irq type>	\tSet IRQ type (0 - Legacy, 1 - MSI, 2 - MSI-X)\n"
			"\t-e			Clear IRQ\n"
			"\t-I			Get current IRQ type configured\n"
			"\t-d			Use DMA\n"
			"\t-l			Legacy IRQ test\n"
			"\t-r			Read buffer test\n"
			"\t-w			Write buffer test\n"
			"\t-c			Copy buffer test\n"
			"\t-s <size>		Size of buffer {default: 100KB}\n"
			"\t-h			Print this help message\n",
			argv[0]);
		return -EINVAL;
	}

	return run_test(test);
}
