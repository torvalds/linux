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
#include "kselftest.h"

static void stats_test(int stats_fd)
{
	ssize_t ret;
	int i;
	size_t size_desc;
	size_t size_data = 0;
	struct kvm_stats_header header;
	char *id;
	struct kvm_stats_desc *stats_desc;
	u64 *stats_data;
	struct kvm_stats_desc *pdesc;
	u32 type, unit, base;

	/* Read kvm stats header */
	read_stats_header(stats_fd, &header);

	size_desc = get_stats_descriptor_size(&header);

	/* Read kvm stats id string */
	id = malloc(header.name_size);
	TEST_ASSERT(id, "Allocate memory for id string");

	ret = read(stats_fd, id, header.name_size);
	TEST_ASSERT(ret == header.name_size, "Read id string");

	/* Check id string, that should start with "kvm" */
	TEST_ASSERT(!strncmp(id, "kvm", 3) && strlen(id) < header.name_size,
		    "Invalid KVM stats type, id: %s", id);

	/* Sanity check for other fields in header */
	if (header.num_desc == 0) {
		ksft_print_msg("No KVM stats defined!\n");
		return;
	}
	/*
	 * The descriptor and data offsets must be valid, they must not overlap
	 * the header, and the descriptor and data blocks must not overlap each
	 * other.  Note, the data block is rechecked after its size is known.
	 */
	TEST_ASSERT(header.desc_offset && header.desc_offset >= sizeof(header) &&
		    header.data_offset && header.data_offset >= sizeof(header),
		    "Invalid offset fields in header");

	TEST_ASSERT(header.desc_offset > header.data_offset ||
		    (header.desc_offset + size_desc * header.num_desc <= header.data_offset),
		    "Descriptor block is overlapped with data block");

	/* Read kvm stats descriptors */
	stats_desc = read_stats_descriptors(stats_fd, &header);

	/* Sanity check for fields in descriptors */
	for (i = 0; i < header.num_desc; ++i) {
		pdesc = get_stats_descriptor(stats_desc, i, &header);
		type = pdesc->flags & KVM_STATS_TYPE_MASK;
		unit = pdesc->flags & KVM_STATS_UNIT_MASK;
		base = pdesc->flags & KVM_STATS_BASE_MASK;

		/* Check name string */
		TEST_ASSERT(strlen(pdesc->name) < header.name_size,
			    "KVM stats name (index: %d) too long", i);

		/* Check type,unit,base boundaries */
		TEST_ASSERT(type <= KVM_STATS_TYPE_MAX,
			    "Unknown KVM stats (%s) type: %u", pdesc->name, type);
		TEST_ASSERT(unit <= KVM_STATS_UNIT_MAX,
			    "Unknown KVM stats (%s) unit: %u", pdesc->name, unit);
		TEST_ASSERT(base <= KVM_STATS_BASE_MAX,
			    "Unknown KVM stats (%s) base: %u", pdesc->name, base);

		/*
		 * Check exponent for stats unit
		 * Exponent for counter should be greater than or equal to 0
		 * Exponent for unit bytes should be greater than or equal to 0
		 * Exponent for unit seconds should be less than or equal to 0
		 * Exponent for unit clock cycles should be greater than or
		 * equal to 0
		 * Exponent for unit boolean should be 0
		 */
		switch (pdesc->flags & KVM_STATS_UNIT_MASK) {
		case KVM_STATS_UNIT_NONE:
		case KVM_STATS_UNIT_BYTES:
		case KVM_STATS_UNIT_CYCLES:
			TEST_ASSERT(pdesc->exponent >= 0,
				    "Unsupported KVM stats (%s) exponent: %i",
				    pdesc->name, pdesc->exponent);
			break;
		case KVM_STATS_UNIT_SECONDS:
			TEST_ASSERT(pdesc->exponent <= 0,
				    "Unsupported KVM stats (%s) exponent: %i",
				    pdesc->name, pdesc->exponent);
			break;
		case KVM_STATS_UNIT_BOOLEAN:
			TEST_ASSERT(pdesc->exponent == 0,
				    "Unsupported KVM stats (%s) exponent: %d",
				    pdesc->name, pdesc->exponent);
			break;
		}

		/* Check size field, which should not be zero */
		TEST_ASSERT(pdesc->size,
			    "KVM descriptor(%s) with size of 0", pdesc->name);
		/* Check bucket_size field */
		switch (pdesc->flags & KVM_STATS_TYPE_MASK) {
		case KVM_STATS_TYPE_LINEAR_HIST:
			TEST_ASSERT(pdesc->bucket_size,
				    "Bucket size of Linear Histogram stats (%s) is zero",
				    pdesc->name);
			break;
		default:
			TEST_ASSERT(!pdesc->bucket_size,
				    "Bucket size of stats (%s) is not zero",
				    pdesc->name);
		}
		size_data = max(size_data, pdesc->offset + pdesc->size * sizeof(*stats_data));
	}

	/*
	 * Now that the size of the data block is known, verify the data block
	 * doesn't overlap the descriptor block.
	 */
	TEST_ASSERT(header.data_offset >= header.desc_offset ||
		    header.data_offset + size_data <= header.desc_offset,
		    "Data block is overlapped with Descriptor block");

	/* Check validity of all stats data size */
	TEST_ASSERT(size_data >= header.num_desc * sizeof(*stats_data),
		    "Data size is not correct");

	/* Allocate memory for stats data */
	stats_data = malloc(size_data);
	TEST_ASSERT(stats_data, "Allocate memory for stats data");
	/* Read kvm stats data as a bulk */
	ret = pread(stats_fd, stats_data, size_data, header.data_offset);
	TEST_ASSERT(ret == size_data, "Read KVM stats data");
	/* Read kvm stats data one by one */
	for (i = 0; i < header.num_desc; ++i) {
		pdesc = get_stats_descriptor(stats_desc, i, &header);
		read_stat_data(stats_fd, &header, pdesc, stats_data,
			       pdesc->size);
	}

	free(stats_data);
	free(stats_desc);
	free(id);
}


