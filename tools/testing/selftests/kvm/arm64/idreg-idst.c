// SPDX-License-Identifier: GPL-2.0

/*
 * Access all FEAT_IDST-handled registers that depend on more than
 * just FEAT_AA64, and fail if we don't get an a trap with an 0x18 EC.
 */

#include <test_util.h>
#include <kvm_util.h>
#include <processor.h>

static volatile bool sys64, undef;

#define __check_sr_read(r)					\
	({							\
		uint64_t val;					\
								\
		sys64 = false;					\
		undef = false;					\
		dsb(sy);					\
		val = read_sysreg_s(SYS_ ## r);			\
		val;						\
	})

/* Fatal checks */
#define check_sr_read(r)					\
	do {							\
		__check_sr_read(r);				\
		__GUEST_ASSERT(!undef, #r " unexpected UNDEF");	\
		__GUEST_ASSERT(sys64, #r " didn't trap");	\
	} while(0)


static void guest_code(void)
{
	check_sr_read(CCSIDR2_EL1);
	check_sr_read(SMIDR_EL1);
	check_sr_read(GMID_EL1);

	GUEST_DONE();
}

static void guest_sys64_handler(struct ex_regs *regs)
{
	sys64 = true;
	undef = false;
	regs->pc += 4;
}

static void guest_undef_handler(struct ex_regs *regs)
{
	sys64 = false;
	undef = true;
	regs->pc += 4;
}

static void test_run_vcpu(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

	do {
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		case UCALL_PRINTF:
			printf("%s", uc.buffer);
			break;
		case UCALL_DONE:
			break;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	} while (uc.cmd != UCALL_DONE);
}

static void test_guest_feat_idst(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	/* This VM has no MTE, no SME, no CCIDX */
	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);

	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
				ESR_ELx_EC_SYS64, guest_sys64_handler);
	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
				ESR_ELx_EC_UNKNOWN, guest_undef_handler);

	test_run_vcpu(vcpu);

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	uint64_t mmfr2;

	test_disable_default_vgic();

	vm = vm_create_with_one_vcpu(&vcpu, NULL);
	mmfr2 = vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64MMFR2_EL1));
	__TEST_REQUIRE(FIELD_GET(ID_AA64MMFR2_EL1_IDS, mmfr2) > 0,
		       "FEAT_IDST not supported");
	kvm_vm_free(vm);

	test_guest_feat_idst();

	return 0;
}
