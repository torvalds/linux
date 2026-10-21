// SPDX-License-Identifier: GPL-2.0
#include "kvm_util.h"
#include "test_util.h"
#include "apic.h"
#include "processor.h"
#include "proc_util.h"

#include <libvfio.h>
#include <linux/sizes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/sysinfo.h>

static u64 timeout_ns = 2ULL * 1000 * 1000 * 1000;
static bool guest_ready_for_irqs[KVM_MAX_VCPUS];
static bool guest_received_irq[KVM_MAX_VCPUS];
static bool guest_received_nmi[KVM_MAX_VCPUS];
static bool x2apic = true;
static bool irq_affinity;
static bool done;

#define GUEST_RECEIVED_IRQ(__vcpu)	\
	SYNC_FROM_GUEST_AND_READ((__vcpu)->vm, guest_received_irq[(__vcpu)->id])
#define GUEST_RECEIVED_NMI(__vcpu)	\
	SYNC_FROM_GUEST_AND_READ((__vcpu)->vm, guest_received_nmi[(__vcpu)->id])

#define GUEST_RECEIVED_INTERRUPT(__vcpu, __nmi)	\
	((__nmi) ? GUEST_RECEIVED_NMI(__vcpu) : GUEST_RECEIVED_IRQ(__vcpu))

static u32 guest_get_vcpu_id(void)
{
	if (x2apic)
		return x2apic_read_reg(APIC_ID);
	else
		return xapic_read_reg(APIC_ID) >> 24;
}

static void guest_irq_handler(struct ex_regs *regs)
{
	WRITE_ONCE(guest_received_irq[guest_get_vcpu_id()], true);

	if (x2apic)
		x2apic_write_reg(APIC_EOI, 0);
	else
		xapic_write_reg(APIC_EOI, 0);
}

static void guest_nmi_handler(struct ex_regs *regs)
{
	WRITE_ONCE(guest_received_nmi[guest_get_vcpu_id()], true);
}

static void guest_code(void)
{
	if (x2apic)
		x2apic_enable();
	else
		xapic_enable();

	sti_nop();

	WRITE_ONCE(guest_ready_for_irqs[guest_get_vcpu_id()], true);

	while (!READ_ONCE(done))
		cpu_relax();

	GUEST_DONE();
}

static void *vcpu_thread_main(void *arg)
{
	struct kvm_vcpu *vcpu = arg;
	struct ucall uc;

	vcpu_run(vcpu);
	TEST_ASSERT_EQ(UCALL_DONE, get_ucall(vcpu, &uc));

	return NULL;
}

static int vfio_setup_msi(struct vfio_pci_device *device)
{
	const int flags = MAP_SHARED | MAP_ANONYMOUS;
	const int prot = PROT_READ | PROT_WRITE;
	struct iova_allocator *allocator;
	struct dma_region *region;

	/* Sanity check that the device+driver can actually send MSIs. */
	TEST_REQUIRE(device->driver.ops);
	TEST_REQUIRE(device->driver.ops->send_msi);

	/*
	 * Set up a DMA-able region for the driver to use.   Very few devices
	 * provide a way to arbitrarily send interrupts (MSIs), e.g. by writing
	 * an MMIO register.  Instead, most devices send MSIs when an action is
	 * completed, and practically all actions involve DMA of some form.
	 */
	allocator = iova_allocator_init(device->iommu);

	region = &device->driver.region;
	region->size = SZ_2M;
	region->iova = iova_allocator_alloc(allocator, region->size);
	region->vaddr = kvm_mmap(region->size, prot, flags, -1);
	TEST_ASSERT(region->vaddr != MAP_FAILED, "mmap() failed\n");
	iommu_map(device->iommu, region);

	iova_allocator_cleanup(allocator);

	vfio_pci_driver_init(device);

	return device->driver.msi;
}

static void trigger_interrupt(struct vfio_pci_device *device, int eventfd)
{
	if (device)
		vfio_pci_driver_send_msi(device);
	else
		eventfd_write(eventfd, 1);
}


static void kvm_route_msi(struct kvm_vm *vm, u32 gsi, struct kvm_vcpu *vcpu,
			  u8 vector, bool use_nmi)
{
	struct {
		struct kvm_irq_routing header;
		struct kvm_irq_routing_entry entry;
	} routing = {
		.header.nr = 1,
		.entry = {
			.gsi = gsi,
			.type = KVM_IRQ_ROUTING_MSI,
			.u.msi.address_lo = 0xFEE00000 | (vcpu->id & GENMASK(7, 0)) << 12,
			.u.msi.address_hi = vcpu->id & GENMASK(31, 8),
			.u.msi.data = use_nmi ? NMI_VECTOR | (4 << 8) : vector,
		},
	};

	vm_ioctl(vm, KVM_SET_GSI_ROUTING, &routing.header);
}

