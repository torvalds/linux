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
#include <linux/sizes.h>

#include <test_util.h>
#include <kvm_util.h>
#include <processor.h>

#define MEM_EXTRA_SIZE		SZ_64K

#define MEM_SIZE		(SZ_512M + MEM_EXTRA_SIZE)
#define MEM_GPA			SZ_256M
#define MEM_AUX_GPA		MEM_GPA
#define MEM_SYNC_GPA		MEM_AUX_GPA
#define MEM_TEST_GPA		(MEM_AUX_GPA + MEM_EXTRA_SIZE)
#define MEM_TEST_SIZE		(MEM_SIZE - MEM_EXTRA_SIZE)

/*
 * 32 MiB is max size that gets well over 100 iterations on 509 slots.
 * Considering that each slot needs to have at least one page up to
 * 8194 slots in use can then be tested (although with slightly
 * limited resolution).
 */
#define MEM_SIZE_MAP		(SZ_32M + MEM_EXTRA_SIZE)
#define MEM_TEST_MAP_SIZE	(MEM_SIZE_MAP - MEM_EXTRA_SIZE)

/*
 * 128 MiB is min size that fills 32k slots with at least one page in each
 * while at the same time gets 100+ iterations in such test
 *
 * 2 MiB chunk size like a typical huge page
 */
#define MEM_TEST_UNMAP_SIZE		SZ_128M
#define MEM_TEST_UNMAP_CHUNK_SIZE	SZ_2M

/*
 * For the move active test the middle of the test area is placed on
 * a memslot boundary: half lies in the memslot being moved, half in
 * other memslot(s).
 *
 * We have different number of memory slots, excluding the reserved
 * memory slot 0, on various architectures and configurations. The
 * memory size in this test is calculated by picking the maximal
 * last memory slot's memory size, with alignment to the largest
 * supported page size (64KB). In this way, the selected memory
 * size for this test is compatible with test_memslot_move_prepare().
 *
 * architecture   slots    memory-per-slot    memory-on-last-slot
 * --------------------------------------------------------------
 * x86-4KB        32763    16KB               160KB
 * arm64-4KB      32766    16KB               112KB
 * arm64-16KB     32766    16KB               112KB
 * arm64-64KB     8192     64KB               128KB
 */
#define MEM_TEST_MOVE_SIZE		(3 * SZ_64K)
#define MEM_TEST_MOVE_GPA_DEST		(MEM_GPA + MEM_SIZE)
static_assert(MEM_TEST_MOVE_SIZE <= MEM_TEST_SIZE,
	      "invalid move test region size");

#define MEM_TEST_VAL_1 0x1122334455667788
#define MEM_TEST_VAL_2 0x99AABBCCDDEEFF00

