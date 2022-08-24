// SPDX-License-Identifier: GPL-2.0
/*
 * vgic init sequence tests
 *
 * Copyright (C) 2020, Red Hat, Inc.
 */
#define _GNU_SOURCE
#include <linux/kernel.h>
#include <sys/syscall.h>
#include <asm/kvm.h>
#include <asm/kvm_para.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vgic.h"

#define NR_VCPUS		4

#define REG_OFFSET(vcpu, offset) (((uint64_t)vcpu << 32) | offset)

#define GICR_TYPER 0x8

#define VGIC_DEV_IS_V2(_d) ((_d) == KVM_DEV_TYPE_ARM_VGIC_V2)
#define VGIC_DEV_IS_V3(_d) ((_d) == KVM_DEV_TYPE_ARM_VGIC_V3)

struct vm_gic {
	struct kvm_vm *vm;
	int gic_fd;
	uint32_t gic_dev_type;
};

static uint64_t max_phys_size;

/*
 * Helpers to access a redistributor register and verify the ioctl() failed or
 * succeeded as expected, and provided the correct value on success.
 */
static void v3_redist_reg_get_errno(int gicv3_fd, int vcpu, int offset,
				    int want, const char *msg)
{
	uint32_t ignored_val;
	int ret = __kvm_device_attr_get(gicv3_fd, KVM_DEV_ARM_VGIC_GRP_REDIST_REGS,
					REG_OFFSET(vcpu, offset), &ignored_val);

	TEST_ASSERT(ret && errno == want, "%s; want errno = %d", msg, want);
}

static void v3_redist_reg_get(int gicv3_fd, int vcpu, int offset, uint32_t want,
			      const char *msg)
{
	uint32_t val;

	kvm_device_attr_get(gicv3_fd, KVM_DEV_ARM_VGIC_GRP_REDIST_REGS,
			    REG_OFFSET(vcpu, offset), &val);
	TEST_ASSERT(val == want, "%s; want '0x%x', got '0x%x'", msg, want, val);
}

/* dummy guest code */
static void guest_code(void)
{
	GUEST_SYNC(0);
	GUEST_SYNC(1);
	GUEST_SYNC(2);
	GUEST_DONE();
}

/* we don't want to assert on run execution, hence that helper */
static int run_vcpu(struct kvm_vcpu *vcpu)
{
	ucall_init(vcpu->vm, NULL);

	return __vcpu_run(vcpu) ? -errno : 0;
}

static struct vm_gic vm_gic_create_with_vcpus(uint32_t gic_dev_type,
					      uint32_t nr_vcpus,
					      struct kvm_vcpu *vcpus[])
{
	struct vm_gic v;

	v.gic_dev_type = gic_dev_type;
	v.vm = vm_create_with_vcpus(nr_vcpus, guest_code, vcpus);
	v.gic_fd = kvm_create_device(v.vm, gic_dev_type);

	return v;
}

static void vm_gic_destroy(struct vm_gic *v)
{
	close(v->gic_fd);
	kvm_vm_free(v->vm);
}

struct vgic_region_attr {
	uint64_t attr;
	uint64_t size;
	uint64_t alignment;
};

struct vgic_region_attr gic_v3_dist_region = {
	.attr = KVM_VGIC_V3_ADDR_TYPE_DIST,
	.size = 0x10000,
	.alignment = 0x10000,
};

struct vgic_region_attr gic_v3_redist_region = {
	.attr = KVM_VGIC_V3_ADDR_TYPE_REDIST,
	.size = NR_VCPUS * 0x20000,
	.alignment = 0x10000,
};

struct vgic_region_attr gic_v2_dist_region = {
	.attr = KVM_VGIC_V2_ADDR_TYPE_DIST,
	.size = 0x1000,
	.alignment = 0x1000,
};

struct vgic_region_attr gic_v2_cpu_region = {
	.attr = KVM_VGIC_V2_ADDR_TYPE_CPU,
	.size = 0x2000,
	.alignment = 0x1000,
};

/**
 * Helper routine that performs KVM device tests in general. Eventually the
 * ARM_VGIC (GICv2 or GICv3) device gets created with an overlapping
 * DIST/REDIST (or DIST/CPUIF for GICv2). Assumption is 4 vcpus are going to be
 * used hence the overlap. In the case of GICv3, A RDIST region is set at @0x0
 * and a DIST region is set @0x70000. The GICv2 case sets a CPUIF @0x0 and a
 * DIST region @0x1000.
 */