static void kvm_set_empty_gsi_routing(struct kvm_vm *vm)
{
	struct kvm_irq_routing routing = {};

	vm_ioctl(vm, KVM_SET_GSI_ROUTING, &routing);
}

static const char *probe_iommu_type(void)
{
	int io_fd;

	io_fd = open("/dev/iommu", O_RDONLY);
	if (io_fd >= 0) {
		close(io_fd);
		return MODE_IOMMUFD;
	}

	io_fd = __open_path_or_exit("/dev/vfio/vfio", O_RDONLY,
				    "Is VFIO (or IOMMUFD) loaded and enabled?");
	close(io_fd);
	return MODE_VFIO_TYPE1_IOMMU;
}

static void help(const char *name)
{
	printf("Usage: %s [-a] [-d <segment:bus:device.function>] [-e] [-h] [-i nr_irqs] [-m] [-n] [-t iommu_type] [-v nr_vcpus] [-x]\n", name);
	printf("\n");
	printf("Tests KVM interrupt routing and delivery via irqfd.\n");
	printf("-a	Affine the device's host IRQ to a random physical CPU\n");
	printf("-d	Use a VFIO device to send MSI-X interrupts instead of manually signaling the eventfd\n");
	printf("-e	Set empty GSI routing in-between some interrupts\n");
	printf("-i	The number of IRQs to generate during the test\n");
	printf("-m	Pin target vCPU to random physical CPU before triggering interrupt\n");
	printf("-n	Deliver 50 percent of IRQs as non-maskable interrupts\n");
	printf("-t	Override the IOMMU type to use (vfio_type1_iommu or iommufd)\n");
	printf("-v	Number of vCPUS to run\n");
	printf("-x	Use xAPIC mode instead of x2APIC mode in the guest\n");
	printf("\n");
	exit(KSFT_FAIL);
}