struct vm_data {
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
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
	uint32_t    guest_page_size;
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
#ifdef __x86_64__
static bool disable_slot_zap_quirk;
#endif

static bool verbose;
#define pr_info_v(...)				\
	do {					\
		if (verbose)			\
			pr_info(__VA_ARGS__);	\
	} while (0)

static void check_mmio_access(struct vm_data *data, struct kvm_run *run)
{
	TEST_ASSERT(data->mmio_ok, "Unexpected mmio exit");
	TEST_ASSERT(run->mmio.is_write, "Unexpected mmio read");
	TEST_ASSERT(run->mmio.len == 8,
		    "Unexpected exit mmio size = %u", run->mmio.len);
	TEST_ASSERT(run->mmio.phys_addr >= data->mmio_gpa_min &&
		    run->mmio.phys_addr <= data->mmio_gpa_max,
		    "Unexpected exit mmio address = 0x%llx",
		    run->mmio.phys_addr);
}

static void *vcpu_worker(void *__data)
{
	struct vm_data *data = __data;
	struct kvm_vcpu *vcpu = data->vcpu;
	struct kvm_run *run = vcpu->run;
	struct ucall uc;

	while (1) {
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			TEST_ASSERT(uc.args[1] == 0,
				"Unexpected sync ucall, got %lx",
				(ulong)uc.args[1]);
			sem_post(&vcpu_ready);
			continue;
		case UCALL_NONE:
			if (run->exit_reason == KVM_EXIT_MMIO)
				check_mmio_access(data, run);
			else
				goto done;
			break;
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
	return NULL;
}

static void wait_for_vcpu(void)
{
	struct timespec ts;

	TEST_ASSERT(!clock_gettime(CLOCK_REALTIME, &ts),
		    "clock_gettime() failed: %d", errno);

	ts.tv_sec += 2;
	TEST_ASSERT(!sem_timedwait(&vcpu_ready, &ts),
		    "sem_timedwait() failed: %d", errno);
}

static void *vm_gpa2hva(struct vm_data *data, uint64_t gpa, uint64_t *rempages)
{
	uint64_t gpage, pgoffs;
	uint32_t slot, slotoffs;
	void *base;
	uint32_t guest_page_size = data->vm->page_size;

	TEST_ASSERT(gpa >= MEM_GPA, "Too low gpa to translate");
	TEST_ASSERT(gpa < MEM_GPA + data->npages * guest_page_size,
		    "Too high gpa to translate");
	gpa -= MEM_GPA;

	gpage = gpa / guest_page_size;
	pgoffs = gpa % guest_page_size;
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
	return (uint8_t *)base + slotoffs * guest_page_size + pgoffs;
}

static uint64_t vm_slot2gpa(struct vm_data *data, uint32_t slot)
{
	uint32_t guest_page_size = data->vm->page_size;

	TEST_ASSERT(slot < data->nslots, "Too high slot number");

	return MEM_GPA + slot * data->pages_per_slot * guest_page_size;
}

static struct vm_data *alloc_vm(void)
{
	struct vm_data *data;

	data = malloc(sizeof(*data));
	TEST_ASSERT(data, "malloc(vmdata) failed");

	data->vm = NULL;
	data->vcpu = NULL;
	data->hva_slots = NULL;

	return data;
}

static bool check_slot_pages(uint32_t host_page_size, uint32_t guest_page_size,
			     uint64_t pages_per_slot, uint64_t rempages)
{
	if (!pages_per_slot)
		return false;

	if ((pages_per_slot * guest_page_size) % host_page_size)
		return false;

	if ((rempages * guest_page_size) % host_page_size)
		return false;

	return true;
}


static uint64_t get_max_slots(struct vm_data *data, uint32_t host_page_size)
{
	uint32_t guest_page_size = data->vm->page_size;
	uint64_t mempages, pages_per_slot, rempages;
	uint64_t slots;

	mempages = data->npages;
	slots = data->nslots;
	while (--slots > 1) {
		pages_per_slot = mempages / slots;
		if (!pages_per_slot)
			continue;

		rempages = mempages % pages_per_slot;
		if (check_slot_pages(host_page_size, guest_page_size,
				     pages_per_slot, rempages))
			return slots + 1;	/* slot 0 is reserved */
	}

	return 0;
}

static bool prepare_vm(struct vm_data *data, int nslots, uint64_t *maxslots,
		       void *guest_code, uint64_t mem_size,
		       struct timespec *slot_runtime)
{
	uint64_t mempages, rempages;
	uint64_t guest_addr;
	uint32_t slot, host_page_size, guest_page_size;
	struct timespec tstart;
	struct sync_area *sync;

	host_page_size = getpagesize();
	guest_page_size = vm_guest_mode_params[VM_MODE_DEFAULT].page_size;
	mempages = mem_size / guest_page_size;

	data->vm = __vm_create_with_one_vcpu(&data->vcpu, mempages, guest_code);
	TEST_ASSERT(data->vm->page_size == guest_page_size, "Invalid VM page size");

	data->npages = mempages;
	TEST_ASSERT(data->npages > 1, "Can't test without any memory");
	data->nslots = nslots;
	data->pages_per_slot = data->npages / data->nslots;
	rempages = data->npages % data->nslots;
	if (!check_slot_pages(host_page_size, guest_page_size,
			      data->pages_per_slot, rempages)) {
		*maxslots = get_max_slots(data, host_page_size);
		return false;
	}

	data->hva_slots = malloc(sizeof(*data->hva_slots) * data->nslots);
	TEST_ASSERT(data->hva_slots, "malloc() fail");

	pr_info_v("Adding slots 1..%i, each slot with %"PRIu64" pages + %"PRIu64" extra pages last\n",
		data->nslots, data->pages_per_slot, rempages);

	clock_gettime(CLOCK_MONOTONIC, &tstart);
	for (slot = 1, guest_addr = MEM_GPA; slot <= data->nslots; slot++) {
		uint64_t npages;

		npages = data->pages_per_slot;
		if (slot == data->nslots)
			npages += rempages;

		vm_userspace_mem_region_add(data->vm, VM_MEM_SRC_ANONYMOUS,
					    guest_addr, slot, npages,
					    0);
		guest_addr += npages * guest_page_size;
	}
	*slot_runtime = timespec_elapsed(tstart);

	for (slot = 1, guest_addr = MEM_GPA; slot <= data->nslots; slot++) {
		uint64_t npages;
		uint64_t gpa;

		npages = data->pages_per_slot;
		if (slot == data->nslots)
			npages += rempages;

		gpa = vm_phy_pages_alloc(data->vm, npages, guest_addr, slot);
		TEST_ASSERT(gpa == guest_addr,
			    "vm_phy_pages_alloc() failed");

		data->hva_slots[slot - 1] = addr_gpa2hva(data->vm, guest_addr);
		memset(data->hva_slots[slot - 1], 0, npages * guest_page_size);

		guest_addr += npages * guest_page_size;
	}

	virt_map(data->vm, MEM_GPA, MEM_GPA, data->npages);

	sync = (typeof(sync))vm_gpa2hva(data, MEM_SYNC_GPA, NULL);
	sync->guest_page_size = data->vm->page_size;
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
	alarm(10);

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
	uint32_t page_size = (typeof(page_size))READ_ONCE(sync->guest_page_size);
	uintptr_t base = (typeof(base))READ_ONCE(sync->move_area_ptr);

	GUEST_SYNC(0);

	guest_spin_until_start();

	while (!guest_should_exit()) {
		uintptr_t ptr;

		for (ptr = base; ptr < base + MEM_TEST_MOVE_SIZE;
		     ptr += page_size)
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
	uint32_t page_size = (typeof(page_size))READ_ONCE(sync->guest_page_size);

	GUEST_SYNC(0);

	guest_spin_until_start();

	while (1) {
		uintptr_t ptr;

		for (ptr = MEM_TEST_GPA;
		     ptr < MEM_TEST_GPA + MEM_TEST_MAP_SIZE / 2;
		     ptr += page_size)
			*(uint64_t *)ptr = MEM_TEST_VAL_1;

		if (!guest_perform_sync())
			break;

		for (ptr = MEM_TEST_GPA + MEM_TEST_MAP_SIZE / 2;
		     ptr < MEM_TEST_GPA + MEM_TEST_MAP_SIZE;
		     ptr += page_size)
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
	struct sync_area *sync = (typeof(sync))MEM_SYNC_GPA;
	uint32_t page_size = (typeof(page_size))READ_ONCE(sync->guest_page_size);

	GUEST_SYNC(0);

	guest_spin_until_start();

	while (1) {
		uintptr_t ptr;

		for (ptr = MEM_TEST_GPA;
		     ptr < MEM_TEST_GPA + MEM_TEST_SIZE; ptr += page_size)
			*(uint64_t *)ptr = MEM_TEST_VAL_1;

		if (!guest_perform_sync())
			break;

		for (ptr = MEM_TEST_GPA + page_size / 2;
		     ptr < MEM_TEST_GPA + MEM_TEST_SIZE; ptr += page_size) {
			uint64_t val = *(uint64_t *)ptr;

			GUEST_ASSERT_EQ(val, MEM_TEST_VAL_2);
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
	uint32_t guest_page_size = data->vm->page_size;
	uint64_t movesrcgpa, movetestgpa;

#ifdef __x86_64__
	if (disable_slot_zap_quirk)
		vm_enable_cap(data->vm, KVM_CAP_DISABLE_QUIRKS2, KVM_X86_QUIRK_SLOT_ZAP_ALL);
#endif

	movesrcgpa = vm_slot2gpa(data, data->nslots - 1);

	if (isactive) {
		uint64_t lastpages;

		vm_gpa2hva(data, movesrcgpa, &lastpages);
		if (lastpages * guest_page_size < MEM_TEST_MOVE_SIZE / 2) {
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
	uint32_t guest_page_size = data->vm->page_size;

	for (gpa = MEM_TEST_GPA + offsp * guest_page_size, ctr = 0; ctr < count; ) {
		uint64_t npages;
		void *hva;
		int ret;

		hva = vm_gpa2hva(data, gpa, &npages);
		TEST_ASSERT(npages, "Empty memory slot at gptr 0x%"PRIx64, gpa);
		npages = min(npages, count - ctr);
		ret = madvise(hva, npages * guest_page_size, MADV_DONTNEED);
		TEST_ASSERT(!ret,
			    "madvise(%p, MADV_DONTNEED) on VM memory should not fail for gptr 0x%"PRIx64,
			    hva, gpa);
		ctr += npages;
		gpa += npages * guest_page_size;
	}
	TEST_ASSERT(ctr == count,
		    "madvise(MADV_DONTNEED) should exactly cover all of the requested area");
}

static void test_memslot_map_unmap_check(struct vm_data *data,
					 uint64_t offsp, uint64_t valexp)
{
	uint64_t gpa;
	uint64_t *val;
	uint32_t guest_page_size = data->vm->page_size;

	if (!map_unmap_verify)
		return;

	gpa = MEM_TEST_GPA + offsp * guest_page_size;
	val = (typeof(val))vm_gpa2hva(data, gpa, NULL);
	TEST_ASSERT(*val == valexp,
		    "Guest written values should read back correctly before unmap (%"PRIu64" vs %"PRIu64" @ %"PRIx64")",
		    *val, valexp, gpa);
	*val = 0;
}

static void test_memslot_map_loop(struct vm_data *data, struct sync_area *sync)
{
	uint32_t guest_page_size = data->vm->page_size;
	uint64_t guest_pages = MEM_TEST_MAP_SIZE / guest_page_size;

	/*
	 * Unmap the second half of the test area while guest writes to (maps)
	 * the first half.
	 */
	test_memslot_do_unmap(data, guest_pages / 2, guest_pages / 2);

	/*
	 * Wait for the guest to finish writing the first half of the test
	 * area, verify the written value on the first and the last page of
	 * this area and then unmap it.
	 * Meanwhile, the guest is writing to (mapping) the second half of
	 * the test area.
	 */
	host_perform_sync(sync);
	test_memslot_map_unmap_check(data, 0, MEM_TEST_VAL_1);
	test_memslot_map_unmap_check(data, guest_pages / 2 - 1, MEM_TEST_VAL_1);
	test_memslot_do_unmap(data, 0, guest_pages / 2);


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
	test_memslot_map_unmap_check(data, guest_pages / 2, MEM_TEST_VAL_2);
	test_memslot_map_unmap_check(data, guest_pages - 1, MEM_TEST_VAL_2);
}

static void test_memslot_unmap_loop_common(struct vm_data *data,
					   struct sync_area *sync,
					   uint64_t chunk)
{
	uint32_t guest_page_size = data->vm->page_size;
	uint64_t guest_pages = MEM_TEST_UNMAP_SIZE / guest_page_size;
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
	for (ctr = 0; ctr < guest_pages / 2; ctr += chunk)
		test_memslot_do_unmap(data, ctr, chunk);

	/* Likewise, but for the opposite host / guest areas */
	host_perform_sync(sync);
	test_memslot_map_unmap_check(data, guest_pages / 2, MEM_TEST_VAL_2);
	for (ctr = guest_pages / 2; ctr < guest_pages; ctr += chunk)
		test_memslot_do_unmap(data, ctr, chunk);
}

static void test_memslot_unmap_loop(struct vm_data *data,
				    struct sync_area *sync)
{
	uint32_t host_page_size = getpagesize();
	uint32_t guest_page_size = data->vm->page_size;
	uint64_t guest_chunk_pages = guest_page_size >= host_page_size ?
					1 : host_page_size / guest_page_size;

	test_memslot_unmap_loop_common(data, sync, guest_chunk_pages);
}

static void test_memslot_unmap_loop_chunked(struct vm_data *data,
					    struct sync_area *sync)
{
	uint32_t guest_page_size = data->vm->page_size;
	uint64_t guest_chunk_pages = MEM_TEST_UNMAP_CHUNK_SIZE / guest_page_size;

	test_memslot_unmap_loop_common(data, sync, guest_chunk_pages);
}

static void test_memslot_rw_loop(struct vm_data *data, struct sync_area *sync)
{
	uint64_t gptr;
	uint32_t guest_page_size = data->vm->page_size;

	for (gptr = MEM_TEST_GPA + guest_page_size / 2;
	     gptr < MEM_TEST_GPA + MEM_TEST_SIZE; gptr += guest_page_size)
		*(uint64_t *)vm_gpa2hva(data, gptr, NULL) = MEM_TEST_VAL_2;

	host_perform_sync(sync);

	for (gptr = MEM_TEST_GPA;
	     gptr < MEM_TEST_GPA + MEM_TEST_SIZE; gptr += guest_page_size) {
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
	uint64_t mem_size = tdata->mem_size ? : MEM_SIZE;
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
		.mem_size = MEM_SIZE_MAP,
		.guest_code = guest_code_test_memslot_map,
		.loop = test_memslot_map_loop,
	},
	{
		.name = "unmap",
		.mem_size = MEM_TEST_UNMAP_SIZE + MEM_EXTRA_SIZE,
		.guest_code = guest_code_test_memslot_unmap,
		.loop = test_memslot_unmap_loop,
	},
	{
		.name = "unmap chunked",
		.mem_size = MEM_TEST_UNMAP_SIZE + MEM_EXTRA_SIZE,
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
	pr_info(" -q: Disable memslot zap quirk during memslot move.\n");
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

static bool check_memory_sizes(void)
{
	uint32_t host_page_size = getpagesize();
	uint32_t guest_page_size = vm_guest_mode_params[VM_MODE_DEFAULT].page_size;

	if (host_page_size > SZ_64K || guest_page_size > SZ_64K) {
		pr_info("Unsupported page size on host (0x%x) or guest (0x%x)\n",
			host_page_size, guest_page_size);
		return false;
	}

	if (MEM_SIZE % guest_page_size ||
	    MEM_TEST_SIZE % guest_page_size) {
		pr_info("invalid MEM_SIZE or MEM_TEST_SIZE\n");
		return false;
	}

	if (MEM_SIZE_MAP % guest_page_size		||
	    MEM_TEST_MAP_SIZE % guest_page_size		||
	    (MEM_TEST_MAP_SIZE / guest_page_size) <= 2	||
	    (MEM_TEST_MAP_SIZE / guest_page_size) % 2) {
		pr_info("invalid MEM_SIZE_MAP or MEM_TEST_MAP_SIZE\n");
		return false;
	}

	if (MEM_TEST_UNMAP_SIZE > MEM_TEST_SIZE		||
	    MEM_TEST_UNMAP_SIZE % guest_page_size	||
	    (MEM_TEST_UNMAP_SIZE / guest_page_size) %
	    (2 * MEM_TEST_UNMAP_CHUNK_SIZE / guest_page_size)) {
		pr_info("invalid MEM_TEST_UNMAP_SIZE or MEM_TEST_UNMAP_CHUNK_SIZE\n");
		return false;
	}

	return true;
}

static bool parse_args(int argc, char *argv[],
		       struct test_args *targs)
{
	uint32_t max_mem_slots;
	int opt;

	while ((opt = getopt(argc, argv, "hvdqs:f:e:l:r:")) != -1) {
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
#ifdef __x86_64__
		case 'q':
			disable_slot_zap_quirk = true;
			TEST_REQUIRE(kvm_check_cap(KVM_CAP_DISABLE_QUIRKS2) &
				     KVM_X86_QUIRK_SLOT_ZAP_ALL);
			break;
#endif
		case 's':
			targs->nslots = atoi_paranoid(optarg);
			if (targs->nslots <= 1 && targs->nslots != -1) {
				pr_info("Slot count cap must be larger than 1 or -1 for no cap\n");
				return false;
			}
			break;
		case 'f':
			targs->tfirst = atoi_non_negative("First test", optarg);
			break;
		case 'e':
			targs->tlast = atoi_non_negative("Last test", optarg);
			if (targs->tlast >= NTESTS) {
				pr_info("Last test to run has to be non-negative and less than %zu\n",
					NTESTS);
				return false;
			}
			break;
		case 'l':
			targs->seconds = atoi_non_negative("Test length", optarg);
			break;
		case 'r':
			targs->runs = atoi_positive("Runs per test", optarg);
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

	max_mem_slots = kvm_check_cap(KVM_CAP_NR_MEMSLOTS);
	if (max_mem_slots <= 1) {
		pr_info("KVM_CAP_NR_MEMSLOTS should be greater than 1\n");
		return false;
	}

	/* Memory slot 0 is reserved */
	if (targs->nslots == -1)
		targs->nslots = max_mem_slots - 1;
	else
		targs->nslots = min_t(int, targs->nslots, max_mem_slots) - 1;

	pr_info_v("Allowed Number of memory slots: %"PRIu32"\n",
		  targs->nslots + 1);

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
	struct test_result result = {};

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
	struct test_result rbestslottime = {};
	int tctr;

	if (!check_memory_sizes())
		return -1;

	if (!parse_args(argc, argv, &targs))
		return -1;

	for (tctr = targs.tfirst; tctr <= targs.tlast; tctr++) {
		const struct test_data *data = &tests[tctr];
		unsigned int runctr;
		struct test_result rbestruntime = {};

		if (tctr > targs.tfirst)
			pr_info("\n");

		pr_info("Testing %s performance with %i runs, %d seconds each\n",
			data->name, targs.runs, targs.seconds);

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
