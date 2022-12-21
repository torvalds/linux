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
#include <sys/eventfd.h>
#include <linux/sizes.h>

#include "processor.h"
#include "test_util.h"
#include "kvm_util.h"
#include "gic.h"
#include "gic_v3.h"
#include "vgic.h"

#define GICD_BASE_GPA		0x08000000ULL
#define GICR_BASE_GPA		0x080A0000ULL

/*
 * Stores the user specified args; it's passed to the guest and to every test
 * function.
 */
struct test_args {
	uint32_t nr_irqs; /* number of KVM supported IRQs. */
	bool eoi_split; /* 1 is eoir+dir, 0 is eoir only */
	bool level_sensitive; /* 1 is level, 0 is edge */
	int kvm_max_routes; /* output of KVM_CAP_IRQ_ROUTING */
	bool kvm_supports_irqfd; /* output of KVM_CAP_IRQFD */
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
#define KVM_PRIO_STEPS		(1 << KVM_PRIO_SHIFT) /* 8 */
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
	KVM_SET_IRQ_LINE,
	KVM_SET_IRQ_LINE_HIGH,
	KVM_SET_LEVEL_INFO_HIGH,
	KVM_INJECT_IRQFD,
	KVM_WRITE_ISPENDR,
	KVM_WRITE_ISACTIVER,
} kvm_inject_cmd;

struct kvm_inject_args {
	kvm_inject_cmd cmd;
	uint32_t first_intid;
	uint32_t num;
	int level;
	bool expect_failure;
};

/* Used on the guest side to perform the hypercall. */
static void kvm_inject_call(kvm_inject_cmd cmd, uint32_t first_intid,
		uint32_t num, int level, bool expect_failure);

/* Used on the host side to get the hypercall info. */
static void kvm_inject_get_call(struct kvm_vm *vm, struct ucall *uc,
		struct kvm_inject_args *args);

#define _KVM_INJECT_MULTI(cmd, intid, num, expect_failure)			\
	kvm_inject_call(cmd, intid, num, -1 /* not used */, expect_failure)

#define KVM_INJECT_MULTI(cmd, intid, num)					\
	_KVM_INJECT_MULTI(cmd, intid, num, false)

#define _KVM_INJECT(cmd, intid, expect_failure)					\
	_KVM_INJECT_MULTI(cmd, intid, 1, expect_failure)

#define KVM_INJECT(cmd, intid)							\
	_KVM_INJECT_MULTI(cmd, intid, 1, false)

#define KVM_ACTIVATE(cmd, intid)						\
	kvm_inject_call(cmd, intid, 1, 1, false);

struct kvm_inject_desc {
	kvm_inject_cmd cmd;
	/* can inject PPIs, PPIs, and/or SPIs. */
	bool sgi, ppi, spi;
};

static struct kvm_inject_desc inject_edge_fns[] = {
	/*                                      sgi    ppi    spi */
	{ KVM_INJECT_EDGE_IRQ_LINE,		false, false, true },
	{ KVM_INJECT_IRQFD,			false, false, true },
	{ KVM_WRITE_ISPENDR,			true,  false, true },
	{ 0, },
};

static struct kvm_inject_desc inject_level_fns[] = {
	/*                                      sgi    ppi    spi */
	{ KVM_SET_IRQ_LINE_HIGH,		false, true,  true },
	{ KVM_SET_LEVEL_INFO_HIGH,		false, true,  true },
	{ KVM_INJECT_IRQFD,			false, false, true },
	{ KVM_WRITE_ISPENDR,			false, true,  true },
	{ 0, },
};

static struct kvm_inject_desc set_active_fns[] = {
	/*                                      sgi    ppi    spi */
	{ KVM_WRITE_ISACTIVER,			true,  true,  true },
	{ 0, },
};

#define for_each_inject_fn(t, f)						\
	for ((f) = (t); (f)->cmd; (f)++)

#define for_each_supported_inject_fn(args, t, f)				\
	for_each_inject_fn(t, f)						\
		if ((args)->kvm_supports_irqfd || (f)->cmd != KVM_INJECT_IRQFD)

#define for_each_supported_activate_fn(args, t, f)				\
	for_each_supported_inject_fn((args), (t), (f))

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

static void gic_write_ap1r0(uint64_t val)
{
	write_sysreg_s(val, SYS_ICV_AP1R0_EL1);
	isb();
}

