// SPDX-License-Identifier: GPL-2.0-only

/* hypercalls: Check the ARM64's psuedo-firmware bitmap register interface.
 *
 * The test validates the basic hypercall functionalities that are exposed
 * via the psuedo-firmware bitmap register. This includes the registers'
 * read/write behavior before and after the VM has started, and if the
 * hypercalls are properly masked or unmasked to the guest when disabled or
 * enabled from the KVM userspace, respectively.
 */
#include <errno.h>
#include <linux/arm-smccc.h>
#include <asm/kvm.h>
#include <kvm_util.h>

#include "processor.h"

#define FW_REG_ULIMIT_VAL(max_feat_bit) (GENMASK(max_feat_bit, 0))

/* Last valid bits of the bitmapped firmware registers */
#define KVM_REG_ARM_STD_BMAP_BIT_MAX		0
#define KVM_REG_ARM_STD_HYP_BMAP_BIT_MAX	0
#define KVM_REG_ARM_VENDOR_HYP_BMAP_BIT_MAX	1

struct kvm_fw_reg_info {
	uint64_t reg;		/* Register definition */
	uint64_t max_feat_bit;	/* Bit that represents the upper limit of the feature-map */
};

#define FW_REG_INFO(r)			\
	{					\
		.reg = r,			\
		.max_feat_bit = r##_BIT_MAX,	\
	}

static const struct kvm_fw_reg_info fw_reg_info[] = {
	FW_REG_INFO(KVM_REG_ARM_STD_BMAP),
	FW_REG_INFO(KVM_REG_ARM_STD_HYP_BMAP),
	FW_REG_INFO(KVM_REG_ARM_VENDOR_HYP_BMAP),
};

enum test_stage {
	TEST_STAGE_REG_IFACE,
	TEST_STAGE_HVC_IFACE_FEAT_DISABLED,
	TEST_STAGE_HVC_IFACE_FEAT_ENABLED,
	TEST_STAGE_HVC_IFACE_FALSE_INFO,
	TEST_STAGE_END,
};

static int stage = TEST_STAGE_REG_IFACE;

struct test_hvc_info {
	uint32_t func_id;
	uint64_t arg1;
};

#define TEST_HVC_INFO(f, a1)	\
	{			\
		.func_id = f,	\
		.arg1 = a1,	\
	}

static const struct test_hvc_info hvc_info[] = {
	/* KVM_REG_ARM_STD_BMAP */
	TEST_HVC_INFO(ARM_SMCCC_TRNG_VERSION, 0),
	TEST_HVC_INFO(ARM_SMCCC_TRNG_FEATURES, ARM_SMCCC_TRNG_RND64),
	TEST_HVC_INFO(ARM_SMCCC_TRNG_GET_UUID, 0),
	TEST_HVC_INFO(ARM_SMCCC_TRNG_RND32, 0),
	TEST_HVC_INFO(ARM_SMCCC_TRNG_RND64, 0),

	/* KVM_REG_ARM_STD_HYP_BMAP */
	TEST_HVC_INFO(ARM_SMCCC_ARCH_FEATURES_FUNC_ID, ARM_SMCCC_HV_PV_TIME_FEATURES),
	TEST_HVC_INFO(ARM_SMCCC_HV_PV_TIME_FEATURES, ARM_SMCCC_HV_PV_TIME_ST),
	TEST_HVC_INFO(ARM_SMCCC_HV_PV_TIME_ST, 0),

	/* KVM_REG_ARM_VENDOR_HYP_BMAP */
	TEST_HVC_INFO(ARM_SMCCC_VENDOR_HYP_KVM_FEATURES_FUNC_ID,
			ARM_SMCCC_VENDOR_HYP_KVM_PTP_FUNC_ID),
	TEST_HVC_INFO(ARM_SMCCC_VENDOR_HYP_CALL_UID_FUNC_ID, 0),
	TEST_HVC_INFO(ARM_SMCCC_VENDOR_HYP_KVM_PTP_FUNC_ID, KVM_PTP_VIRT_COUNTER),
};