static void subtest_dist_rdist(struct vm_gic *v)
{
	int ret;
	uint64_t addr;
	struct vgic_region_attr rdist; /* CPU interface in GICv2*/
	struct vgic_region_attr dist;

	rdist = VGIC_DEV_IS_V3(v->gic_dev_type) ? gic_v3_redist_region
						: gic_v2_cpu_region;
	dist = VGIC_DEV_IS_V3(v->gic_dev_type) ? gic_v3_dist_region
						: gic_v2_dist_region;

	/* Check existing group/attributes */
	kvm_has_device_attr(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR, dist.attr);

	kvm_has_device_attr(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR, rdist.attr);

	/* check non existing attribute */
	ret = __kvm_has_device_attr(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR, -1);
	TEST_ASSERT(ret && errno == ENXIO, "attribute not supported");

	/* misaligned DIST and REDIST address settings */
	addr = dist.alignment / 0x10;
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    dist.attr, &addr);
	TEST_ASSERT(ret && errno == EINVAL, "GIC dist base not aligned");

	addr = rdist.alignment / 0x10;
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    rdist.attr, &addr);
	TEST_ASSERT(ret && errno == EINVAL, "GIC redist/cpu base not aligned");

	/* out of range address */
	addr = max_phys_size;
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    dist.attr, &addr);
	TEST_ASSERT(ret && errno == E2BIG, "dist address beyond IPA limit");

	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    rdist.attr, &addr);
	TEST_ASSERT(ret && errno == E2BIG, "redist address beyond IPA limit");

	/* Space for half a rdist (a rdist is: 2 * rdist.alignment). */
	addr = max_phys_size - dist.alignment;
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    rdist.attr, &addr);
	TEST_ASSERT(ret && errno == E2BIG,
			"half of the redist is beyond IPA limit");

	/* set REDIST base address @0x0*/
	addr = 0x00000;
	kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    rdist.attr, &addr);

	/* Attempt to create a second legacy redistributor region */
	addr = 0xE0000;
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    rdist.attr, &addr);
	TEST_ASSERT(ret && errno == EEXIST, "GIC redist base set again");

	ret = __kvm_has_device_attr(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				     KVM_VGIC_V3_ADDR_TYPE_REDIST);
	if (!ret) {
		/* Attempt to mix legacy and new redistributor regions */
		addr = REDIST_REGION_ATTR_ADDR(NR_VCPUS, 0x100000, 0, 0);
		ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
					    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
		TEST_ASSERT(ret && errno == EINVAL,
			    "attempt to mix GICv3 REDIST and REDIST_REGION");
	}

	/*
	 * Set overlapping DIST / REDIST, cannot be detected here. Will be detected
	 * on first vcpu run instead.
	 */
	addr = rdist.size - rdist.alignment;
	kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    dist.attr, &addr);
}

