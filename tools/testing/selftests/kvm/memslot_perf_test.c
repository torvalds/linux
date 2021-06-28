// SPDX-License-Identifier: GPL-2.0
/*
 * A memslot-related performance benchmark.
 *
 * Copyright (C) 2021 Oracle and/or its affiliates.
 *
 * Basic guest setup / host vCPU thread code lifted from set_memory_region_test.
 */
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <linux/compiler.h>

#include <test_util.h>
#include <kvm_util.h>
#include <processor.h>

#define VCPU_ID 0

#define MEM_SIZE		((512U << 20) + 4096)
#define MEM_SIZE_PAGES		(MEM_SIZE / 4096)
#define MEM_GPA		0x10000000UL
#define MEM_AUX_GPA		MEM_GPA
#define MEM_SYNC_GPA		MEM_AUX_GPA
#define MEM_TEST_GPA		(MEM_AUX_GPA + 4096)
#define MEM_TEST_SIZE		(MEM_SIZE - 4096)
static_assert(MEM_SIZE % 4096 == 0, "invalid mem size");
static_assert(MEM_TEST_SIZE % 4096 == 0, "invalid mem test size");

/*
 * 32 MiB is max size that gets well over 100 iterations on 509 slots.
 * Considering that each slot needs to have at least one page up to
 * 8194 slots in use can then be tested (although with slightly
 * limited resolution).
 */
#define MEM_SIZE_MAP		((32U << 20) + 4096)
#define MEM_SIZE_MAP_PAGES	(MEM_SIZE_MAP / 4096)
#define MEM_TEST_MAP_SIZE	(MEM_SIZE_MAP - 4096)
#define MEM_TEST_MAP_SIZE_PAGES (MEM_TEST_MAP_SIZE / 4096)
static_assert(MEM_SIZE_MAP % 4096 == 0, "invalid map test region size");
static_assert(MEM_TEST_MAP_SIZE % 4096 == 0, "invalid map test region size");
static_assert(MEM_TEST_MAP_SIZE_PAGES % 2 == 0, "invalid map test region size");
static_assert(MEM_TEST_MAP_SIZE_PAGES > 2, "invalid map test region size");

/*
 * 128 MiB is min size that fills 32k slots with at least one page in each
 * while at the same time gets 100+ iterations in such test
 */
#define MEM_TEST_UNMAP_SIZE		(128U << 20)
#define MEM_TEST_UNMAP_SIZE_PAGES	(MEM_TEST_UNMAP_SIZE / 4096)
/* 2 MiB chunk size like a typical huge page */
#define MEM_TEST_UNMAP_CHUNK_PAGES	(2U << (20 - 12))
static_assert(MEM_TEST_UNMAP_SIZE <= MEM_TEST_SIZE,
	      "invalid unmap test region size");
static_assert(MEM_TEST_UNMAP_SIZE % 4096 == 0,
	      "invalid unmap test region size");
static_assert(MEM_TEST_UNMAP_SIZE_PAGES %
	      (2 * MEM_TEST_UNMAP_CHUNK_PAGES) == 0,
	      "invalid unmap test region size");

/*
 * For the move active test the middle of the test area is placed on
 * a memslot boundary: half lies in the memslot being moved, half in
 * other memslot(s).
 *
 * When running this test with 32k memslots (32764, really) each memslot
 * contains 4 pages.
 * The last one additionally contains the remaining 21 pages of memory,
 * for the total size of 25 pages.
 * Hence, the maximum size here is 50 pages.
 */
#define MEM_TEST_MOVE_SIZE_PAGES	(50)
#define MEM_TEST_MOVE_SIZE		(MEM_TEST_MOVE_SIZE_PAGES * 4096)
#define MEM_TEST_MOVE_GPA_DEST		(MEM_GPA + MEM_SIZE)
static_assert(MEM_TEST_MOVE_SIZE <= MEM_TEST_SIZE,
	      "invalid move test region size");

#define MEM_TEST_VAL_1 0x1122334455667788
#define MEM_TEST_VAL_2 0x99AABBCCDDEEFF00

struct vm_data {
	struct kvm_vm *vm;
	pthread_t vcpu_thread;
	uint32_t nslots;
	uint64_t npages;
	uint64_t pages_per_slot;
	void **hva_slots;
	bool mmio_ok;
	uint64_t mmio_gpa_min;
	uint64_t mmio_gpa_max;
};