static void guest_set_irq_line(uint32_t intid, uint32_t level);

static void guest_irq_generic_handler(bool eoi_split, bool level_sensitive)
{
	uint32_t intid = gic_get_and_ack_irq();

	if (intid == IAR_SPURIOUS)
		return;

	GUEST_ASSERT(gic_irq_get_active(intid));

	if (!level_sensitive)
		GUEST_ASSERT(!gic_irq_get_pending(intid));

	if (level_sensitive)
		guest_set_irq_line(intid, 0);

	GUEST_ASSERT(intid < MAX_SPI);
	irqnr_received[intid] += 1;
	irq_handled += 1;

	gic_set_eoi(intid);
	GUEST_ASSERT_EQ(gic_read_ap1r0(), 0);
	if (eoi_split)
		gic_set_dir(intid);

	GUEST_ASSERT(!gic_irq_get_active(intid));
	GUEST_ASSERT(!gic_irq_get_pending(intid));
}

static void kvm_inject_call(kvm_inject_cmd cmd, uint32_t first_intid,
		uint32_t num, int level, bool expect_failure)
{
	struct kvm_inject_args args = {
		.cmd = cmd,
		.first_intid = first_intid,
		.num = num,
		.level = level,
		.expect_failure = expect_failure,
	};
	GUEST_SYNC(&args);
}

#define GUEST_ASSERT_IAR_EMPTY()						\
do { 										\
	uint32_t _intid;							\
	_intid = gic_get_and_ack_irq();						\
	GUEST_ASSERT(_intid == 0 || _intid == IAR_SPURIOUS);			\
} while (0)

#define CAT_HELPER(a, b) a ## b
#define CAT(a, b) CAT_HELPER(a, b)
#define PREFIX guest_irq_handler_
#define GUEST_IRQ_HANDLER_NAME(split, lev) CAT(PREFIX, CAT(split, lev))
#define GENERATE_GUEST_IRQ_HANDLER(split, lev)					\
static void CAT(PREFIX, CAT(split, lev))(struct ex_regs *regs)			\
{										\
	guest_irq_generic_handler(split, lev);					\
}

GENERATE_GUEST_IRQ_HANDLER(0, 0);
GENERATE_GUEST_IRQ_HANDLER(0, 1);
GENERATE_GUEST_IRQ_HANDLER(1, 0);
GENERATE_GUEST_IRQ_HANDLER(1, 1);

static void (*guest_irq_handlers[2][2])(struct ex_regs *) = {
	{GUEST_IRQ_HANDLER_NAME(0, 0), GUEST_IRQ_HANDLER_NAME(0, 1),},
	{GUEST_IRQ_HANDLER_NAME(1, 0), GUEST_IRQ_HANDLER_NAME(1, 1),},
};

static void reset_priorities(struct test_args *args)
{
	int i;

	for (i = 0; i < args->nr_irqs; i++)
		gic_set_priority(i, IRQ_DEFAULT_PRIO_REG);
}

static void guest_set_irq_line(uint32_t intid, uint32_t level)
{
	kvm_inject_call(KVM_SET_IRQ_LINE, intid, 1, level, false);
}