/* Test the new REDIST region API */
static void subtest_v3_redist_regions(struct vm_gic *v)
{
	uint64_t addr, expected_addr;
	int ret;

	ret = __kvm_has_device_attr(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST);
	TEST_ASSERT(!ret, "Multiple redist regions advertised");

	addr = REDIST_REGION_ATTR_ADDR(NR_VCPUS, 0x100000, 2, 0);
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(ret && errno == EINVAL, "redist region attr value with flags != 0");

	addr = REDIST_REGION_ATTR_ADDR(0, 0x100000, 0, 0);
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(ret && errno == EINVAL, "redist region attr value with count== 0");

	addr = REDIST_REGION_ATTR_ADDR(2, 0x200000, 0, 1);
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(ret && errno == EINVAL,
		    "attempt to register the first rdist region with index != 0");

	addr = REDIST_REGION_ATTR_ADDR(2, 0x201000, 0, 1);
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(ret && errno == EINVAL, "rdist region with misaligned address");

	addr = REDIST_REGION_ATTR_ADDR(2, 0x200000, 0, 0);
	kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);

	addr = REDIST_REGION_ATTR_ADDR(2, 0x200000, 0, 1);
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(ret && errno == EINVAL, "register an rdist region with already used index");

	addr = REDIST_REGION_ATTR_ADDR(1, 0x210000, 0, 2);
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(ret && errno == EINVAL,
		    "register an rdist region overlapping with another one");

	addr = REDIST_REGION_ATTR_ADDR(1, 0x240000, 0, 2);
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(ret && errno == EINVAL, "register redist region with index not +1");

	addr = REDIST_REGION_ATTR_ADDR(1, 0x240000, 0, 1);
	kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);

	addr = REDIST_REGION_ATTR_ADDR(1, max_phys_size, 0, 2);
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(ret && errno == E2BIG,
		    "register redist region with base address beyond IPA range");

	/* The last redist is above the pa range. */
	addr = REDIST_REGION_ATTR_ADDR(2, max_phys_size - 0x30000, 0, 2);
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(ret && errno == E2BIG,
		    "register redist region with top address beyond IPA range");

	addr = 0x260000;
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST, &addr);
	TEST_ASSERT(ret && errno == EINVAL,
		    "Mix KVM_VGIC_V3_ADDR_TYPE_REDIST and REDIST_REGION");

	/*
	 * Now there are 2 redist regions:
	 * region 0 @ 0x200000 2 redists
	 * region 1 @ 0x240000 1 redist
	 * Attempt to read their characteristics
	 */

	addr = REDIST_REGION_ATTR_ADDR(0, 0, 0, 0);
	expected_addr = REDIST_REGION_ATTR_ADDR(2, 0x200000, 0, 0);
	ret = __kvm_device_attr_get(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(!ret && addr == expected_addr, "read characteristics of region #0");

	addr = REDIST_REGION_ATTR_ADDR(0, 0, 0, 1);
	expected_addr = REDIST_REGION_ATTR_ADDR(1, 0x240000, 0, 1);
	ret = __kvm_device_attr_get(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(!ret && addr == expected_addr, "read characteristics of region #1");

	addr = REDIST_REGION_ATTR_ADDR(0, 0, 0, 2);
	ret = __kvm_device_attr_get(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(ret && errno == ENOENT, "read characteristics of non existing region");

	addr = 0x260000;
	kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V3_ADDR_TYPE_DIST, &addr);

	addr = REDIST_REGION_ATTR_ADDR(1, 0x260000, 0, 2);
	ret = __kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(ret && errno == EINVAL, "register redist region colliding with dist");
}

/*
 * VGIC KVM device is created and initialized before the secondary CPUs
 * get created
 */
static void test_vgic_then_vcpus(uint32_t gic_dev_type)
{
	struct kvm_vcpu *vcpus[NR_VCPUS];
	struct vm_gic v;
	int ret, i;

	v = vm_gic_create_with_vcpus(gic_dev_type, 1, vcpus);

	subtest_dist_rdist(&v);

	/* Add the rest of the VCPUs */
	for (i = 1; i < NR_VCPUS; ++i)
		vcpus[i] = vm_vcpu_add(v.vm, i, guest_code);

	ret = run_vcpu(vcpus[3]);
	TEST_ASSERT(ret == -EINVAL, "dist/rdist overlap detected on 1st vcpu run");

	vm_gic_destroy(&v);
}

/* All the VCPUs are created before the VGIC KVM device gets initialized */
static void test_vcpus_then_vgic(uint32_t gic_dev_type)
{
	struct kvm_vcpu *vcpus[NR_VCPUS];
	struct vm_gic v;
	int ret;

	v = vm_gic_create_with_vcpus(gic_dev_type, NR_VCPUS, vcpus);

	subtest_dist_rdist(&v);

	ret = run_vcpu(vcpus[3]);
	TEST_ASSERT(ret == -EINVAL, "dist/rdist overlap detected on 1st vcpu run");

	vm_gic_destroy(&v);
}

static void test_v3_new_redist_regions(void)
{
	struct kvm_vcpu *vcpus[NR_VCPUS];
	void *dummy = NULL;
	struct vm_gic v;
	uint64_t addr;
	int ret;

	v = vm_gic_create_with_vcpus(KVM_DEV_TYPE_ARM_VGIC_V3, NR_VCPUS, vcpus);
	subtest_v3_redist_regions(&v);
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	ret = run_vcpu(vcpus[3]);
	TEST_ASSERT(ret == -ENXIO, "running without sufficient number of rdists");
	vm_gic_destroy(&v);

	/* step2 */

	v = vm_gic_create_with_vcpus(KVM_DEV_TYPE_ARM_VGIC_V3, NR_VCPUS, vcpus);
	subtest_v3_redist_regions(&v);

	addr = REDIST_REGION_ATTR_ADDR(1, 0x280000, 0, 2);
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);

	ret = run_vcpu(vcpus[3]);
	TEST_ASSERT(ret == -EBUSY, "running without vgic explicit init");

	vm_gic_destroy(&v);

	/* step 3 */

	v = vm_gic_create_with_vcpus(KVM_DEV_TYPE_ARM_VGIC_V3, NR_VCPUS, vcpus);
	subtest_v3_redist_regions(&v);

	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, dummy);
	TEST_ASSERT(ret && errno == EFAULT,
		    "register a third region allowing to cover the 4 vcpus");

	addr = REDIST_REGION_ATTR_ADDR(1, 0x280000, 0, 2);
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);

	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	ret = run_vcpu(vcpus[3]);
	TEST_ASSERT(!ret, "vcpu run");

	vm_gic_destroy(&v);
}