struct sync_area {
	atomic_bool start_flag;
	atomic_bool exit_flag;
	atomic_bool sync_flag;
	void *move_area_ptr;
};

/*
 * Technically, we need also for the atomic bool to be address-free, which
 * is recommended, but not strictly required, by C11 for lockless
 * implementations.
 * However, in practice both GCC and Clang fulfill this requirement on
 * all KVM-supported platforms.
 */
static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "atomic bool is not lockless");

static sem_t vcpu_ready;

static bool map_unmap_verify;

static bool verbose;
#define pr_info_v(...)				\
	do {					\
		if (verbose)			\
			pr_info(__VA_ARGS__);	\
	} while (0)

static void *vcpu_worker(void *data)
{
	struct vm_data *vm = data;
	struct kvm_run *run;
	struct ucall uc;
	uint64_t cmd;

	run = vcpu_state(vm->vm, VCPU_ID);
	while (1) {
		vcpu_run(vm->vm, VCPU_ID);

		if (run->exit_reason == KVM_EXIT_IO) {
			cmd = get_ucall(vm->vm, VCPU_ID, &uc);
			if (cmd != UCALL_SYNC)
				break;

			sem_post(&vcpu_ready);
			continue;
		}

		if (run->exit_reason != KVM_EXIT_MMIO)
			break;

		TEST_ASSERT(vm->mmio_ok, "Unexpected mmio exit");
		TEST_ASSERT(run->mmio.is_write, "Unexpected mmio read");
		TEST_ASSERT(run->mmio.len == 8,
			    "Unexpected exit mmio size = %u", run->mmio.len);
		TEST_ASSERT(run->mmio.phys_addr >= vm->mmio_gpa_min &&
			    run->mmio.phys_addr <= vm->mmio_gpa_max,
			    "Unexpected exit mmio address = 0x%llx",
			    run->mmio.phys_addr);
	}

	if (run->exit_reason == KVM_EXIT_IO && cmd == UCALL_ABORT)
		TEST_FAIL("%s at %s:%ld, val = %lu", (const char *)uc.args[0],
			  __FILE__, uc.args[1], uc.args[2]);

	return NULL;
}

static void wait_for_vcpu(void)
{
	struct timespec ts;

	TEST_ASSERT(!clock_gettime(CLOCK_REALTIME, &ts),
		    "clock_gettime() failed: %d\n", errno);

	ts.tv_sec += 2;
	TEST_ASSERT(!sem_timedwait(&vcpu_ready, &ts),
		    "sem_timedwait() failed: %d\n", errno);
}

static void *vm_gpa2hva(struct vm_data *data, uint64_t gpa, uint64_t *rempages)
{
	uint64_t gpage, pgoffs;
	uint32_t slot, slotoffs;
	void *base;

	TEST_ASSERT(gpa >= MEM_GPA, "Too low gpa to translate");
	TEST_ASSERT(gpa < MEM_GPA + data->npages * 4096,
		    "Too high gpa to translate");
	gpa -= MEM_GPA;

	gpage = gpa / 4096;
	pgoffs = gpa % 4096;
	slot = min(gpage / data->pages_per_slot, (uint64_t)data->nslots - 1);
	slotoffs = gpage - (slot * data->pages_per_slot);

	if (rempages) {
		uint64_t slotpages;

		if (slot == data->nslots - 1)
			slotpages = data->npages - slot * data->pages_per_slot;
		else
			slotpages = data->pages_per_slot;

		TEST_ASSERT(!pgoffs,
			    "Asking for remaining pages in slot but gpa not page aligned");
		*rempages = slotpages - slotoffs;
	}

	base = data->hva_slots[slot];
	return (uint8_t *)base + slotoffs * 4096 + pgoffs;
}

static uint64_t vm_slot2gpa(struct vm_data *data, uint32_t slot)
{
	TEST_ASSERT(slot < data->nslots, "Too high slot number");

	return MEM_GPA + slot * data->pages_per_slot * 4096;
}

static struct vm_data *alloc_vm(void)
{
	struct vm_data *data;

	data = malloc(sizeof(*data));
	TEST_ASSERT(data, "malloc(vmdata) failed");

	data->vm = NULL;
	data->hva_slots = NULL;

	return data;
}

