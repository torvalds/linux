// SPDX-License-Identifier: GPL-2.0-only
/*
 * kvm_binary_stats_test
 *
 * Copyright (C) 2021, Google LLC.
 *
 * Test the fd-based interface for KVM statistics.
 */

#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "test_util.h"

#include "kvm_util.h"
#include "asm/kvm.h"
#include "linux/kvm.h"

static void stats_test(int stats_fd)
{
	ssize_t ret;
	int i;
	size_t size_desc;
	size_t size_data = 0;
	struct kvm_stats_header *header;
	char *id;
	struct kvm_stats_desc *stats_desc;
	u64 *stats_data;
	struct kvm_stats_desc *pdesc;

	/* Read kvm stats header */
	header = malloc(sizeof(*header));
	TEST_ASSERT(header, "Allocate memory for stats header");

	ret = read(stats_fd, header, sizeof(*header));
	TEST_ASSERT(ret == sizeof(*header), "Read stats header");
	size_desc = sizeof(*stats_desc) + header->name_size;

	/* Read kvm stats id string */
	id = malloc(header->name_size);
	TEST_ASSERT(id, "Allocate memory for id string");
	ret = read(stats_fd, id, header->name_size);
	TEST_ASSERT(ret == header->name_size, "Read id string");

	/* Check id string, that should start with "kvm" */
	TEST_ASSERT(!strncmp(id, "kvm", 3) && strlen(id) < header->name_size,
				"Invalid KVM stats type, id: %s", id);

	/* Sanity check for other fields in header */
	if (header->num_desc == 0) {
		printf("No KVM stats defined!");
		return;
	}
	/* Check overlap */
	TEST_ASSERT(header->desc_offset > 0 && header->data_offset > 0
			&& header->desc_offset >= sizeof(*header)
			&& header->data_offset >= sizeof(*header),
			"Invalid offset fields in header");
	TEST_ASSERT(header->desc_offset > header->data_offset ||
			(header->desc_offset + size_desc * header->num_desc <=
							header->data_offset),
			"Descriptor block is overlapped with data block");

	/* Allocate memory for stats descriptors */
	stats_desc = calloc(header->num_desc, size_desc);
	TEST_ASSERT(stats_desc, "Allocate memory for stats descriptors");
	/* Read kvm stats descriptors */
	ret = pread(stats_fd, stats_desc,
			size_desc * header->num_desc, header->desc_offset);
	TEST_ASSERT(ret == size_desc * header->num_desc,
			"Read KVM stats descriptors");

	/* Sanity check for fields in descriptors */
	for (i = 0; i < header->num_desc; ++i) {
		pdesc = (void *)stats_desc + i * size_desc;
		/* Check type,unit,base boundaries */
		TEST_ASSERT((pdesc->flags & KVM_STATS_TYPE_MASK)
				<= KVM_STATS_TYPE_MAX, "Unknown KVM stats type");
		TEST_ASSERT((pdesc->flags & KVM_STATS_UNIT_MASK)
				<= KVM_STATS_UNIT_MAX, "Unknown KVM stats unit");
		TEST_ASSERT((pdesc->flags & KVM_STATS_BASE_MASK)
				<= KVM_STATS_BASE_MAX, "Unknown KVM stats base");
		/* Check exponent for stats unit
		 * Exponent for counter should be greater than or equal to 0
		 * Exponent for unit bytes should be greater than or equal to 0
		 * Exponent for unit seconds should be less than or equal to 0
		 * Exponent for unit clock cycles should be greater than or
		 * equal to 0
		 */
		switch (pdesc->flags & KVM_STATS_UNIT_MASK) {
		case KVM_STATS_UNIT_NONE:
		case KVM_STATS_UNIT_BYTES:
		case KVM_STATS_UNIT_CYCLES:
			TEST_ASSERT(pdesc->exponent >= 0,
					"Unsupported KVM stats unit");
			break;
		case KVM_STATS_UNIT_SECONDS:
			TEST_ASSERT(pdesc->exponent <= 0,
					"Unsupported KVM stats unit");
			break;
		}
		/* Check name string */
		TEST_ASSERT(strlen(pdesc->name) < header->name_size,
				"KVM stats name(%s) too long", pdesc->name);
		/* Check size field, which should not be zero */
		TEST_ASSERT(pdesc->size, "KVM descriptor(%s) with size of 0",
				pdesc->name);
		size_data += pdesc->size * sizeof(*stats_data);
	}
	/* Check overlap */
	TEST_ASSERT(header->data_offset >= header->desc_offset
		|| header->data_offset + size_data <= header->desc_offset,
		"Data block is overlapped with Descriptor block");
	/* Check validity of all stats data size */
	TEST_ASSERT(size_data >= header->num_desc * sizeof(*stats_data),
			"Data size is not correct");
	/* Check stats offset */
	for (i = 0; i < header->num_desc; ++i) {
		pdesc = (void *)stats_desc + i * size_desc;
		TEST_ASSERT(pdesc->offset < size_data,
			"Invalid offset (%u) for stats: %s",
			pdesc->offset, pdesc->name);
	}

	/* Allocate memory for stats data */
	stats_data = malloc(size_data);
	TEST_ASSERT(stats_data, "Allocate memory for stats data");
	/* Read kvm stats data as a bulk */
	ret = pread(stats_fd, stats_data, size_data, header->data_offset);
	TEST_ASSERT(ret == size_data, "Read KVM stats data");
	/* Read kvm stats data one by one */
	size_data = 0;
	for (i = 0; i < header->num_desc; ++i) {
		pdesc = (void *)stats_desc + i * size_desc;
		ret = pread(stats_fd, stats_data,
				pdesc->size * sizeof(*stats_data),
				header->data_offset + size_data);
		TEST_ASSERT(ret == pdesc->size * sizeof(*stats_data),
				"Read data of KVM stats: %s", pdesc->name);
		size_data += pdesc->size * sizeof(*stats_data);
	}

	free(stats_data);
	free(stats_desc);
	free(id);
	free(header);
}


