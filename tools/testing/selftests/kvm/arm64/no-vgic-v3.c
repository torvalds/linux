// SPDX-License-Identifier: GPL-2.0

// Check that, on a GICv3 system, not configuring GICv3 correctly
// results in all of the sysregs generating an UNDEF exception.

#include <test_util.h>
#include <kvm_util.h>
#include <processor.h>

static volatile bool handled;

#define __check_sr_read(r)					\
	({							\
		uint64_t val;					\
								\
		handled = false;				\
		dsb(sy);					\
		val = read_sysreg_s(SYS_ ## r);			\
		val;						\
	})

#define __check_sr_write(r)					\
	do {							\
		handled = false;				\
		dsb(sy);					\
		write_sysreg_s(0, SYS_ ## r);			\
		isb();						\
	} while(0)

/* Fatal checks */
#define check_sr_read(r)					\
	do {							\
		__check_sr_read(r);				\
		__GUEST_ASSERT(handled, #r " no read trap");	\
	} while(0)

#define check_sr_write(r)					\
	do {							\
		__check_sr_write(r);				\
		__GUEST_ASSERT(handled, #r " no write trap");	\
	} while(0)

#define check_sr_rw(r)				\
	do {					\
		check_sr_read(r);		\
		check_sr_write(r);		\
	} while(0)

static void guest_code(void)
{
	uint64_t val;

	/*
	 * Check that we advertise that ID_AA64PFR0_EL1.GIC == 0, having
	 * hidden the feature at runtime without any other userspace action.
	 */
	__GUEST_ASSERT(FIELD_GET(ID_AA64PFR0_EL1_GIC,
				 read_sysreg(id_aa64pfr0_el1)) == 0,
		       "GICv3 wrongly advertised");

	/*
	 * Access all GICv3 registers, and fail if we don't get an UNDEF.
	 * Note that we happily access all the APxRn registers without
	 * checking their existance, as all we want to see is a failure.
	 */
	check_sr_rw(ICC_PMR_EL1);
	check_sr_read(ICC_IAR0_EL1);
	check_sr_write(ICC_EOIR0_EL1);
	check_sr_rw(ICC_HPPIR0_EL1);
	check_sr_rw(ICC_BPR0_EL1);
	check_sr_rw(ICC_AP0R0_EL1);
	check_sr_rw(ICC_AP0R1_EL1);
	check_sr_rw(ICC_AP0R2_EL1);
	check_sr_rw(ICC_AP0R3_EL1);
	check_sr_rw(ICC_AP1R0_EL1);
	check_sr_rw(ICC_AP1R1_EL1);
	check_sr_rw(ICC_AP1R2_EL1);
	check_sr_rw(ICC_AP1R3_EL1);
	check_sr_write(ICC_DIR_EL1);
	check_sr_read(ICC_RPR_EL1);
	check_sr_write(ICC_SGI1R_EL1);
	check_sr_write(ICC_ASGI1R_EL1);
	check_sr_write(ICC_SGI0R_EL1);
	check_sr_read(ICC_IAR1_EL1);
	check_sr_write(ICC_EOIR1_EL1);
	check_sr_rw(ICC_HPPIR1_EL1);
	check_sr_rw(ICC_BPR1_EL1);
	check_sr_rw(ICC_CTLR_EL1);
	check_sr_rw(ICC_IGRPEN0_EL1);
	check_sr_rw(ICC_IGRPEN1_EL1);

	/*
	 * ICC_SRE_EL1 may not be trappable, as ICC_SRE_EL2.Enable can
	 * be RAO/WI. Engage in non-fatal accesses, starting with a
	 * write of 0 to try and disable SRE, and let's see if it
	 * sticks.
	 */
	__check_sr_write(ICC_SRE_EL1);
	if (!handled)
		GUEST_PRINTF("ICC_SRE_EL1 write not trapping (OK)\n");

	val = __check_sr_read(ICC_SRE_EL1);
	if (!handled) {
		__GUEST_ASSERT((val & BIT(0)),
			       "ICC_SRE_EL1 not trapped but ICC_SRE_EL1.SRE not set\n");
		GUEST_PRINTF("ICC_SRE_EL1 read not trapping (OK)\n");
	}

	GUEST_DONE();
}

static void guest_undef_handler(struct ex_regs *regs)
{
	/* Success, we've gracefully exploded! */
	handled = true;
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

static void test_guest_no_gicv3(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	/* Create a VM without a GICv3 */
	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);

	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
				ESR_ELx_EC_UNKNOWN, guest_undef_handler);

	test_run_vcpu(vcpu);

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	uint64_t pfr0;

	test_disable_default_vgic();

	vm = vm_create_with_one_vcpu(&vcpu, NULL);
	pfr0 = vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64PFR0_EL1));
	__TEST_REQUIRE(FIELD_GET(ID_AA64PFR0_EL1_GIC, pfr0),
		       "GICv3 not supported.");
	kvm_vm_free(vm);

	test_guest_no_gicv3();

	return 0;
}
