// SPDX-License-Identifier: GPL-2.0
/*
 * vgic_irq.c - Test userspace injection of IRQs
 *
 * This test validates the injection of IRQs from userspace using various
 * methods (e.g., KVM_IRQ_LINE) and modes (e.g., EOI). The guest "asks" the
 * host to inject a specific intid via a GUEST_SYNC call, and then checks that
 * it received it.
 */

#include <asm/kvm.h>
#include <asm/kvm_para.h>
#include <linux/sizes.h>

#include "processor.h"
#include "test_util.h"
#include "kvm_util.h"
#include "gic.h"
#include "gic_v3.h"
#include "vgic.h"

#define GICD_BASE_GPA		0x08000000ULL
#define GICR_BASE_GPA		0x080A0000ULL
#define VCPU_ID			0

/*
 * KVM implements 32 priority levels:
 * 0x00 (highest priority) - 0xF8 (lowest priority), in steps of 8
 *
 * Note that these macros will still be correct in the case that KVM implements
 * more priority levels. Also note that 32 is the minimum for GICv3 and GICv2.
 */
#define KVM_NUM_PRIOS		32
#define KVM_PRIO_SHIFT		3 /* steps of 8 = 1 << 3 */
#define LOWEST_PRIO		(KVM_NUM_PRIOS - 1)
#define CPU_PRIO_MASK		(LOWEST_PRIO << KVM_PRIO_SHIFT)	/* 0xf8 */
#define IRQ_DEFAULT_PRIO	(LOWEST_PRIO - 1)
#define IRQ_DEFAULT_PRIO_REG	(IRQ_DEFAULT_PRIO << KVM_PRIO_SHIFT) /* 0xf0 */

static void *dist = (void *)GICD_BASE_GPA;
static void *redist = (void *)GICR_BASE_GPA;

/*
 * The kvm_inject_* utilities are used by the guest to ask the host to inject
 * interrupts (e.g., using the KVM_IRQ_LINE ioctl).
 */

typedef enum {
	KVM_INJECT_EDGE_IRQ_LINE = 1,
} kvm_inject_cmd;

struct kvm_inject_args {
	kvm_inject_cmd cmd;
	uint32_t intid;
};

/* Used on the guest side to perform the hypercall. */
static void kvm_inject_call(kvm_inject_cmd cmd, uint32_t intid);

#define KVM_INJECT(cmd, intid)							\
	kvm_inject_call(cmd, intid)

/* Used on the host side to get the hypercall info. */
static void kvm_inject_get_call(struct kvm_vm *vm, struct ucall *uc,
		struct kvm_inject_args *args);

struct kvm_inject_desc {
	kvm_inject_cmd cmd;
	/* can inject PPIs, PPIs, and/or SPIs. */
	bool sgi, ppi, spi;
};

static struct kvm_inject_desc inject_edge_fns[] = {
	/*                                      sgi    ppi    spi */
	{ KVM_INJECT_EDGE_IRQ_LINE,		false, false, true },
	{ 0, },
};

#define for_each_inject_fn(t, f)						\
	for ((f) = (t); (f)->cmd; (f)++)

/* Shared between the guest main thread and the IRQ handlers. */
volatile uint64_t irq_handled;
volatile uint32_t irqnr_received[MAX_SPI + 1];

static void reset_stats(void)
{
	int i;

	irq_handled = 0;
	for (i = 0; i <= MAX_SPI; i++)
		irqnr_received[i] = 0;
}

static uint64_t gic_read_ap1r0(void)
{
	uint64_t reg = read_sysreg_s(SYS_ICV_AP1R0_EL1);

	dsb(sy);
	return reg;
}

static void guest_irq_handler(struct ex_regs *regs)
{
	uint32_t intid = gic_get_and_ack_irq();

	if (intid == IAR_SPURIOUS)
		return;

	GUEST_ASSERT(gic_irq_get_active(intid));

	GUEST_ASSERT(!gic_irq_get_pending(intid));

	GUEST_ASSERT(intid < MAX_SPI);
	irqnr_received[intid] += 1;
	irq_handled += 1;

	gic_set_eoi(intid);
	GUEST_ASSERT_EQ(gic_read_ap1r0(), 0);

	GUEST_ASSERT(!gic_irq_get_active(intid));
	GUEST_ASSERT(!gic_irq_get_pending(intid));
}

