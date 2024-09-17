// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test code for the s390x kvm ucontrol interface
 *
 * Copyright IBM Corp. 2024
 *
 * Authors:
 *  Christoph Schlameuss <schlameuss@linux.ibm.com>
 */
#include "debug_print.h"
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

/* Test program setting some registers and looping */
extern char test_gprs_asm[];
asm("test_gprs_asm:\n"
	"xgr	%r0, %r0\n"
	"lgfi	%r1,1\n"
	"lgfi	%r2,2\n"
	"lgfi	%r3,3\n"
	"lgfi	%r4,4\n"
	"lgfi	%r5,5\n"
	"lgfi	%r6,6\n"
	"lgfi	%r7,7\n"
	"0:\n"
	"	diag	0,0,0x44\n"
	"	ahi	%r0,1\n"
	"	j	0b\n"
);

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

/* verify SIEIC exit
 * * fail on codes not expected in the test cases
 */
static bool uc_handle_sieic(FIXTURE_DATA(uc_kvm) * self)
{
	struct kvm_s390_sie_block *sie_block = self->sie_block;
	struct kvm_run *run = self->run;

	/* check SIE interception code */
	pr_info("sieic: 0x%.2x 0x%.4x 0x%.4x\n",
		run->s390_sieic.icptcode,
		run->s390_sieic.ipa,
		run->s390_sieic.ipb);
	switch (run->s390_sieic.icptcode) {
	case ICPT_INST:
		/* end execution in caller on intercepted instruction */
		pr_info("sie instruction interception\n");
		return false;
	case ICPT_OPEREXC:
		/* operation exception */
		TEST_FAIL("sie exception on %.4x%.8x", sie_block->ipa, sie_block->ipb);
	default:
		TEST_FAIL("UNEXPECTED SIEIC CODE %d", run->s390_sieic.icptcode);
	}
	return true;
}

/* verify VM state on exit */
static bool uc_handle_exit(FIXTURE_DATA(uc_kvm) * self)
{
	struct kvm_run *run = self->run;

	switch (run->exit_reason) {
	case KVM_EXIT_S390_SIEIC:
		return uc_handle_sieic(self);
	default:
		pr_info("exit_reason %2d not handled\n", run->exit_reason);
	}
	return true;
}

/* run the VM until interrupted */
static int uc_run_once(FIXTURE_DATA(uc_kvm) * self)
{
	int rc;

	rc = ioctl(self->vcpu_fd, KVM_RUN, NULL);
	print_run(self->run, self->sie_block);
	print_regs(self->run);
	pr_debug("run %d / %d %s\n", rc, errno, strerror(errno));
	return rc;
}

static void uc_assert_diag44(FIXTURE_DATA(uc_kvm) * self)
{
	struct kvm_s390_sie_block *sie_block = self->sie_block;

	/* assert vm was interrupted by diag 0x0044 */
	TEST_ASSERT_EQ(KVM_EXIT_S390_SIEIC, self->run->exit_reason);
	TEST_ASSERT_EQ(ICPT_INST, sie_block->icptcode);
	TEST_ASSERT_EQ(0x8300, sie_block->ipa);
	TEST_ASSERT_EQ(0x440000, sie_block->ipb);
}

TEST_F(uc_kvm, uc_gprs)
{
	struct kvm_sync_regs *sync_regs = &self->run->s.regs;
	struct kvm_run *run = self->run;
	struct kvm_regs regs = {};

	/* Set registers to values that are different from the ones that we expect below */
	for (int i = 0; i < 8; i++)
		sync_regs->gprs[i] = 8;
	run->kvm_dirty_regs |= KVM_SYNC_GPRS;

	/* copy test_gprs_asm to code_hva / code_gpa */
	TH_LOG("copy code %p to vm mapped memory %p / %p",
	       &test_gprs_asm, (void *)self->code_hva, (void *)self->code_gpa);
	memcpy((void *)self->code_hva, &test_gprs_asm, PAGE_SIZE);

	/* DAT disabled + 64 bit mode */
	run->psw_mask = 0x0000000180000000ULL;
	run->psw_addr = self->code_gpa;

	/* run and expect interception of diag 44 */
	ASSERT_EQ(0, uc_run_once(self));
	ASSERT_EQ(false, uc_handle_exit(self));
	uc_assert_diag44(self);

	/* Retrieve and check guest register values */
	ASSERT_EQ(0, ioctl(self->vcpu_fd, KVM_GET_REGS, &regs));
	for (int i = 0; i < 8; i++) {
		ASSERT_EQ(i, regs.gprs[i]);
		ASSERT_EQ(i, sync_regs->gprs[i]);
	}

	/* run and expect interception of diag 44 again */
	ASSERT_EQ(0, uc_run_once(self));
	ASSERT_EQ(false, uc_handle_exit(self));
	uc_assert_diag44(self);

	/* check continued increment of register 0 value */
	ASSERT_EQ(0, ioctl(self->vcpu_fd, KVM_GET_REGS, &regs));
	ASSERT_EQ(1, regs.gprs[0]);
	ASSERT_EQ(1, sync_regs->gprs[0]);
}

TEST_HARNESS_MAIN