static void test_v3_typer_accesses(void)
{
	struct vm_gic v;
	uint64_t addr;
	int ret, i;

	v.vm = vm_create(NR_VCPUS);
	(void)vm_vcpu_add(v.vm, 0, guest_code);

	v.gic_fd = kvm_create_device(v.vm, KVM_DEV_TYPE_ARM_VGIC_V3);

	(void)vm_vcpu_add(v.vm, 3, guest_code);

	v3_redist_reg_get_errno(v.gic_fd, 1, GICR_TYPER, EINVAL,
				"attempting to read GICR_TYPER of non created vcpu");

	(void)vm_vcpu_add(v.vm, 1, guest_code);

	v3_redist_reg_get_errno(v.gic_fd, 1, GICR_TYPER, EBUSY,
				"read GICR_TYPER before GIC initialized");

	(void)vm_vcpu_add(v.vm, 2, guest_code);

	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	for (i = 0; i < NR_VCPUS ; i++) {
		v3_redist_reg_get(v.gic_fd, i, GICR_TYPER, i * 0x100,
				  "read GICR_TYPER before rdist region setting");
	}

	addr = REDIST_REGION_ATTR_ADDR(2, 0x200000, 0, 0);
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);

	/* The 2 first rdists should be put there (vcpu 0 and 3) */
	v3_redist_reg_get(v.gic_fd, 0, GICR_TYPER, 0x0, "read typer of rdist #0");
	v3_redist_reg_get(v.gic_fd, 3, GICR_TYPER, 0x310, "read typer of rdist #1");

	addr = REDIST_REGION_ATTR_ADDR(10, 0x100000, 0, 1);
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);
	TEST_ASSERT(ret && errno == EINVAL, "collision with previous rdist region");

	v3_redist_reg_get(v.gic_fd, 1, GICR_TYPER, 0x100,
			  "no redist region attached to vcpu #1 yet, last cannot be returned");
	v3_redist_reg_get(v.gic_fd, 2, GICR_TYPER, 0x200,
			  "no redist region attached to vcpu #2, last cannot be returned");

	addr = REDIST_REGION_ATTR_ADDR(10, 0x20000, 0, 1);
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);

	v3_redist_reg_get(v.gic_fd, 1, GICR_TYPER, 0x100, "read typer of rdist #1");
	v3_redist_reg_get(v.gic_fd, 2, GICR_TYPER, 0x210,
			  "read typer of rdist #1, last properly returned");

	vm_gic_destroy(&v);
}

static struct vm_gic vm_gic_v3_create_with_vcpuids(int nr_vcpus,
						   uint32_t vcpuids[])
{
	struct vm_gic v;
	int i;

	v.vm = vm_create(nr_vcpus);
	for (i = 0; i < nr_vcpus; i++)
		vm_vcpu_add(v.vm, vcpuids[i], guest_code);

	v.gic_fd = kvm_create_device(v.vm, KVM_DEV_TYPE_ARM_VGIC_V3);

	return v;
}