static bool prepare_vm(struct vm_data *data, int nslots, uint64_t *maxslots,
		       void *guest_code, uint64_t mempages,
		       struct timespec *slot_runtime)
{
	uint32_t max_mem_slots;
	uint64_t rempages;
	uint64_t guest_addr;
	uint32_t slot;
	struct timespec tstart;
	struct sync_area *sync;

	max_mem_slots = kvm_check_cap(KVM_CAP_NR_MEMSLOTS);
	TEST_ASSERT(max_mem_slots > 1,
		    "KVM_CAP_NR_MEMSLOTS should be greater than 1");
	TEST_ASSERT(nslots > 1 || nslots == -1,
		    "Slot count cap should be greater than 1");
	if (nslots != -1)
		max_mem_slots = min(max_mem_slots, (uint32_t)nslots);
	pr_info_v("Allowed number of memory slots: %"PRIu32"\n", max_mem_slots);

	TEST_ASSERT(mempages > 1,
		    "Can't test without any memory");

	data->npages = mempages;
	data->nslots = max_mem_slots - 1;
	data->pages_per_slot = mempages / data->nslots;
	if (!data->pages_per_slot) {
		*maxslots = mempages + 1;
		return false;
	}

	rempages = mempages % data->nslots;
	data->hva_slots = malloc(sizeof(*data->hva_slots) * data->nslots);
	TEST_ASSERT(data->hva_slots, "malloc() fail");

	data->vm = vm_create_default(VCPU_ID, mempages, guest_code);

	pr_info_v("Adding slots 1..%i, each slot with %"PRIu64" pages + %"PRIu64" extra pages last\n",
		max_mem_slots - 1, data->pages_per_slot, rempages);

	clock_gettime(CLOCK_MONOTONIC, &tstart);
	for (slot = 1, guest_addr = MEM_GPA; slot < max_mem_slots; slot++) {
		uint64_t npages;

		npages = data->pages_per_slot;
		if (slot == max_mem_slots - 1)
			npages += rempages;

		vm_userspace_mem_region_add(data->vm, VM_MEM_SRC_ANONYMOUS,
					    guest_addr, slot, npages,
					    0);
		guest_addr += npages * 4096;
	}
	*slot_runtime = timespec_elapsed(tstart);

	for (slot = 0, guest_addr = MEM_GPA; slot < max_mem_slots - 1; slot++) {
		uint64_t npages;
		uint64_t gpa;

		npages = data->pages_per_slot;
		if (slot == max_mem_slots - 2)
			npages += rempages;

		gpa = vm_phy_pages_alloc(data->vm, npages, guest_addr,
					 slot + 1);
		TEST_ASSERT(gpa == guest_addr,
			    "vm_phy_pages_alloc() failed\n");

		data->hva_slots[slot] = addr_gpa2hva(data->vm, guest_addr);
		memset(data->hva_slots[slot], 0, npages * 4096);

		guest_addr += npages * 4096;
	}

	virt_map(data->vm, MEM_GPA, MEM_GPA, mempages);

	sync = (typeof(sync))vm_gpa2hva(data, MEM_SYNC_GPA, NULL);
	atomic_init(&sync->start_flag, false);
	atomic_init(&sync->exit_flag, false);
	atomic_init(&sync->sync_flag, false);

	data->mmio_ok = false;

	return true;
}

static void launch_vm(struct vm_data *data)
{
	pr_info_v("Launching the test VM\n");

	pthread_create(&data->vcpu_thread, NULL, vcpu_worker, data);

	/* Ensure the guest thread is spun up. */
	wait_for_vcpu();
}

static void free_vm(struct vm_data *data)
{
	kvm_vm_free(data->vm);
	free(data->hva_slots);
	free(data);
}

static void wait_guest_exit(struct vm_data *data)
{
	pthread_join(data->vcpu_thread, NULL);
}

static void let_guest_run(struct sync_area *sync)
{
	atomic_store_explicit(&sync->start_flag, true, memory_order_release);
}

static void guest_spin_until_start(void)
{
	struct sync_area *sync = (typeof(sync))MEM_SYNC_GPA;

	while (!atomic_load_explicit(&sync->start_flag, memory_order_acquire))
		;
}

static void make_guest_exit(struct sync_area *sync)
{
	atomic_store_explicit(&sync->exit_flag, true, memory_order_release);
}