int main(int argc, char **argv)
{
	/*
	 * Pick a random vector and a random GSI to use for device IRQ.
	 *
	 * Pick an IRQ vector in range [32, UINT8_MAX]. Min value is 32 because
	 * Linux/x86 reserves vectors 0-31 for exceptions and architecture
	 * defined NMIs and interrupts.
	 *
	 * Pick a GSI in range [24, KVM_MAX_IRQ_ROUTES - 1]. The min value is 24
	 * because KVM reserves GSIs 0-15 for legacy ISA IRQs and 16-23 only go
	 * to the IOAPIC. The max is KVM_MAX_IRQ_ROUTES - 1, because
	 * KVM_MAX_IRQ_ROUTES is exclusive.
	 */
	u32 gsi = kvm_random_u64_in_range(&kvm_rng, 24, KVM_MAX_IRQ_ROUTES - 1);
	u8 vector = kvm_random_u64_in_range(&kvm_rng, 32, UINT8_MAX);

	pthread_t vcpu_threads[KVM_MAX_VCPUS];
	struct kvm_vcpu *vcpus[KVM_MAX_VCPUS];
	struct vfio_pci_device *device = NULL;
	int nr_irqs = 1000, nr_vcpus = 1;
	bool set_empty_routing = false;
	const char *device_bdf = NULL;
	const char *iommu_type = NULL;
	int i, j, c, msix, eventfd;
	bool migrate_vcpus = false;
	cpu_set_t available_cpus;
	bool use_nmi = false;
	struct iommu *iommu;
	struct kvm_vm *vm;
	int irq, irq_cpu;

	while ((c = getopt(argc, argv, "ad:ehi:mnt:v:x")) != -1) {
		switch (c) {
		case 'a':
			irq_affinity = true;
			break;
		case 'd':
			device_bdf = optarg;
			break;
		case 'e':
			set_empty_routing = true;
			break;
		case 'i':
			nr_irqs = atoi_positive("Number of IRQs", optarg);
			break;
		case 'm':
			migrate_vcpus = true;
			break;
		case 'n':
			use_nmi = true;
			break;
		case 't':
			iommu_type = optarg;
			break;
		case 'v':
			nr_vcpus = atoi_positive("Number of vCPUS", optarg);
			TEST_ASSERT(nr_vcpus <= KVM_MAX_VCPUS,
				    "KVM selftests support at most %u vCPUs", KVM_MAX_VCPUS);
			break;
		case 'x':
			x2apic = false;
			break;
		case 'h':
		default:
			help(argv[0]);
		}
	}

	TEST_REQUIRE(kvm_arch_has_default_irqchip());

	vm = vm_create_with_vcpus(nr_vcpus, guest_code, vcpus);
	vm_enable_cap(vm, KVM_CAP_X2APIC_API, KVM_X2APIC_API_USE_32BIT_IDS |
					      KVM_X2APIC_API_DISABLE_BROADCAST_QUIRK);

	vm_install_exception_handler(vm, vector, guest_irq_handler);
	vm_install_exception_handler(vm, NMI_VECTOR, guest_nmi_handler);

	if (!x2apic) {
		TEST_ASSERT(nr_vcpus < 256, "xAPIC can only target IDs [0-254] (255 vCPUs)");
		virt_pg_map(vm, APIC_DEFAULT_GPA, APIC_DEFAULT_GPA);
	}

	if (device_bdf) {
		if (!iommu_type)
			iommu_type = probe_iommu_type();
		iommu = iommu_init(iommu_type);
		device = vfio_pci_device_init(device_bdf, iommu);
		msix = vfio_setup_msi(device);
		irq = vfio_msix_to_host_irq(device_bdf, msix);
		eventfd = device->msi_eventfds[msix];
		printf("Using device %s MSI-X[%d] (IRQ-%u)\n", device_bdf, msix,
		       irq);
	} else {
		TEST_ASSERT(!irq_affinity,
			    "Setting IRQ affinity (-a) requires a backing device (-d)");

		eventfd = kvm_new_eventfd();
		irq = -1;
	}

	pr_info("Injecting interrupts for GSI %d (guest vector 0x%x) %d times\n",
		gsi, vector, nr_irqs);

	kvm_assign_irqfd(vm, gsi, eventfd);

	sync_global_to_guest(vm, x2apic);

	if (migrate_vcpus)
		kvm_sched_getaffinity(0, sizeof(available_cpus), &available_cpus);

	for (i = 0; i < nr_vcpus; i++)
		kvm_pthread_create(&vcpu_threads[i], NULL, vcpu_thread_main, vcpus[i]);

	for (i = 0; i < nr_vcpus; i++) {
		struct kvm_vcpu *vcpu = vcpus[i];

		while (!SYNC_FROM_GUEST_AND_READ(vm, guest_ready_for_irqs[vcpu->id]))
			continue;
	}

	irq_cpu = -1;

	for (i = 0; i < nr_irqs; i++) {
		const bool do_set_empty_routing = set_empty_routing && (i & BIT(3));
		const bool do_use_nmi = use_nmi && (i & BIT(2));
		struct kvm_vcpu *vcpu = vcpus[i % nr_vcpus];
		struct timespec start;

		if (do_set_empty_routing)
			kvm_set_empty_gsi_routing(vm);

		kvm_route_msi(vm, gsi, vcpu, vector, do_use_nmi);

		if (irq_affinity) {
			irq_cpu = kvm_random_u64(&kvm_rng) % get_nprocs();
			proc_irq_set_smp_affinity(irq, irq_cpu);
		}

		if (migrate_vcpus)
			pin_task_to_random_cpu(vcpu_threads[i % nr_vcpus], &available_cpus);

		for (j = 0; j < nr_vcpus; j++) {
			TEST_ASSERT(!GUEST_RECEIVED_IRQ(vcpus[j]),
				    "IRQ flag for vCPU %d not clear prior to test",
				    vcpus[j]->id);
			TEST_ASSERT(!GUEST_RECEIVED_NMI(vcpus[j]),
				    "NMI flag for vCPU %d not clear prior to test",
				    vcpus[j]->id);
		}

		trigger_interrupt(device, eventfd);

		clock_gettime(CLOCK_MONOTONIC, &start);
		while (!GUEST_RECEIVED_INTERRUPT(vcpu, do_use_nmi) &&
		       timespec_to_ns(timespec_elapsed(start)) <= timeout_ns)
			cpu_relax();

		TEST_ASSERT(GUEST_RECEIVED_INTERRUPT(vcpu, do_use_nmi),
			    "vCPU %d timed out waiting for %s (vector 0x%x) from GSI %d (via CPU %d)\n",
			    vcpu->id, do_use_nmi ? "NMI" : "IRQ",
			    do_use_nmi ? NMI_VECTOR : vector, gsi, irq_cpu);

		if (do_use_nmi)
			WRITE_AND_SYNC_TO_GUEST(vm, guest_received_nmi[vcpu->id], false);
		else
			WRITE_AND_SYNC_TO_GUEST(vm, guest_received_irq[vcpu->id], false);
	}

	WRITE_AND_SYNC_TO_GUEST(vm, done, true);

	for (i = 0; i < nr_vcpus; i++)
		kvm_pthread_join(vcpu_threads[i], NULL);

	return 0;
}
