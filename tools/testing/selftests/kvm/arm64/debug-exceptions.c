// SPDX-License-Identifier: GPL-2.0
#include <test_util.h>
#include <kvm_util.h>
#include <processor.h>
#include <linux/bitfield.h>

#define MDSCR_KDE	(1 << 13)
#define MDSCR_MDE	(1 << 15)
#define MDSCR_SS	(1 << 0)

#define DBGBCR_LEN8	(0xff << 5)
#define DBGBCR_EXEC	(0x0 << 3)
#define DBGBCR_EL1	(0x1 << 1)
#define DBGBCR_E	(0x1 << 0)
#define DBGBCR_LBN_SHIFT	16
#define DBGBCR_BT_SHIFT		20
#define DBGBCR_BT_ADDR_LINK_CTX	(0x1 << DBGBCR_BT_SHIFT)
#define DBGBCR_BT_CTX_LINK	(0x3 << DBGBCR_BT_SHIFT)

#define DBGWCR_LEN8	(0xff << 5)
#define DBGWCR_RD	(0x1 << 3)
#define DBGWCR_WR	(0x2 << 3)
#define DBGWCR_EL1	(0x1 << 1)
#define DBGWCR_E	(0x1 << 0)
#define DBGWCR_LBN_SHIFT	16
#define DBGWCR_WT_SHIFT		20
#define DBGWCR_WT_LINK		(0x1 << DBGWCR_WT_SHIFT)

#define SPSR_D		(1 << 9)
#define SPSR_SS		(1 << 21)

extern unsigned char sw_bp, sw_bp2, hw_bp, hw_bp2, bp_svc, bp_brk, hw_wp, ss_start, hw_bp_ctx;
extern unsigned char iter_ss_begin, iter_ss_end;
static volatile uint64_t sw_bp_addr, hw_bp_addr;
static volatile uint64_t wp_addr, wp_data_addr;
static volatile uint64_t svc_addr;
static volatile uint64_t ss_addr[4], ss_idx;
#define  PC(v)  ((uint64_t)&(v))