static bool _guest_should_exit(void)
{
	struct sync_area *sync = (typeof(sync))MEM_SYNC_GPA;

	return atomic_load_explicit(&sync->exit_flag, memory_order_acquire);
}

#define guest_should_exit() unlikely(_guest_should_exit())

/*
 * noinline so we can easily see how much time the host spends waiting
 * for the guest.
 * For the same reason use alarm() instead of polling clock_gettime()
 * to implement a wait timeout.
 */
static noinline void host_perform_sync(struct sync_area *sync)
{
	alarm(2);

	atomic_store_explicit(&sync->sync_flag, true, memory_order_release);
	while (atomic_load_explicit(&sync->sync_flag, memory_order_acquire))
		;

	alarm(0);
}

static bool guest_perform_sync(void)
{
	struct sync_area *sync = (typeof(sync))MEM_SYNC_GPA;
	bool expected;

	do {
		if (guest_should_exit())
			return false;

		expected = true;
	} while (!atomic_compare_exchange_weak_explicit(&sync->sync_flag,
							&expected, false,
							memory_order_acq_rel,
							memory_order_relaxed));

	return true;
}

static void guest_code_test_memslot_move(void)
{
	struct sync_area *sync = (typeof(sync))MEM_SYNC_GPA;
	uintptr_t base = (typeof(base))READ_ONCE(sync->move_area_ptr);

	GUEST_SYNC(0);

	guest_spin_until_start();

	while (!guest_should_exit()) {
		uintptr_t ptr;

		for (ptr = base; ptr < base + MEM_TEST_MOVE_SIZE;
		     ptr += 4096)
			*(uint64_t *)ptr = MEM_TEST_VAL_1;

		/*
		 * No host sync here since the MMIO exits are so expensive
		 * that the host would spend most of its time waiting for
		 * the guest and so instead of measuring memslot move
		 * performance we would measure the performance and
		 * likelihood of MMIO exits
		 */
	}

	GUEST_DONE();
}

static void guest_code_test_memslot_map(void)
{
	struct sync_area *sync = (typeof(sync))MEM_SYNC_GPA;

	GUEST_SYNC(0);

	guest_spin_until_start();

	while (1) {
		uintptr_t ptr;

		for (ptr = MEM_TEST_GPA;
		     ptr < MEM_TEST_GPA + MEM_TEST_MAP_SIZE / 2; ptr += 4096)
			*(uint64_t *)ptr = MEM_TEST_VAL_1;

		if (!guest_perform_sync())
			break;

		for (ptr = MEM_TEST_GPA + MEM_TEST_MAP_SIZE / 2;
		     ptr < MEM_TEST_GPA + MEM_TEST_MAP_SIZE; ptr += 4096)
			*(uint64_t *)ptr = MEM_TEST_VAL_2;

		if (!guest_perform_sync())
			break;
	}

	GUEST_DONE();
}

static void guest_code_test_memslot_unmap(void)
{
	struct sync_area *sync = (typeof(sync))MEM_SYNC_GPA;

	GUEST_SYNC(0);

	guest_spin_until_start();

	while (1) {
		uintptr_t ptr = MEM_TEST_GPA;

		/*
		 * We can afford to access (map) just a small number of pages
		 * per host sync as otherwise the host will spend
		 * a significant amount of its time waiting for the guest
		 * (instead of doing unmap operations), so this will
		 * effectively turn this test into a map performance test.
		 *
		 * Just access a single page to be on the safe side.
		 */
		*(uint64_t *)ptr = MEM_TEST_VAL_1;

		if (!guest_perform_sync())
			break;

		ptr += MEM_TEST_UNMAP_SIZE / 2;
		*(uint64_t *)ptr = MEM_TEST_VAL_2;

		if (!guest_perform_sync())
			break;
	}

	GUEST_DONE();
}

static void guest_code_test_memslot_rw(void)
{
	GUEST_SYNC(0);

	guest_spin_until_start();

	while (1) {
		uintptr_t ptr;

		for (ptr = MEM_TEST_GPA;
		     ptr < MEM_TEST_GPA + MEM_TEST_SIZE; ptr += 4096)
			*(uint64_t *)ptr = MEM_TEST_VAL_1;

		if (!guest_perform_sync())
			break;

		for (ptr = MEM_TEST_GPA + 4096 / 2;
		     ptr < MEM_TEST_GPA + MEM_TEST_SIZE; ptr += 4096) {
			uint64_t val = *(uint64_t *)ptr;

			GUEST_ASSERT_1(val == MEM_TEST_VAL_2, val);
			*(uint64_t *)ptr = 0;
		}

		if (!guest_perform_sync())
			break;
	}

	GUEST_DONE();
}

