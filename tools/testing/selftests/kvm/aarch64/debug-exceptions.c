// SPDX-License-Identifier: GPL-2.0
#include <test_util.h>
#include <kvm_util.h>
#include <processor.h>

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
extern unsigned char iter_ss_begin, iter_ss_end;
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

enum single_step_op {
	SINGLE_STEP_ENABLE = 0,
	SINGLE_STEP_DISABLE = 1,
};

static void guest_code_ss(int test_cnt)
{
	uint64_t i;
	uint64_t bvr, wvr, w_bvr, w_wvr;

	for (i = 0; i < test_cnt; i++) {
		/* Bits [1:0] of dbg{b,w}vr are RES0 */
		w_bvr = i << 2;
		w_wvr = i << 2;

		/* Enable Single Step execution */
		GUEST_SYNC(SINGLE_STEP_ENABLE);

		/*
		 * The userspace will veriry that the pc is as expected during
		 * single step execution between iter_ss_begin and iter_ss_end.
		 */
		asm volatile("iter_ss_begin:nop\n");

		write_sysreg(w_bvr, dbgbvr0_el1);
		write_sysreg(w_wvr, dbgwvr0_el1);
		bvr = read_sysreg(dbgbvr0_el1);
		wvr = read_sysreg(dbgwvr0_el1);

		asm volatile("iter_ss_end:\n");

		/* Disable Single Step execution */
		GUEST_SYNC(SINGLE_STEP_DISABLE);

		GUEST_ASSERT(bvr == w_bvr);
		GUEST_ASSERT(wvr == w_wvr);
	}
	GUEST_DONE();
}

static int debug_version(struct kvm_vcpu *vcpu)
{
	uint64_t id_aa64dfr0;

	vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64DFR0_EL1), &id_aa64dfr0);
	return id_aa64dfr0 & 0xf;
}

static void test_guest_debug_exceptions(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	int stage;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	ucall_init(vm, NULL);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);

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
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			TEST_ASSERT(uc.args[1] == stage,
				"Stage %d: Unexpected sync ucall, got %lx",
				stage, (ulong)uc.args[1]);
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT_2(uc, "values: %#lx, %#lx");
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}

done:
	kvm_vm_free(vm);
}

void test_single_step_from_userspace(int test_cnt)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	struct kvm_run *run;
	uint64_t pc, cmd;
	uint64_t test_pc = 0;
	bool ss_enable = false;
	struct kvm_guest_debug debug = {};

	vm = vm_create_with_one_vcpu(&vcpu, guest_code_ss);
	ucall_init(vm, NULL);
	run = vcpu->run;
	vcpu_args_set(vcpu, 1, test_cnt);

	while (1) {
		vcpu_run(vcpu);
		if (run->exit_reason != KVM_EXIT_DEBUG) {
			cmd = get_ucall(vcpu, &uc);
			if (cmd == UCALL_ABORT) {
				REPORT_GUEST_ASSERT(uc);
				/* NOT REACHED */
			} else if (cmd == UCALL_DONE) {
				break;
			}

			TEST_ASSERT(cmd == UCALL_SYNC,
				    "Unexpected ucall cmd 0x%lx", cmd);

			if (uc.args[1] == SINGLE_STEP_ENABLE) {
				debug.control = KVM_GUESTDBG_ENABLE |
						KVM_GUESTDBG_SINGLESTEP;
				ss_enable = true;
			} else {
				debug.control = SINGLE_STEP_DISABLE;
				ss_enable = false;
			}

			vcpu_guest_debug_set(vcpu, &debug);
			continue;
		}

		TEST_ASSERT(ss_enable, "Unexpected KVM_EXIT_DEBUG");

		/* Check if the current pc is expected. */
		vcpu_get_reg(vcpu, ARM64_CORE_REG(regs.pc), &pc);
		TEST_ASSERT(!test_pc || pc == test_pc,
			    "Unexpected pc 0x%lx (expected 0x%lx)",
			    pc, test_pc);

		/*
		 * If the current pc is between iter_ss_bgin and
		 * iter_ss_end, the pc for the next KVM_EXIT_DEBUG should
		 * be the current pc + 4.
		 */
		if ((pc >= (uint64_t)&iter_ss_begin) &&
		    (pc < (uint64_t)&iter_ss_end))
			test_pc = pc + 4;
		else
			test_pc = 0;
	}

	kvm_vm_free(vm);
}

static void help(char *name)
{
	puts("");
	printf("Usage: %s [-h] [-i iterations of the single step test]\n", name);
	puts("");
	exit(0);
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	int opt;
	int ss_iteration = 10000;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	__TEST_REQUIRE(debug_version(vcpu) >= 6,
		       "Armv8 debug architecture not supported.");
	kvm_vm_free(vm);

	while ((opt = getopt(argc, argv, "i:")) != -1) {
		switch (opt) {
		case 'i':
			ss_iteration = atoi(optarg);
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	test_guest_debug_exceptions();
	test_single_step_from_userspace(ss_iteration);

	return 0;
}