/* Feed false hypercall info to test the KVM behavior */
static const struct test_hvc_info false_hvc_info[] = {
	/* Feature support check against a different family of hypercalls */
	TEST_HVC_INFO(ARM_SMCCC_TRNG_FEATURES, ARM_SMCCC_VENDOR_HYP_KVM_PTP_FUNC_ID),
	TEST_HVC_INFO(ARM_SMCCC_ARCH_FEATURES_FUNC_ID, ARM_SMCCC_TRNG_RND64),
	TEST_HVC_INFO(ARM_SMCCC_HV_PV_TIME_FEATURES, ARM_SMCCC_TRNG_RND64),
};

static void guest_test_hvc(const struct test_hvc_info *hc_info)
{
	unsigned int i;
	struct arm_smccc_res res;
	unsigned int hvc_info_arr_sz;

	hvc_info_arr_sz =
	hc_info == hvc_info ? ARRAY_SIZE(hvc_info) : ARRAY_SIZE(false_hvc_info);

	for (i = 0; i < hvc_info_arr_sz; i++, hc_info++) {
		memset(&res, 0, sizeof(res));
		smccc_hvc(hc_info->func_id, hc_info->arg1, 0, 0, 0, 0, 0, 0, &res);

		switch (stage) {
		case TEST_STAGE_HVC_IFACE_FEAT_DISABLED:
		case TEST_STAGE_HVC_IFACE_FALSE_INFO:
			__GUEST_ASSERT(res.a0 == SMCCC_RET_NOT_SUPPORTED,
				       "a0 = 0x%lx, func_id = 0x%x, arg1 = 0x%llx, stage = %u",
					res.a0, hc_info->func_id, hc_info->arg1, stage);
			break;
		case TEST_STAGE_HVC_IFACE_FEAT_ENABLED:
			__GUEST_ASSERT(res.a0 != SMCCC_RET_NOT_SUPPORTED,
				       "a0 = 0x%lx, func_id = 0x%x, arg1 = 0x%llx, stage = %u",
					res.a0, hc_info->func_id, hc_info->arg1, stage);
			break;
		default:
			GUEST_FAIL("Unexpected stage = %u", stage);
		}
	}
}

static void guest_code(void)
{
	while (stage != TEST_STAGE_END) {
		switch (stage) {
		case TEST_STAGE_REG_IFACE:
			break;
		case TEST_STAGE_HVC_IFACE_FEAT_DISABLED:
		case TEST_STAGE_HVC_IFACE_FEAT_ENABLED:
			guest_test_hvc(hvc_info);
			break;
		case TEST_STAGE_HVC_IFACE_FALSE_INFO:
			guest_test_hvc(false_hvc_info);
			break;
		default:
			GUEST_FAIL("Unexpected stage = %u", stage);
		}

		GUEST_SYNC(stage);
	}

	GUEST_DONE();
}

struct st_time {
	uint32_t rev;
	uint32_t attr;
	uint64_t st_time;
};

#define STEAL_TIME_SIZE		((sizeof(struct st_time) + 63) & ~63)
#define ST_GPA_BASE		(1 << 30)

static void steal_time_init(struct kvm_vcpu *vcpu)
{
	uint64_t st_ipa = (ulong)ST_GPA_BASE;
	unsigned int gpages;

	gpages = vm_calc_num_guest_pages(VM_MODE_DEFAULT, STEAL_TIME_SIZE);
	vm_userspace_mem_region_add(vcpu->vm, VM_MEM_SRC_ANONYMOUS, ST_GPA_BASE, 1, gpages, 0);

	vcpu_device_attr_set(vcpu, KVM_ARM_VCPU_PVTIME_CTRL,
			     KVM_ARM_VCPU_PVTIME_IPA, &st_ipa);
}