static bool test_memslot_move_prepare(struct vm_data *data,
				      struct sync_area *sync,
				      uint64_t *maxslots, bool isactive)
{
	uint64_t movesrcgpa, movetestgpa;

	movesrcgpa = vm_slot2gpa(data, data->nslots - 1);

	if (isactive) {
		uint64_t lastpages;

		vm_gpa2hva(data, movesrcgpa, &lastpages);
		if (lastpages < MEM_TEST_MOVE_SIZE_PAGES / 2) {
			*maxslots = 0;
			return false;
		}
	}

	movetestgpa = movesrcgpa - (MEM_TEST_MOVE_SIZE / (isactive ? 2 : 1));
	sync->move_area_ptr = (void *)movetestgpa;

	if (isactive) {
		data->mmio_ok = true;
		data->mmio_gpa_min = movesrcgpa;
		data->mmio_gpa_max = movesrcgpa + MEM_TEST_MOVE_SIZE / 2 - 1;
	}

	return true;
}

static bool test_memslot_move_prepare_active(struct vm_data *data,
					     struct sync_area *sync,
					     uint64_t *maxslots)
{
	return test_memslot_move_prepare(data, sync, maxslots, true);
}

static bool test_memslot_move_prepare_inactive(struct vm_data *data,
					       struct sync_area *sync,
					       uint64_t *maxslots)
{
	return test_memslot_move_prepare(data, sync, maxslots, false);
}

static void test_memslot_move_loop(struct vm_data *data, struct sync_area *sync)
{
	uint64_t movesrcgpa;

	movesrcgpa = vm_slot2gpa(data, data->nslots - 1);
	vm_mem_region_move(data->vm, data->nslots - 1 + 1,
			   MEM_TEST_MOVE_GPA_DEST);
	vm_mem_region_move(data->vm, data->nslots - 1 + 1, movesrcgpa);
}

static void test_memslot_do_unmap(struct vm_data *data,
				  uint64_t offsp, uint64_t count)
{
	uint64_t gpa, ctr;

	for (gpa = MEM_TEST_GPA + offsp * 4096, ctr = 0; ctr < count; ) {
		uint64_t npages;
		void *hva;
		int ret;

		hva = vm_gpa2hva(data, gpa, &npages);
		TEST_ASSERT(npages, "Empty memory slot at gptr 0x%"PRIx64, gpa);
		npages = min(npages, count - ctr);
		ret = madvise(hva, npages * 4096, MADV_DONTNEED);
		TEST_ASSERT(!ret,
			    "madvise(%p, MADV_DONTNEED) on VM memory should not fail for gptr 0x%"PRIx64,
			    hva, gpa);
		ctr += npages;
		gpa += npages * 4096;
	}
	TEST_ASSERT(ctr == count,
		    "madvise(MADV_DONTNEED) should exactly cover all of the requested area");
}

static void test_memslot_map_unmap_check(struct vm_data *data,
					 uint64_t offsp, uint64_t valexp)
{
	uint64_t gpa;
	uint64_t *val;

	if (!map_unmap_verify)
		return;

	gpa = MEM_TEST_GPA + offsp * 4096;
	val = (typeof(val))vm_gpa2hva(data, gpa, NULL);
	TEST_ASSERT(*val == valexp,
		    "Guest written values should read back correctly before unmap (%"PRIu64" vs %"PRIu64" @ %"PRIx64")",
		    *val, valexp, gpa);
	*val = 0;
}

