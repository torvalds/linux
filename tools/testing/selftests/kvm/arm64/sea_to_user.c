// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test KVM returns to userspace with KVM_EXIT_ARM_SEA if host APEI fails
 * to handle SEA and userspace has opt-ed in KVM_CAP_ARM_SEA_TO_USER.
 *
 * After reaching userspace with expected arm_sea info, also test userspace
 * injecting a synchronous external data abort into the guest.
 *
 * This test utilizes EINJ to generate a REAL synchronous external data
 * abort by consuming a recoverable uncorrectable memory error. Therefore
 * the device under test must support EINJ in both firmware and host kernel,
 * including the notrigger feature. Otherwise the test will be skipped.
 * The under-test platform's APEI should be unable to claim SEA. Otherwise
 * the test will also be skipped.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "guest_modes.h"

#define PAGE_PRESENT		(1ULL << 63)
#define PAGE_PHYSICAL		0x007fffffffffffffULL
#define PAGE_ADDR_MASK		(~(0xfffULL))

/* Group ISV and ISS[23:14]. */
#define ESR_ELx_INST_SYNDROME	((ESR_ELx_ISV) | (ESR_ELx_SAS) | \
				 (ESR_ELx_SSE) | (ESR_ELx_SRT_MASK) | \
				 (ESR_ELx_SF) | (ESR_ELx_AR))

#define EINJ_ETYPE		"/sys/kernel/debug/apei/einj/error_type"
#define EINJ_ADDR		"/sys/kernel/debug/apei/einj/param1"
#define EINJ_MASK		"/sys/kernel/debug/apei/einj/param2"
#define EINJ_FLAGS		"/sys/kernel/debug/apei/einj/flags"
#define EINJ_NOTRIGGER		"/sys/kernel/debug/apei/einj/notrigger"
#define EINJ_DOIT		"/sys/kernel/debug/apei/einj/error_inject"
/* Memory Uncorrectable non-fatal. */
#define ERROR_TYPE_MEMORY_UER	0x10
/* Memory address and mask valid (param1 and param2). */
#define MASK_MEMORY_UER		0b10

/* Guest virtual address region = [2G, 3G).  */
#define START_GVA		0x80000000UL
#define VM_MEM_SIZE		0x40000000UL
/* Note: EINJ_OFFSET must < VM_MEM_SIZE. */
#define EINJ_OFFSET		0x01234badUL
#define EINJ_GVA		((START_GVA) + (EINJ_OFFSET))

static vm_paddr_t einj_gpa;
static void *einj_hva;
static uint64_t einj_hpa;
static bool far_invalid;

static uint64_t translate_to_host_paddr(unsigned long vaddr)
{
	uint64_t pinfo;
	int64_t offset = vaddr / getpagesize() * sizeof(pinfo);
	int fd;
	uint64_t page_addr;
	uint64_t paddr;

	fd = open("/proc/self/pagemap", O_RDONLY);
	if (fd < 0)
		ksft_exit_fail_perror("Failed to open /proc/self/pagemap");
	if (pread(fd, &pinfo, sizeof(pinfo), offset) != sizeof(pinfo)) {
		close(fd);
		ksft_exit_fail_perror("Failed to read /proc/self/pagemap");
	}

	close(fd);

	if ((pinfo & PAGE_PRESENT) == 0)
		ksft_exit_fail_perror("Page not present");

	page_addr = (pinfo & PAGE_PHYSICAL) << MIN_PAGE_SHIFT;
	paddr = page_addr + (vaddr & (getpagesize() - 1));
	return paddr;
}

static void write_einj_entry(const char *einj_path, uint64_t val)
{
	char cmd[256] = {0};
	FILE *cmdfile = NULL;

	sprintf(cmd, "echo %#lx > %s", val, einj_path);
	cmdfile = popen(cmd, "r");

	if (pclose(cmdfile) == 0)
		ksft_print_msg("echo %#lx > %s - done\n", val, einj_path);
	else
		ksft_exit_fail_perror("Failed to write EINJ entry");
}

static void inject_uer(uint64_t paddr)
{
	if (access("/sys/firmware/acpi/tables/EINJ", R_OK) == -1)
		ksft_test_result_skip("EINJ table no available in firmware");

	if (access(EINJ_ETYPE, R_OK | W_OK) == -1)
		ksft_test_result_skip("EINJ module probably not loaded?");

	write_einj_entry(EINJ_ETYPE, ERROR_TYPE_MEMORY_UER);
	write_einj_entry(EINJ_FLAGS, MASK_MEMORY_UER);
	write_einj_entry(EINJ_ADDR, paddr);
	write_einj_entry(EINJ_MASK, ~0x0UL);
	write_einj_entry(EINJ_NOTRIGGER, 1);
	write_einj_entry(EINJ_DOIT, 1);
}

