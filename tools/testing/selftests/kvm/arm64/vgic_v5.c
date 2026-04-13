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

struct vm_gic {
	struct kvm_vm *vm;
	int gic_fd;
	uint32_t gic_dev_type;
};

static uint64_t max_phys_size;

#define GUEST_CMD_IRQ_CDIA	10
#define GUEST_CMD_IRQ_DIEOI	11
#define GUEST_CMD_IS_AWAKE	12
#define GUEST_CMD_IS_READY	13

static void guest_irq_handler(struct ex_regs *regs)
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
	while (1);
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

static void test_vgic_v5_ppis(uint32_t gic_dev_type)
{
	struct kvm_vcpu *vcpus[NR_VCPUS];
	struct ucall uc;
	u64 user_ppis[2];
	struct vm_gic v;
	int ret, i;

	v.gic_dev_type = gic_dev_type;
	v.vm = __vm_create(VM_SHAPE_DEFAULT, NR_VCPUS, 0);

	v.gic_fd = kvm_create_device(v.vm, gic_dev_type);

	for (i = 0; i < NR_VCPUS; i++)
		vcpus[i] = vm_vcpu_add(v.vm, i, guest_code);

	vm_init_descriptor_tables(v.vm);
	vm_install_exception_handler(v.vm, VECTOR_IRQ_CURRENT, guest_irq_handler);

	for (i = 0; i < NR_VCPUS; i++)
		vcpu_init_descriptor_tables(vcpus[i]);

	kvm_device_attr_set(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_CTRL_INIT, NULL);

	/* Read out the PPIs that user space is allowed to drive. */
	kvm_device_attr_get(v.gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			    KVM_DEV_ARM_VGIC_USERSPACE_PPIS, &user_ppis);

	/* We should always be able to drive the SW_PPI. */
	TEST_ASSERT(user_ppis[0] & BIT(GICV5_ARCH_PPI_SW_PPI),
		"SW_PPI is not drivable by userspace");

	while (1) {
		ret = run_vcpu(vcpus[0]);

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

				_kvm_irq_line(v.vm, irq, level);
			} else if (uc.args[1] == GUEST_CMD_IS_AWAKE) {
				pr_info("Guest skipping WFI due to pending IRQ\n");
			} else if (uc.args[1] == GUEST_CMD_IRQ_CDIA) {
				pr_info("Guest acknowledged IRQ\n");
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

/*
 * Returns 0 if it's possible to create GIC device of a given type (V5).
 */
int test_kvm_device(uint32_t gic_dev_type)
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

void run_tests(uint32_t gic_dev_type)
{
	pr_info("Test VGICv5 PPIs\n");
	test_vgic_v5_ppis(gic_dev_type);
}

int main(int ac, char **av)
{
	int ret;
	int pa_bits;

	test_disable_default_vgic();

	pa_bits = vm_guest_mode_params[VM_MODE_DEFAULT].pa_bits;
	max_phys_size = 1ULL << pa_bits;

	ret = test_kvm_device(KVM_DEV_TYPE_ARM_VGIC_V5);
	if (ret) {
		pr_info("No GICv5 support; Not running GIC_v5 tests.\n");
		exit(KSFT_SKIP);
	}

	pr_info("Running VGIC_V5 tests.\n");
	run_tests(KVM_DEV_TYPE_ARM_VGIC_V5);

	return 0;
}
