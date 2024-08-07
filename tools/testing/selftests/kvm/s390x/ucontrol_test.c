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

#include <linux/capability.h>
#include <linux/sizes.h>

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
