// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <sys/syscall.h>
#include <asm/kvm.h>
#include <asm/kvm_para.h>

#include <arm64/gic_v5.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vgic.h"

#define NR_VCPUS		1
#define VGIC_V5_LPI_NR_VCPUS	2
#define VGIC_V5_SPARSE_NR_VCPUS	2
#define VGIC_V5_SPARSE_VCPU0_ID	17
#define VGIC_V5_SPARSE_VCPU1_ID	3
#define VGIC_V5_LPI_MEMSLOT	1
#define VGIC_V5_NR_PRIVATE_IRQS	64
#define VGIC_V5_DEFAULT_NR_SPIS	32
#define VGIC_V5_MAX_NR_SPIS	BIT(10)
#define VGIC_V5_IRS_SIZE		0x20000
#define VGIC_V5_IRS_WAIT_RETRIES	1000000
#define VGIC_V5_LPI_IST_BASE_GPA	(GICV5_IRS_CONFIG_BASE_GPA + VGIC_V5_IRS_SIZE)
#define VGIC_V5_LPI_IST_SIZE		0x10000
#define LPI_TEST_TO_VPE1		0
#define LPI_TEST_TO_VPE0		1
#define RESTORE_TEST_LPI_PENDING		0
#define RESTORE_TEST_LPI_ENABLED		1
#define RESTORE_TEST_SPI_PRIO_HIGH		0
#define RESTORE_TEST_SPI_PRIO_LOW		1
#define RESTORE_TEST_SPI_PENDING_DISABLED	2
#define RESTORE_TEST_SPI_ENABLED		3
#define VGIC_V5_IST_PRIO_HIGH		0x08
#define VGIC_V5_IST_PRIO_LOW		0x10

static u64 max_phys_size;

#define VGIC_V5_TEST_IST_BASER_GPA	0x10000000ULL

struct vm_gic {
	struct kvm_vm *vm;
	int gic_fd;
	u32 gic_dev_type;
};

#define GUEST_CMD_IRQ_CDIA	10
#define GUEST_CMD_IRQ_DIEOI	11
#define GUEST_CMD_IS_AWAKE	12
#define GUEST_CMD_IS_READY	13
#define GUEST_CMD_LPI_SENT	14
#define GUEST_CMD_LPI_REPLIED	15
#define GUEST_CMD_IST_SOURCE_READY	16
#define GUEST_CMD_IST_SPI_INJECT_READY	17
#define GUEST_CMD_IST_LPI_INJECT_READY	18

static struct kvm_vgic_v5_ist vgic_v5_ist_attr(void *spi_ist, size_t spi_size,
					       void *lpi_ist, size_t lpi_size)
{
	return (struct kvm_vgic_v5_ist) {
		.spi_ist_addr = (uintptr_t)spi_ist,
		.spi_ist_size = spi_size,
		.lpi_ist_addr = (uintptr_t)lpi_ist,
		.lpi_ist_size = lpi_size,
	};
}

static u32 spi_line_expected;
static bool spi_line_level_sensitive;
static bool lpi_ist_ready;
static u32 ist_restore_irq_count[VGIC_V5_LPI_NR_VCPUS];

static u64 gicv5_hwirq(u32 type, u32 intid)
{
	return FIELD_PREP(GICV5_HWIRQ_TYPE, type) |
	       FIELD_PREP(GICV5_HWIRQ_ID, intid);
}

static u64 gicv5_lpi_hwirq(u32 lpi)
{
	return gicv5_hwirq(GICV5_HWIRQ_TYPE_LPI, lpi);
}

static u64 gicv5_spi_hwirq(u32 spi)
{
	return gicv5_hwirq(GICV5_HWIRQ_TYPE_SPI, spi);
}

static void gicv5_setup_and_enable_hwirq(u64 hwirq, u32 target_vpe)
{
	u64 val;

	val = hwirq | FIELD_PREP(GICV5_GIC_CDPRI_PRIORITY_MASK,
				 GICV5_IRQ_DEFAULT_PRI);
	gic_insn(val, CDPRI);

	val = hwirq | FIELD_PREP(GICV5_GIC_CDAFF_IAFFID_MASK, target_vpe);
	gic_insn(val, CDAFF);

	gic_insn(hwirq, CDEN);
}

static void gicv5_set_hwirq_priority(u64 hwirq, u32 priority)
{
	u64 val;

	val = hwirq | FIELD_PREP(GICV5_GIC_CDPRI_PRIORITY_MASK, priority);
	gic_insn(val, CDPRI);
}

static void gicv5_set_hwirq_affinity(u64 hwirq, u32 target_vpe)
{
	u64 val;

	val = hwirq | FIELD_PREP(GICV5_GIC_CDAFF_IAFFID_MASK, target_vpe);
	gic_insn(val, CDAFF);
}

static void gicv5_enable_hwirq(u64 hwirq)
{
	gic_insn(hwirq, CDEN);
}

static void gicv5_enable_spi(u32 spi, u32 target_vpe)
{
	gicv5_setup_and_enable_hwirq(gicv5_spi_hwirq(spi), target_vpe);
}

static void gicv5_enable_lpi(u32 lpi, u32 target_vpe)
{
	gicv5_setup_and_enable_hwirq(gicv5_lpi_hwirq(lpi), target_vpe);
}

static void gicv5_send_lpi(u32 lpi)
{
	u64 hwirq = gicv5_lpi_hwirq(lpi);

	gic_insn(hwirq | GICV5_GIC_CDPEND_PENDING_MASK, CDPEND);
}

static void gicv5_wait_for_irs_idle(u32 reg, u32 idle)
{
	int i;

	for (i = 0; i < VGIC_V5_IRS_WAIT_RETRIES; i++) {
		if (readl(GICV5_IRS_CONFIG_BASE_GVA + reg) & idle)
			return;

		cpu_relax();
	}

	GUEST_FAIL("IRS operation did not become idle");
}

static void gicv5_configure_spi(u32 spi, bool level)
{
	u32 val;

	val = FIELD_PREP(GICV5_IRS_SPI_SELR_ID, spi);
	writel(val, GICV5_IRS_CONFIG_BASE_GVA + GICV5_IRS_SPI_SELR);
	gicv5_wait_for_irs_idle(GICV5_IRS_SPI_STATUSR,
				GICV5_IRS_SPI_STATUSR_IDLE);

	val = level ? GICV5_IRS_SPI_CFGR_TM : 0;
	writel(val, GICV5_IRS_CONFIG_BASE_GVA + GICV5_IRS_SPI_CFGR);
	gicv5_wait_for_irs_idle(GICV5_IRS_SPI_STATUSR,
				GICV5_IRS_SPI_STATUSR_IDLE);
}

static u32 gicv5_lpi_istsz(u32 idr2, u32 lpi_id_bits)
{
	if (!(idr2 & GICV5_IRS_IDR2_ISTMD))
		return GICV5_IRS_IST_CFGR_ISTSZ_4;

	if (lpi_id_bits >= FIELD_GET(GICV5_IRS_IDR2_ISTMD_SZ, idr2))
		return GICV5_IRS_IST_CFGR_ISTSZ_16;

	return GICV5_IRS_IST_CFGR_ISTSZ_8;
}

static void gicv5_configure_lpi_ist(void)
{
	u32 idr2, min_lpi_id_bits, lpi_id_bits, id_bits, istsz;
	u64 val;

	idr2 = readl(GICV5_IRS_CONFIG_BASE_GVA + GICV5_IRS_IDR2);
	GUEST_ASSERT(idr2 & GICV5_IRS_IDR2_LPI);

	min_lpi_id_bits = FIELD_GET(GICV5_IRS_IDR2_MIN_LPI_ID_BITS, idr2);
	id_bits = FIELD_GET(GICV5_IRS_IDR2_ID_BITS, idr2);
	/* Default to 32 LPIs, unless the min requirement is higher */
	lpi_id_bits = max(5U, min_lpi_id_bits);
	GUEST_ASSERT(lpi_id_bits <= id_bits);

	istsz = gicv5_lpi_istsz(idr2, lpi_id_bits);
	/*
	 * We allocate a fixed-size IST buffer on the host to avoid dynamic
	 * allocation from the guest. Make sure it fits the linear IST described
	 * by IDR2 before programming IRS_IST_BASER.
	 */
	GUEST_ASSERT((BIT_ULL(lpi_id_bits) << (istsz + 2)) <= VGIC_V5_LPI_IST_SIZE);

	val = FIELD_PREP(GICV5_IRS_IST_CFGR_LPI_ID_BITS, lpi_id_bits) |
	      FIELD_PREP(GICV5_IRS_IST_CFGR_ISTSZ, istsz);
	writel(val, GICV5_IRS_CONFIG_BASE_GVA + GICV5_IRS_IST_CFGR);

	val = FIELD_PREP(GICV5_IRS_IST_BASER_ADDR_MASK,
			 VGIC_V5_LPI_IST_BASE_GPA >> GICV5_IRS_IST_BASER_ADDR_SHIFT);
	val |= GICV5_IRS_IST_BASER_VALID;
	writeq(val, GICV5_IRS_CONFIG_BASE_GVA + GICV5_IRS_IST_BASER);

	GUEST_ASSERT_EQ(readl(GICV5_IRS_CONFIG_BASE_GVA + GICV5_IRS_IST_STATUSR),
			GICV5_IRS_IST_STATUSR_IDLE);

	/* The address is immutable while the BASER is valid. */
	writeq(val ^ FIELD_PREP(GICV5_IRS_IST_BASER_ADDR_MASK, 1),
	       GICV5_IRS_CONFIG_BASE_GVA + GICV5_IRS_IST_BASER);
	GUEST_ASSERT_EQ(readq(GICV5_IRS_CONFIG_BASE_GVA +
			      GICV5_IRS_IST_BASER), val);
}

static void gicv5_enable_irs(void)
{
	writel(GICV5_IRS_CR0_IRSEN,
	       GICV5_IRS_CONFIG_BASE_GVA + GICV5_IRS_CR0);
	gicv5_wait_for_irs_idle(GICV5_IRS_CR0, GICV5_IRS_CR0_IDLE);
}