static void test_memslot_map_loop(struct vm_data *data, struct sync_area *sync)
{
	/*
	 * Unmap the second half of the test area while guest writes to (maps)
	 * the first half.
	 */
	test_memslot_do_unmap(data, MEM_TEST_MAP_SIZE_PAGES / 2,
			      MEM_TEST_MAP_SIZE_PAGES / 2);

	/*
	 * Wait for the guest to finish writing the first half of the test
	 * area, verify the written value on the first and the last page of
	 * this area and then unmap it.
	 * Meanwhile, the guest is writing to (mapping) the second half of
	 * the test area.
	 */
	host_perform_sync(sync);
	test_memslot_map_unmap_check(data, 0, MEM_TEST_VAL_1);
	test_memslot_map_unmap_check(data,
				     MEM_TEST_MAP_SIZE_PAGES / 2 - 1,
				     MEM_TEST_VAL_1);
	test_memslot_do_unmap(data, 0, MEM_TEST_MAP_SIZE_PAGES / 2);


	/*
	 * Wait for the guest to finish writing the second half of the test
	 * area and verify the written value on the first and the last page
	 * of this area.
	 * The area will be unmapped at the beginning of the next loop
	 * iteration.
	 * Meanwhile, the guest is writing to (mapping) the first half of
	 * the test area.
	 */
	host_perform_sync(sync);
	test_memslot_map_unmap_check(data, MEM_TEST_MAP_SIZE_PAGES / 2,
				     MEM_TEST_VAL_2);
	test_memslot_map_unmap_check(data, MEM_TEST_MAP_SIZE_PAGES - 1,
				     MEM_TEST_VAL_2);
}

static void test_memslot_unmap_loop_common(struct vm_data *data,
					   struct sync_area *sync,
					   uint64_t chunk)
{
	uint64_t ctr;

	/*
	 * Wait for the guest to finish mapping page(s) in the first half
	 * of the test area, verify the written value and then perform unmap
	 * of this area.
	 * Meanwhile, the guest is writing to (mapping) page(s) in the second
	 * half of the test area.
	 */
	host_perform_sync(sync);
	test_memslot_map_unmap_check(data, 0, MEM_TEST_VAL_1);
	for (ctr = 0; ctr < MEM_TEST_UNMAP_SIZE_PAGES / 2; ctr += chunk)
		test_memslot_do_unmap(data, ctr, chunk);

	/* Likewise, but for the opposite host / guest areas */
	host_perform_sync(sync);
	test_memslot_map_unmap_check(data, MEM_TEST_UNMAP_SIZE_PAGES / 2,
				     MEM_TEST_VAL_2);
	for (ctr = MEM_TEST_UNMAP_SIZE_PAGES / 2;
	     ctr < MEM_TEST_UNMAP_SIZE_PAGES; ctr += chunk)
		test_memslot_do_unmap(data, ctr, chunk);
}

static void test_memslot_unmap_loop(struct vm_data *data,
				    struct sync_area *sync)
{
	test_memslot_unmap_loop_common(data, sync, 1);
}

static void test_memslot_unmap_loop_chunked(struct vm_data *data,
					    struct sync_area *sync)
{
	test_memslot_unmap_loop_common(data, sync, MEM_TEST_UNMAP_CHUNK_PAGES);
}

static void test_memslot_rw_loop(struct vm_data *data, struct sync_area *sync)
{
	uint64_t gptr;

	for (gptr = MEM_TEST_GPA + 4096 / 2;
	     gptr < MEM_TEST_GPA + MEM_TEST_SIZE; gptr += 4096)
		*(uint64_t *)vm_gpa2hva(data, gptr, NULL) = MEM_TEST_VAL_2;

	host_perform_sync(sync);

	for (gptr = MEM_TEST_GPA;
	     gptr < MEM_TEST_GPA + MEM_TEST_SIZE; gptr += 4096) {
		uint64_t *vptr = (typeof(vptr))vm_gpa2hva(data, gptr, NULL);
		uint64_t val = *vptr;

		TEST_ASSERT(val == MEM_TEST_VAL_1,
			    "Guest written values should read back correctly (is %"PRIu64" @ %"PRIx64")",
			    val, gptr);
		*vptr = 0;
	}

	host_perform_sync(sync);
}

struct test_data {
	const char *name;
	uint64_t mem_size;
	void (*guest_code)(void);
	bool (*prepare)(struct vm_data *data, struct sync_area *sync,
			uint64_t *maxslots);
	void (*loop)(struct vm_data *data, struct sync_area *sync);
};

static bool test_execute(int nslots, uint64_t *maxslots,
			 unsigned int maxtime,
			 const struct test_data *tdata,
			 uint64_t *nloops,
			 struct timespec *slot_runtime,
			 struct timespec *guest_runtime)
{
	uint64_t mem_size = tdata->mem_size ? : MEM_SIZE_PAGES;
	struct vm_data *data;
	struct sync_area *sync;
	struct timespec tstart;
	bool ret = true;

