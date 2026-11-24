// SPDX-License-Identifier: GPL-2.0
/*
 * vgic_lpi_stress - Tests for KVM's ITS emulation
 *
 * Copyright (c) 2024 Google LLC
 */

#include <linux/sizes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/sysinfo.h>

#include "kvm_util.h"
#include "gic.h"
#include "gic_v3.h"
#include "gic_v3_its.h"
#include "processor.h"
#include "ucall.h"
#include "vgic.h"

#define TEST_MEMSLOT_INDEX	1

#define GIC_LPI_OFFSET	8192

static size_t nr_iterations = 1000;

static struct kvm_vm *vm;
static struct kvm_vcpu **vcpus;
static int its_fd;

enum move_cmd {
	MOVE_MOVI,
	MOVE_MAPC_MOVALL,
};

struct test_data {
	bool		request_vcpus_stop;
	u32		nr_cpus;
	u32		nr_devices;
	u32		nr_event_ids;
	enum move_cmd	move_cmd;

	gpa_t		device_table;
	gpa_t		collection_table;
	gpa_t		cmdq_base;
	void		*cmdq_base_va;
	gpa_t		itt_tables;

	gpa_t		lpi_prop_table;
	gpa_t		lpi_pend_tables;
};

static struct test_data test_data;

static struct test_data stress_args = {
	.nr_cpus	= 1,
	.nr_devices	= 1,
	.nr_event_ids	= 16,
};

static void guest_irq_handler(struct ex_regs *regs)
{
	u32 intid = gic_get_and_ack_irq();

	if (intid == IAR_SPURIOUS)
		return;

	GUEST_ASSERT(intid >= GIC_LPI_OFFSET);
	gic_set_eoi(intid);
}

static void guest_setup_its_mappings(void)
{
	u32 coll_id, device_id, event_id, intid = GIC_LPI_OFFSET;
	u32 nr_events = test_data.nr_event_ids;
	u32 nr_devices = test_data.nr_devices;
	u32 nr_cpus = test_data.nr_cpus;

	for (coll_id = 0; coll_id < nr_cpus; coll_id++)
		its_send_mapc_cmd(test_data.cmdq_base_va, coll_id, coll_id, true);

	/* Round-robin the LPIs to all of the vCPUs in the VM */
	coll_id = 0;
	for (device_id = 0; device_id < nr_devices; device_id++) {
		gpa_t itt_base = test_data.itt_tables + (device_id * SZ_64K);

		its_send_mapd_cmd(test_data.cmdq_base_va, device_id,
				  itt_base, SZ_64K, true);

		for (event_id = 0; event_id < nr_events; event_id++) {
			its_send_mapti_cmd(test_data.cmdq_base_va, device_id,
					   event_id, coll_id, intid++);

			coll_id = (coll_id + 1) % test_data.nr_cpus;
		}
	}
}

static void guest_invalidate_all_rdists(void)
{
	int i;

	for (i = 0; i < test_data.nr_cpus; i++)
		its_send_invall_cmd(test_data.cmdq_base_va, i);
}

static void guest_setup_gic(void)
{
	static atomic_int nr_cpus_ready = 0;
	u32 cpuid = guest_get_vcpuid();

	gic_init(GIC_V3, test_data.nr_cpus);
	gic_rdist_enable_lpis(test_data.lpi_prop_table, SZ_64K,
			      test_data.lpi_pend_tables + (cpuid * SZ_64K));

	atomic_fetch_add(&nr_cpus_ready, 1);

	if (cpuid > 0)
		return;

	while (atomic_load(&nr_cpus_ready) < test_data.nr_cpus)
		cpu_relax();

	its_init(test_data.collection_table, SZ_64K,
		 test_data.device_table, SZ_64K,
		 test_data.cmdq_base, SZ_64K);

	guest_setup_its_mappings();
	guest_invalidate_all_rdists();

	/* SYNC to ensure ITS setup is complete */
	for (cpuid = 0; cpuid < test_data.nr_cpus; cpuid++)
		its_send_sync_cmd(test_data.cmdq_base_va, cpuid);
}