static void kvm_inject_call(kvm_inject_cmd cmd, uint32_t intid)
{
	struct kvm_inject_args args = {
		.cmd = cmd,
		.intid = intid,
	};
	GUEST_SYNC(&args);
}

#define GUEST_ASSERT_IAR_EMPTY()						\
do { 										\
	uint32_t _intid;							\
	_intid = gic_get_and_ack_irq();						\
	GUEST_ASSERT(_intid == 0 || _intid == IAR_SPURIOUS);			\
} while (0)

static void guest_inject(uint32_t intid, kvm_inject_cmd cmd)
{
	reset_stats();

	asm volatile("msr daifset, #2" : : : "memory");
	KVM_INJECT(cmd, intid);

	while (irq_handled < 1) {
		asm volatile("wfi\n"
			     "msr daifclr, #2\n"
			     /* handle IRQ */
			     "msr daifset, #2\n"
			     : : : "memory");
	}
	asm volatile("msr daifclr, #2" : : : "memory");

	GUEST_ASSERT_EQ(irq_handled, 1);
	GUEST_ASSERT_EQ(irqnr_received[intid], 1);
	GUEST_ASSERT_IAR_EMPTY();
}

static void test_injection(struct kvm_inject_desc *f)
{
	if (f->sgi)
		guest_inject(MIN_SGI, f->cmd);

	if (f->ppi)
		guest_inject(MIN_PPI, f->cmd);

	if (f->spi)
		guest_inject(MIN_SPI, f->cmd);
}

static void guest_code(void)
{
	uint32_t i;
	uint32_t nr_irqs = 64; /* absolute minimum number of IRQs supported. */
	struct kvm_inject_desc *f;

	gic_init(GIC_V3, 1, dist, redist);

	for (i = 0; i < nr_irqs; i++) {
		gic_irq_enable(i);
		gic_set_priority(i, IRQ_DEFAULT_PRIO_REG);
	}

	gic_set_priority_mask(CPU_PRIO_MASK);

	local_irq_enable();

	/* Start the tests. */
	for_each_inject_fn(inject_edge_fns, f)
		test_injection(f);

	GUEST_DONE();
}

static void run_guest_cmd(struct kvm_vm *vm, int gic_fd,
		struct kvm_inject_args *inject_args)
{
	kvm_inject_cmd cmd = inject_args->cmd;
	uint32_t intid = inject_args->intid;

	switch (cmd) {
	case KVM_INJECT_EDGE_IRQ_LINE:
		kvm_arm_irq_line(vm, intid, 1);
		kvm_arm_irq_line(vm, intid, 0);
		break;
	default:
		break;
	}
}

static void kvm_inject_get_call(struct kvm_vm *vm, struct ucall *uc,
		struct kvm_inject_args *args)
{
	struct kvm_inject_args *kvm_args_hva;
	vm_vaddr_t kvm_args_gva;

	kvm_args_gva = uc->args[1];
	kvm_args_hva = (struct kvm_inject_args *)addr_gva2hva(vm, kvm_args_gva);
	memcpy(args, kvm_args_hva, sizeof(struct kvm_inject_args));
}


static void test_vgic(void)
{
	struct ucall uc;
	int gic_fd;
	struct kvm_vm *vm;
	struct kvm_inject_args inject_args;

	vm = vm_create_default(VCPU_ID, 0, guest_code);
	ucall_init(vm, NULL);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);

	gic_fd = vgic_v3_setup(vm, 1, GICD_BASE_GPA, GICR_BASE_GPA);

	vm_install_exception_handler(vm, VECTOR_IRQ_CURRENT,
			guest_irq_handler);

	while (1) {
		vcpu_run(vm, VCPU_ID);

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_SYNC:
			kvm_inject_get_call(vm, &uc, &inject_args);
			run_guest_cmd(vm, gic_fd, &inject_args);
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
	close(gic_fd);
	kvm_vm_free(vm);
}

int main(int ac, char **av)
{
	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	test_vgic();

	return 0;
}
