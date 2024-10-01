// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <limits.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <sys/sysmacros.h>
#define __USE_GNU
#include <sys/mman.h>

#include <linux/gunyah.h>

#define DEFAULT_GUEST_BASE	0x80000000
#define DEFAULT_GUEST_SIZE	0x6400000 /* 100 MiB */
#define DEFAULT_DTB_OFFSET	0x45f0000 /* 70MiB - 64 KiB */
#define DEFAULT_RAMDISK_OFFSET	0x4600000 /* 70MiB */

struct vm_config {
	int image_fd;
	int dtb_fd;
	int ramdisk_fd;

	uint64_t guest_base;
	uint64_t guest_size;

	off_t image_size;
	uint64_t dtb_offset;
	off_t dtb_size;
	uint64_t ramdisk_offset;
	off_t ramdisk_size;
};

static struct option options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "image", required_argument, NULL, 'i' },
	{ "dtb", required_argument, NULL, 'd' },
	{ "ramdisk", optional_argument, NULL, 'r' },
	{ "base", optional_argument, NULL, 'B' },
	{ "size", optional_argument, NULL, 'S' },
	{ "dtb_offset", optional_argument, NULL, 'D' },
	{ "ramdisk_offset", optional_argument, NULL, 'R' },
	{ }
};

static void print_help(char *cmd)
{
	printf("gunyah_vmm, a sample tool to launch Gunyah VMs\n"
	       "Usage: %s <options>\n"
	       "       --help,    -h  this menu\n"
	       "       --image,   -i <image> VM image file to load (e.g. a kernel Image) [Required]\n"
	       "       --dtb,     -d <dtb>   Devicetree file to load [Required]\n"
	       "       --ramdisk, -r <ramdisk>  Ramdisk file to load\n"
	       "       --base,    -B <address>  Set the base address of guest's memory [Default: 0x%08x]\n"
	       "       --size,    -S <number>   The number of bytes large to make the guest's memory [Default: 0x%08x]\n"
	       "        --dtb_offset,  -D <number>  Offset into guest memory to load the DTB [Default: 0x%08x]\n"
	       "        --ramdisk_offset, -R <number>  Offset into guest memory to load a ramdisk [Default: 0x%08x]\n"
	       , cmd, DEFAULT_GUEST_BASE, DEFAULT_GUEST_SIZE,
	       DEFAULT_DTB_OFFSET, DEFAULT_RAMDISK_OFFSET);
}