static void test_inject_fail(struct test_args *args,
		uint32_t intid, kvm_inject_cmd cmd)
{
	reset_stats();

	_KVM_INJECT(cmd, intid, true);
	/* no IRQ to handle on entry */

	GUEST_ASSERT_EQ(irq_handled, 0);
	GUEST_ASSERT_IAR_EMPTY();
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

/*
 * Restore the active state of multiple concurrent IRQs (given by
 * concurrent_irqs).  This does what a live-migration would do on the
 * destination side assuming there are some active IRQs that were not
 * deactivated yet.
 */
static void guest_restore_active(struct test_args *args,
		uint32_t first_intid, uint32_t num,
		kvm_inject_cmd cmd)
{
	uint32_t prio, intid, ap1r;
	int i;

	/*
	 * Set the priorities of the first (KVM_NUM_PRIOS - 1) IRQs
	 * in descending order, so intid+1 can preempt intid.
	 */
	for (i = 0, prio = (num - 1) * 8; i < num; i++, prio -= 8) {
		GUEST_ASSERT(prio >= 0);
		intid = i + first_intid;
		gic_set_priority(intid, prio);
	}

	/*
	 * In a real migration, KVM would restore all GIC state before running
	 * guest code.
	 */
	for (i = 0; i < num; i++) {
		intid = i + first_intid;
		KVM_ACTIVATE(cmd, intid);
		ap1r = gic_read_ap1r0();
		ap1r |= 1U << i;
		gic_write_ap1r0(ap1r);
	}

	/* This is where the "migration" would occur. */

	/* finish handling the IRQs starting with the highest priority one. */
	for (i = 0; i < num; i++) {
		intid = num - i - 1 + first_intid;
		gic_set_eoi(intid);
		if (args->eoi_split)
			gic_set_dir(intid);
	}

	for (i = 0; i < num; i++)
		GUEST_ASSERT(!gic_irq_get_active(i + first_intid));
	GUEST_ASSERT_EQ(gic_read_ap1r0(), 0);
	GUEST_ASSERT_IAR_EMPTY();
}

/*
 * Polls the IAR until it's not a spurious interrupt.
 *
 * This function should only be used in test_inject_preemption (with IRQs
 * masked).
 */
static uint32_t wait_for_and_activate_irq(void)
{
	uint32_t intid;

	do {
		asm volatile("wfi" : : : "memory");
		intid = gic_get_and_ack_irq();
	} while (intid == IAR_SPURIOUS);

	return intid;
}

/*
 * Inject multiple concurrent IRQs (num IRQs starting at first_intid) and
 * handle them without handling the actual exceptions.  This is done by masking
 * interrupts for the whole test.
 */
static void test_inject_preemption(struct test_args *args,
		uint32_t first_intid, int num,
		kvm_inject_cmd cmd)
{
	uint32_t intid, prio, step = KVM_PRIO_STEPS;
	int i;

	/* Set the priorities of the first (KVM_NUM_PRIOS - 1) IRQs
	 * in descending order, so intid+1 can preempt intid.
	 */
	for (i = 0, prio = (num - 1) * step; i < num; i++, prio -= step) {
		GUEST_ASSERT(prio >= 0);
		intid = i + first_intid;
		gic_set_priority(intid, prio);
	}

	local_irq_disable();

	for (i = 0; i < num; i++) {
		uint32_t tmp;
		intid = i + first_intid;
		KVM_INJECT(cmd, intid);
		/* Each successive IRQ will preempt the previous one. */
		tmp = wait_for_and_activate_irq();
		GUEST_ASSERT_EQ(tmp, intid);
		if (args->level_sensitive)
			guest_set_irq_line(intid, 0);
	}

	/* finish handling the IRQs starting with the highest priority one. */
	for (i = 0; i < num; i++) {
		intid = num - i - 1 + first_intid;
		gic_set_eoi(intid);
		if (args->eoi_split)
			gic_set_dir(intid);
	}

	local_irq_enable();

	for (i = 0; i < num; i++)
		GUEST_ASSERT(!gic_irq_get_active(i + first_intid));
	GUEST_ASSERT_EQ(gic_read_ap1r0(), 0);
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

static void test_injection_failure(struct test_args *args,
		struct kvm_inject_desc *f)
{
	uint32_t bad_intid[] = { args->nr_irqs, 1020, 1024, 1120, 5120, ~0U, };
	int i;

	for (i = 0; i < ARRAY_SIZE(bad_intid); i++)
		test_inject_fail(args, bad_intid[i], f->cmd);
}

static void test_preemption(struct test_args *args, struct kvm_inject_desc *f)
{
	/*
	 * Test up to 4 levels of preemption. The reason is that KVM doesn't
	 * currently implement the ability to have more than the number-of-LRs
	 * number of concurrently active IRQs. The number of LRs implemented is
	 * IMPLEMENTATION DEFINED, however, it seems that most implement 4.
	 */
	if (f->sgi)
		test_inject_preemption(args, MIN_SGI, 4, f->cmd);

	if (f->ppi)
		test_inject_preemption(args, MIN_PPI, 4, f->cmd);

	if (f->spi)
		test_inject_preemption(args, MIN_SPI, 4, f->cmd);
}

static void test_restore_active(struct test_args *args, struct kvm_inject_desc *f)
{
	/* Test up to 4 active IRQs. Same reason as in test_preemption. */
	if (f->sgi)
		guest_restore_active(args, MIN_SGI, 4, f->cmd);

	if (f->ppi)
		guest_restore_active(args, MIN_PPI, 4, f->cmd);

	if (f->spi)
		guest_restore_active(args, MIN_SPI, 4, f->cmd);
}

static void guest_code(struct test_args *args)
{
	uint32_t i, nr_irqs = args->nr_irqs;
	bool level_sensitive = args->level_sensitive;
	struct kvm_inject_desc *f, *inject_fns;

	gic_init(GIC_V3, 1, dist, redist);

	for (i = 0; i < nr_irqs; i++)
		gic_irq_enable(i);

	for (i = MIN_SPI; i < nr_irqs; i++)
		gic_irq_set_config(i, !level_sensitive);

	gic_set_eoi_split(args->eoi_split);

	reset_priorities(args);
	gic_set_priority_mask(CPU_PRIO_MASK);

	inject_fns  = level_sensitive ? inject_level_fns
				      : inject_edge_fns;

	local_irq_enable();

	/* Start the tests. */
	for_each_supported_inject_fn(args, inject_fns, f) {
		test_injection(args, f);
		test_preemption(args, f);
		test_injection_failure(args, f);
	}

	/*
	 * Restore the active state of IRQs. This would happen when live
	 * migrating IRQs in the middle of being handled.
	 */
	for_each_supported_activate_fn(args, set_active_fns, f)
		test_restore_active(args, f);

	GUEST_DONE();
}

static void kvm_irq_line_check(struct kvm_vm *vm, uint32_t intid, int level,
			struct test_args *test_args, bool expect_failure)
{
	int ret;

	if (!expect_failure) {
		kvm_arm_irq_line(vm, intid, level);
	} else {
		/* The interface doesn't allow larger intid's. */
		if (intid > KVM_ARM_IRQ_NUM_MASK)
			return;

		ret = _kvm_arm_irq_line(vm, intid, level);
		TEST_ASSERT(ret != 0 && errno == EINVAL,
				"Bad intid %i did not cause KVM_IRQ_LINE "
				"error: rc: %i errno: %i", intid, ret, errno);
	}
}

void kvm_irq_set_level_info_check(int gic_fd, uint32_t intid, int level,
			bool expect_failure)
{
	if (!expect_failure) {
		kvm_irq_set_level_info(gic_fd, intid, level);
	} else {
		int ret = _kvm_irq_set_level_info(gic_fd, intid, level);
		/*
		 * The kernel silently fails for invalid SPIs and SGIs (which
		 * are not level-sensitive). It only checks for intid to not
		 * spill over 1U << 10 (the max reserved SPI). Also, callers
		 * are supposed to mask the intid with 0x3ff (1023).
		 */
		if (intid > VGIC_MAX_RESERVED)
			TEST_ASSERT(ret != 0 && errno == EINVAL,
				"Bad intid %i did not cause VGIC_GRP_LEVEL_INFO "
				"error: rc: %i errno: %i", intid, ret, errno);
		else
			TEST_ASSERT(!ret, "KVM_DEV_ARM_VGIC_GRP_LEVEL_INFO "
				"for intid %i failed, rc: %i errno: %i",
				intid, ret, errno);
	}
}

static void kvm_set_gsi_routing_irqchip_check(struct kvm_vm *vm,
		uint32_t intid, uint32_t num, uint32_t kvm_max_routes,
		bool expect_failure)
{
	struct kvm_irq_routing *routing;
	int ret;
	uint64_t i;

	assert(num <= kvm_max_routes && kvm_max_routes <= KVM_MAX_IRQ_ROUTES);

	routing = kvm_gsi_routing_create();
	for (i = intid; i < (uint64_t)intid + num; i++)
		kvm_gsi_routing_irqchip_add(routing, i - MIN_SPI, i - MIN_SPI);

	if (!expect_failure) {
		kvm_gsi_routing_write(vm, routing);
	} else {
		ret = _kvm_gsi_routing_write(vm, routing);
		/* The kernel only checks e->irqchip.pin >= KVM_IRQCHIP_NUM_PINS */
		if (((uint64_t)intid + num - 1 - MIN_SPI) >= KVM_IRQCHIP_NUM_PINS)
			TEST_ASSERT(ret != 0 && errno == EINVAL,
				"Bad intid %u did not cause KVM_SET_GSI_ROUTING "
				"error: rc: %i errno: %i", intid, ret, errno);
		else
			TEST_ASSERT(ret == 0, "KVM_SET_GSI_ROUTING "
				"for intid %i failed, rc: %i errno: %i",
				intid, ret, errno);
	}
}

static void kvm_irq_write_ispendr_check(int gic_fd, uint32_t intid,
					struct kvm_vcpu *vcpu,
					bool expect_failure)
{
	/*
	 * Ignore this when expecting failure as invalid intids will lead to
	 * either trying to inject SGIs when we configured the test to be
	 * level_sensitive (or the reverse), or inject large intids which
	 * will lead to writing above the ISPENDR register space (and we
	 * don't want to do that either).
	 */
	if (!expect_failure)
		kvm_irq_write_ispendr(gic_fd, intid, vcpu);
}

static void kvm_routing_and_irqfd_check(struct kvm_vm *vm,
		uint32_t intid, uint32_t num, uint32_t kvm_max_routes,
		bool expect_failure)
{
	int fd[MAX_SPI];
	uint64_t val;
	int ret, f;
	uint64_t i;

	/*
	 * There is no way to try injecting an SGI or PPI as the interface
	 * starts counting from the first SPI (above the private ones), so just
	 * exit.
	 */
	if (INTID_IS_SGI(intid) || INTID_IS_PPI(intid))
		return;

	kvm_set_gsi_routing_irqchip_check(vm, intid, num,
			kvm_max_routes, expect_failure);

	/*
	 * If expect_failure, then just to inject anyway. These
	 * will silently fail. And in any case, the guest will check
	 * that no actual interrupt was injected for those cases.
	 */

	for (f = 0, i = intid; i < (uint64_t)intid + num; i++, f++) {
		fd[f] = eventfd(0, 0);
		TEST_ASSERT(fd[f] != -1, __KVM_SYSCALL_ERROR("eventfd()", fd[f]));
	}

	for (f = 0, i = intid; i < (uint64_t)intid + num; i++, f++) {
		struct kvm_irqfd irqfd = {
			.fd  = fd[f],
			.gsi = i - MIN_SPI,
		};
		assert(i <= (uint64_t)UINT_MAX);
		vm_ioctl(vm, KVM_IRQFD, &irqfd);
	}

	for (f = 0, i = intid; i < (uint64_t)intid + num; i++, f++) {
		val = 1;
		ret = write(fd[f], &val, sizeof(uint64_t));
		TEST_ASSERT(ret == sizeof(uint64_t),
			    __KVM_SYSCALL_ERROR("write()", ret));
	}

	for (f = 0, i = intid; i < (uint64_t)intid + num; i++, f++)
		close(fd[f]);
}

/* handles the valid case: intid=0xffffffff num=1 */
#define for_each_intid(first, num, tmp, i)					\
	for ((tmp) = (i) = (first);						\
		(tmp) < (uint64_t)(first) + (uint64_t)(num);			\
		(tmp)++, (i)++)

static void run_guest_cmd(struct kvm_vcpu *vcpu, int gic_fd,
			  struct kvm_inject_args *inject_args,
			  struct test_args *test_args)
{
	kvm_inject_cmd cmd = inject_args->cmd;
	uint32_t intid = inject_args->first_intid;
	uint32_t num = inject_args->num;
	int level = inject_args->level;
	bool expect_failure = inject_args->expect_failure;
	struct kvm_vm *vm = vcpu->vm;
	uint64_t tmp;
	uint32_t i;

	/* handles the valid case: intid=0xffffffff num=1 */
	assert(intid < UINT_MAX - num || num == 1);

	switch (cmd) {
	case KVM_INJECT_EDGE_IRQ_LINE:
		for_each_intid(intid, num, tmp, i)
			kvm_irq_line_check(vm, i, 1, test_args,
					expect_failure);
		for_each_intid(intid, num, tmp, i)
			kvm_irq_line_check(vm, i, 0, test_args,
					expect_failure);
		break;
	case KVM_SET_IRQ_LINE:
		for_each_intid(intid, num, tmp, i)
			kvm_irq_line_check(vm, i, level, test_args,
					expect_failure);
		break;
	case KVM_SET_IRQ_LINE_HIGH:
		for_each_intid(intid, num, tmp, i)
			kvm_irq_line_check(vm, i, 1, test_args,
					expect_failure);
		break;
	case KVM_SET_LEVEL_INFO_HIGH:
		for_each_intid(intid, num, tmp, i)
			kvm_irq_set_level_info_check(gic_fd, i, 1,
					expect_failure);
		break;
	case KVM_INJECT_IRQFD:
		kvm_routing_and_irqfd_check(vm, intid, num,
					test_args->kvm_max_routes,
					expect_failure);
		break;
	case KVM_WRITE_ISPENDR:
		for (i = intid; i < intid + num; i++)
			kvm_irq_write_ispendr_check(gic_fd, i, vcpu,
						    expect_failure);
		break;
	case KVM_WRITE_ISACTIVER:
		for (i = intid; i < intid + num; i++)
			kvm_irq_write_isactiver(gic_fd, i, vcpu);
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
	printf("nr-irqs=%d level-sensitive=%d eoi-split=%d\n",
			args->nr_irqs, args->level_sensitive,
			args->eoi_split);
}

static void test_vgic(uint32_t nr_irqs, bool level_sensitive, bool eoi_split)
{
	struct ucall uc;
	int gic_fd;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct kvm_inject_args inject_args;
	vm_vaddr_t args_gva;

	struct test_args args = {
		.nr_irqs = nr_irqs,
		.level_sensitive = level_sensitive,
		.eoi_split = eoi_split,
		.kvm_max_routes = kvm_check_cap(KVM_CAP_IRQ_ROUTING),
		.kvm_supports_irqfd = kvm_check_cap(KVM_CAP_IRQFD),
	};

	print_args(&args);

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);

	/* Setup the guest args page (so it gets the args). */
	args_gva = vm_vaddr_alloc_page(vm);
	memcpy(addr_gva2hva(vm, args_gva), &args, sizeof(args));
	vcpu_args_set(vcpu, 1, args_gva);

	gic_fd = vgic_v3_setup(vm, 1, nr_irqs,
			GICD_BASE_GPA, GICR_BASE_GPA);
	__TEST_REQUIRE(gic_fd >= 0, "Failed to create vgic-v3, skipping");

	vm_install_exception_handler(vm, VECTOR_IRQ_CURRENT,
		guest_irq_handlers[args.eoi_split][args.level_sensitive]);

	while (1) {
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			kvm_inject_get_call(vm, &uc, &inject_args);
			run_guest_cmd(vcpu, gic_fd, &inject_args, &args);
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
	close(gic_fd);
	kvm_vm_free(vm);
}

static void help(const char *name)
{
	printf(
	"\n"
	"usage: %s [-n num_irqs] [-e eoi_split] [-l level_sensitive]\n", name);
	printf(" -n: specify number of IRQs to setup the vgic with. "
		"It has to be a multiple of 32 and between 64 and 1024.\n");
	printf(" -e: if 1 then EOI is split into a write to DIR on top "
		"of writing EOI.\n");
	printf(" -l: specify whether the IRQs are level-sensitive (1) or not (0).");
	puts("");
	exit(1);
}

int main(int argc, char **argv)
{
	uint32_t nr_irqs = 64;
	bool default_args = true;
	bool level_sensitive = false;
	int opt;
	bool eoi_split = false;

	while ((opt = getopt(argc, argv, "hn:e:l:")) != -1) {
		switch (opt) {
		case 'n':
			nr_irqs = atoi_non_negative("Number of IRQs", optarg);
			if (nr_irqs > 1024 || nr_irqs % 32)
				help(argv[0]);
			break;
		case 'e':
			eoi_split = (bool)atoi_paranoid(optarg);
			default_args = false;
			break;
		case 'l':
			level_sensitive = (bool)atoi_paranoid(optarg);
			default_args = false;
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	/*
	 * If the user just specified nr_irqs and/or gic_version, then run all
	 * combinations.
	 */
	if (default_args) {
		test_vgic(nr_irqs, false /* level */, false /* eoi_split */);
		test_vgic(nr_irqs, false /* level */, true /* eoi_split */);
		test_vgic(nr_irqs, true /* level */, false /* eoi_split */);
		test_vgic(nr_irqs, true /* level */, true /* eoi_split */);
	} else {
		test_vgic(nr_irqs, level_sensitive, eoi_split);
	}

	return 0;
}
