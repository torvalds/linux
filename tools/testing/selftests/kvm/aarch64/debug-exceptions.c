// SPDX-License-Identifier: GPL-2.0
#include <test_util.h>
#include <kvm_util.h>
#include <processor.h>

#define VCPU_ID 0

#define MDSCR_KDE	(1 << 13)
#define MDSCR_MDE	(1 << 15)
#define MDSCR_SS	(1 << 0)

#define DBGBCR_LEN8	(0xff << 5)
#define DBGBCR_EXEC	(0x0 << 3)
#define DBGBCR_EL1	(0x1 << 1)
#define DBGBCR_E	(0x1 << 0)

#define DBGWCR_LEN8	(0xff << 5)
#define DBGWCR_RD	(0x1 << 3)
#define DBGWCR_WR	(0x2 << 3)
#define DBGWCR_EL1	(0x1 << 1)
#define DBGWCR_E	(0x1 << 0)

#define SPSR_D		(1 << 9)
#define SPSR_SS		(1 << 21)

extern unsigned char sw_bp, sw_bp2, hw_bp, hw_bp2, bp_svc, bp_brk, hw_wp, ss_start;
static volatile uint64_t sw_bp_addr, hw_bp_addr;
static volatile uint64_t wp_addr, wp_data_addr;
static volatile uint64_t svc_addr;
static volatile uint64_t ss_addr[4], ss_idx;
#define  PC(v)  ((uint64_t)&(v))

static void reset_debug_state(void)
{
	asm volatile("msr daifset, #8");

	write_sysreg(0, osdlr_el1);
	write_sysreg(0, oslar_el1);
	isb();

	write_sysreg(0, mdscr_el1);
	/* This test only uses the first bp and wp slot. */
	write_sysreg(0, dbgbvr0_el1);
	write_sysreg(0, dbgbcr0_el1);
	write_sysreg(0, dbgwcr0_el1);
	write_sysreg(0, dbgwvr0_el1);
	isb();
}

static void enable_os_lock(void)
{
	write_sysreg(1, oslar_el1);
	isb();

	GUEST_ASSERT(read_sysreg(oslsr_el1) & 2);
}

static void install_wp(uint64_t addr)
{
	uint32_t wcr;
	uint32_t mdscr;

	wcr = DBGWCR_LEN8 | DBGWCR_RD | DBGWCR_WR | DBGWCR_EL1 | DBGWCR_E;
	write_sysreg(wcr, dbgwcr0_el1);
	write_sysreg(addr, dbgwvr0_el1);
	isb();

	asm volatile("msr daifclr, #8");

	mdscr = read_sysreg(mdscr_el1) | MDSCR_KDE | MDSCR_MDE;
	write_sysreg(mdscr, mdscr_el1);
	isb();
}

static void install_hw_bp(uint64_t addr)
{
	uint32_t bcr;
	uint32_t mdscr;

	bcr = DBGBCR_LEN8 | DBGBCR_EXEC | DBGBCR_EL1 | DBGBCR_E;
	write_sysreg(bcr, dbgbcr0_el1);
	write_sysreg(addr, dbgbvr0_el1);
	isb();

	asm volatile("msr daifclr, #8");

	mdscr = read_sysreg(mdscr_el1) | MDSCR_KDE | MDSCR_MDE;
	write_sysreg(mdscr, mdscr_el1);
	isb();
}

static void install_ss(void)
{
	uint32_t mdscr;

	asm volatile("msr daifclr, #8");

	mdscr = read_sysreg(mdscr_el1) | MDSCR_KDE | MDSCR_SS;
	write_sysreg(mdscr, mdscr_el1);
	isb();
}

static volatile char write_data;