static void vm_stats_test(struct kvm_vm *vm)
{
	int stats_fd = vm_get_stats_fd(vm);

	stats_test(stats_fd);
	close(stats_fd);
	TEST_ASSERT(fcntl(stats_fd, F_GETFD) == -1, "Stats fd not freed");
}

static void vcpu_stats_test(struct kvm_vcpu *vcpu)
{
	int stats_fd = vcpu_get_stats_fd(vcpu);

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
	struct kvm_vcpu **vcpus;
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

	ksft_print_header();

	/* Check the extension for binary stats */
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_BINARY_STATS_FD));

	ksft_set_plan(max_vm);

	/* Create VMs and VCPUs */
	vms = malloc(sizeof(vms[0]) * max_vm);
	TEST_ASSERT(vms, "Allocate memory for storing VM pointers");

	vcpus = malloc(sizeof(struct kvm_vcpu *) * max_vm * max_vcpu);
	TEST_ASSERT(vcpus, "Allocate memory for storing vCPU pointers");

	for (i = 0; i < max_vm; ++i) {
		vms[i] = vm_create_barebones();
		for (j = 0; j < max_vcpu; ++j)
			vcpus[i * max_vcpu + j] = __vm_vcpu_add(vms[i], j);
	}

	/* Check stats read for every VM and VCPU */
	for (i = 0; i < max_vm; ++i) {
		vm_stats_test(vms[i]);
		for (j = 0; j < max_vcpu; ++j)
			vcpu_stats_test(vcpus[i * max_vcpu + j]);
		ksft_test_result_pass("vm%i\n", i);
	}

	for (i = 0; i < max_vm; ++i)
		kvm_vm_free(vms[i]);
	free(vms);

	ksft_finished();	/* Print results and exit() accordingly */
}
