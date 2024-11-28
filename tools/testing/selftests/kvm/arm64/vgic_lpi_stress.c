// SPDX-License-Identifier: GPL-2.0
/*
 * vgic_lpi_stress - Stress test for KVM's ITS emulation
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
static vm_paddr_t gpa_base;

static struct kvm_vm *vm;
static struct kvm_vcpu **vcpus;
static int gic_fd, its_fd;

static struct test_data {
	bool		request_vcpus_stop;
	u32		nr_cpus;
	u32		nr_devices;
	u32		nr_event_ids;

	vm_paddr_t	device_table;
	vm_paddr_t	collection_table;
	vm_paddr_t	cmdq_base;
	void		*cmdq_base_va;
	vm_paddr_t	itt_tables;

	vm_paddr_t	lpi_prop_table;
	vm_paddr_t	lpi_pend_tables;
} test_data =  {
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
		vm_paddr_t itt_base = test_data.itt_tables + (device_id * SZ_64K);

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
}

static void guest_code(size_t nr_lpis)
{
	guest_setup_gic();

	GUEST_SYNC(0);

	/*
	 * Don't use WFI here to avoid blocking the vCPU thread indefinitely and
	 * never getting the stop signal.
	 */
	while (!READ_ONCE(test_data.request_vcpus_stop))
		cpu_relax();

	GUEST_DONE();
}

static void setup_memslot(void)
{
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
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS, gpa_base,
				    TEST_MEMSLOT_INDEX, pages, 0);
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
	vm_paddr_t cmdq_base;

	test_data.device_table = vm_phy_pages_alloc(vm, pages_per_64k,
						    gpa_base,
						    TEST_MEMSLOT_INDEX);

	test_data.collection_table = vm_phy_pages_alloc(vm, pages_per_64k,
							gpa_base,
							TEST_MEMSLOT_INDEX);

	cmdq_base = vm_phy_pages_alloc(vm, pages_per_64k, gpa_base,
				       TEST_MEMSLOT_INDEX);
	virt_map(vm, cmdq_base, cmdq_base, pages_per_64k);
	test_data.cmdq_base = cmdq_base;
	test_data.cmdq_base_va = (void *)cmdq_base;

	test_data.itt_tables = vm_phy_pages_alloc(vm, pages_per_64k * nr_devices,
						  gpa_base, TEST_MEMSLOT_INDEX);

	test_data.lpi_prop_table = vm_phy_pages_alloc(vm, pages_per_64k,
						      gpa_base, TEST_MEMSLOT_INDEX);
	configure_lpis();

	test_data.lpi_pend_tables = vm_phy_pages_alloc(vm, pages_per_64k * nr_cpus,
						       gpa_base, TEST_MEMSLOT_INDEX);

	sync_global_to_guest(vm, test_data);
}

static void setup_gic(void)
{
	gic_fd = vgic_v3_setup(vm, test_data.nr_cpus, 64);
	__TEST_REQUIRE(gic_fd >= 0, "Failed to create GICv3");

	its_fd = vgic_its_setup(vm);
}

static void signal_lpi(u32 device_id, u32 event_id)
{
	vm_paddr_t db_addr = GITS_BASE_GPA + GITS_TRANSLATER;

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

static void run_test(void)
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
		pthread_create(&vcpu_threads[i], NULL, vcpu_worker_thread, vcpus[i]);

	for (i = 0; i < nr_devices; i++)
		pthread_create(&lpi_threads[i], NULL, lpi_worker_thread, (void *)i);

	pthread_barrier_wait(&test_setup_barrier);

	clock_gettime(CLOCK_MONOTONIC, &start);

	for (i = 0; i < nr_devices; i++)
		pthread_join(lpi_threads[i], NULL);

	delta = timespec_elapsed(start);
	write_guest_global(vm, test_data.request_vcpus_stop, true);

	for (i = 0; i < nr_vcpus; i++)
		pthread_join(vcpu_threads[i], NULL);

	report_stats(delta);
}

static void setup_vm(void)
{
	int i;

	vcpus = malloc(test_data.nr_cpus * sizeof(struct kvm_vcpu));
	TEST_ASSERT(vcpus, "Failed to allocate vCPU array");

	vm = vm_create_with_vcpus(test_data.nr_cpus, guest_code, vcpus);

	vm_init_descriptor_tables(vm);
	for (i = 0; i < test_data.nr_cpus; i++)
		vcpu_init_descriptor_tables(vcpus[i]);

	vm_install_exception_handler(vm, VECTOR_IRQ_CURRENT, guest_irq_handler);

	setup_memslot();

	setup_gic();

	setup_test_data();
}

static void destroy_vm(void)
{
	close(its_fd);
	close(gic_fd);
	kvm_vm_free(vm);
	free(vcpus);
}

static void pr_usage(const char *name)
{
	pr_info("%s [-v NR_VCPUS] [-d NR_DEVICES] [-e NR_EVENTS] [-i ITERS] -h\n", name);
	pr_info("  -v:\tnumber of vCPUs (default: %u)\n", test_data.nr_cpus);
	pr_info("  -d:\tnumber of devices (default: %u)\n", test_data.nr_devices);
	pr_info("  -e:\tnumber of event IDs per device (default: %u)\n", test_data.nr_event_ids);
	pr_info("  -i:\tnumber of iterations (default: %lu)\n", nr_iterations);
}

int main(int argc, char **argv)
{
	u32 nr_threads;
	int c;

	while ((c = getopt(argc, argv, "hv:d:e:i:")) != -1) {
		switch (c) {
		case 'v':
			test_data.nr_cpus = atoi(optarg);
			break;
		case 'd':
			test_data.nr_devices = atoi(optarg);
			break;
		case 'e':
			test_data.nr_event_ids = atoi(optarg);
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

	nr_threads = test_data.nr_cpus + test_data.nr_devices;
	if (nr_threads > get_nprocs())
		pr_info("WARNING: running %u threads on %d CPUs; performance is degraded.\n",
			 nr_threads, get_nprocs());

	setup_vm();

	run_test();

	destroy_vm();

	return 0;
}