static void gicv5_configure_test_lpis(void)
{
	gicv5_enable_lpi(LPI_TEST_TO_VPE1, 1);
	gicv5_enable_lpi(LPI_TEST_TO_VPE0, 0);
}

static void gicv5_set_pcr(u32 priority)
{
	u64 pcr;

	pcr = FIELD_PREP(ICC_PCR_EL1_PRIORITY, priority);
	write_sysreg_s(pcr, SYS_ICC_PCR_EL1);
	isb();
}

static void guest_ppi_irq_handler(struct ex_regs *regs)
{
	bool valid;
	u32 hwirq;
	u64 ia;
	static int count;

	/*
	 * We have pending interrupts. Should never actually enter WFI
	 * here!
	 */
	wfi();
	GUEST_SYNC(GUEST_CMD_IS_AWAKE);

	ia = gicr_insn(CDIA);
	valid = GICV5_GICR_CDIA_VALID(ia);

	GUEST_SYNC(GUEST_CMD_IRQ_CDIA);

	if (!valid)
		return;

	gsb_ack();
	isb();

	hwirq = FIELD_GET(GICV5_GICR_CDIA_INTID, ia);

	gic_insn(hwirq, CDDI);
	gic_insn(0, CDEOI);

	GUEST_SYNC(GUEST_CMD_IRQ_DIEOI);

	if (++count >= 2)
		GUEST_DONE();

	/* Ask for the next interrupt to be injected */
	GUEST_SYNC(GUEST_CMD_IS_READY);
}

static void guest_code(void)
{
	local_irq_disable();

	gicv5_cpu_enable_interrupts();
	local_irq_enable();

	/* Enable the SW_PPI (3) */
	write_sysreg_s(BIT_ULL(3), SYS_ICC_PPI_ENABLER0_EL1);

	/* Ask for the first interrupt to be injected */
	GUEST_SYNC(GUEST_CMD_IS_READY);

	/* Loop forever waiting for interrupts */
	for (;;)
		cpu_relax();
}

static void guest_iaffid_code(void)
{
	GUEST_SYNC(FIELD_GET(ICC_IAFFIDR_EL1_IAFFID,
				     read_sysreg_s(SYS_ICC_IAFFIDR_EL1)));
	GUEST_DONE();
}

static void guest_spi_irq_handler(struct ex_regs *regs)
{
	bool valid;
	u32 hwirq;
	u64 ia;

	ia = gicr_insn(CDIA);
	valid = GICV5_GICR_CDIA_VALID(ia);

	if (!valid)
		return;

	gsb_ack();
	isb();

	hwirq = FIELD_GET(GICV5_GICR_CDIA_INTID, ia);

	GUEST_ASSERT_EQ(hwirq, gicv5_spi_hwirq(READ_ONCE(spi_line_expected)));

	gic_insn(hwirq, CDDI);
	gic_insn(0, CDEOI);

	GUEST_DONE();
}

static void guest_spi_line_code(void)
{
	local_irq_disable();

	gicv5_enable_irs();
	gicv5_cpu_enable_interrupts();
	gicv5_configure_spi(READ_ONCE(spi_line_expected),
			    READ_ONCE(spi_line_level_sensitive));
	gicv5_enable_spi(READ_ONCE(spi_line_expected), 0);

	local_irq_enable();

	GUEST_SYNC(GUEST_CMD_IS_READY);

	while (1)
		wfi();
}

static void guest_lpi_irq_handler(struct ex_regs *regs)
{
	u32 vcpu_id = guest_get_vcpuid();
	u32 expected_lpi = vcpu_id ? LPI_TEST_TO_VPE1 : LPI_TEST_TO_VPE0;
	u64 expected_hwirq = gicv5_lpi_hwirq(expected_lpi);
	u32 hwirq;
	u64 ia;

	ia = gicr_insn(CDIA);
	if (!GICV5_GICR_CDIA_VALID(ia))
		return;

	gsb_ack();
	isb();

	hwirq = FIELD_GET(GICV5_GICR_CDIA_INTID, ia);
	GUEST_ASSERT_EQ(hwirq, expected_hwirq);

	gic_insn(hwirq, CDDI);
	gic_insn(0, CDEOI);

	if (vcpu_id) {
		gicv5_send_lpi(LPI_TEST_TO_VPE0);
		GUEST_SYNC(GUEST_CMD_LPI_REPLIED);
		while (1)
			wfi();
	}

	GUEST_DONE();
}

static void guest_unexpected_irq_handler(struct ex_regs *regs)
{
	GUEST_FAIL("Unexpected IRQ");
}

static void guest_lpi_code(void)
{
	u32 vcpu_id = guest_get_vcpuid();

	local_irq_disable();

	if (!vcpu_id) {
		gicv5_enable_irs();
		gicv5_configure_lpi_ist();
		gicv5_configure_test_lpis();
		WRITE_ONCE(lpi_ist_ready, true);
	}

	/* Go bang if we run VCPU1 before VCPU0 */
	GUEST_ASSERT(READ_ONCE(lpi_ist_ready));

	gicv5_cpu_enable_interrupts();
	local_irq_enable();

	GUEST_SYNC(GUEST_CMD_IS_READY);

	if (!vcpu_id) {
		gicv5_send_lpi(LPI_TEST_TO_VPE1);
		GUEST_SYNC(GUEST_CMD_LPI_SENT);
	}

	while (1)
		wfi();
}

static void restore_test_setup_hwirq(u64 hwirq, u32 target_vpe,
				     u32 priority, bool enable)
{
	gicv5_set_hwirq_priority(hwirq, priority);
	gicv5_set_hwirq_affinity(hwirq, target_vpe);

	if (enable)
		gicv5_enable_hwirq(hwirq);
}

/*
 * The source VM configures SPI and LPI state for the IST save/restore test.
 * It leaves interrupts disabled, allowing userspace to save the state before
 * the restore VM enables interrupts and checks delivery.
 */
static void guest_ist_source_code(void)
{
	local_irq_disable();

	gicv5_enable_irs();
	gicv5_configure_lpi_ist();

	restore_test_setup_hwirq(gicv5_lpi_hwirq(RESTORE_TEST_LPI_PENDING),
				 1, VGIC_V5_IST_PRIO_HIGH, true);
	restore_test_setup_hwirq(gicv5_lpi_hwirq(RESTORE_TEST_LPI_ENABLED),
				 1, VGIC_V5_IST_PRIO_HIGH, true);
	restore_test_setup_hwirq(gicv5_spi_hwirq(RESTORE_TEST_SPI_PRIO_HIGH),
				 0, VGIC_V5_IST_PRIO_HIGH, true);
	restore_test_setup_hwirq(gicv5_spi_hwirq(RESTORE_TEST_SPI_PRIO_LOW),
				 0, VGIC_V5_IST_PRIO_LOW, true);
	restore_test_setup_hwirq(gicv5_spi_hwirq(RESTORE_TEST_SPI_ENABLED),
				 0, VGIC_V5_IST_PRIO_HIGH, true);
	restore_test_setup_hwirq(gicv5_spi_hwirq(RESTORE_TEST_SPI_PENDING_DISABLED),
				 0, VGIC_V5_IST_PRIO_HIGH, false);

	gicv5_send_lpi(RESTORE_TEST_LPI_PENDING);

	GUEST_SYNC(GUEST_CMD_IST_SOURCE_READY);

	while (1)
		wfi();
}

/*
 * This is the restore VM for the IST save/restore test. The IRS regs and ISTs
 * have been restored before this code runs. Based on which VPE is running, a
 * different set of interrupts is expected. We check that we get what we
 * expected before terminating the test.
 *
 * The expected interrupts for VPE0 are:
 * - High-priority SPI, pending and enabled before save.
 * - Low-priority SPI, pending and enabled before save. This is only delivered
 *   after the guest lowers the masked priority via ICC_PCR_EL1.
 * - SPI that was pending but disabled before save. This is only delivered
 *   after the guest enables it after restore.
 * - SPI that was enabled but not pending before save. Userspace injects this
 *   after restore to check that the enable state and affinity were restored.
 *
 * The expected interrupts for VPE1 are:
 * - LPI that was pending and enabled before save.
 * - LPI that was enabled but not pending before save. The guest injects this
 *   after restore to check that the enable state and affinity were restored.
 */
static void guest_ist_restore_irq_handler(struct ex_regs *regs)
{
	u32 vcpu_id = guest_get_vcpuid();
	u32 count = ist_restore_irq_count[vcpu_id];
	u64 hwirq, expected_hwirq = 0;
	u64 ia;

	ia = gicr_insn(CDIA);
	if (!GICV5_GICR_CDIA_VALID(ia))
		return;

	gsb_ack();
	isb();

	hwirq = FIELD_GET(GICV5_GICR_CDIA_INTID, ia);

	if (!vcpu_id) {
		switch (count) {
		case 0:
			expected_hwirq = gicv5_spi_hwirq(RESTORE_TEST_SPI_PRIO_HIGH);
			break;
		case 1:
			expected_hwirq = gicv5_spi_hwirq(RESTORE_TEST_SPI_PRIO_LOW);
			break;
		case 2:
			expected_hwirq = gicv5_spi_hwirq(RESTORE_TEST_SPI_PENDING_DISABLED);
			break;
		case 3:
			expected_hwirq = gicv5_spi_hwirq(RESTORE_TEST_SPI_ENABLED);
			break;
		default:
			GUEST_FAIL("Unexpected VPE0 IST restore interrupt");
		}
	} else {
		switch (count) {
		case 0:
			expected_hwirq = gicv5_lpi_hwirq(RESTORE_TEST_LPI_PENDING);
			break;
		case 1:
			expected_hwirq = gicv5_lpi_hwirq(RESTORE_TEST_LPI_ENABLED);
			break;
		default:
			GUEST_FAIL("Unexpected VPE1 IST restore interrupt");
		}
	}

	GUEST_ASSERT_EQ(hwirq, expected_hwirq);

	gic_insn(hwirq, CDDI);
	gic_insn(0, CDEOI);
	ist_restore_irq_count[vcpu_id]++;

	if (!vcpu_id) {
		switch (count) {
		case 0:
			gicv5_set_pcr(VGIC_V5_IST_PRIO_LOW);
			break;
		case 1:
			gicv5_enable_hwirq(
				gicv5_spi_hwirq(RESTORE_TEST_SPI_PENDING_DISABLED));
			break;
		case 2:
			GUEST_SYNC(GUEST_CMD_IST_SPI_INJECT_READY);
			break;
		case 3:
			GUEST_DONE();
		}
	} else {
		switch (count) {
		case 0:
			gicv5_send_lpi(RESTORE_TEST_LPI_ENABLED);
			GUEST_SYNC(GUEST_CMD_IST_LPI_INJECT_READY);
			break;
		case 1:
			GUEST_DONE();
		}
	}
}