#define GEN_DEBUG_WRITE_REG(reg_name)			\
static void write_##reg_name(int num, uint64_t val)	\
{							\
	switch (num) {					\
	case 0:						\
		write_sysreg(val, reg_name##0_el1);	\
		break;					\
	case 1:						\
		write_sysreg(val, reg_name##1_el1);	\
		break;					\
	case 2:						\
		write_sysreg(val, reg_name##2_el1);	\
		break;					\
	case 3:						\
		write_sysreg(val, reg_name##3_el1);	\
		break;					\
	case 4:						\
		write_sysreg(val, reg_name##4_el1);	\
		break;					\
	case 5:						\
		write_sysreg(val, reg_name##5_el1);	\
		break;					\
	case 6:						\
		write_sysreg(val, reg_name##6_el1);	\
		break;					\
	case 7:						\
		write_sysreg(val, reg_name##7_el1);	\
		break;					\
	case 8:						\
		write_sysreg(val, reg_name##8_el1);	\
		break;					\
	case 9:						\
		write_sysreg(val, reg_name##9_el1);	\
		break;					\
	case 10:					\
		write_sysreg(val, reg_name##10_el1);	\
		break;					\
	case 11:					\
		write_sysreg(val, reg_name##11_el1);	\
		break;					\
	case 12:					\
		write_sysreg(val, reg_name##12_el1);	\
		break;					\
	case 13:					\
		write_sysreg(val, reg_name##13_el1);	\
		break;					\
	case 14:					\
		write_sysreg(val, reg_name##14_el1);	\
		break;					\
	case 15:					\
		write_sysreg(val, reg_name##15_el1);	\
		break;					\
	default:					\
		GUEST_ASSERT(0);			\
	}						\
}

/* Define write_dbgbcr()/write_dbgbvr()/write_dbgwcr()/write_dbgwvr() */
GEN_DEBUG_WRITE_REG(dbgbcr)
GEN_DEBUG_WRITE_REG(dbgbvr)
GEN_DEBUG_WRITE_REG(dbgwcr)
GEN_DEBUG_WRITE_REG(dbgwvr)

static void reset_debug_state(void)
{
	uint8_t brps, wrps, i;
	uint64_t dfr0;

	asm volatile("msr daifset, #8");

	write_sysreg(0, osdlr_el1);
	write_sysreg(0, oslar_el1);
	isb();

	write_sysreg(0, mdscr_el1);
	write_sysreg(0, contextidr_el1);

	/* Reset all bcr/bvr/wcr/wvr registers */
	dfr0 = read_sysreg(id_aa64dfr0_el1);
	brps = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_BRPs), dfr0);
	for (i = 0; i <= brps; i++) {
		write_dbgbcr(i, 0);
		write_dbgbvr(i, 0);
	}
	wrps = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_WRPs), dfr0);
	for (i = 0; i <= wrps; i++) {
		write_dbgwcr(i, 0);
		write_dbgwvr(i, 0);
	}

	isb();
}

static void enable_os_lock(void)
{
	write_sysreg(1, oslar_el1);
	isb();

	GUEST_ASSERT(read_sysreg(oslsr_el1) & 2);
}

static void enable_monitor_debug_exceptions(void)
{
	uint64_t mdscr;

	asm volatile("msr daifclr, #8");

	mdscr = read_sysreg(mdscr_el1) | MDSCR_KDE | MDSCR_MDE;
	write_sysreg(mdscr, mdscr_el1);
	isb();
}

static void install_wp(uint8_t wpn, uint64_t addr)
{
	uint32_t wcr;

	wcr = DBGWCR_LEN8 | DBGWCR_RD | DBGWCR_WR | DBGWCR_EL1 | DBGWCR_E;
	write_dbgwcr(wpn, wcr);
	write_dbgwvr(wpn, addr);

	isb();

	enable_monitor_debug_exceptions();
}

static void install_hw_bp(uint8_t bpn, uint64_t addr)
{
	uint32_t bcr;

	bcr = DBGBCR_LEN8 | DBGBCR_EXEC | DBGBCR_EL1 | DBGBCR_E;
	write_dbgbcr(bpn, bcr);
	write_dbgbvr(bpn, addr);
	isb();

	enable_monitor_debug_exceptions();
}

static void install_wp_ctx(uint8_t addr_wp, uint8_t ctx_bp, uint64_t addr,
			   uint64_t ctx)
{
	uint32_t wcr;
	uint64_t ctx_bcr;

	/* Setup a context-aware breakpoint for Linked Context ID Match */
	ctx_bcr = DBGBCR_LEN8 | DBGBCR_EXEC | DBGBCR_EL1 | DBGBCR_E |
		  DBGBCR_BT_CTX_LINK;
	write_dbgbcr(ctx_bp, ctx_bcr);
	write_dbgbvr(ctx_bp, ctx);

	/* Setup a linked watchpoint (linked to the context-aware breakpoint) */
	wcr = DBGWCR_LEN8 | DBGWCR_RD | DBGWCR_WR | DBGWCR_EL1 | DBGWCR_E |
	      DBGWCR_WT_LINK | ((uint32_t)ctx_bp << DBGWCR_LBN_SHIFT);
	write_dbgwcr(addr_wp, wcr);
	write_dbgwvr(addr_wp, addr);
	isb();

	enable_monitor_debug_exceptions();
}

void install_hw_bp_ctx(uint8_t addr_bp, uint8_t ctx_bp, uint64_t addr,
		       uint64_t ctx)
{
	uint32_t addr_bcr, ctx_bcr;

	/* Setup a context-aware breakpoint for Linked Context ID Match */
	ctx_bcr = DBGBCR_LEN8 | DBGBCR_EXEC | DBGBCR_EL1 | DBGBCR_E |
		  DBGBCR_BT_CTX_LINK;
	write_dbgbcr(ctx_bp, ctx_bcr);
	write_dbgbvr(ctx_bp, ctx);

	/*
	 * Setup a normal breakpoint for Linked Address Match, and link it
	 * to the context-aware breakpoint.
	 */
	addr_bcr = DBGBCR_LEN8 | DBGBCR_EXEC | DBGBCR_EL1 | DBGBCR_E |
		   DBGBCR_BT_ADDR_LINK_CTX |
		   ((uint32_t)ctx_bp << DBGBCR_LBN_SHIFT);
	write_dbgbcr(addr_bp, addr_bcr);
	write_dbgbvr(addr_bp, addr);
	isb();

	enable_monitor_debug_exceptions();
}

static void install_ss(void)
{
	uint64_t mdscr;

	asm volatile("msr daifclr, #8");

	mdscr = read_sysreg(mdscr_el1) | MDSCR_KDE | MDSCR_SS;
	write_sysreg(mdscr, mdscr_el1);
	isb();
}

static volatile char write_data;

static void guest_code(uint8_t bpn, uint8_t wpn, uint8_t ctx_bpn)
{
	uint64_t ctx = 0xabcdef;	/* a random context number */

	/* Software-breakpoint */
	reset_debug_state();
	asm volatile("sw_bp: brk #0");
	GUEST_ASSERT_EQ(sw_bp_addr, PC(sw_bp));

	/* Hardware-breakpoint */
	reset_debug_state();
	install_hw_bp(bpn, PC(hw_bp));
	asm volatile("hw_bp: nop");
	GUEST_ASSERT_EQ(hw_bp_addr, PC(hw_bp));

	/* Hardware-breakpoint + svc */
	reset_debug_state();
	install_hw_bp(bpn, PC(bp_svc));
	asm volatile("bp_svc: svc #0");
	GUEST_ASSERT_EQ(hw_bp_addr, PC(bp_svc));
	GUEST_ASSERT_EQ(svc_addr, PC(bp_svc) + 4);

	/* Hardware-breakpoint + software-breakpoint */
	reset_debug_state();
	install_hw_bp(bpn, PC(bp_brk));
	asm volatile("bp_brk: brk #0");
	GUEST_ASSERT_EQ(sw_bp_addr, PC(bp_brk));
	GUEST_ASSERT_EQ(hw_bp_addr, PC(bp_brk));

	/* Watchpoint */
	reset_debug_state();
	install_wp(wpn, PC(write_data));
	write_data = 'x';
	GUEST_ASSERT_EQ(write_data, 'x');
	GUEST_ASSERT_EQ(wp_data_addr, PC(write_data));

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

	/* OS Lock does not block software-breakpoint */
	reset_debug_state();
	enable_os_lock();
	sw_bp_addr = 0;
	asm volatile("sw_bp2: brk #0");
	GUEST_ASSERT_EQ(sw_bp_addr, PC(sw_bp2));

	/* OS Lock blocking hardware-breakpoint */
	reset_debug_state();
	enable_os_lock();
	install_hw_bp(bpn, PC(hw_bp2));
	hw_bp_addr = 0;
	asm volatile("hw_bp2: nop");
	GUEST_ASSERT_EQ(hw_bp_addr, 0);

	/* OS Lock blocking watchpoint */
	reset_debug_state();
	enable_os_lock();
	write_data = '\0';
	wp_data_addr = 0;
	install_wp(wpn, PC(write_data));
	write_data = 'x';
	GUEST_ASSERT_EQ(write_data, 'x');
	GUEST_ASSERT_EQ(wp_data_addr, 0);

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

	/* Linked hardware-breakpoint */
	hw_bp_addr = 0;
	reset_debug_state();
	install_hw_bp_ctx(bpn, ctx_bpn, PC(hw_bp_ctx), ctx);
	/* Set context id */
	write_sysreg(ctx, contextidr_el1);
	isb();
	asm volatile("hw_bp_ctx: nop");
	write_sysreg(0, contextidr_el1);
	GUEST_ASSERT_EQ(hw_bp_addr, PC(hw_bp_ctx));

	/* Linked watchpoint */
	reset_debug_state();
	install_wp_ctx(wpn, ctx_bpn, PC(write_data), ctx);
	/* Set context id */
	write_sysreg(ctx, contextidr_el1);
	isb();
	write_data = 'x';
	GUEST_ASSERT_EQ(write_data, 'x');
	GUEST_ASSERT_EQ(wp_data_addr, PC(write_data));

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
	__GUEST_ASSERT(ss_idx < 4, "Expected index < 4, got '%lu'", ss_idx);
	ss_addr[ss_idx++] = regs->pc;
	regs->pstate |= SPSR_SS;
}

static void guest_svc_handler(struct ex_regs *regs)
{
	svc_addr = regs->pc;
}

static void guest_code_ss(int test_cnt)
{
	uint64_t i;
	uint64_t bvr, wvr, w_bvr, w_wvr;

	for (i = 0; i < test_cnt; i++) {
		/* Bits [1:0] of dbg{b,w}vr are RES0 */
		w_bvr = i << 2;
		w_wvr = i << 2;

		/*
		 * Enable Single Step execution.  Note!  This _must_ be a bare
		 * ucall as the ucall() path uses atomic operations to manage
		 * the ucall structures, and the built-in "atomics" are usually
		 * implemented via exclusive access instructions.  The exlusive
		 * monitor is cleared on ERET, and so taking debug exceptions
		 * during a LDREX=>STREX sequence will prevent forward progress
		 * and hang the guest/test.
		 */
		GUEST_UCALL_NONE();

		/*
		 * The userspace will verify that the pc is as expected during
		 * single step execution between iter_ss_begin and iter_ss_end.
		 */
		asm volatile("iter_ss_begin:nop\n");

		write_sysreg(w_bvr, dbgbvr0_el1);
		write_sysreg(w_wvr, dbgwvr0_el1);
		bvr = read_sysreg(dbgbvr0_el1);
		wvr = read_sysreg(dbgwvr0_el1);

		/* Userspace disables Single Step when the end is nigh. */
		asm volatile("iter_ss_end:\n");

		GUEST_ASSERT_EQ(bvr, w_bvr);
		GUEST_ASSERT_EQ(wvr, w_wvr);
	}
	GUEST_DONE();
}

static int debug_version(uint64_t id_aa64dfr0)
{
	return FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_DebugVer), id_aa64dfr0);
}

static void test_guest_debug_exceptions(uint8_t bpn, uint8_t wpn, uint8_t ctx_bpn)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);

	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
				ESR_ELx_EC_BRK64, guest_sw_bp_handler);
	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
				ESR_ELx_EC_BREAKPT_CUR, guest_hw_bp_handler);
	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
				ESR_ELx_EC_WATCHPT_CUR, guest_wp_handler);
	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
				ESR_ELx_EC_SOFTSTP_CUR, guest_ss_handler);
	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT,
				ESR_ELx_EC_SVC64, guest_svc_handler);

	/* Specify bpn/wpn/ctx_bpn to be tested */
	vcpu_args_set(vcpu, 3, bpn, wpn, ctx_bpn);
	pr_debug("Use bpn#%d, wpn#%d and ctx_bpn#%d\n", bpn, wpn, ctx_bpn);

	vcpu_run(vcpu);
	switch (get_ucall(vcpu, &uc)) {
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
		break;
	case UCALL_DONE:
		goto done;
	default:
		TEST_FAIL("Unknown ucall %lu", uc.cmd);
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

			TEST_ASSERT(cmd == UCALL_NONE,
				    "Unexpected ucall cmd 0x%lx", cmd);

			debug.control = KVM_GUESTDBG_ENABLE |
					KVM_GUESTDBG_SINGLESTEP;
			ss_enable = true;
			vcpu_guest_debug_set(vcpu, &debug);
			continue;
		}

		TEST_ASSERT(ss_enable, "Unexpected KVM_EXIT_DEBUG");

		/* Check if the current pc is expected. */
		pc = vcpu_get_reg(vcpu, ARM64_CORE_REG(regs.pc));
		TEST_ASSERT(!test_pc || pc == test_pc,
			    "Unexpected pc 0x%lx (expected 0x%lx)",
			    pc, test_pc);

		if ((pc + 4) == (uint64_t)&iter_ss_end) {
			test_pc = 0;
			debug.control = KVM_GUESTDBG_ENABLE;
			ss_enable = false;
			vcpu_guest_debug_set(vcpu, &debug);
			continue;
		}

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

/*
 * Run debug testing using the various breakpoint#, watchpoint# and
 * context-aware breakpoint# with the given ID_AA64DFR0_EL1 configuration.
 */
void test_guest_debug_exceptions_all(uint64_t aa64dfr0)
{
	uint8_t brp_num, wrp_num, ctx_brp_num, normal_brp_num, ctx_brp_base;
	int b, w, c;

	/* Number of breakpoints */
	brp_num = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_BRPs), aa64dfr0) + 1;
	__TEST_REQUIRE(brp_num >= 2, "At least two breakpoints are required");

	/* Number of watchpoints */
	wrp_num = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_WRPs), aa64dfr0) + 1;

	/* Number of context aware breakpoints */
	ctx_brp_num = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_CTX_CMPs), aa64dfr0) + 1;

	pr_debug("%s brp_num:%d, wrp_num:%d, ctx_brp_num:%d\n", __func__,
		 brp_num, wrp_num, ctx_brp_num);

	/* Number of normal (non-context aware) breakpoints */
	normal_brp_num = brp_num - ctx_brp_num;

	/* Lowest context aware breakpoint number */
	ctx_brp_base = normal_brp_num;

	/* Run tests with all supported breakpoints/watchpoints */
	for (c = ctx_brp_base; c < ctx_brp_base + ctx_brp_num; c++) {
		for (b = 0; b < normal_brp_num; b++) {
			for (w = 0; w < wrp_num; w++)
				test_guest_debug_exceptions(b, w, c);
		}
	}
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
	uint64_t aa64dfr0;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	aa64dfr0 = vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64DFR0_EL1));
	__TEST_REQUIRE(debug_version(aa64dfr0) >= 6,
		       "Armv8 debug architecture not supported.");
	kvm_vm_free(vm);

	while ((opt = getopt(argc, argv, "i:")) != -1) {
		switch (opt) {
		case 'i':
			ss_iteration = atoi_positive("Number of iterations", optarg);
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	test_guest_debug_exceptions_all(aa64dfr0);
	test_single_step_from_userspace(ss_iteration);

	return 0;
}