static void test_fw_regs_before_vm_start(struct kvm_vcpu *vcpu)
{
	uint64_t val;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(fw_reg_info); i++) {
		const struct kvm_fw_reg_info *reg_info = &fw_reg_info[i];

		/* First 'read' should be an upper limit of the features supported */
		vcpu_get_reg(vcpu, reg_info->reg, &val);
		TEST_ASSERT(val == FW_REG_ULIMIT_VAL(reg_info->max_feat_bit),
			"Expected all the features to be set for reg: 0x%lx; expected: 0x%lx; read: 0x%lx\n",
			reg_info->reg, FW_REG_ULIMIT_VAL(reg_info->max_feat_bit), val);

		/* Test a 'write' by disabling all the features of the register map */
		ret = __vcpu_set_reg(vcpu, reg_info->reg, 0);
		TEST_ASSERT(ret == 0,
			"Failed to clear all the features of reg: 0x%lx; ret: %d\n",
			reg_info->reg, errno);

		vcpu_get_reg(vcpu, reg_info->reg, &val);
		TEST_ASSERT(val == 0,
			"Expected all the features to be cleared for reg: 0x%lx\n", reg_info->reg);

		/*
		 * Test enabling a feature that's not supported.
		 * Avoid this check if all the bits are occupied.
		 */
		if (reg_info->max_feat_bit < 63) {
			ret = __vcpu_set_reg(vcpu, reg_info->reg, BIT(reg_info->max_feat_bit + 1));
			TEST_ASSERT(ret != 0 && errno == EINVAL,
			"Unexpected behavior or return value (%d) while setting an unsupported feature for reg: 0x%lx\n",
			errno, reg_info->reg);
		}
	}
}

static void test_fw_regs_after_vm_start(struct kvm_vcpu *vcpu)
{
	uint64_t val;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(fw_reg_info); i++) {
		const struct kvm_fw_reg_info *reg_info = &fw_reg_info[i];

		/*
		 * Before starting the VM, the test clears all the bits.
		 * Check if that's still the case.
		 */
		vcpu_get_reg(vcpu, reg_info->reg, &val);
		TEST_ASSERT(val == 0,
			"Expected all the features to be cleared for reg: 0x%lx\n",
			reg_info->reg);

		/*
		 * Since the VM has run at least once, KVM shouldn't allow modification of
		 * the registers and should return EBUSY. Set the registers and check for
		 * the expected errno.
		 */
		ret = __vcpu_set_reg(vcpu, reg_info->reg, FW_REG_ULIMIT_VAL(reg_info->max_feat_bit));
		TEST_ASSERT(ret != 0 && errno == EBUSY,
		"Unexpected behavior or return value (%d) while setting a feature while VM is running for reg: 0x%lx\n",
		errno, reg_info->reg);
	}
}

static struct kvm_vm *test_vm_create(struct kvm_vcpu **vcpu)
{
	struct kvm_vm *vm;

	vm = vm_create_with_one_vcpu(vcpu, guest_code);

	steal_time_init(*vcpu);

	return vm;
}

static void test_guest_stage(struct kvm_vm **vm, struct kvm_vcpu **vcpu)
{
	int prev_stage = stage;

	pr_debug("Stage: %d\n", prev_stage);

	/* Sync the stage early, the VM might be freed below. */
	stage++;
	sync_global_to_guest(*vm, stage);

	switch (prev_stage) {
	case TEST_STAGE_REG_IFACE:
		test_fw_regs_after_vm_start(*vcpu);
		break;
	case TEST_STAGE_HVC_IFACE_FEAT_DISABLED:
		/* Start a new VM so that all the features are now enabled by default */
		kvm_vm_free(*vm);
		*vm = test_vm_create(vcpu);
		break;
	case TEST_STAGE_HVC_IFACE_FEAT_ENABLED:
	case TEST_STAGE_HVC_IFACE_FALSE_INFO:
		break;
	default:
		TEST_FAIL("Unknown test stage: %d\n", prev_stage);
	}
}

static void test_run(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	bool guest_done = false;

	vm = test_vm_create(&vcpu);

	test_fw_regs_before_vm_start(vcpu);

	while (!guest_done) {
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			test_guest_stage(&vm, &vcpu);
			break;
		case UCALL_DONE:
			guest_done = true;
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		default:
			TEST_FAIL("Unexpected guest exit\n");
		}
	}

	kvm_vm_free(vm);
}

int main(void)
{
	test_run();
	return 0;
}
