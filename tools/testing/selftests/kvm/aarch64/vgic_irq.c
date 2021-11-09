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
 * Stores the user specified args; it's passed to the guest and to every test
 * function.
 */
struct test_args {
	uint32_t nr_irqs; /* number of KVM supported IRQs. */
};

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
	uint32_t first_intid;
	uint32_t num;
};

/* Used on the guest side to perform the hypercall. */
static void kvm_inject_call(kvm_inject_cmd cmd, uint32_t first_intid, uint32_t num);

#define KVM_INJECT(cmd, intid)							\
	kvm_inject_call(cmd, intid, 1)

#define KVM_INJECT_MULTI(cmd, intid, num)					\
	kvm_inject_call(cmd, intid, num)

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

static void kvm_inject_call(kvm_inject_cmd cmd, uint32_t first_intid, uint32_t num)
{
	struct kvm_inject_args args = {
		.cmd = cmd,
		.first_intid = first_intid,
		.num = num,
	};
	GUEST_SYNC(&args);
}

#define GUEST_ASSERT_IAR_EMPTY()						\
do { 										\
	uint32_t _intid;							\
	_intid = gic_get_and_ack_irq();						\
	GUEST_ASSERT(_intid == 0 || _intid == IAR_SPURIOUS);			\
} while (0)

static void reset_priorities(struct test_args *args)
{
	int i;

	for (i = 0; i < args->nr_irqs; i++)
		gic_set_priority(i, IRQ_DEFAULT_PRIO_REG);
}

static void guest_inject(struct test_args *args,
		uint32_t first_intid, uint32_t num,
		kvm_inject_cmd cmd)
{
	uint32_t i;

	reset_stats();

	/* Cycle over all priorities to make things more interesting. */
	for (i = first_intid; i < num + first_intid; i++)
		gic_set_priority(i, (i % (KVM_NUM_PRIOS - 1)) << 3);

	asm volatile("msr daifset, #2" : : : "memory");
	KVM_INJECT_MULTI(cmd, first_intid, num);

	while (irq_handled < num) {
		asm volatile("wfi\n"
			     "msr daifclr, #2\n"
			     /* handle IRQ */
			     "msr daifset, #2\n"
			     : : : "memory");
	}
	asm volatile("msr daifclr, #2" : : : "memory");

	GUEST_ASSERT_EQ(irq_handled, num);
	for (i = first_intid; i < num + first_intid; i++)
		GUEST_ASSERT_EQ(irqnr_received[i], 1);
	GUEST_ASSERT_IAR_EMPTY();

	reset_priorities(args);
}

static void test_injection(struct test_args *args, struct kvm_inject_desc *f)
{
	uint32_t nr_irqs = args->nr_irqs;

	if (f->sgi) {
		guest_inject(args, MIN_SGI, 1, f->cmd);
		guest_inject(args, 0, 16, f->cmd);
	}

	if (f->ppi)
		guest_inject(args, MIN_PPI, 1, f->cmd);

	if (f->spi) {
		guest_inject(args, MIN_SPI, 1, f->cmd);
		guest_inject(args, nr_irqs - 1, 1, f->cmd);
		guest_inject(args, MIN_SPI, nr_irqs - MIN_SPI, f->cmd);
	}
}

static void guest_code(struct test_args args)
{
	uint32_t i, nr_irqs = args.nr_irqs;
	struct kvm_inject_desc *f;

	gic_init(GIC_V3, 1, dist, redist);

	for (i = 0; i < nr_irqs; i++)
		gic_irq_enable(i);

	reset_priorities(&args);
	gic_set_priority_mask(CPU_PRIO_MASK);

	local_irq_enable();

	/* Start the tests. */
	for_each_inject_fn(inject_edge_fns, f)
		test_injection(&args, f);

	GUEST_DONE();
}

static void run_guest_cmd(struct kvm_vm *vm, int gic_fd,
		struct kvm_inject_args *inject_args,
		struct test_args *test_args)
{
	kvm_inject_cmd cmd = inject_args->cmd;
	uint32_t intid = inject_args->first_intid;
	uint32_t num = inject_args->num;
	uint32_t i;

	assert(intid < UINT_MAX - num);

	switch (cmd) {
	case KVM_INJECT_EDGE_IRQ_LINE:
		for (i = intid; i < intid + num; i++)
			kvm_arm_irq_line(vm, i, 1);
		for (i = intid; i < intid + num; i++)
			kvm_arm_irq_line(vm, i, 0);
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

static void print_args(struct test_args *args)
{
	printf("nr-irqs=%d\n", args->nr_irqs);
}

static void test_vgic(uint32_t nr_irqs)
{
	struct ucall uc;
	int gic_fd;
	struct kvm_vm *vm;
	struct kvm_inject_args inject_args;

	struct test_args args = {
		.nr_irqs = nr_irqs,
	};

	print_args(&args);

	vm = vm_create_default(VCPU_ID, 0, guest_code);
	ucall_init(vm, NULL);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);

	/* Setup the guest args page (so it gets the args). */
	vcpu_args_set(vm, 0, 1, args);

	gic_fd = vgic_v3_setup(vm, 1, nr_irqs,
			GICD_BASE_GPA, GICR_BASE_GPA);

	vm_install_exception_handler(vm, VECTOR_IRQ_CURRENT,
			guest_irq_handler);

	while (1) {
		vcpu_run(vm, VCPU_ID);

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_SYNC:
			kvm_inject_get_call(vm, &uc, &inject_args);
			run_guest_cmd(vm, gic_fd, &inject_args, &args);
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

static void help(const char *name)
{
	printf(
	"\n"
	"usage: %s [-n num_irqs]\n", name);
	printf(" -n: specify the number of IRQs to configure the vgic with.\n");
	puts("");
	exit(1);
}

int main(int argc, char **argv)
{
	uint32_t nr_irqs = 64;
	int opt;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	while ((opt = getopt(argc, argv, "hg:n:")) != -1) {
		switch (opt) {
		case 'n':
			nr_irqs = atoi(optarg);
			if (nr_irqs > 1024 || nr_irqs % 32)
				help(argv[0]);
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	test_vgic(nr_irqs);

	return 0;
}