static void guest_ist_restore_code(void)
{
	local_irq_disable();

	gicv5_cpu_enable_interrupts();

	if (!guest_get_vcpuid())
		gicv5_set_pcr(VGIC_V5_IST_PRIO_HIGH);

	GUEST_SYNC(GUEST_CMD_IS_READY);

	local_irq_enable();

	while (1)
		wfi();
}

/* we don't want to assert on run execution, hence that helper */
static int run_vcpu(struct kvm_vcpu *vcpu)
{
	return __vcpu_run(vcpu) ? -errno : 0;
}

static void vm_gic_destroy(struct vm_gic *v)
{
	close(v->gic_fd);
	kvm_vm_free(v->vm);
}

static void vgic_v5_map_irs(struct kvm_vm *vm)
{
	unsigned int nr_irs_pages;

	nr_irs_pages = vm_calc_num_guest_pages(vm->mode, VGIC_V5_IRS_SIZE);

	/* Map the IRS at VA == IPA so guest MMIO writes hit the IRS IODEV. */
	virt_map(vm, GICV5_IRS_CONFIG_BASE_GPA,
		 GICV5_IRS_CONFIG_BASE_GPA, nr_irs_pages);
}

static u32 vgic_v5_irq_line_payload(u32 type, u32 num)
{
	return (type << KVM_ARM_IRQ_TYPE_SHIFT) |
	       FIELD_PREP(KVM_ARM_IRQ_NUM_MASK, num);
}

static int __vgic_v5_irq_line(struct kvm_vm *vm, u32 type, u32 num, int level)
{
	return _kvm_irq_line(vm, vgic_v5_irq_line_payload(type, num), level);
}

static void vgic_v5_spi_line(struct kvm_vm *vm, u32 spi, int level)
{
	int ret = __vgic_v5_irq_line(vm, KVM_ARM_IRQ_TYPE_SPI, spi, level);

	TEST_ASSERT(!ret, "KVM_IRQ_LINE failed for SPI %u level %d",
		    spi, level);
}

static void vgic_v5_spi_line_vm_create(struct vm_gic *v,
				       struct kvm_vcpu **vcpu,
				       u32 nr_spis)
{
	u64 attr;

	v->gic_dev_type = KVM_DEV_TYPE_ARM_VGIC_V5;
	v->vm = __vm_create(VM_SHAPE_DEFAULT, NR_VCPUS, 0);
	v->gic_fd = kvm_create_device(v->vm, v->gic_dev_type);
	*vcpu = vm_vcpu_add(v->vm, 0, guest_spi_line_code);
	TEST_ASSERT(*vcpu, "Failed to create vCPU");

	vm_init_descriptor_tables(v->vm);
	vm_install_exception_handler(v->vm, VECTOR_IRQ_CURRENT,
				     guest_spi_irq_handler);
	vcpu_init_descriptor_tables(*vcpu);

	kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS, 0,
			    &nr_spis);

	attr = GICV5_IRS_CONFIG_BASE_GPA;
	kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V5_ADDR_TYPE_IRS, &attr);
	vgic_v5_map_irs(v->vm);
	kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);
}

static void vgic_v5_lpi_ist_vm_create(struct vm_gic *v,
				      struct kvm_vcpu *vcpus[VGIC_V5_LPI_NR_VCPUS],
				      void (*guest_code)(void),
				      void (*irq_handler)(struct ex_regs *))
{
	unsigned int nr_lpi_ist_pages;
	u64 attr;
	int i;

	v->gic_dev_type = KVM_DEV_TYPE_ARM_VGIC_V5;
	v->vm = __vm_create(VM_SHAPE_DEFAULT, VGIC_V5_LPI_NR_VCPUS, 0);
	v->gic_fd = kvm_create_device(v->vm, v->gic_dev_type);

	for (i = 0; i < VGIC_V5_LPI_NR_VCPUS; i++) {
		vcpus[i] = vm_vcpu_add(v->vm, i, guest_code);
		TEST_ASSERT(vcpus[i], "Failed to create vCPU");
	}

	vm_init_descriptor_tables(v->vm);
	vm_install_exception_handler(v->vm, VECTOR_IRQ_CURRENT,
				     irq_handler);

	for (i = 0; i < VGIC_V5_LPI_NR_VCPUS; i++)
		vcpu_init_descriptor_tables(vcpus[i]);

	attr = GICV5_IRS_CONFIG_BASE_GPA;
	kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V5_ADDR_TYPE_IRS, &attr);
	vgic_v5_map_irs(v->vm);

	nr_lpi_ist_pages = vm_calc_num_guest_pages(v->vm->mode,
						   VGIC_V5_LPI_IST_SIZE);
	vm_userspace_mem_region_add(v->vm, VM_MEM_SRC_ANONYMOUS,
				    VGIC_V5_LPI_IST_BASE_GPA,
				    VGIC_V5_LPI_MEMSLOT,
				    nr_lpi_ist_pages, 0);
	/*
	 * Map the IST at VA == IPA so the guest can program the same BASER
	 * address.
	 */
	virt_map(v->vm, VGIC_V5_LPI_IST_BASE_GPA,
		 VGIC_V5_LPI_IST_BASE_GPA, nr_lpi_ist_pages);

	kvm_device_attr_set(v->gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);
}

static void vgic_v5_lpi_vm_create(struct vm_gic *v,
				  struct kvm_vcpu *vcpus[VGIC_V5_LPI_NR_VCPUS])
{
	vgic_v5_lpi_ist_vm_create(v, vcpus, guest_lpi_code,
				  guest_lpi_irq_handler);
}

static void vgic_v5_ist_restore_vm_create(struct vm_gic *v,
					  struct kvm_vcpu *vcpus[VGIC_V5_LPI_NR_VCPUS],
					  void (*guest_code)(void),
					  void (*irq_handler)(struct ex_regs *))
{
	vgic_v5_lpi_ist_vm_create(v, vcpus, guest_code, irq_handler);
}

static void vgic_v5_run_spi_line_test(u32 nr_spis, u32 expected_spi,
				      bool level_sensitive,
				      bool lower_before_run)
{
	struct kvm_vcpu *vcpu;
	struct vm_gic v;
	struct ucall uc;
	int ret;

	spi_line_expected = expected_spi;
	spi_line_level_sensitive = level_sensitive;

	vgic_v5_spi_line_vm_create(&v, &vcpu, nr_spis);

	sync_global_to_guest(v.vm, spi_line_expected);
	sync_global_to_guest(v.vm, spi_line_level_sensitive);

	ret = run_vcpu(vcpu);
	TEST_ASSERT(!ret, "Failed to run GICv5 IRQ_LINE VM");
	TEST_ASSERT(get_ucall(vcpu, &uc) == UCALL_SYNC &&
		    uc.args[1] == GUEST_CMD_IS_READY,
		    "GICv5 IRQ_LINE guest did not become ready");

	vgic_v5_spi_line(v.vm, expected_spi, 1);
	/*
	 * For edge-triggered SPIs, a following low transition must be ignored:
	 * once the edge has made the SPI pending, it cannot be recalled. This
	 * allows us to test that.
	 */
	if (lower_before_run)
		vgic_v5_spi_line(v.vm, expected_spi, 0);

	while (1) {
		ret = run_vcpu(vcpu);
		TEST_ASSERT(!ret, "Failed to run GICv5 IRQ_LINE VM");

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}

done:
	vm_gic_destroy(&v);
}

static void vgic_v5_expect_sync(struct kvm_vcpu *vcpu, u64 cmd,
				const char *msg)
{
	struct ucall uc;

	TEST_ASSERT(get_ucall(vcpu, &uc) == UCALL_SYNC && uc.args[1] == cmd,
		    "%s", msg);
}

struct vgic_region_attr {
	u64 attr;
	u64 size;
	u64 alignment;
};

static const struct vgic_region_attr gic_v5_irs_region = {
	.attr = KVM_VGIC_V5_ADDR_TYPE_IRS,
	.size = GICV5_IRS_SIZE,
	.alignment = GICV5_IRS_ALIGN,
};

struct vgic_sysreg_attr {
	const char	*name;
	u32		encoding;
};

struct vgic_irs_reg_attr {
	const char	*name;
	u64		attr;
};

#define PACK_SR(r)						\
	((sys_reg_Op0(r) << 14) |				\
	 (sys_reg_Op1(r) << 11) |				\
	 (sys_reg_CRn(r) << 7) |				\
	 (sys_reg_CRm(r) << 3) |				\
	 (sys_reg_Op2(r)))

#define SR(r)							\
	{							\
		.name		= #r,				\
		.encoding	= r,				\
	}

#define IRS_REG(r)						\
	{							\
		.name		= #r,				\
		.attr		= r,				\
	}