/*
 * When host APEI successfully claims the SEA caused by guest_code, kernel
 * will send SIGBUS signal with BUS_MCEERR_AR to test thread.
 *
 * We set up this SIGBUS handler to skip the test for that case.
 */
static void sigbus_signal_handler(int sig, siginfo_t *si, void *v)
{
	ksft_print_msg("SIGBUS (%d) received, dumping siginfo...\n", sig);
	ksft_print_msg("si_signo=%d, si_errno=%d, si_code=%d, si_addr=%p\n",
		       si->si_signo, si->si_errno, si->si_code, si->si_addr);
	if (si->si_code == BUS_MCEERR_AR)
		ksft_test_result_skip("SEA is claimed by host APEI\n");
	else
		ksft_test_result_fail("Exit with signal unhandled\n");

	exit(0);
}

static void setup_sigbus_handler(void)
{
	struct sigaction act;

	memset(&act, 0, sizeof(act));
	sigemptyset(&act.sa_mask);
	act.sa_sigaction = sigbus_signal_handler;
	act.sa_flags = SA_SIGINFO;
	TEST_ASSERT(sigaction(SIGBUS, &act, NULL) == 0,
		    "Failed to setup SIGBUS handler");
}

static void guest_code(void)
{
	uint64_t guest_data;

	/* Consumes error will cause a SEA. */
	guest_data = *(uint64_t *)EINJ_GVA;

	GUEST_FAIL("Poison not protected by SEA: gva=%#lx, guest_data=%#lx\n",
		   EINJ_GVA, guest_data);
}

static void expect_sea_handler(struct ex_regs *regs)
{
	u64 esr = read_sysreg(esr_el1);
	u64 far = read_sysreg(far_el1);
	bool expect_far_invalid = far_invalid;

	GUEST_PRINTF("Handling Guest SEA\n");
	GUEST_PRINTF("ESR_EL1=%#lx, FAR_EL1=%#lx\n", esr, far);

	GUEST_ASSERT_EQ(ESR_ELx_EC(esr), ESR_ELx_EC_DABT_CUR);
	GUEST_ASSERT_EQ(esr & ESR_ELx_FSC_TYPE, ESR_ELx_FSC_EXTABT);

	if (expect_far_invalid) {
		GUEST_ASSERT_EQ(esr & ESR_ELx_FnV, ESR_ELx_FnV);
		GUEST_PRINTF("Guest observed garbage value in FAR\n");
	} else {
		GUEST_ASSERT_EQ(esr & ESR_ELx_FnV, 0);
		GUEST_ASSERT_EQ(far, EINJ_GVA);
	}

	GUEST_DONE();
}

static void vcpu_inject_sea(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_events events = {};

	events.exception.ext_dabt_pending = true;
	vcpu_events_set(vcpu, &events);
}