static void guest_code(void)
{
	GUEST_SYNC(0);

	/* Software-breakpoint */
	reset_debug_state();
	asm volatile("sw_bp: brk #0");
	GUEST_ASSERT_EQ(sw_bp_addr, PC(sw_bp));

	GUEST_SYNC(1);

	/* Hardware-breakpoint */
	reset_debug_state();
	install_hw_bp(PC(hw_bp));
	asm volatile("hw_bp: nop");
	GUEST_ASSERT_EQ(hw_bp_addr, PC(hw_bp));

	GUEST_SYNC(2);

	/* Hardware-breakpoint + svc */
	reset_debug_state();
	install_hw_bp(PC(bp_svc));
	asm volatile("bp_svc: svc #0");
	GUEST_ASSERT_EQ(hw_bp_addr, PC(bp_svc));
	GUEST_ASSERT_EQ(svc_addr, PC(bp_svc) + 4);

	GUEST_SYNC(3);

	/* Hardware-breakpoint + software-breakpoint */
	reset_debug_state();
	install_hw_bp(PC(bp_brk));
	asm volatile("bp_brk: brk #0");
	GUEST_ASSERT_EQ(sw_bp_addr, PC(bp_brk));
	GUEST_ASSERT_EQ(hw_bp_addr, PC(bp_brk));

	GUEST_SYNC(4);

	/* Watchpoint */
	reset_debug_state();
	install_wp(PC(write_data));
	write_data = 'x';
	GUEST_ASSERT_EQ(write_data, 'x');
	GUEST_ASSERT_EQ(wp_data_addr, PC(write_data));

	GUEST_SYNC(5);

	/* Single-step */
	reset_debug_state();
	install_ss();
	ss_idx = 0;
	asm volatile("ss_start:\n"
		     "mrs x0, esr_el1\n"
		     "add x0, x0, #1\n"
		     "msr daifset, #8\n"
		     : : : "x0");
	GUEST_ASSERT_EQ(ss_addr[0], PC(ss_start));
	GUEST_ASSERT_EQ(ss_addr[1], PC(ss_start) + 4);
	GUEST_ASSERT_EQ(ss_addr[2], PC(ss_start) + 8);

	GUEST_SYNC(6);

	/* OS Lock does not block software-breakpoint */
	reset_debug_state();
	enable_os_lock();
	sw_bp_addr = 0;
	asm volatile("sw_bp2: brk #0");
	GUEST_ASSERT_EQ(sw_bp_addr, PC(sw_bp2));

	GUEST_SYNC(7);

	/* OS Lock blocking hardware-breakpoint */
	reset_debug_state();
	enable_os_lock();
	install_hw_bp(PC(hw_bp2));
	hw_bp_addr = 0;
	asm volatile("hw_bp2: nop");
	GUEST_ASSERT_EQ(hw_bp_addr, 0);

	GUEST_SYNC(8);

	/* OS Lock blocking watchpoint */
	reset_debug_state();
	enable_os_lock();
	write_data = '\0';
	wp_data_addr = 0;
	install_wp(PC(write_data));
	write_data = 'x';
	GUEST_ASSERT_EQ(write_data, 'x');
	GUEST_ASSERT_EQ(wp_data_addr, 0);

	GUEST_SYNC(9);

	/* OS Lock blocking single-step */
	reset_debug_state();
	enable_os_lock();
	ss_addr[0] = 0;
	install_ss();
	ss_idx = 0;
	asm volatile("mrs x0, esr_el1\n\t"
		     "add x0, x0, #1\n\t"
		     "msr daifset, #8\n\t"
		     : : : "x0");
	GUEST_ASSERT_EQ(ss_addr[0], 0);

	GUEST_DONE();
}

static void guest_sw_bp_handler(struct ex_regs *regs)
{
	sw_bp_addr = regs->pc;
	regs->pc += 4;
}

static void guest_hw_bp_handler(struct ex_regs *regs)
{
	hw_bp_addr = regs->pc;
	regs->pstate |= SPSR_D;
}

static void guest_wp_handler(struct ex_regs *regs)
{
	wp_data_addr = read_sysreg(far_el1);
	wp_addr = regs->pc;
	regs->pstate |= SPSR_D;
}

static void guest_ss_handler(struct ex_regs *regs)
{
	GUEST_ASSERT_1(ss_idx < 4, ss_idx);
	ss_addr[ss_idx++] = regs->pc;
	regs->pstate |= SPSR_SS;
}

static void guest_svc_handler(struct ex_regs *regs)
{
	svc_addr = regs->pc;
}

static int debug_version(struct kvm_vm *vm)
{
	uint64_t id_aa64dfr0;

	get_reg(vm, VCPU_ID, KVM_ARM64_SYS_REG(SYS_ID_AA64DFR0_EL1), &id_aa64dfr0);
	return id_aa64dfr0 & 0xf;
}

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	struct ucall uc;
	int stage;

	vm = vm_create_default(VCPU_ID, 0, guest_code);
	ucall_init(vm, NULL);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);

	if (debug_version(vm) < 6) {
		print_skip("Armv8 debug architecture not supported.");
		kvm_vm_free(vm);
		exit(KSFT_SKIP);
	}

	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
				ESR_EC_BRK_INS, guest_sw_bp_handler);
	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
				ESR_EC_HW_BP_CURRENT, guest_hw_bp_handler);
	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
				ESR_EC_WP_CURRENT, guest_wp_handler);
	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
				ESR_EC_SSTEP_CURRENT, guest_ss_handler);
	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
				ESR_EC_SVC64, guest_svc_handler);

	for (stage = 0; stage < 11; stage++) {
		vcpu_run(vm, VCPU_ID);

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_SYNC:
			TEST_ASSERT(uc.args[1] == stage,
				"Stage %d: Unexpected sync ucall, got %lx",
				stage, (ulong)uc.args[1]);
			break;
		case UCALL_ABORT:
			TEST_FAIL("%s at %s:%ld\n\tvalues: %#lx, %#lx",
				(const char *)uc.args[0],
				__FILE__, uc.args[1], uc.args[2], uc.args[3]);
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}

done:
	kvm_vm_free(vm);
	return 0;
}