static const struct vgic_irs_reg_attr gic_v5_irs_regs[] = {
	IRS_REG(GICV5_IRS_IDR0),
	IRS_REG(GICV5_IRS_IDR1),
	IRS_REG(GICV5_IRS_IDR2),
	IRS_REG(GICV5_IRS_IDR3),
	IRS_REG(GICV5_IRS_IDR4),
	IRS_REG(GICV5_IRS_IDR5),
	IRS_REG(GICV5_IRS_IDR6),
	IRS_REG(GICV5_IRS_IDR7),
	IRS_REG(GICV5_IRS_IIDR),
	IRS_REG(GICV5_IRS_AIDR),
	IRS_REG(GICV5_IRS_CR0),
	IRS_REG(GICV5_IRS_CR1),
	IRS_REG(GICV5_IRS_SYNCR),
	IRS_REG(GICV5_IRS_SYNC_STATUSR),
	IRS_REG(GICV5_IRS_SPI_VMR),
	IRS_REG(GICV5_IRS_SPI_SELR),
	IRS_REG(GICV5_IRS_SPI_DOMAINR),
	IRS_REG(GICV5_IRS_SPI_RESAMPLER),
	IRS_REG(GICV5_IRS_SPI_CFGR),
	IRS_REG(GICV5_IRS_SPI_STATUSR),
	IRS_REG(GICV5_IRS_PE_SELR),
	IRS_REG(GICV5_IRS_PE_STATUSR),
	IRS_REG(GICV5_IRS_PE_CR0),
	IRS_REG(GICV5_IRS_IST_BASER),
	IRS_REG(GICV5_IRS_IST_CFGR),
	IRS_REG(GICV5_IRS_IST_STATUSR),
	IRS_REG(GICV5_IRS_MAP_L2_ISTR),
	IRS_REG(GICV5_IRS_VMT_BASER),
	IRS_REG(GICV5_IRS_VMT_CFGR),
	IRS_REG(GICV5_IRS_VMT_STATUSR),
	IRS_REG(GICV5_IRS_VPE_SELR),
	IRS_REG(GICV5_IRS_VPE_DBR),
	IRS_REG(GICV5_IRS_VPE_HPPIR),
	IRS_REG(GICV5_IRS_VPE_CR0),
	IRS_REG(GICV5_IRS_VPE_STATUSR),
	IRS_REG(GICV5_IRS_VM_DBR),
	IRS_REG(GICV5_IRS_VM_SELR),
	IRS_REG(GICV5_IRS_VM_STATUSR),
	IRS_REG(GICV5_IRS_VMAP_L2_VMTR),
	IRS_REG(GICV5_IRS_VMAP_VMR),
	IRS_REG(GICV5_IRS_VMAP_VISTR),
	IRS_REG(GICV5_IRS_VMAP_L2_VISTR),
	IRS_REG(GICV5_IRS_VMAP_VPER),
	IRS_REG(GICV5_IRS_SAVE_VMR),
	IRS_REG(GICV5_IRS_SAVE_VM_STATUSR),
	IRS_REG(GICV5_IRS_MEC_IDR),
	IRS_REG(GICV5_IRS_MEC_MECID_R),
	IRS_REG(GICV5_IRS_MPAM_IDR),
	IRS_REG(GICV5_IRS_MPAM_PARTID_R),
	IRS_REG(GICV5_IRS_SWERR_STATUSR),
	IRS_REG(GICV5_IRS_SWERR_SYNDROMER0),
	IRS_REG(GICV5_IRS_SWERR_SYNDROMER1),
};

static const struct vgic_sysreg_attr gic_v5_cpu_sysregs[] = {
	SR(SYS_ICC_ICSR_EL1),
	SR(SYS_ICC_PPI_ENABLER0_EL1),
	SR(SYS_ICC_PPI_ENABLER1_EL1),
	SR(SYS_ICC_PPI_SACTIVER0_EL1),
	SR(SYS_ICC_PPI_SACTIVER1_EL1),
	SR(SYS_ICC_PPI_SPENDR0_EL1),
	SR(SYS_ICC_PPI_SPENDR1_EL1),
	SR(SYS_ICC_PPI_PRIORITYR0_EL1),
	SR(SYS_ICC_PPI_PRIORITYR1_EL1),
	SR(SYS_ICC_PPI_PRIORITYR2_EL1),
	SR(SYS_ICC_PPI_PRIORITYR3_EL1),
	SR(SYS_ICC_PPI_PRIORITYR4_EL1),
	SR(SYS_ICC_PPI_PRIORITYR5_EL1),
	SR(SYS_ICC_PPI_PRIORITYR6_EL1),
	SR(SYS_ICC_PPI_PRIORITYR7_EL1),
	SR(SYS_ICC_PPI_PRIORITYR8_EL1),
	SR(SYS_ICC_PPI_PRIORITYR9_EL1),
	SR(SYS_ICC_PPI_PRIORITYR10_EL1),
	SR(SYS_ICC_PPI_PRIORITYR11_EL1),
	SR(SYS_ICC_PPI_PRIORITYR12_EL1),
	SR(SYS_ICC_PPI_PRIORITYR13_EL1),
	SR(SYS_ICC_PPI_PRIORITYR14_EL1),
	SR(SYS_ICC_PPI_PRIORITYR15_EL1),
	SR(SYS_ICC_APR_EL1),
	SR(SYS_ICC_CR0_EL1),
	SR(SYS_ICC_PCR_EL1),
};

static void vgic_v5_save_irs_regs(struct vm_gic *v, u64 *regs)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(gic_v5_irs_regs); i++) {
		ret = __kvm_device_attr_get(v->gic_fd,
					    KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
					    gic_v5_irs_regs[i].attr,
					    &regs[i]);
		TEST_ASSERT(!ret, "GICv5 IRS_REGS save failed for %s",
			    gic_v5_irs_regs[i].name);
	}
}

static void vgic_v5_restore_irs_regs(struct vm_gic *v, u64 *regs)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(gic_v5_irs_regs); i++) {
		ret = __kvm_device_attr_set(v->gic_fd,
					    KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
					    gic_v5_irs_regs[i].attr,
					    &regs[i]);
		TEST_ASSERT(!ret, "GICv5 IRS_REGS restore failed for %s",
			    gic_v5_irs_regs[i].name);
	}
}

static u64 vgic_v5_saved_irs_reg(u64 *regs, u64 attr)
{
	for (int i = 0; i < ARRAY_SIZE(gic_v5_irs_regs); i++) {
		if (gic_v5_irs_regs[i].attr == attr)
			return regs[i];
	}

	TEST_FAIL("GICv5 IRS register 0x%lx was not saved", attr);
}

static size_t vgic_v5_lpi_ist_state_size(u64 *irs_regs)
{
	u64 cfgr = vgic_v5_saved_irs_reg(irs_regs, GICV5_IRS_IST_CFGR);
	u32 lpi_id_bits = FIELD_GET(GICV5_IRS_IST_CFGR_LPI_ID_BITS, cfgr);

	return BIT(lpi_id_bits) * sizeof(__u32);
}

static void test_vgic_v5_addr_attrs(void)
{
	struct kvm_vcpu *vcpu;
	struct vm_gic v;
	u64 addr;
	int ret;

	v.gic_dev_type = KVM_DEV_TYPE_ARM_VGIC_V5;
	v.vm = __vm_create(VM_SHAPE_DEFAULT, NR_VCPUS, 0);
	v.gic_fd = kvm_create_device(v.vm, v.gic_dev_type);

	/* Check existing group/attributes */
	kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR, gic_v5_irs_region.attr);

	/* check non existing attribute */
	ret = __kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR, -1);
	TEST_ASSERT(ret && errno == ENXIO, "attribute not supported");

	/* get IRS base address before setting*/
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V5_ADDR_TYPE_IRS, &addr);
	TEST_ASSERT(!ret && addr == (-1ULL), "GICv5 IRS returns VGIC_ADDR_UNDEF");

	/* misaligned IRS address settings */
	addr = gic_v5_irs_region.alignment / 0x10;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V5_ADDR_TYPE_IRS, &addr);
	TEST_ASSERT(ret && errno == EINVAL, "GIC IRS base not aligned");

	/* out of range address */
	addr = max_phys_size;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V5_ADDR_TYPE_IRS, &addr);
	TEST_ASSERT(ret && errno == E2BIG, "IRS address beyond IPA limit");

	/* Space for half an IRS (an IRS is: 2 * irs.alignment). */
	addr = max_phys_size - gic_v5_irs_region.alignment;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V5_ADDR_TYPE_IRS, &addr);
	TEST_ASSERT(ret && errno == E2BIG,
			"half of the IRS is beyond IPA limit");

	/* set IRS base address @0x0*/
	addr = 0x00000;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V5_ADDR_TYPE_IRS, &addr);
	TEST_ASSERT(!ret, "GICv5 IRS base correctly set");

	/* get IRS base address */
	addr = 0xbad;
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V5_ADDR_TYPE_IRS, &addr);
	TEST_ASSERT(!ret && addr == 0, "GICv5 IRS base correctly set");

	/* Attempt to create a second IRS region */
	addr = 0xE0000;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
				    KVM_VGIC_V5_ADDR_TYPE_IRS, &addr);
	TEST_ASSERT(ret && errno == EEXIST, "GICv5 IRS base set again");

	vm_gic_destroy(&v);

	/* Try running a VM without ever setting the IRS base addr */
	v.vm = __vm_create(VM_SHAPE_DEFAULT, NR_VCPUS, 0);
	v.gic_fd = kvm_create_device(v.vm, v.gic_dev_type);
	vcpu = vm_vcpu_add(v.vm, 0, NULL);
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);
	ret = run_vcpu(vcpu);
	TEST_ASSERT(ret && errno == ENXIO, "GICv5 IRS base not set");

	vm_gic_destroy(&v);
}