int main(int argc, char **argv)
{
	int gunyah_fd, vm_fd, guest_fd;
	struct gh_userspace_memory_region guest_mem_desc = { 0 };
	struct gh_vm_dtb_config dtb_config = { 0 };
	char *guest_mem;
	struct vm_config config = {
		/* Defaults good enough to boot static kernel and a basic ramdisk */
		.image_fd = -1,
		.dtb_fd = -1,
		.ramdisk_fd = -1,
		.guest_base = DEFAULT_GUEST_BASE,
		.guest_size = DEFAULT_GUEST_SIZE,
		.dtb_offset = DEFAULT_DTB_OFFSET,
		.ramdisk_offset = DEFAULT_RAMDISK_OFFSET,
	};
	struct stat st;
	int opt, optidx, ret = 0;
	long l;

	while ((opt = getopt_long(argc, argv, "hi:d:r:B:S:D:R:c:", options, &optidx)) != -1) {
		switch (opt) {
		case 'i':
			config.image_fd = open(optarg, O_RDONLY | O_CLOEXEC);
			if (config.image_fd < 0) {
				perror("Failed to open image");
				return -1;
			}
			if (stat(optarg, &st) < 0) {
				perror("Failed to stat image");
				return -1;
			}
			config.image_size = st.st_size;
			break;
		case 'd':
			config.dtb_fd = open(optarg, O_RDONLY | O_CLOEXEC);
			if (config.dtb_fd < 0) {
				perror("Failed to open dtb");
				return -1;
			}
			if (stat(optarg, &st) < 0) {
				perror("Failed to stat dtb");
				return -1;
			}
			config.dtb_size = st.st_size;
			break;
		case 'r':
			config.ramdisk_fd = open(optarg, O_RDONLY | O_CLOEXEC);
			if (config.ramdisk_fd < 0) {
				perror("Failed to open ramdisk");
				return -1;
			}
			if (stat(optarg, &st) < 0) {
				perror("Failed to stat ramdisk");
				return -1;
			}
			config.ramdisk_size = st.st_size;
			break;
		case 'B':
			l = strtol(optarg, NULL, 0);
			if (l == LONG_MIN) {
				perror("Failed to parse base address");
				return -1;
			}
			config.guest_base = l;
			break;
		case 'S':
			l = strtol(optarg, NULL, 0);
			if (l == LONG_MIN) {
				perror("Failed to parse memory size");
				return -1;
			}
			config.guest_size = l;
			break;
		case 'D':
			l = strtol(optarg, NULL, 0);
			if (l == LONG_MIN) {
				perror("Failed to parse dtb offset");
				return -1;
			}
			config.dtb_offset = l;
			break;
		case 'R':
			l = strtol(optarg, NULL, 0);
			if (l == LONG_MIN) {
				perror("Failed to parse ramdisk offset");
				return -1;
			}
			config.ramdisk_offset = l;
			break;
		case 'h':
			print_help(argv[0]);
			return 0;
		default:
			print_help(argv[0]);
			return -1;
		}
	}

	if (config.image_fd == -1 || config.dtb_fd == -1) {
		print_help(argv[0]);
		return -1;
	}

	if (config.image_size > config.guest_size) {
		fprintf(stderr, "Image size puts it outside guest memory. Make image smaller or increase guest memory size.\n");
		return -1;
	}

	if (config.dtb_offset + config.dtb_size > config.guest_size) {
		fprintf(stderr, "DTB offset and size puts it outside guest memory. Make dtb smaller or increase guest memory size.\n");
		return -1;
	}

	if (config.ramdisk_fd == -1 &&
		config.ramdisk_offset + config.ramdisk_size > config.guest_size) {
		fprintf(stderr, "Ramdisk offset and size puts it outside guest memory. Make ramdisk smaller or increase guest memory size.\n");
		return -1;
	}

	gunyah_fd = open("/dev/gunyah", O_RDWR | O_CLOEXEC);
	if (gunyah_fd < 0) {
		perror("Failed to open /dev/gunyah");
		return -1;
	}

	vm_fd = ioctl(gunyah_fd, GH_CREATE_VM, 0);
	if (vm_fd < 0) {
		perror("Failed to create vm");
		return -1;
	}

	guest_fd = memfd_create("guest_memory", MFD_CLOEXEC);
	if (guest_fd < 0) {
		perror("Failed to create guest memfd");
		return -1;
	}

	if (ftruncate(guest_fd, config.guest_size) < 0) {
		perror("Failed to grow guest memory");
		return -1;
	}

	guest_mem = mmap(NULL, config.guest_size, PROT_READ | PROT_WRITE, MAP_SHARED, guest_fd, 0);
	if (guest_mem == MAP_FAILED) {
		perror("Not enough memory");
		return -1;
	}

	if (read(config.image_fd, guest_mem, config.image_size) < 0) {
		perror("Failed to read image into guest memory");
		return -1;
	}

	if (read(config.dtb_fd, guest_mem + config.dtb_offset, config.dtb_size) < 0) {
		perror("Failed to read dtb into guest memory");
		return -1;
	}

	if (config.ramdisk_fd > 0 &&
		read(config.ramdisk_fd, guest_mem + config.ramdisk_offset,
			config.ramdisk_size) < 0) {
		perror("Failed to read ramdisk into guest memory");
		return -1;
	}

	guest_mem_desc.label = 0;
	guest_mem_desc.flags = GH_MEM_ALLOW_READ | GH_MEM_ALLOW_WRITE | GH_MEM_ALLOW_EXEC;
	guest_mem_desc.guest_phys_addr = config.guest_base;
	guest_mem_desc.memory_size = config.guest_size;
	guest_mem_desc.userspace_addr = (__u64)guest_mem;

	if (ioctl(vm_fd, GH_VM_SET_USER_MEM_REGION, &guest_mem_desc) < 0) {
		perror("Failed to register guest memory with VM");
		return -1;
	}

	dtb_config.guest_phys_addr = config.guest_base + config.dtb_offset;
	dtb_config.size = config.dtb_size;
	if (ioctl(vm_fd, GH_VM_SET_DTB_CONFIG, &dtb_config) < 0) {
		perror("Failed to set DTB configuration for VM");
		return -1;
	}

	ret = ioctl(vm_fd, GH_VM_START);
	if (ret) {
		perror("GH_VM_START failed");
		return -1;
	}

	while (1)
		pause();

	return 0;
}