/**
 * Test GICR_TYPER last bit with new redist regions
 * rdist regions #1 and #2 are contiguous
 * rdist region #0 @0x100000 2 rdist capacity
 *     rdists: 0, 3 (Last)
 * rdist region #1 @0x240000 2 rdist capacity
 *     rdists:  5, 4 (Last)
 * rdist region #2 @0x200000 2 rdist capacity
 *     rdists: 1, 2
 */
static void test_v3_last_bit_redist_regions(void)
{
	uint32_t vcpuids[] = { 0, 3, 5, 4, 1, 2 };
	struct vm_gic v;
	uint64_t addr;

	v = vm_gic_v3_create_with_vcpuids(ARRAY_SIZE(vcpuids), vcpuids);

	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	addr = REDIST_REGION_ATTR_ADDR(2, 0x100000, 0, 0);
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);

	addr = REDIST_REGION_ATTR_ADDR(2, 0x240000, 0, 1);
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);

	addr = REDIST_REGION_ATTR_ADDR(2, 0x200000, 0, 2);
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &addr);

	v3_redist_reg_get(v.gic_fd, 0, GICR_TYPER, 0x000, "read typer of rdist #0");
	v3_redist_reg_get(v.gic_fd, 1, GICR_TYPER, 0x100, "read typer of rdist #1");
	v3_redist_reg_get(v.gic_fd, 2, GICR_TYPER, 0x200, "read typer of rdist #2");
	v3_redist_reg_get(v.gic_fd, 3, GICR_TYPER, 0x310, "read typer of rdist #3");
	v3_redist_reg_get(v.gic_fd, 5, GICR_TYPER, 0x500, "read typer of rdist #5");
	v3_redist_reg_get(v.gic_fd, 4, GICR_TYPER, 0x410, "read typer of rdist #4");

	vm_gic_destroy(&v);
}

/* Test last bit with legacy region */
static void test_v3_last_bit_single_rdist(void)
{
	uint32_t vcpuids[] = { 0, 3, 5, 4, 1, 2 };
	struct vm_gic v;
	uint64_t addr;

	v = vm_gic_v3_create_with_vcpuids(ARRAY_SIZE(vcpuids), vcpuids);

	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	addr = 0x10000;
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V3_ADDR_TYPE_REDIST, &addr);

	v3_redist_reg_get(v.gic_fd, 0, GICR_TYPER, 0x000, "read typer of rdist #0");
	v3_redist_reg_get(v.gic_fd, 3, GICR_TYPER, 0x300, "read typer of rdist #1");
	v3_redist_reg_get(v.gic_fd, 5, GICR_TYPER, 0x500, "read typer of rdist #2");
	v3_redist_reg_get(v.gic_fd, 1, GICR_TYPER, 0x100, "read typer of rdist #3");
	v3_redist_reg_get(v.gic_fd, 2, GICR_TYPER, 0x210, "read typer of rdist #3");

	vm_gic_destroy(&v);
}

/* Uses the legacy REDIST region API. */
static void test_v3_redist_ipa_range_check_at_vcpu_run(void)
{
	struct kvm_vcpu *vcpus[NR_VCPUS];
	struct vm_gic v;
	int ret, i;
	uint64_t addr;

	v = vm_gic_create_with_vcpus(KVM_DEV_TYPE_ARM_VGIC_V3, 1, vcpus);

	/* Set space for 3 redists, we have 1 vcpu, so this succeeds. */
	addr = max_phys_size - (3 * 2 * 0x10000);
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V3_ADDR_TYPE_REDIST, &addr);

	addr = 0x00000;
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V3_ADDR_TYPE_DIST, &addr);

	/* Add the rest of the VCPUs */
	for (i = 1; i < NR_VCPUS; ++i)
		vcpus[i] = vm_vcpu_add(v.vm, i, guest_code);

	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	/* Attempt to run a vcpu without enough redist space. */
	ret = run_vcpu(vcpus[2]);
	TEST_ASSERT(ret && errno == EINVAL,
		"redist base+size above PA range detected on 1st vcpu run");

	vm_gic_destroy(&v);
}