static void test_vgic_v5_nr_irqs_attrs(void)
{
	struct kvm_vcpu *vcpu;
	struct vm_gic v;
	u32 nr_irqs;
	int ret;

	v.gic_dev_type = KVM_DEV_TYPE_ARM_VGIC_V5;
	v.vm = __vm_create(VM_SHAPE_DEFAULT, NR_VCPUS, 0);
	v.gic_fd = kvm_create_device(v.vm, v.gic_dev_type);

	/* Check existing group/attribute */
	kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS, 0);

	/* Before userspace sets NR_IRQS, no SPI count has been selected. */
	nr_irqs = 0xbad;
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS,
				    0, &nr_irqs);
	TEST_ASSERT(!ret && nr_irqs == 0, "GICv5 NR_IRQS defaults to 0 before init");

	/* Too few SPIs */
	nr_irqs = VGIC_V5_DEFAULT_NR_SPIS - 1;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS,
				    0, &nr_irqs);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 NR_IRQS below minimum");

	/* Not a multiple of 32 */
	nr_irqs = VGIC_V5_DEFAULT_NR_SPIS + 1;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS,
				    0, &nr_irqs);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 NR_IRQS not 32-aligned");

	/* Larger than KVM's supported VGICv5 SPI count */
	nr_irqs = VGIC_V5_MAX_NR_SPIS + 32;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS,
				    0, &nr_irqs);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 NR_IRQS above maximum");

	/* Valid custom SPI count */
	nr_irqs = VGIC_V5_MAX_NR_SPIS;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS,
				    0, &nr_irqs);
	TEST_ASSERT(!ret, "GICv5 NR_IRQS accepts valid custom SPI count");

	nr_irqs = 0xbad;
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS,
				    0, &nr_irqs);
	TEST_ASSERT(!ret && nr_irqs == VGIC_V5_MAX_NR_SPIS,
		    "GICv5 NR_IRQS returns SPI count only");

	/* A second successful configuration attempt must be rejected. */
	nr_irqs = VGIC_V5_DEFAULT_NR_SPIS;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS,
				    0, &nr_irqs);
	TEST_ASSERT(ret && errno == EBUSY, "GICv5 NR_IRQS set twice");

	/* The maximum supported count must also initialize successfully. */
	vcpu = vm_vcpu_add(v.vm, 0, NULL);
	TEST_ASSERT(vcpu, "Failed to create vCPU");
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	vm_gic_destroy(&v);

	/* If userspace does not set NR_IRQS, init selects the default. */
	v.vm = __vm_create(VM_SHAPE_DEFAULT, NR_VCPUS, 0);
	v.gic_fd = kvm_create_device(v.vm, v.gic_dev_type);
	vcpu = vm_vcpu_add(v.vm, 0, NULL);
	TEST_ASSERT(vcpu, "Failed to create vCPU");
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	nr_irqs = 0xbad;
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS,
				    0, &nr_irqs);
	TEST_ASSERT(!ret && nr_irqs == VGIC_V5_DEFAULT_NR_SPIS,
		    "GICv5 NR_IRQS defaults to 32 SPIs after init");

	nr_irqs = VGIC_V5_DEFAULT_NR_SPIS * 2;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS,
				    0, &nr_irqs);
	TEST_ASSERT(ret && errno == EBUSY, "GICv5 NR_IRQS set after init");

	vm_gic_destroy(&v);

}

static void test_vgic_v5_irs_regs_attrs(void)
{
	struct kvm_vcpu *vcpu;
	struct vm_gic v;
	u64 attr, val;
	int ret, i;

	v.gic_dev_type = KVM_DEV_TYPE_ARM_VGIC_V5;
	v.vm = __vm_create(VM_SHAPE_DEFAULT, NR_VCPUS, 0);
	v.gic_fd = kvm_create_device(v.vm, v.gic_dev_type);
	vcpu = vm_vcpu_add(v.vm, 0, NULL);
	TEST_ASSERT(vcpu, "Failed to create vCPU");

	/* IRS_REGS attributes can be probed before the IRS base is set. */
	kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
			    GICV5_IRS_IDR0);

	attr = GICV5_IRS_CONFIG_BASE_GPA;
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V5_ADDR_TYPE_IRS, &attr);

	/* Check existing group/attribute */
	kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
			    GICV5_IRS_IDR0);

	/* IRS_REGS are not accessible before the VGIC is initialized. */
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR0, &val);
	TEST_ASSERT(ret && errno == EBUSY, "GICv5 IRS_REGS get before init");

	val = 0;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR0, &val);
	TEST_ASSERT(ret && errno == EBUSY, "GICv5 IRS_REGS set before init");

	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	/* Read all supported IRS regs and write the value back. */
	for (i = 0; i < ARRAY_SIZE(gic_v5_irs_regs); i++) {
		attr = gic_v5_irs_regs[i].attr;
		ret = __kvm_has_device_attr(v.gic_fd,
					    KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
					    attr);
		TEST_ASSERT(!ret, "GICv5 IRS_REGS missing %s",
			    gic_v5_irs_regs[i].name);

		val = 0xbad;
		ret = __kvm_device_attr_get(v.gic_fd,
					    KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
					    attr, &val);
		TEST_ASSERT(!ret, "GICv5 IRS_REGS get failed for %s",
			    gic_v5_irs_regs[i].name);

		ret = __kvm_device_attr_set(v.gic_fd,
					    KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
					    attr, &val);
		TEST_ASSERT(!ret, "GICv5 IRS_REGS set failed for %s",
			    gic_v5_irs_regs[i].name);
	}

	/* Check bad offsets */
	attr = 0x10000;
	ret = __kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    attr);
	TEST_ASSERT(ret && errno == ENXIO, "GICv5 IRS_REGS accepted bad offset");

	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    attr, &val);
	TEST_ASSERT(ret && errno == ENXIO, "GICv5 IRS_REGS get bad offset");

	/* Check alignment for 32-bit and 64-bit IRS regs. */
	attr = GICV5_IRS_IDR0 + 2;
	ret = __kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IRS_REGS accepted unaligned 32-bit attr");

	attr = GICV5_IRS_IST_BASER + 4;
	ret = __kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IRS_REGS accepted unaligned 64-bit attr");

	/* Check bad user pointers */
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR0, NULL);
	TEST_ASSERT(ret && errno == EFAULT, "GICv5 IRS_REGS get with bad pointer");

	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR0, NULL);
	TEST_ASSERT(ret && errno == EFAULT, "GICv5 IRS_REGS set with bad pointer");

	/* ID restore validation rejects unsupported values. */
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR0, &val);
	TEST_ASSERT(!ret, "GICv5 IRS_REGS get IDR0 failed");
	val &= ~GICV5_IRS_IDR0_INT_DOM;
	val |= FIELD_PREP(GICV5_IRS_IDR0_INT_DOM,
			  GICV5_IRS_IDR0_INT_DOM_SECURE);
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR0, &val);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IRS_REGS accepted bad IDR0 domain");

	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR0, &val);
	TEST_ASSERT(!ret, "GICv5 IRS_REGS get IDR0 failed");
	val |= GICV5_IRS_IDR0_SETLPI;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR0, &val);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IRS_REGS accepted unsupported IDR0");

	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR1, &val);
	TEST_ASSERT(!ret, "GICv5 IRS_REGS get IDR1 failed");
	val |= FIELD_PREP(GICV5_IRS_IDR1_PRIORITY_BITS, 0x7);
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR1, &val);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IRS_REGS accepted bad IDR1");

	val = 0;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR2, &val);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IRS_REGS accepted bad IDR2");

	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR5, &val);
	TEST_ASSERT(!ret, "GICv5 IRS_REGS get IDR5 failed");
	val++;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR5, &val);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IRS_REGS accepted bad IDR5");

	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR6, &val);
	TEST_ASSERT(!ret, "GICv5 IRS_REGS get IDR6 failed");
	val++;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR6, &val);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IRS_REGS accepted bad IDR6");

	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR7, &val);
	TEST_ASSERT(!ret, "GICv5 IRS_REGS get IDR7 failed");
	val++;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IDR7, &val);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IRS_REGS accepted bad IDR7");

	/* Status registers read as idle through userspace accessors. */
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_SYNC_STATUSR, &val);
	TEST_ASSERT(!ret && val == GICV5_IRS_SYNC_STATUSR_IDLE,
		    "GICv5 IRS_REGS SYNC_STATUSR is not idle");

	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_SPI_STATUSR, &val);
	TEST_ASSERT(!ret && val == GICV5_IRS_SPI_STATUSR_IDLE,
		    "GICv5 IRS_REGS SPI_STATUSR is not idle");

	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_PE_STATUSR, &val);
	TEST_ASSERT(!ret && val == GICV5_IRS_PE_STATUSR_IDLE,
		    "GICv5 IRS_REGS PE_STATUSR is not idle");

	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IST_STATUSR, &val);
	TEST_ASSERT(!ret && val == GICV5_IRS_IST_STATUSR_IDLE,
		    "GICv5 IRS_REGS IST_STATUSR is not idle");

	/*
	 * Userspace restores IST_BASER without allocating or accessing an IST.
	 * Supply a synthetic, aligned guest address only to verify the register
	 * round trip.
	 */
	val = FIELD_PREP(GICV5_IRS_IST_BASER_ADDR_MASK,
			 VGIC_V5_TEST_IST_BASER_GPA >> GICV5_IRS_IST_BASER_ADDR_SHIFT);
	val |= GICV5_IRS_IST_BASER_VALID;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IST_BASER, &val);
	TEST_ASSERT(!ret, "GICv5 IRS_REGS failed to restore IST_BASER");

	attr = 0xbad;
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IST_BASER, &attr);
	TEST_ASSERT(!ret && attr == val, "GICv5 IRS_REGS IST_BASER restore mismatch");

	vm_gic_destroy(&v);
}

