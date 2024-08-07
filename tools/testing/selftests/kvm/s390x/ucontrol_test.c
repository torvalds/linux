// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test code for the s390x kvm ucontrol interface
 *
 * Copyright IBM Corp. 2024
 *
 * Authors:
 *  Christoph Schlameuss <schlameuss@linux.ibm.com>
 */
#include "kselftest_harness.h"
#include "kvm_util.h"
#include "processor.h"
#include "sie.h"

#include <linux/capability.h>
#include <linux/sizes.h>

#define VM_MEM_SIZE (4 * SZ_1M)

/* so directly declare capget to check caps without libcap */
int capget(cap_user_header_t header, cap_user_data_t data);

/**
 * In order to create user controlled virtual machines on S390,
 * check KVM_CAP_S390_UCONTROL and use the flag KVM_VM_S390_UCONTROL
 * as privileged user (SYS_ADMIN).
 */
void require_ucontrol_admin(void)
{
	struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
	struct __user_cap_header_struct hdr = {
		.version = _LINUX_CAPABILITY_VERSION_3,
	};
	int rc;

	rc = capget(&hdr, data);
	TEST_ASSERT_EQ(0, rc);
	TEST_REQUIRE((data->effective & CAP_TO_MASK(CAP_SYS_ADMIN)) > 0);

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_S390_UCONTROL));
}

FIXTURE(uc_kvm)
{
	struct kvm_s390_sie_block *sie_block;
	struct kvm_run *run;
	uintptr_t base_gpa;
	uintptr_t code_gpa;
	uintptr_t base_hva;
	uintptr_t code_hva;
	int kvm_run_size;
	void *vm_mem;
	int vcpu_fd;
	int kvm_fd;
	int vm_fd;
};

/**
 * create VM with single vcpu, map kvm_run and SIE control block for easy access
 */
FIXTURE_SETUP(uc_kvm)
{
	struct kvm_s390_vm_cpu_processor info;
	int rc;

	require_ucontrol_admin();

	self->kvm_fd = open_kvm_dev_path_or_exit();
	self->vm_fd = ioctl(self->kvm_fd, KVM_CREATE_VM, KVM_VM_S390_UCONTROL);
	ASSERT_GE(self->vm_fd, 0);

	kvm_device_attr_get(self->vm_fd, KVM_S390_VM_CPU_MODEL,
			    KVM_S390_VM_CPU_PROCESSOR, &info);
	TH_LOG("create VM 0x%llx", info.cpuid);

	self->vcpu_fd = ioctl(self->vm_fd, KVM_CREATE_VCPU, 0);
	ASSERT_GE(self->vcpu_fd, 0);

	self->kvm_run_size = ioctl(self->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, NULL);
	ASSERT_GE(self->kvm_run_size, sizeof(struct kvm_run))
		  TH_LOG(KVM_IOCTL_ERROR(KVM_GET_VCPU_MMAP_SIZE, self->kvm_run_size));
	self->run = (struct kvm_run *)mmap(NULL, self->kvm_run_size,
		    PROT_READ | PROT_WRITE, MAP_SHARED, self->vcpu_fd, 0);
	ASSERT_NE(self->run, MAP_FAILED);
	/**
	 * For virtual cpus that have been created with S390 user controlled
	 * virtual machines, the resulting vcpu fd can be memory mapped at page
	 * offset KVM_S390_SIE_PAGE_OFFSET in order to obtain a memory map of
	 * the virtual cpu's hardware control block.
	 */
	self->sie_block = (struct kvm_s390_sie_block *)mmap(NULL, PAGE_SIZE,
			  PROT_READ | PROT_WRITE, MAP_SHARED,
			  self->vcpu_fd, KVM_S390_SIE_PAGE_OFFSET << PAGE_SHIFT);
	ASSERT_NE(self->sie_block, MAP_FAILED);

	TH_LOG("VM created %p %p", self->run, self->sie_block);

	self->base_gpa = 0;
	self->code_gpa = self->base_gpa + (3 * SZ_1M);

	self->vm_mem = aligned_alloc(SZ_1M, VM_MEM_SIZE);
	ASSERT_NE(NULL, self->vm_mem) TH_LOG("malloc failed %u", errno);
	self->base_hva = (uintptr_t)self->vm_mem;
	self->code_hva = self->base_hva - self->base_gpa + self->code_gpa;
	struct kvm_s390_ucas_mapping map = {
		.user_addr = self->base_hva,
		.vcpu_addr = self->base_gpa,
		.length = VM_MEM_SIZE,
	};
	TH_LOG("ucas map %p %p 0x%llx",
	       (void *)map.user_addr, (void *)map.vcpu_addr, map.length);
	rc = ioctl(self->vcpu_fd, KVM_S390_UCAS_MAP, &map);
	ASSERT_EQ(0, rc) TH_LOG("ucas map result %d not expected, %s",
				rc, strerror(errno));

	TH_LOG("page in %p", (void *)self->base_gpa);
	rc = ioctl(self->vcpu_fd, KVM_S390_VCPU_FAULT, self->base_gpa);
	ASSERT_EQ(0, rc) TH_LOG("vcpu fault (%p) result %d not expected, %s",
				(void *)self->base_hva, rc, strerror(errno));

	self->sie_block->cpuflags &= ~CPUSTAT_STOPPED;
}