	data = alloc_vm();
	if (!prepare_vm(data, nslots, maxslots, tdata->guest_code,
			mem_size, slot_runtime)) {
		ret = false;
		goto exit_free;
	}

	sync = (typeof(sync))vm_gpa2hva(data, MEM_SYNC_GPA, NULL);

	if (tdata->prepare &&
	    !tdata->prepare(data, sync, maxslots)) {
		ret = false;
		goto exit_free;
	}

	launch_vm(data);

	clock_gettime(CLOCK_MONOTONIC, &tstart);
	let_guest_run(sync);

	while (1) {
		*guest_runtime = timespec_elapsed(tstart);
		if (guest_runtime->tv_sec >= maxtime)
			break;

		tdata->loop(data, sync);

		(*nloops)++;
	}

	make_guest_exit(sync);
	wait_guest_exit(data);

exit_free:
	free_vm(data);

	return ret;
}

static const struct test_data tests[] = {
	{
		.name = "map",
		.mem_size = MEM_SIZE_MAP_PAGES,
		.guest_code = guest_code_test_memslot_map,
		.loop = test_memslot_map_loop,
	},
	{
		.name = "unmap",
		.mem_size = MEM_TEST_UNMAP_SIZE_PAGES + 1,
		.guest_code = guest_code_test_memslot_unmap,
		.loop = test_memslot_unmap_loop,
	},
	{
		.name = "unmap chunked",
		.mem_size = MEM_TEST_UNMAP_SIZE_PAGES + 1,
		.guest_code = guest_code_test_memslot_unmap,
		.loop = test_memslot_unmap_loop_chunked,
	},
	{
		.name = "move active area",
		.guest_code = guest_code_test_memslot_move,
		.prepare = test_memslot_move_prepare_active,
		.loop = test_memslot_move_loop,
	},
	{
		.name = "move inactive area",
		.guest_code = guest_code_test_memslot_move,
		.prepare = test_memslot_move_prepare_inactive,
		.loop = test_memslot_move_loop,
	},
	{
		.name = "RW",
		.guest_code = guest_code_test_memslot_rw,
		.loop = test_memslot_rw_loop
	},
};

#define NTESTS ARRAY_SIZE(tests)

struct test_args {
	int tfirst;
	int tlast;
	int nslots;
	int seconds;
	int runs;
};

static void help(char *name, struct test_args *targs)
{
	int ctr;

	pr_info("usage: %s [-h] [-v] [-d] [-s slots] [-f first_test] [-e last_test] [-l test_length] [-r run_count]\n",
		name);
	pr_info(" -h: print this help screen.\n");
	pr_info(" -v: enable verbose mode (not for benchmarking).\n");
	pr_info(" -d: enable extra debug checks.\n");
	pr_info(" -s: specify memslot count cap (-1 means no cap; currently: %i)\n",
		targs->nslots);
	pr_info(" -f: specify the first test to run (currently: %i; max %zu)\n",
		targs->tfirst, NTESTS - 1);
	pr_info(" -e: specify the last test to run (currently: %i; max %zu)\n",
		targs->tlast, NTESTS - 1);
	pr_info(" -l: specify the test length in seconds (currently: %i)\n",
		targs->seconds);
	pr_info(" -r: specify the number of runs per test (currently: %i)\n",
		targs->runs);

	pr_info("\nAvailable tests:\n");
	for (ctr = 0; ctr < NTESTS; ctr++)
		pr_info("%d: %s\n", ctr, tests[ctr].name);
}

static bool parse_args(int argc, char *argv[],
		       struct test_args *targs)
{
	int opt;

	while ((opt = getopt(argc, argv, "hvds:f:e:l:r:")) != -1) {
		switch (opt) {
		case 'h':
		default:
			help(argv[0], targs);
			return false;
		case 'v':
			verbose = true;
			break;
		case 'd':
			map_unmap_verify = true;
			break;
		case 's':
			targs->nslots = atoi(optarg);
			if (targs->nslots <= 0 && targs->nslots != -1) {
				pr_info("Slot count cap has to be positive or -1 for no cap\n");
				return false;
			}
			break;
		case 'f':
			targs->tfirst = atoi(optarg);
			if (targs->tfirst < 0) {
				pr_info("First test to run has to be non-negative\n");
				return false;
			}
			break;
		case 'e':
			targs->tlast = atoi(optarg);
			if (targs->tlast < 0 || targs->tlast >= NTESTS) {
				pr_info("Last test to run has to be non-negative and less than %zu\n",
					NTESTS);
				return false;
			}
			break;
		case 'l':
			targs->seconds = atoi(optarg);
			if (targs->seconds < 0) {
				pr_info("Test length in seconds has to be non-negative\n");
				return false;
			}
			break;
		case 'r':
			targs->runs = atoi(optarg);
			if (targs->runs <= 0) {
				pr_info("Runs per test has to be positive\n");
				return false;
			}
			break;
		}
	}