static void test_vgic_v5_ist_attrs(void)
{
	struct kvm_vgic_v5_ist ist_attr;
	struct kvm_vcpu *vcpu;
	struct vm_gic v;
	u32 spi_ist[VGIC_V5_DEFAULT_NR_SPIS];
	u64 attr;
	int ret;

	v.gic_dev_type = KVM_DEV_TYPE_ARM_VGIC_V5;
	v.vm = __vm_create(VM_SHAPE_DEFAULT, NR_VCPUS, 0);
	v.gic_fd = kvm_create_device(v.vm, v.gic_dev_type);
	vcpu = vm_vcpu_add(v.vm, 0, NULL);
	TEST_ASSERT(vcpu, "Failed to create vCPU");

	/* Check existing group/attribute */
	kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST, 0);

	/* Check non-existing attribute */
	ret = __kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST, 1);
	TEST_ASSERT(ret && errno == ENXIO, "GICv5 IST accepted bad attr");

	ist_attr = vgic_v5_ist_attr(spi_ist, sizeof(spi_ist), NULL, 0);
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    1, &ist_attr);
	TEST_ASSERT(ret && errno == ENXIO, "GICv5 IST get accepted bad attr");

	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    1, &ist_attr);
	TEST_ASSERT(ret && errno == ENXIO, "GICv5 IST set accepted bad attr");

	attr = GICV5_IRS_CONFIG_BASE_GPA;
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V5_ADDR_TYPE_IRS, &attr);

	/* IST save/restore is not accessible before the VGIC is initialized. */
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EBUSY, "GICv5 IST get before init");

	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EBUSY, "GICv5 IST set before init");

	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	/* A VM with SPIs must provide a userspace IST descriptor. */
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, NULL);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IST get accepted NULL descriptor");

	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, NULL);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IST set accepted NULL descriptor");

	/* Check bad userspace IST descriptors. */
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, (void *)1);
	TEST_ASSERT(ret && errno == EFAULT, "GICv5 IST get with bad descriptor");

	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, (void *)1);
	TEST_ASSERT(ret && errno == EFAULT, "GICv5 IST set with bad descriptor");

	/* Check missing and incorrectly sized SPI IST buffers. */
	ist_attr = vgic_v5_ist_attr(NULL, 0, NULL, 0);
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IST get accepted missing SPI buffer");
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IST set accepted missing SPI buffer");

	ist_attr = vgic_v5_ist_attr(spi_ist, sizeof(spi_ist) - sizeof(__u32),
					 NULL, 0);
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IST set accepted bad SPI size");
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IST get accepted bad SPI size");

	/* LPI storage must be absent when no LPI IST is configured. */
	ist_attr = vgic_v5_ist_attr(spi_ist, sizeof(spi_ist), spi_ist,
					 sizeof(__u32));
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IST get accepted unexpected LPI buffer");
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IST set accepted unexpected LPI buffer");

	vm_gic_destroy(&v);

	/* IST restore is rejected after the VM has run. */
	v.vm = __vm_create(VM_SHAPE_DEFAULT, NR_VCPUS, 0);
	v.gic_fd = kvm_create_device(v.vm, v.gic_dev_type);
	vcpu = vm_vcpu_add(v.vm, 0, guest_code);

	attr = GICV5_IRS_CONFIG_BASE_GPA;
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V5_ADDR_TYPE_IRS, &attr);
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	ret = run_vcpu(vcpu);
	TEST_ASSERT(!ret, "Failed to run GICv5 VM before IST restore test");

	ist_attr = vgic_v5_ist_attr(spi_ist, sizeof(spi_ist), NULL, 0);
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EBUSY, "GICv5 IST restore after run");

	vm_gic_destroy(&v);
}

static void test_vgic_v5_userspace_ppis_attrs(void)
{
	struct kvm_vcpu *vcpu;
	struct vm_gic v;
	u64 user_ppis[2];
	int ret;

	v.gic_dev_type = KVM_DEV_TYPE_ARM_VGIC_V5;
	v.vm = __vm_create(VM_SHAPE_DEFAULT, NR_VCPUS, 0);
	v.gic_fd = kvm_create_device(v.vm, v.gic_dev_type);
	vcpu = vm_vcpu_add(v.vm, 0, NULL);
	TEST_ASSERT(vcpu, "Failed to create vCPU");

	kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT);
	kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_USERSPACE_PPIS);

	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	user_ppis[0] = 0;
	user_ppis[1] = 0xbad;
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
				    KVM_DEV_ARM_VGIC_USERSPACE_PPIS, user_ppis);
	TEST_ASSERT(!ret, "GICv5 USERSPACE_PPIS get failed");
	TEST_ASSERT(user_ppis[0] & BIT(GICV5_ARCH_PPI_SW_PPI),
		    "GICv5 USERSPACE_PPIS does not expose SW_PPI");
	TEST_ASSERT(!user_ppis[1], "GICv5 USERSPACE_PPIS upper word is not zero");

	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
				    KVM_DEV_ARM_VGIC_USERSPACE_PPIS, user_ppis);
	TEST_ASSERT(ret && errno == ENXIO, "GICv5 USERSPACE_PPIS set accepted");

	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
				    KVM_DEV_ARM_VGIC_USERSPACE_PPIS, NULL);
	TEST_ASSERT(ret && errno == EFAULT, "GICv5 USERSPACE_PPIS get with bad pointer");

	vm_gic_destroy(&v);
}

static void test_vgic_v5_cpu_sysreg_attrs(void)
{
	struct kvm_vcpu *vcpu;
	struct vm_gic v;
	u64 attr, val;
	int ret, i;

	v.gic_dev_type = KVM_DEV_TYPE_ARM_VGIC_V5;
	v.vm = __vm_create(VM_SHAPE_DEFAULT, NR_VCPUS, 0);
	v.gic_fd = kvm_create_device(v.vm, v.gic_dev_type);
	vcpu = vm_vcpu_add(v.vm, 0, NULL);
	TEST_ASSERT(vcpu, "Failed to create vCPU");

	/* Check existing group/attribute */
	attr = PACK_SR(SYS_ICC_CR0_EL1);
	kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS, attr);

	/* CPU sysregs are not accessible before the VGIC is initialized. */
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
				    attr, &val);
	TEST_ASSERT(ret && errno == EBUSY, "GICv5 CPU_SYSREGS get before init");

	val = 0;
	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
				    attr, &val);
	TEST_ASSERT(ret && errno == EBUSY, "GICv5 CPU_SYSREGS set before init");

	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	/* Read all exposed CPU sysregs and write the value back. */
	for (i = 0; i < ARRAY_SIZE(gic_v5_cpu_sysregs); i++) {
		attr = PACK_SR(gic_v5_cpu_sysregs[i].encoding);
		ret = __kvm_has_device_attr(v.gic_fd,
					    KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
					    attr);
		TEST_ASSERT(!ret, "GICv5 CPU_SYSREGS missing %s",
			    gic_v5_cpu_sysregs[i].name);

		val = 0xbad;
		ret = __kvm_device_attr_get(v.gic_fd,
					    KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
					    attr, &val);
		TEST_ASSERT(!ret, "GICv5 CPU_SYSREGS get failed for %s",
			    gic_v5_cpu_sysregs[i].name);

		ret = __kvm_device_attr_set(v.gic_fd,
					    KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
					    attr, &val);
		TEST_ASSERT(!ret, "GICv5 CPU_SYSREGS set failed for %s",
			    gic_v5_cpu_sysregs[i].name);
	}

	/* Check non existent GICv3 sysreg */
	attr = PACK_SR(SYS_ICC_CTLR_EL1);
	ret = __kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
				    attr);
	TEST_ASSERT(ret && errno == ENXIO, "GICv5 CPU_SYSREGS accepted bad sysreg");

	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
				    attr, &val);
	TEST_ASSERT(ret && errno == ENOENT, "GICv5 CPU_SYSREGS get bad sysreg");

	/* Check non existing vCPU */
	attr = PACK_SR(SYS_ICC_CR0_EL1) |
	       (1ULL << KVM_DEV_ARM_VGIC_V3_MPIDR_SHIFT);
	ret = __kvm_has_device_attr(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
				    attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 CPU_SYSREGS accepted bad MPIDR");

	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
				    attr, &val);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 CPU_SYSREGS get bad MPIDR");

	/* Check bad user pointers */
	attr = PACK_SR(SYS_ICC_CR0_EL1);
	ret = __kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
				    attr, NULL);
	TEST_ASSERT(ret && errno == EFAULT, "GICv5 CPU_SYSREGS get with bad pointer");

	ret = __kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
				    attr, NULL);
	TEST_ASSERT(ret && errno == EFAULT, "GICv5 CPU_SYSREGS set with bad pointer");

	vm_gic_destroy(&v);
}

static void test_vgic_v5_sparse_vcpu_ids(void)
{
	struct kvm_vcpu *vcpus[VGIC_V5_SPARSE_NR_VCPUS];
	const u32 vcpu_ids[VGIC_V5_SPARSE_NR_VCPUS] = {
		VGIC_V5_SPARSE_VCPU0_ID,
		VGIC_V5_SPARSE_VCPU1_ID,
	};
	int ret, i;
	struct ucall uc;
	struct vm_gic v;
	u64 attr;

	v.gic_dev_type = KVM_DEV_TYPE_ARM_VGIC_V5;
	v.vm = __vm_create(VM_SHAPE_DEFAULT, VGIC_V5_SPARSE_NR_VCPUS, 0);
	v.gic_fd = kvm_create_device(v.vm, v.gic_dev_type);

	/* Create two vCPUs with legal, albeit sparse, IDs. */
	for (i = 0; i < ARRAY_SIZE(vcpus); i++) {
		vcpus[i] = vm_vcpu_add(v.vm, vcpu_ids[i], guest_iaffid_code);
		TEST_ASSERT(vcpus[i], "Failed to create vCPU %u", vcpu_ids[i]);
	}

	attr = GICV5_IRS_CONFIG_BASE_GPA;
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V5_ADDR_TYPE_IRS, &attr);
	vgic_v5_map_irs(v.vm);
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	/* Check both are runnable, and read back their correct IAFFIDs */
	for (i = 0; i < ARRAY_SIZE(vcpus); i++) {
		/* IAFFID is the VPE ID and must match the userspace vCPU ID. */
		ret = run_vcpu(vcpus[i]);
		TEST_ASSERT(!ret, "Failed to run GICv5 vCPU %u", vcpu_ids[i]);
		TEST_ASSERT(get_ucall(vcpus[i], &uc) == UCALL_SYNC &&
			    uc.args[1] == vcpu_ids[i],
			    "GICv5 vCPU %u IAFFID mismatch", vcpu_ids[i]);

		ret = run_vcpu(vcpus[i]);
		TEST_ASSERT(!ret, "Failed to complete GICv5 vCPU %u", vcpu_ids[i]);
		TEST_ASSERT(get_ucall(vcpus[i], NULL) == UCALL_DONE,
			    "GICv5 vCPU %u did not complete", vcpu_ids[i]);
	}

	vm_gic_destroy(&v);
}