static void run_vm(struct kvm_vm *vm, struct kvm_vcpu *vcpu)
{
	struct ucall uc;
	bool guest_done = false;
	struct kvm_run *run = vcpu->run;
	u64 esr;

	/* Resume the vCPU after error injection to consume the error. */
	vcpu_run(vcpu);

	ksft_print_msg("Dump kvm_run info about KVM_EXIT_%s\n",
		       exit_reason_str(run->exit_reason));
	ksft_print_msg("kvm_run.arm_sea: esr=%#llx, flags=%#llx\n",
		       run->arm_sea.esr, run->arm_sea.flags);
	ksft_print_msg("kvm_run.arm_sea: gva=%#llx, gpa=%#llx\n",
		       run->arm_sea.gva, run->arm_sea.gpa);

	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_ARM_SEA);

	esr = run->arm_sea.esr;
	TEST_ASSERT_EQ(ESR_ELx_EC(esr), ESR_ELx_EC_DABT_LOW);
	TEST_ASSERT_EQ(esr & ESR_ELx_FSC_TYPE, ESR_ELx_FSC_EXTABT);
	TEST_ASSERT_EQ(ESR_ELx_ISS2(esr), 0);
	TEST_ASSERT_EQ((esr & ESR_ELx_INST_SYNDROME), 0);
	TEST_ASSERT_EQ(esr & ESR_ELx_VNCR, 0);

	if (!(esr & ESR_ELx_FnV)) {
		ksft_print_msg("Expect gva to match given FnV bit is 0\n");
		TEST_ASSERT_EQ(run->arm_sea.gva, EINJ_GVA);
	}

	if (run->arm_sea.flags & KVM_EXIT_ARM_SEA_FLAG_GPA_VALID) {
		ksft_print_msg("Expect gpa to match given KVM_EXIT_ARM_SEA_FLAG_GPA_VALID is set\n");
		TEST_ASSERT_EQ(run->arm_sea.gpa, einj_gpa & PAGE_ADDR_MASK);
	}

	far_invalid = esr & ESR_ELx_FnV;

	/* Inject a SEA into guest and expect handled in SEA handler. */
	vcpu_inject_sea(vcpu);

	/* Expect the guest to reach GUEST_DONE gracefully. */
	do {
		vcpu_run(vcpu);
		switch (get_ucall(vcpu, &uc)) {
		case UCALL_PRINTF:
			ksft_print_msg("From guest: %s", uc.buffer);
			break;
		case UCALL_DONE:
			ksft_print_msg("Guest done gracefully!\n");
			guest_done = 1;
			break;
		case UCALL_ABORT:
			ksft_print_msg("Guest aborted!\n");
			guest_done = 1;
			REPORT_GUEST_ASSERT(uc);
			break;
		default:
			TEST_FAIL("Unexpected ucall: %lu\n", uc.cmd);
		}
	} while (!guest_done);
}

static struct kvm_vm *vm_create_with_sea_handler(struct kvm_vcpu **vcpu)
{
	size_t backing_page_size;
	size_t guest_page_size;
	size_t alignment;
	uint64_t num_guest_pages;
	vm_paddr_t start_gpa;
	enum vm_mem_backing_src_type src_type = VM_MEM_SRC_ANONYMOUS_HUGETLB_1GB;
	struct kvm_vm *vm;

	backing_page_size = get_backing_src_pagesz(src_type);
	guest_page_size = vm_guest_mode_params[VM_MODE_DEFAULT].page_size;
	alignment = max(backing_page_size, guest_page_size);
	num_guest_pages = VM_MEM_SIZE / guest_page_size;

	vm = __vm_create_with_one_vcpu(vcpu, num_guest_pages, guest_code);
	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(*vcpu);

	vm_install_sync_handler(vm,
		/*vector=*/VECTOR_SYNC_CURRENT,
		/*ec=*/ESR_ELx_EC_DABT_CUR,
		/*handler=*/expect_sea_handler);

	start_gpa = (vm->max_gfn - num_guest_pages) * guest_page_size;
	start_gpa = align_down(start_gpa, alignment);

	vm_userspace_mem_region_add(
		/*vm=*/vm,
		/*src_type=*/src_type,
		/*guest_paddr=*/start_gpa,
		/*slot=*/1,
		/*npages=*/num_guest_pages,
		/*flags=*/0);

	virt_map(vm, START_GVA, start_gpa, num_guest_pages);

	ksft_print_msg("Mapped %#lx pages: gva=%#lx to gpa=%#lx\n",
		       num_guest_pages, START_GVA, start_gpa);
	return vm;
}

static void vm_inject_memory_uer(struct kvm_vm *vm)
{
	uint64_t guest_data;

	einj_gpa = addr_gva2gpa(vm, EINJ_GVA);
	einj_hva = addr_gva2hva(vm, EINJ_GVA);

	/* Populate certain data before injecting UER. */
	*(uint64_t *)einj_hva = 0xBAADCAFE;
	guest_data = *(uint64_t *)einj_hva;
	ksft_print_msg("Before EINJect: data=%#lx\n",
		guest_data);

	einj_hpa = translate_to_host_paddr((unsigned long)einj_hva);

	ksft_print_msg("EINJ_GVA=%#lx, einj_gpa=%#lx, einj_hva=%p, einj_hpa=%#lx\n",
		       EINJ_GVA, einj_gpa, einj_hva, einj_hpa);

	inject_uer(einj_hpa);
	ksft_print_msg("Memory UER EINJected\n");
}

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_ARM_SEA_TO_USER));

	setup_sigbus_handler();

	vm = vm_create_with_sea_handler(&vcpu);
	vm_enable_cap(vm, KVM_CAP_ARM_SEA_TO_USER, 0);
	vm_inject_memory_uer(vm);
	run_vm(vm, vcpu);
	kvm_vm_free(vm);

	return 0;
}