static void guest_wait_for_stop(void)
{
	/*
	 * Don't use WFI here to avoid blocking the vCPU thread indefinitely and
	 * never getting the stop signal.
	 */
	while (!READ_ONCE(test_data.request_vcpus_stop))
		cpu_relax();

	GUEST_DONE();
}

static void guest_stress_code(size_t nr_lpis)
{
	guest_setup_gic();
	local_irq_enable();

	GUEST_SYNC(0);

	guest_wait_for_stop();
}

static void setup_memslot(void)
{
	gpa_t gpa_base;
	size_t pages;
	size_t sz;

	/*
	 * For the ITS:
	 *  - A single level device table
	 *  - A single level collection table
	 *  - The command queue
	 *  - An ITT for each device
	 */
	sz = (3 + test_data.nr_devices) * SZ_64K;

	/*
	 * For the redistributors:
	 *  - A shared LPI configuration table
	 *  - An LPI pending table for each vCPU
	 */
	sz += (1 + test_data.nr_cpus) * SZ_64K;

	pages = sz / vm->page_size;
	gpa_base = ((vm_compute_max_gfn(vm) + 1) * vm->page_size) - sz;
	vm_override_mem_region(vm, MEM_REGION_TEST_EXTRA, VM_MEM_SRC_ANONYMOUS,
			       gpa_base, TEST_MEMSLOT_INDEX, pages);
}

#define LPI_PROP_DEFAULT_PRIO	0xa0

static void configure_lpis(void)
{
	size_t nr_lpis = test_data.nr_devices * test_data.nr_event_ids;
	u8 *tbl = addr_gpa2hva(vm, test_data.lpi_prop_table);
	size_t i;

	for (i = 0; i < nr_lpis; i++) {
		tbl[i] = LPI_PROP_DEFAULT_PRIO |
			 LPI_PROP_GROUP1 |
			 LPI_PROP_ENABLED;
	}
}

static void setup_test_data(void)
{
	size_t pages_per_64k = vm_calc_num_guest_pages(vm->mode, SZ_64K);
	u32 nr_devices = test_data.nr_devices;
	u32 nr_cpus = test_data.nr_cpus;
	gpa_t cmdq_base;

	test_data.device_table = vm_phy_pages_alloc(vm, pages_per_64k,
						    MEM_REGION_TEST_EXTRA);

	test_data.collection_table = vm_phy_pages_alloc(vm, pages_per_64k,
							MEM_REGION_TEST_EXTRA);

	cmdq_base = vm_phy_pages_alloc(vm, pages_per_64k, MEM_REGION_TEST_EXTRA);
	virt_map(vm, cmdq_base, cmdq_base, pages_per_64k);
	test_data.cmdq_base = cmdq_base;
	test_data.cmdq_base_va = (void *)cmdq_base;

	test_data.itt_tables = vm_phy_pages_alloc(vm, pages_per_64k * nr_devices,
						  MEM_REGION_TEST_EXTRA);

	test_data.lpi_prop_table = vm_phy_pages_alloc(vm, pages_per_64k,
						      MEM_REGION_TEST_EXTRA);
	configure_lpis();

	test_data.lpi_pend_tables = vm_phy_pages_alloc(vm, pages_per_64k * nr_cpus,
						       MEM_REGION_TEST_EXTRA);

	sync_global_to_guest(vm, test_data);
}

static void setup_gic(void)
{
	its_fd = vgic_its_setup(vm);
}

static void signal_lpi(u32 device_id, u32 event_id)
{
	gpa_t db_addr = GITS_BASE_GPA + GITS_TRANSLATER;

	struct kvm_msi msi = {
		.address_lo	= db_addr,
		.address_hi	= db_addr >> 32,
		.data		= event_id,
		.devid		= device_id,
		.flags		= KVM_MSI_VALID_DEVID,
	};

	/*
	 * KVM_SIGNAL_MSI returns 1 if the MSI wasn't 'blocked' by the VM,
	 * which for arm64 implies having a valid translation in the ITS.
	 */
	TEST_ASSERT(__vm_ioctl(vm, KVM_SIGNAL_MSI, &msi) == 1,
		    "KVM_SIGNAL_MSI ioctl failed");
}

static pthread_barrier_t test_setup_barrier;