static void test_vgic_v5_ppis(u32 gic_dev_type)
{
	struct kvm_vcpu *vcpus[NR_VCPUS];
	struct ucall uc;
	u64 user_ppis[2];
	struct vm_gic v;
	uint64_t attr;
	int ret, i;

	v.gic_dev_type = gic_dev_type;
	v.vm = __vm_create(VM_SHAPE_DEFAULT, NR_VCPUS, 0);

	v.gic_fd = kvm_create_device(v.vm, gic_dev_type);

	for (i = 0; i < NR_VCPUS; i++)
		vcpus[i] = vm_vcpu_add(v.vm, i, guest_code);

	vm_init_descriptor_tables(v.vm);
	vm_install_exception_handler(v.vm, VECTOR_IRQ_CURRENT,
				     guest_ppi_irq_handler);

	for (i = 0; i < NR_VCPUS; i++)
		vcpu_init_descriptor_tables(vcpus[i]);

	/* Set the address of the IRS before initialising the GIC */
	attr = GICV5_IRS_CONFIG_BASE_GPA;
	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			    KVM_VGIC_V5_ADDR_TYPE_IRS, &attr);

	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	/* Read out the PPIs that user space is allowed to drive. */
	kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_USERSPACE_PPIS, &user_ppis);

	/* We should always be able to drive the SW_PPI. */
	TEST_ASSERT(user_ppis[0] & BIT(GICV5_ARCH_PPI_SW_PPI),
		"SW_PPI is not drivable by userspace");

	/* PPIs not explicitly exposed to userspace must be rejected. */
	for (i = 0; i < VGIC_V5_NR_PRIVATE_IRQS; i++) {
		if (user_ppis[i / 64] & BIT_ULL(i % 64))
			continue;

		ret = __vgic_v5_irq_line(v.vm, KVM_ARM_IRQ_TYPE_PPI, i, 1);
		TEST_ASSERT(ret && errno == EINVAL,
			    "GICv5 accepted non-userspace PPI %d", i);
	}

	ret = __vgic_v5_irq_line(v.vm, KVM_ARM_IRQ_TYPE_PPI,
				 VGIC_V5_NR_PRIVATE_IRQS, 1);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 accepted out-of-range PPI");

	while (1) {
		ret = run_vcpu(vcpus[0]);
		if (ret)
			break;

		switch (get_ucall(vcpus[0], &uc)) {
		case UCALL_SYNC:
			/*
			 * The guest is ready for the next level change. Set
			 * high if ready, and lower if it has been consumed.
			 */
			if (uc.args[1] == GUEST_CMD_IS_READY ||
			    uc.args[1] == GUEST_CMD_IRQ_DIEOI) {
				u64 irq;
				bool level = uc.args[1] == GUEST_CMD_IRQ_DIEOI ? 0 : 1;

				irq = FIELD_PREP(KVM_ARM_IRQ_NUM_MASK, 3);
				irq |= KVM_ARM_IRQ_TYPE_PPI << KVM_ARM_IRQ_TYPE_SHIFT;

				kvm_irq_line(v.vm, irq, level);
			} else if (uc.args[1] == GUEST_CMD_IS_AWAKE) {
				pr_debug("Guest skipping WFI due to pending IRQ\n");
			} else if (uc.args[1] == GUEST_CMD_IRQ_CDIA) {
				pr_debug("Guest acknowledged IRQ\n");
			}

			continue;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}

done:
	TEST_ASSERT(ret == 0, "Failed to test GICv5 PPIs");

	vm_gic_destroy(&v);
}

static void test_vgic_v5_spis(void)
{
	struct kvm_vcpu *vcpu;
	struct vm_gic v;
	int ret;

	/* Default NR_IRQS exposes 32 SPIs, numbered 0..31 in KVM_IRQ_LINE. */
	vgic_v5_spi_line_vm_create(&v, &vcpu, VGIC_V5_DEFAULT_NR_SPIS);
	ret = __vgic_v5_irq_line(v.vm, KVM_ARM_IRQ_TYPE_SPI,
				 VGIC_V5_DEFAULT_NR_SPIS, 1);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 accepted first invalid SPI");

	vm_gic_destroy(&v);

	/* Basic SPI injection through KVM_IRQ_LINE. */
	vgic_v5_run_spi_line_test(VGIC_V5_DEFAULT_NR_SPIS, 0, false, false);

	/* The last valid SPI in the maximum configured range is injectable. */
	vgic_v5_run_spi_line_test(VGIC_V5_MAX_NR_SPIS,
				  VGIC_V5_MAX_NR_SPIS - 1, false, false);

	vgic_v5_spi_line_vm_create(&v, &vcpu, VGIC_V5_MAX_NR_SPIS);
	ret = __vgic_v5_irq_line(v.vm, KVM_ARM_IRQ_TYPE_SPI,
				 VGIC_V5_MAX_NR_SPIS, 1);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 accepted configured invalid SPI");
	vm_gic_destroy(&v);

	/*
	 * Edge SPIs remain pending after the line is lowered.  The low
	 * transition is injected before the guest runs and must be ignored:
	 * once an edge interrupt is pending, lowering the line cannot recall it.
	 */
	vgic_v5_run_spi_line_test(VGIC_V5_DEFAULT_NR_SPIS, 1, false, true);

	/* Level SPIs can be raised and delivered through KVM_IRQ_LINE. */
	vgic_v5_run_spi_line_test(VGIC_V5_DEFAULT_NR_SPIS, 2, true, false);
}

static void test_vgic_v5_lpis(void)
{
	struct kvm_vcpu *vcpus[VGIC_V5_LPI_NR_VCPUS];
	struct ucall uc;
	struct vm_gic v;
	int ret;

	lpi_ist_ready = false;
	vgic_v5_lpi_vm_create(&v, vcpus);
	sync_global_to_guest(v.vm, lpi_ist_ready);

	/* VPE0 programs a linear LPI IST from the virtual IRS ID registers. */
	ret = run_vcpu(vcpus[0]);
	TEST_ASSERT(!ret, "Failed to run GICv5 LPI vCPU0");
	TEST_ASSERT(get_ucall(vcpus[0], &uc) == UCALL_SYNC &&
		    uc.args[1] == GUEST_CMD_IS_READY,
		    "GICv5 LPI vCPU0 did not become ready");

	/* VPE1 waits for the shared LPI setup and enables its CPU interface. */
	ret = run_vcpu(vcpus[1]);
	TEST_ASSERT(!ret, "Failed to run GICv5 LPI vCPU1");
	TEST_ASSERT(get_ucall(vcpus[1], &uc) == UCALL_SYNC &&
		    uc.args[1] == GUEST_CMD_IS_READY,
		    "GICv5 LPI vCPU1 did not become ready");

	/* VPE0 sends an LPI to VPE1. */
	ret = run_vcpu(vcpus[0]);
	TEST_ASSERT(!ret, "Failed to run GICv5 LPI vCPU0 sender");
	TEST_ASSERT(get_ucall(vcpus[0], &uc) == UCALL_SYNC &&
		    uc.args[1] == GUEST_CMD_LPI_SENT,
		    "GICv5 LPI vCPU0 did not send LPI");

	/* VPE1 consumes the LPI and replies with another LPI. */
	ret = run_vcpu(vcpus[1]);
	TEST_ASSERT(!ret, "Failed to run GICv5 LPI vCPU1 receiver");
	TEST_ASSERT(get_ucall(vcpus[1], &uc) == UCALL_SYNC &&
		    uc.args[1] == GUEST_CMD_LPI_REPLIED,
		    "GICv5 LPI vCPU1 did not reply");

	/* VPE0 consumes the reply LPI and ends the test. */
	ret = run_vcpu(vcpus[0]);
	TEST_ASSERT(!ret, "Failed to run GICv5 LPI vCPU0 receiver");
	TEST_ASSERT(get_ucall(vcpus[0], &uc) == UCALL_DONE,
		    "GICv5 LPI vCPU0 did not complete");

	vm_gic_destroy(&v);
}