static void test_v3_its_region(void)
{
	struct kvm_vcpu *vcpus[NR_VCPUS];
	struct vm_gic v;
	uint64_t addr;
	int its_fd, ret;

	v = vm_gic_create_with_vcpus(KVM_DEV_TYPE_ARM_VGIC_V3, NR_VCPUS, vcpus);
	its_fd = kvm_create_device(v.vm, KVM_DEV_TYPE_ARM_VGIC_ITS);

	addr = 0x401000;
	ret = __kvm_device_attr_set(its_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_ITS_ADDR_TYPE, &addr);
	TEST_ASSERT(ret && errno == EINVAL,
		"ITS region with misaligned address");

	addr = max_phys_size;
	ret = __kvm_device_attr_set(its_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_ITS_ADDR_TYPE, &addr);
	TEST_ASSERT(ret && errno == E2BIG,
		"register ITS region with base address beyond IPA range");

	addr = max_phys_size - 0x10000;
	ret = __kvm_device_attr_set(its_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_ITS_ADDR_TYPE, &addr);
	TEST_ASSERT(ret && errno == E2BIG,
		"Half of ITS region is beyond IPA range");

	/* This one succeeds setting the ITS base */
	addr = 0x400000;
	kvm_device_attr_set(its_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_ITS_ADDR_TYPE, &addr);

	addr = 0x300000;
	ret = __kvm_device_attr_set(its_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_ITS_ADDR_TYPE, &addr);
	TEST_ASSERT(ret && errno == EEXIST, "ITS base set again");

	close(its_fd);
	vm_gic_destroy(&v);
}

/*
 * Returns 0 if it's possible to create GIC device of a given type (V2 or V3).
 */
int test_kvm_device(uint32_t gic_dev_type)
{
	struct kvm_vcpu *vcpus[NR_VCPUS];
	struct vm_gic v;
	uint32_t other;
	int ret;

	v.vm = vm_create_with_vcpus(NR_VCPUS, guest_code, vcpus);

	/* try to create a non existing KVM device */
	ret = __kvm_test_create_device(v.vm, 0);
	TEST_ASSERT(ret && errno == ENODEV, "unsupported device");

	/* trial mode */
	ret = __kvm_test_create_device(v.vm, gic_dev_type);
	if (ret)
		return ret;
	v.gic_fd = kvm_create_device(v.vm, gic_dev_type);

	ret = __kvm_create_device(v.vm, gic_dev_type);
	TEST_ASSERT(ret < 0 && errno == EEXIST, "create GIC device twice");

	/* try to create the other gic_dev_type */
	other = VGIC_DEV_IS_V2(gic_dev_type) ? KVM_DEV_TYPE_ARM_VGIC_V3
					     : KVM_DEV_TYPE_ARM_VGIC_V2;

	if (!__kvm_test_create_device(v.vm, other)) {
		ret = __kvm_test_create_device(v.vm, other);
		TEST_ASSERT(ret && (errno == EINVAL || errno == EEXIST),
				"create GIC device while other version exists");
	}

	vm_gic_destroy(&v);

	return 0;
}

void run_tests(uint32_t gic_dev_type)
{
	test_vcpus_then_vgic(gic_dev_type);
	test_vgic_then_vcpus(gic_dev_type);

	if (VGIC_DEV_IS_V3(gic_dev_type)) {
		test_v3_new_redist_regions();
		test_v3_typer_accesses();
		test_v3_last_bit_redist_regions();
		test_v3_last_bit_single_rdist();
		test_v3_redist_ipa_range_check_at_vcpu_run();
		test_v3_its_region();
	}
}

int main(int ac, char **av)
{
	int ret;
	int pa_bits;
	int cnt_impl = 0;

	pa_bits = vm_guest_mode_params[VM_MODE_DEFAULT].pa_bits;
	max_phys_size = 1ULL << pa_bits;

	ret = test_kvm_device(KVM_DEV_TYPE_ARM_VGIC_V3);
	if (!ret) {
		pr_info("Running GIC_v3 tests.\n");
		run_tests(KVM_DEV_TYPE_ARM_VGIC_V3);
		cnt_impl++;
	}

	ret = test_kvm_device(KVM_DEV_TYPE_ARM_VGIC_V2);
	if (!ret) {
		pr_info("Running GIC_v2 tests.\n");
		run_tests(KVM_DEV_TYPE_ARM_VGIC_V2);
		cnt_impl++;
	}

	if (!cnt_impl) {
		print_skip("No GICv2 nor GICv3 support");
		exit(KSFT_SKIP);
	}
	return 0;
}