static void *lpi_worker_thread(void *data)
{
	u32 device_id = (size_t)data;
	u32 event_id;
	size_t i;

	pthread_barrier_wait(&test_setup_barrier);

	for (i = 0; i < nr_iterations; i++)
		for (event_id = 0; event_id < test_data.nr_event_ids; event_id++)
			signal_lpi(device_id, event_id);

	return NULL;
}

static void *vcpu_worker_thread(void *data)
{
	struct kvm_vcpu *vcpu = data;
	struct ucall uc;

	while (true) {
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			pthread_barrier_wait(&test_setup_barrier);
			continue;
		case UCALL_DONE:
			return NULL;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		default:
			TEST_FAIL("Unknown ucall: %lu", uc.cmd);
		}
	}

	return NULL;
}

static void report_stats(struct timespec delta)
{
	double nr_lpis;
	double time;

	nr_lpis = test_data.nr_devices * test_data.nr_event_ids * nr_iterations;

	time = delta.tv_sec;
	time += ((double)delta.tv_nsec) / NSEC_PER_SEC;

	pr_info("Rate: %.2f LPIs/sec\n", nr_lpis / time);
}

static void run_stress_test(void)
{
	u32 nr_devices = test_data.nr_devices;
	u32 nr_vcpus = test_data.nr_cpus;
	pthread_t *lpi_threads = malloc(nr_devices * sizeof(pthread_t));
	pthread_t *vcpu_threads = malloc(nr_vcpus * sizeof(pthread_t));
	struct timespec start, delta;
	size_t i;

	TEST_ASSERT(lpi_threads && vcpu_threads, "Failed to allocate pthread arrays");

	pthread_barrier_init(&test_setup_barrier, NULL, nr_vcpus + nr_devices + 1);

	for (i = 0; i < nr_vcpus; i++)
		kvm_pthread_create(&vcpu_threads[i], NULL, vcpu_worker_thread, vcpus[i]);

	for (i = 0; i < nr_devices; i++)
		kvm_pthread_create(&lpi_threads[i], NULL, lpi_worker_thread, (void *)i);

	pthread_barrier_wait(&test_setup_barrier);

	clock_gettime(CLOCK_MONOTONIC, &start);

	for (i = 0; i < nr_devices; i++)
		kvm_pthread_join(lpi_threads[i], NULL);

	delta = timespec_elapsed(start);
	write_guest_global(vm, test_data.request_vcpus_stop, true);

	for (i = 0; i < nr_vcpus; i++)
		kvm_pthread_join(vcpu_threads[i], NULL);

	pthread_barrier_destroy(&test_setup_barrier);

	report_stats(delta);
}

static void setup_vm(void *guest_code, handler_fn irq_handler)
{
	int i;

	vcpus = malloc(test_data.nr_cpus * sizeof(struct kvm_vcpu *));
	TEST_ASSERT(vcpus, "Failed to allocate vCPU array");

	vm = vm_create_with_vcpus(test_data.nr_cpus, guest_code, vcpus);

	vm_init_descriptor_tables(vm);
	for (i = 0; i < test_data.nr_cpus; i++)
		vcpu_init_descriptor_tables(vcpus[i]);

	vm_install_exception_handler(vm, VECTOR_IRQ_CURRENT, irq_handler);

	setup_memslot();

	setup_gic();

	setup_test_data();
}

static void destroy_vm(void)
{
	close(its_fd);
	kvm_vm_free(vm);
	free(vcpus);
}

static void test_lpi_stress(void)
{
	test_data = stress_args;

	setup_vm(guest_stress_code, guest_irq_handler);

	run_stress_test();

	destroy_vm();
}

/*
 * Pending move: A is pending at vCPU0's CPU interface and B at vCPU1's, both
 * vCPUs with IRQs masked, when vCPU1 moves A to vCPU2. Once unmasked, A must
 * be taken on vCPU2 only, and B on vCPU1 only.
 */
#define MOVE_NR_VCPUS		3
#define MOVE_NR_LPIS		2
#define MOVE_DEVICE		0

/* guest_setup_its_mappings() maps event N to vCPU N's collection */
#define LPI_A			0
#define LPI_B			1
#define SRC_VCPU		0
#define B_VCPU			1
#define DST_VCPU		2