static void test_vgic_v5_ist_save_restore(void)
{
	static u32 lpi_ist[VGIC_V5_LPI_IST_SIZE / sizeof(u32)];
	static u32 lpi_ist_resave[VGIC_V5_LPI_IST_SIZE / sizeof(u32)];
	static u32 spi_ist[VGIC_V5_DEFAULT_NR_SPIS];
	static u32 spi_ist_resave[VGIC_V5_DEFAULT_NR_SPIS];
	static u64 irs_regs[ARRAY_SIZE(gic_v5_irs_regs)];
	struct kvm_vcpu *vcpus[VGIC_V5_LPI_NR_VCPUS];
	struct kvm_vgic_v5_ist ist_attr;
	struct vm_gic src, dst;
	u64 ist_baser, bad_ist_baser;
	u64 ist_cfgr, bad_ist_cfgr;
	size_t lpi_ist_size;
	int ret;

	memset(ist_restore_irq_count, 0, sizeof(ist_restore_irq_count));

	vgic_v5_ist_restore_vm_create(&src, vcpus, guest_ist_source_code,
				      guest_unexpected_irq_handler);

	ret = run_vcpu(vcpus[0]);
	TEST_ASSERT(!ret, "Failed to run GICv5 IST save source VM");
	vgic_v5_expect_sync(vcpus[0], GUEST_CMD_IST_SOURCE_READY,
			    "GICv5 IST source guest did not become ready");

	/*
	 * The guest configured enable/priority/affinity state. Make selected
	 * SPIs pending from userspace so the SPI IST save path sees them.
	 */
	vgic_v5_spi_line(src.vm, RESTORE_TEST_SPI_PRIO_HIGH, 1);
	vgic_v5_spi_line(src.vm, RESTORE_TEST_SPI_PRIO_LOW, 1);
	vgic_v5_spi_line(src.vm, RESTORE_TEST_SPI_PENDING_DISABLED, 1);

	vgic_v5_save_irs_regs(&src, irs_regs);
	lpi_ist_size = vgic_v5_lpi_ist_state_size(irs_regs);

	/*
	 * VCPU0 has configured the initial state. KVM must reject missing or
	 * incorrectly sized LPI storage before accepting the complete IST image.
	 */
	ist_attr = vgic_v5_ist_attr(spi_ist, sizeof(spi_ist), NULL, 0);
	ret = __kvm_device_attr_get(src.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IST save accepted missing LPI buffer");

	ist_attr = vgic_v5_ist_attr(spi_ist, sizeof(spi_ist), lpi_ist,
					 lpi_ist_size - sizeof(__u32));
	ret = __kvm_device_attr_get(src.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IST save accepted bad LPI size");

	ist_attr = vgic_v5_ist_attr(spi_ist, sizeof(spi_ist), lpi_ist,
					 lpi_ist_size);
	ret = __kvm_device_attr_get(src.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(!ret, "GICv5 IST save failed");

	vm_gic_destroy(&src);

	/* Create the guest that we are restoring state into */
	memset(ist_restore_irq_count, 0, sizeof(ist_restore_irq_count));
	vgic_v5_ist_restore_vm_create(&dst, vcpus, guest_ist_restore_code,
				      guest_ist_restore_irq_handler);
	sync_global_to_guest(dst.vm, ist_restore_irq_count);

	ist_attr = vgic_v5_ist_attr(spi_ist, sizeof(spi_ist), lpi_ist,
					 lpi_ist_size);
	ret = __kvm_device_attr_set(dst.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IST restore before IRS state");

	vgic_v5_restore_irs_regs(&dst, irs_regs);

	/* BASER remains mutable until the host LPI IST has been allocated. */
	ist_baser = vgic_v5_saved_irs_reg(irs_regs, GICV5_IRS_IST_BASER);
	bad_ist_baser = ist_baser ^
			 FIELD_PREP(GICV5_IRS_IST_BASER_ADDR_MASK, 1);
	ret = __kvm_device_attr_set(dst.gic_fd,
				    KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IST_BASER, &bad_ist_baser);
	TEST_ASSERT(!ret, "GICv5 rejected changed unallocated IST_BASER");

	bad_ist_baser = ist_baser & ~GICV5_IRS_IST_BASER_VALID;
	ret = __kvm_device_attr_set(dst.gic_fd,
				    KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IST_BASER, &bad_ist_baser);
	TEST_ASSERT(!ret, "GICv5 rejected invalidated unallocated IST_BASER");

	ret = __kvm_device_attr_set(dst.gic_fd,
				    KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IST_BASER, &ist_baser);
	TEST_ASSERT(!ret, "GICv5 failed to restore IST_BASER");

	ist_attr = vgic_v5_ist_attr(spi_ist, sizeof(spi_ist), NULL, 0);
	ret = __kvm_device_attr_set(dst.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IST restore accepted missing LPI buffer");

	ist_attr = vgic_v5_ist_attr(spi_ist, sizeof(spi_ist), lpi_ist,
					 lpi_ist_size - sizeof(__u32));
	ret = __kvm_device_attr_set(dst.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(ret && errno == EINVAL, "GICv5 IST restore accepted bad LPI size");

	/* Restore the VMM-owned SPI and LPI IST images. */
	ist_attr = vgic_v5_ist_attr(spi_ist, sizeof(spi_ist), lpi_ist,
					 lpi_ist_size);
	ret = __kvm_device_attr_set(dst.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(!ret, "GICv5 IST restore failed");

	/* A live IST makes BASER and the guest-visible configuration immutable. */
	ret = __kvm_device_attr_set(dst.gic_fd,
				    KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IST_BASER, &ist_baser);
	TEST_ASSERT(!ret, "GICv5 rejected unchanged live IST_BASER");

	bad_ist_baser = ist_baser ^
			 FIELD_PREP(GICV5_IRS_IST_BASER_ADDR_MASK, 1);
	ret = __kvm_device_attr_set(dst.gic_fd,
				    KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IST_BASER, &bad_ist_baser);
	TEST_ASSERT(ret && errno == EINVAL,
		    "GICv5 accepted changed live IST_BASER");

	bad_ist_baser = ist_baser & ~GICV5_IRS_IST_BASER_VALID;
	ret = __kvm_device_attr_set(dst.gic_fd,
				    KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IST_BASER, &bad_ist_baser);
	TEST_ASSERT(ret && errno == EINVAL,
		    "GICv5 accepted invalidated live IST_BASER");

	ist_cfgr = vgic_v5_saved_irs_reg(irs_regs, GICV5_IRS_IST_CFGR);
	ret = __kvm_device_attr_set(dst.gic_fd,
				    KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IST_CFGR, &ist_cfgr);
	TEST_ASSERT(!ret, "GICv5 rejected unchanged live IST_CFGR");

	bad_ist_cfgr = ist_cfgr ^
			FIELD_PREP(GICV5_IRS_IST_CFGR_LPI_ID_BITS, 1);
	ret = __kvm_device_attr_set(dst.gic_fd,
				    KVM_DEV_ARM_VGIC_GRP_IRS_REGS,
				    GICV5_IRS_IST_CFGR, &bad_ist_cfgr);
	TEST_ASSERT(ret && errno == EINVAL,
		    "GICv5 accepted changed live IST_CFGR");

	/*
	 * Re-save the restored IST image before the destination VM runs, so a
	 * dropped serialized field fails independently of later interrupt
	 * delivery.
	 */
	ist_attr = vgic_v5_ist_attr(spi_ist_resave, sizeof(spi_ist_resave),
				     lpi_ist_resave, lpi_ist_size);
	ret = __kvm_device_attr_get(dst.gic_fd, KVM_DEV_ARM_VGIC_GRP_IST,
				    0, &ist_attr);
	TEST_ASSERT(!ret, "GICv5 IST re-save failed");
	TEST_ASSERT(!memcmp(spi_ist, spi_ist_resave, sizeof(spi_ist)),
		    "GICv5 SPI IST state changed across restore");
	TEST_ASSERT(!memcmp(lpi_ist, lpi_ist_resave, lpi_ist_size),
		    "GICv5 LPI IST state changed across restore");

	/*
	 * At this stage, the guest's IST state should match what was saved. Run
	 * each vcpu once to let it enable interrupts, etc.
	 */
	ret = run_vcpu(vcpus[0]);
	TEST_ASSERT(!ret, "Failed to run GICv5 IST restore vCPU0");
	vgic_v5_expect_sync(vcpus[0], GUEST_CMD_IS_READY,
			    "GICv5 IST restore vCPU0 did not become ready");

	ret = run_vcpu(vcpus[1]);
	TEST_ASSERT(!ret, "Failed to run GICv5 IST restore vCPU1");
	vgic_v5_expect_sync(vcpus[1], GUEST_CMD_IS_READY,
			    "GICv5 IST restore vCPU1 did not become ready");

	/*
	 * VPE0 consumes two pending SPIs in restored priority order, then
	 * enables a restored pending-but-disabled SPI.
	 */
	ret = run_vcpu(vcpus[0]);
	TEST_ASSERT(!ret, "Failed to consume restored GICv5 SPIs");
	vgic_v5_expect_sync(vcpus[0], GUEST_CMD_IST_SPI_INJECT_READY,
			    "GICv5 IST restore vCPU0 did not consume restored SPIs");

	/*
	 * This SPI was enabled before save, but not pending.  Inject it after
	 * restore to prove enable, priority and affinity were restored.
	 */
	vgic_v5_spi_line(dst.vm, RESTORE_TEST_SPI_ENABLED, 1);

	ret = run_vcpu(vcpus[0]);
	TEST_ASSERT(!ret, "Failed to consume post-restore GICv5 SPI");
	TEST_ASSERT(get_ucall(vcpus[0], NULL) == UCALL_DONE,
		    "GICv5 IST restore vCPU0 did not complete");

	/*
	 * VPE1 consumes a restored pending LPI, then pends another LPI that
	 * was enabled before save but not pending.
	 */
	ret = run_vcpu(vcpus[1]);
	TEST_ASSERT(!ret, "Failed to consume restored GICv5 LPI");
	vgic_v5_expect_sync(vcpus[1], GUEST_CMD_IST_LPI_INJECT_READY,
			    "GICv5 IST restore vCPU1 did not consume restored LPI");

	ret = run_vcpu(vcpus[1]);
	TEST_ASSERT(!ret, "Failed to consume post-restore GICv5 LPI");
	TEST_ASSERT(get_ucall(vcpus[1], NULL) == UCALL_DONE,
		    "GICv5 IST restore vCPU1 did not complete");

	vm_gic_destroy(&dst);
}

/*
 * Returns 0 if it's possible to create GIC device of a given type (V5).
 */
int test_kvm_device(u32 gic_dev_type)
{
	struct kvm_vcpu *vcpus[NR_VCPUS];
	struct vm_gic v;
	int ret;

	v.vm = vm_create_with_vcpus(NR_VCPUS, guest_code, vcpus);

	/* try to create a non existing KVM device */
	ret = __kvm_test_create_device(v.vm, 0);
	TEST_ASSERT(ret && errno == ENODEV, "unsupported device");

	/* trial mode */
	ret = __kvm_test_create_device(v.vm, gic_dev_type);
	if (ret)
		return ret;
	v.gic_fd = kvm_create_device(v.vm, gic_dev_type);

	ret = __kvm_create_device(v.vm, gic_dev_type);
	TEST_ASSERT(ret < 0 && errno == EEXIST, "create GIC device twice");

	vm_gic_destroy(&v);

	return 0;
}

void run_tests(u32 gic_dev_type)
{
	pr_info("Test VGICv5 address attrs\n");
	test_vgic_v5_addr_attrs();

	pr_info("Test VGICv5 NR_IRQS attrs\n");
	test_vgic_v5_nr_irqs_attrs();

	pr_info("Test VGICv5 IRS_REGS attrs\n");
	test_vgic_v5_irs_regs_attrs();

	pr_info("Test VGICv5 IST attrs\n");
	test_vgic_v5_ist_attrs();

	pr_info("Test VGICv5 userspace PPI attrs\n");
	test_vgic_v5_userspace_ppis_attrs();

	pr_info("Test VGICv5 CPU sysreg attrs\n");
	test_vgic_v5_cpu_sysreg_attrs();

	pr_info("Test VGICv5 sparse vCPU IDs\n");
	test_vgic_v5_sparse_vcpu_ids();

	pr_info("Test VGICv5 PPIs\n");
	test_vgic_v5_ppis(gic_dev_type);

	pr_info("Test VGICv5 SPIs\n");
	test_vgic_v5_spis();

	pr_info("Test VGICv5 LPIs\n");
	test_vgic_v5_lpis();

	pr_info("Test VGICv5 IST save/restore\n");
	test_vgic_v5_ist_save_restore();
}

int main(int ac, char **av)
{
	int pa_bits, ret;

	pa_bits = vm_guest_mode_params[VM_MODE_DEFAULT].pa_bits;
	max_phys_size = 1ULL << pa_bits;

	test_disable_default_vgic();

	ret = test_kvm_device(KVM_DEV_TYPE_ARM_VGIC_V5);
	if (ret) {
		pr_info("No GICv5 support; Not running GIC_v5 tests.\n");
		exit(KSFT_SKIP);
	}

	pr_info("Running VGIC_V5 tests.\n");
	run_tests(KVM_DEV_TYPE_ARM_VGIC_V5);

	return 0;
}