FIXTURE_TEARDOWN(uc_kvm)
{
	munmap(self->sie_block, PAGE_SIZE);
	munmap(self->run, self->kvm_run_size);
	close(self->vcpu_fd);
	close(self->vm_fd);
	close(self->kvm_fd);
	free(self->vm_mem);
}

TEST_F(uc_kvm, uc_sie_assertions)
{
	/* assert interception of Code 08 (Program Interruption) is set */
	EXPECT_EQ(0, self->sie_block->ecb & ECB_SPECI);
}

TEST_F(uc_kvm, uc_attr_mem_limit)
{
	u64 limit;
	struct kvm_device_attr attr = {
		.group = KVM_S390_VM_MEM_CTRL,
		.attr = KVM_S390_VM_MEM_LIMIT_SIZE,
		.addr = (unsigned long)&limit,
	};
	int rc;

	rc = ioctl(self->vm_fd, KVM_GET_DEVICE_ATTR, &attr);
	EXPECT_EQ(0, rc);
	EXPECT_EQ(~0UL, limit);

	/* assert set not supported */
	rc = ioctl(self->vm_fd, KVM_SET_DEVICE_ATTR, &attr);
	EXPECT_EQ(-1, rc);
	EXPECT_EQ(EINVAL, errno);
}

TEST_F(uc_kvm, uc_no_dirty_log)
{
	struct kvm_dirty_log dlog;
	int rc;

	rc = ioctl(self->vm_fd, KVM_GET_DIRTY_LOG, &dlog);
	EXPECT_EQ(-1, rc);
	EXPECT_EQ(EINVAL, errno);
}

/**
 * Assert HPAGE CAP cannot be enabled on UCONTROL VM
 */
TEST(uc_cap_hpage)
{
	int rc, kvm_fd, vm_fd, vcpu_fd;
	struct kvm_enable_cap cap = {
		.cap = KVM_CAP_S390_HPAGE_1M,
	};

	require_ucontrol_admin();

	kvm_fd = open_kvm_dev_path_or_exit();
	vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, KVM_VM_S390_UCONTROL);
	ASSERT_GE(vm_fd, 0);

	/* assert hpages are not supported on ucontrol vm */
	rc = ioctl(vm_fd, KVM_CHECK_EXTENSION, KVM_CAP_S390_HPAGE_1M);
	EXPECT_EQ(0, rc);

	/* Test that KVM_CAP_S390_HPAGE_1M can't be enabled for a ucontrol vm */
	rc = ioctl(vm_fd, KVM_ENABLE_CAP, cap);
	EXPECT_EQ(-1, rc);
	EXPECT_EQ(EINVAL, errno);

	/* assert HPAGE CAP is rejected after vCPU creation */
	vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
	ASSERT_GE(vcpu_fd, 0);
	rc = ioctl(vm_fd, KVM_ENABLE_CAP, cap);
	EXPECT_EQ(-1, rc);
	EXPECT_EQ(EBUSY, errno);

	close(vcpu_fd);
	close(vm_fd);
	close(kvm_fd);
}

TEST_HARNESS_MAIN