static void vm_stats_test(struct kvm_vm *vm)
{
	int stats_fd;

	/* Get fd for VM stats */
	stats_fd = vm_get_stats_fd(vm);
	TEST_ASSERT(stats_fd >= 0, "Get VM stats fd");

	stats_test(stats_fd);
	close(stats_fd);
	TEST_ASSERT(fcntl(stats_fd, F_GETFD) == -1, "Stats fd not freed");
}

static void vcpu_stats_test(struct kvm_vm *vm, int vcpu_id)
{
	int stats_fd;

	/* Get fd for VCPU stats */
	stats_fd = vcpu_get_stats_fd(vm, vcpu_id);
	TEST_ASSERT(stats_fd >= 0, "Get VCPU stats fd");

	stats_test(stats_fd);
	close(stats_fd);
	TEST_ASSERT(fcntl(stats_fd, F_GETFD) == -1, "Stats fd not freed");
}

#define DEFAULT_NUM_VM		4
#define DEFAULT_NUM_VCPU	4

/*
 * Usage: kvm_bin_form_stats [#vm] [#vcpu]
 * The first parameter #vm set the number of VMs being created.
 * The second parameter #vcpu set the number of VCPUs being created.
 * By default, DEFAULT_NUM_VM VM and DEFAULT_NUM_VCPU VCPU for the VM would be
 * created for testing.
 */

int main(int argc, char *argv[])
{
	int i, j;
	struct kvm_vm **vms;
	int max_vm = DEFAULT_NUM_VM;
	int max_vcpu = DEFAULT_NUM_VCPU;

	/* Get the number of VMs and VCPUs that would be created for testing. */
	if (argc > 1) {
		max_vm = strtol(argv[1], NULL, 0);
		if (max_vm <= 0)
			max_vm = DEFAULT_NUM_VM;
	}
	if (argc > 2) {
		max_vcpu = strtol(argv[2], NULL, 0);
		if (max_vcpu <= 0)
			max_vcpu = DEFAULT_NUM_VCPU;
	}

	/* Check the extension for binary stats */
	if (kvm_check_cap(KVM_CAP_BINARY_STATS_FD) <= 0) {
		print_skip("Binary form statistics interface is not supported");
		exit(KSFT_SKIP);
	}

	/* Create VMs and VCPUs */
	vms = malloc(sizeof(vms[0]) * max_vm);
	TEST_ASSERT(vms, "Allocate memory for storing VM pointers");
	for (i = 0; i < max_vm; ++i) {
		vms[i] = vm_create(VM_MODE_DEFAULT,
				DEFAULT_GUEST_PHY_PAGES, O_RDWR);
		for (j = 0; j < max_vcpu; ++j)
			vm_vcpu_add(vms[i], j);
	}

	/* Check stats read for every VM and VCPU */
	for (i = 0; i < max_vm; ++i) {
		vm_stats_test(vms[i]);
		for (j = 0; j < max_vcpu; ++j)
			vcpu_stats_test(vms[i], j);
	}

	for (i = 0; i < max_vm; ++i)
		kvm_vm_free(vms[i]);
	free(vms);
	return 0;
}