static atomic_uint lpi_taken[MOVE_NR_VCPUS][MOVE_NR_LPIS];
static atomic_bool lpi_a_pending, lpi_a_moved, vcpu_unmasked[MOVE_NR_VCPUS];

static void guest_count_irq_handler(struct ex_regs *regs)
{
	u32 intid = gic_get_and_ack_irq();

	if (intid == IAR_SPURIOUS)
		return;

	GUEST_ASSERT(intid >= GIC_LPI_OFFSET && intid < GIC_LPI_OFFSET + MOVE_NR_LPIS);
	atomic_fetch_add(&lpi_taken[guest_get_vcpuid()][intid - GIC_LPI_OFFSET], 1);
	gic_set_eoi(intid);
}

/* HPPIR shows an LPI pending at this CPU interface even with IRQs masked */
static bool guest_lpi_pending(u32 lpi)
{
	return (read_sysreg_s(SYS_ICC_HPPIR1_EL1) & GENMASK(23, 0)) == GIC_LPI_OFFSET + lpi;
}

static void guest_wait_pending(u32 lpi)
{
	while (!guest_lpi_pending(lpi))
		cpu_relax();
}

/* An LPI still pending here is taken before the host is told to check */
static void guest_unmask(u32 lpi)
{
	local_irq_enable();

	while (guest_lpi_pending(lpi))
		cpu_relax();

	atomic_store(&vcpu_unmasked[guest_get_vcpuid()], true);
}

static void guest_move_lpi_a(void)
{
	void *cmdq = test_data.cmdq_base_va;

	if (test_data.move_cmd == MOVE_MOVI) {
		its_send_movi_cmd(cmdq, MOVE_DEVICE, LPI_A, DST_VCPU);
	} else {
		its_send_mapc_cmd(cmdq, DST_VCPU, SRC_VCPU, true);
		/* The GICv3 spec's MAPC description requires this SYNC before MOVALL */
		its_send_sync_cmd(cmdq, SRC_VCPU);
		its_send_movall_cmd(cmdq, SRC_VCPU, DST_VCPU);
	}

	/* Complete the move at both redistributors before vCPU0 unmasks */
	its_send_sync_cmd(cmdq, SRC_VCPU);
	its_send_sync_cmd(cmdq, DST_VCPU);
}

static void guest_pending_move_code(void)
{
	u32 cpuid = guest_get_vcpuid();

	guest_setup_gic();

	if (cpuid == DST_VCPU)
		local_irq_enable();

	GUEST_SYNC(0);

	if (cpuid == SRC_VCPU) {
		guest_wait_pending(LPI_A);
		atomic_store(&lpi_a_pending, true);

		while (!atomic_load(&lpi_a_moved))
			cpu_relax();
		guest_unmask(LPI_A);
	} else if (cpuid == B_VCPU) {
		guest_wait_pending(LPI_B);

		while (!atomic_load(&lpi_a_pending))
			cpu_relax();
		guest_move_lpi_a();
		atomic_store(&lpi_a_moved, true);
		guest_unmask(LPI_B);
	}

	guest_wait_for_stop();
}

static unsigned int nr_lpis_taken(atomic_uint (*taken)[MOVE_NR_LPIS])
{
	unsigned int nr = 0;
	int vcpu, lpi;

	for (vcpu = 0; vcpu < MOVE_NR_VCPUS; vcpu++)
		for (lpi = 0; lpi < MOVE_NR_LPIS; lpi++)
			nr += atomic_load(&taken[vcpu][lpi]);

	return nr;
}

static bool pending_move_done(atomic_uint (*taken)[MOVE_NR_LPIS], atomic_bool *unmasked)
{
	return nr_lpis_taken(taken) >= MOVE_NR_LPIS &&
	       atomic_load(&unmasked[SRC_VCPU]) && atomic_load(&unmasked[B_VCPU]);
}