	if (optind < argc) {
		help(argv[0], targs);
		return false;
	}

	if (targs->tfirst > targs->tlast) {
		pr_info("First test to run cannot be greater than the last test to run\n");
		return false;
	}

	return true;
}

struct test_result {
	struct timespec slot_runtime, guest_runtime, iter_runtime;
	int64_t slottimens, runtimens;
	uint64_t nloops;
};

static bool test_loop(const struct test_data *data,
		      const struct test_args *targs,
		      struct test_result *rbestslottime,
		      struct test_result *rbestruntime)
{
	uint64_t maxslots;
	struct test_result result;

	result.nloops = 0;
	if (!test_execute(targs->nslots, &maxslots, targs->seconds, data,
			  &result.nloops,
			  &result.slot_runtime, &result.guest_runtime)) {
		if (maxslots)
			pr_info("Memslot count too high for this test, decrease the cap (max is %"PRIu64")\n",
				maxslots);
		else
			pr_info("Memslot count may be too high for this test, try adjusting the cap\n");

		return false;
	}

	pr_info("Test took %ld.%.9lds for slot setup + %ld.%.9lds all iterations\n",
		result.slot_runtime.tv_sec, result.slot_runtime.tv_nsec,
		result.guest_runtime.tv_sec, result.guest_runtime.tv_nsec);
	if (!result.nloops) {
		pr_info("No full loops done - too short test time or system too loaded?\n");
		return true;
	}

	result.iter_runtime = timespec_div(result.guest_runtime,
					   result.nloops);
	pr_info("Done %"PRIu64" iterations, avg %ld.%.9lds each\n",
		result.nloops,
		result.iter_runtime.tv_sec,
		result.iter_runtime.tv_nsec);
	result.slottimens = timespec_to_ns(result.slot_runtime);
	result.runtimens = timespec_to_ns(result.iter_runtime);

	/*
	 * Only rank the slot setup time for tests using the whole test memory
	 * area so they are comparable
	 */
	if (!data->mem_size &&
	    (!rbestslottime->slottimens ||
	     result.slottimens < rbestslottime->slottimens))
		*rbestslottime = result;
	if (!rbestruntime->runtimens ||
	    result.runtimens < rbestruntime->runtimens)
		*rbestruntime = result;

	return true;
}

int main(int argc, char *argv[])
{
	struct test_args targs = {
		.tfirst = 0,
		.tlast = NTESTS - 1,
		.nslots = -1,
		.seconds = 5,
		.runs = 1,
	};
	struct test_result rbestslottime;
	int tctr;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	if (!parse_args(argc, argv, &targs))
		return -1;

	rbestslottime.slottimens = 0;
	for (tctr = targs.tfirst; tctr <= targs.tlast; tctr++) {
		const struct test_data *data = &tests[tctr];
		unsigned int runctr;
		struct test_result rbestruntime;

		if (tctr > targs.tfirst)
			pr_info("\n");

		pr_info("Testing %s performance with %i runs, %d seconds each\n",
			data->name, targs.runs, targs.seconds);

		rbestruntime.runtimens = 0;
		for (runctr = 0; runctr < targs.runs; runctr++)
			if (!test_loop(data, &targs,
				       &rbestslottime, &rbestruntime))
				break;

		if (rbestruntime.runtimens)
			pr_info("Best runtime result was %ld.%.9lds per iteration (with %"PRIu64" iterations)\n",
				rbestruntime.iter_runtime.tv_sec,
				rbestruntime.iter_runtime.tv_nsec,
				rbestruntime.nloops);
	}

	if (rbestslottime.slottimens)
		pr_info("Best slot setup time for the whole test area was %ld.%.9lds\n",
			rbestslottime.slot_runtime.tv_sec,
			rbestslottime.slot_runtime.tv_nsec);

	return 0;
}