static void check_lpi_taken(atomic_uint (*taken)[MOVE_NR_LPIS], int lpi, int want_vcpu)
{
	const char *cmd = test_data.move_cmd == MOVE_MOVI ? "MOVI" : "MAPC+MOVALL";
	unsigned int nr[MOVE_NR_VCPUS];
	bool ok = true;
	int vcpu;

	for (vcpu = 0; vcpu < MOVE_NR_VCPUS; vcpu++) {
		nr[vcpu] = atomic_load(&taken[vcpu][lpi]);
		ok &= nr[vcpu] == (vcpu == want_vcpu);
	}

	TEST_ASSERT(ok, "%s: LPI %c taken %u/%u/%u times on vCPU0/1/2, expected once on vCPU%d",
		    cmd, 'A' + lpi, nr[0], nr[1], nr[2], want_vcpu);
}

static void run_pending_move_test(void)
{
	atomic_uint (*taken)[MOVE_NR_LPIS] = addr_gva2hva(vm, (gva_t)lpi_taken);
	atomic_bool *unmasked = addr_gva2hva(vm, (gva_t)vcpu_unmasked);
	pthread_t vcpu_threads[MOVE_NR_VCPUS];
	int i;

	pthread_barrier_init(&test_setup_barrier, NULL, MOVE_NR_VCPUS + 1);

	for (i = 0; i < MOVE_NR_VCPUS; i++)
		kvm_pthread_create(&vcpu_threads[i], NULL, vcpu_worker_thread, vcpus[i]);

	pthread_barrier_wait(&test_setup_barrier);

	signal_lpi(MOVE_DEVICE, LPI_A);
	signal_lpi(MOVE_DEVICE, LPI_B);

	for (i = 0; i < 10000 && !pending_move_done(taken, unmasked); i++)
		usleep(1000);

	/* Check first: a vCPU still waiting for its LPI never sees the stop */
	check_lpi_taken(taken, LPI_A, DST_VCPU);
	check_lpi_taken(taken, LPI_B, B_VCPU);

	write_guest_global(vm, test_data.request_vcpus_stop, true);

	for (i = 0; i < MOVE_NR_VCPUS; i++)
		kvm_pthread_join(vcpu_threads[i], NULL);

	pthread_barrier_destroy(&test_setup_barrier);
}

static void test_pending_move(enum move_cmd move_cmd)
{
	test_data = (struct test_data) {
		.nr_cpus	= MOVE_NR_VCPUS,
		.nr_devices	= 1,
		.nr_event_ids	= MOVE_NR_LPIS,
		.move_cmd	= move_cmd,
	};

	setup_vm(guest_pending_move_code, guest_count_irq_handler);

	run_pending_move_test();

	destroy_vm();
}

static void pr_usage(const char *name)
{
	pr_info("%s [-v NR_VCPUS] [-d NR_DEVICES] [-e NR_EVENTS] [-i ITERS] -h\n", name);
	pr_info("  -v:\tnumber of vCPUs (default: %u)\n", stress_args.nr_cpus);
	pr_info("  -d:\tnumber of devices (default: %u)\n", stress_args.nr_devices);
	pr_info("  -e:\tnumber of event IDs per device (default: %u)\n", stress_args.nr_event_ids);
	pr_info("  -i:\tnumber of iterations (default: %lu)\n", nr_iterations);
}

int main(int argc, char **argv)
{
	u32 nr_threads;
	int c;

	TEST_REQUIRE(kvm_supports_vgic_v3());

	while ((c = getopt(argc, argv, "hv:d:e:i:")) != -1) {
		switch (c) {
		case 'v':
			stress_args.nr_cpus = atoi(optarg);
			break;
		case 'd':
			stress_args.nr_devices = atoi(optarg);
			break;
		case 'e':
			stress_args.nr_event_ids = atoi(optarg);
			break;
		case 'i':
			nr_iterations = strtoul(optarg, NULL, 0);
			break;
		case 'h':
		default:
			pr_usage(argv[0]);
			return 1;
		}
	}

	nr_threads = stress_args.nr_cpus + stress_args.nr_devices;
	if (nr_threads > get_nprocs())
		pr_info("WARNING: running %u threads on %d CPUs; performance is degraded.\n",
			 nr_threads, get_nprocs());

	test_lpi_stress();

	test_pending_move(MOVE_MOVI);
	test_pending_move(MOVE_MAPC_MOVALL);

	return 0;
}
