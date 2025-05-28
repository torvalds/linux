// SPDX-License-Identifier: GPL-2.0
/*
 * KVM dirty page logging test
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/atomic.h>
#include <asm/barrier.h>

#include "kvm_util.h"
#include "test_util.h"
#include "guest_modes.h"
#include "processor.h"
#include "ucall_common.h"

#define DIRTY_MEM_BITS 30 /* 1G */
#define PAGE_SHIFT_4K  12

/* The memory slot index to track dirty pages */
#define TEST_MEM_SLOT_INDEX		1

/* Default guest test virtual memory offset */
#define DEFAULT_GUEST_TEST_MEM		0xc0000000

/* How many host loops to run (one KVM_GET_DIRTY_LOG for each loop) */
#define TEST_HOST_LOOP_N		32UL

/* Interval for each host loop (ms) */
#define TEST_HOST_LOOP_INTERVAL		10UL

/*
 * Ensure the vCPU is able to perform a reasonable number of writes in each
 * iteration to provide a lower bound on coverage.
 */
#define TEST_MIN_WRITES_PER_ITERATION	0x100

/* Dirty bitmaps are always little endian, so we need to swap on big endian */
#if defined(__s390x__)
# define BITOP_LE_SWIZZLE	((BITS_PER_LONG-1) & ~0x7)
# define test_bit_le(nr, addr) \
	test_bit((nr) ^ BITOP_LE_SWIZZLE, addr)
# define __set_bit_le(nr, addr) \
	__set_bit((nr) ^ BITOP_LE_SWIZZLE, addr)
# define __clear_bit_le(nr, addr) \
	__clear_bit((nr) ^ BITOP_LE_SWIZZLE, addr)
# define __test_and_set_bit_le(nr, addr) \
	__test_and_set_bit((nr) ^ BITOP_LE_SWIZZLE, addr)
# define __test_and_clear_bit_le(nr, addr) \
	__test_and_clear_bit((nr) ^ BITOP_LE_SWIZZLE, addr)
#else
# define test_bit_le			test_bit
# define __set_bit_le			__set_bit
# define __clear_bit_le			__clear_bit
# define __test_and_set_bit_le		__test_and_set_bit
# define __test_and_clear_bit_le	__test_and_clear_bit
#endif

#define TEST_DIRTY_RING_COUNT		65536

#define SIG_IPI SIGUSR1

/*
 * Guest/Host shared variables. Ensure addr_gva2hva() and/or
 * sync_global_to/from_guest() are used when accessing from
 * the host. READ/WRITE_ONCE() should also be used with anything
 * that may change.
 */
static uint64_t host_page_size;
static uint64_t guest_page_size;
static uint64_t guest_num_pages;
static uint64_t iteration;
static uint64_t nr_writes;
static bool vcpu_stop;

/*
 * Guest physical memory offset of the testing memory slot.
 * This will be set to the topmost valid physical address minus
 * the test memory size.
 */
static uint64_t guest_test_phys_mem;

/*
 * Guest virtual memory offset of the testing memory slot.
 * Must not conflict with identity mapped test code.
 */
static uint64_t guest_test_virt_mem = DEFAULT_GUEST_TEST_MEM;

/*
 * Continuously write to the first 8 bytes of a random pages within
 * the testing memory region.
 */
static void guest_code(void)
{
	uint64_t addr;

#ifdef __s390x__
	uint64_t i;

	/*
	 * On s390x, all pages of a 1M segment are initially marked as dirty
	 * when a page of the segment is written to for the very first time.
	 * To compensate this specialty in this test, we need to touch all
	 * pages during the first iteration.
	 */
	for (i = 0; i < guest_num_pages; i++) {
		addr = guest_test_virt_mem + i * guest_page_size;
		vcpu_arch_put_guest(*(uint64_t *)addr, READ_ONCE(iteration));
		nr_writes++;
	}
#endif

	while (true) {
		while (!READ_ONCE(vcpu_stop)) {
			addr = guest_test_virt_mem;
			addr += (guest_random_u64(&guest_rng) % guest_num_pages)
				* guest_page_size;
			addr = align_down(addr, host_page_size);

			vcpu_arch_put_guest(*(uint64_t *)addr, READ_ONCE(iteration));
			nr_writes++;
		}

		GUEST_SYNC(1);
	}
}

/* Host variables */
static bool host_quit;

/* Points to the test VM memory region on which we track dirty logs */
static void *host_test_mem;
static uint64_t host_num_pages;

/* For statistics only */
static uint64_t host_dirty_count;
static uint64_t host_clear_count;

/* Whether dirty ring reset is requested, or finished */
static sem_t sem_vcpu_stop;
static sem_t sem_vcpu_cont;

/*
 * This is updated by the vcpu thread to tell the host whether it's a
 * ring-full event.  It should only be read until a sem_wait() of
 * sem_vcpu_stop and before vcpu continues to run.
 */
static bool dirty_ring_vcpu_ring_full;

/*
 * This is only used for verifying the dirty pages.  Dirty ring has a very
 * tricky case when the ring just got full, kvm will do userspace exit due to
 * ring full.  When that happens, the very last PFN is set but actually the
 * data is not changed (the guest WRITE is not really applied yet), because
 * we found that the dirty ring is full, refused to continue the vcpu, and
 * recorded the dirty gfn with the old contents.
 *
 * For this specific case, it's safe to skip checking this pfn for this
 * bit, because it's a redundant bit, and when the write happens later the bit
 * will be set again.  We use this variable to always keep track of the latest
 * dirty gfn we've collected, so that if a mismatch of data found later in the
 * verifying process, we let it pass.
 */
static uint64_t dirty_ring_last_page = -1ULL;

/*
 * In addition to the above, it is possible (especially if this
 * test is run nested) for the above scenario to repeat multiple times:
 *
 * The following can happen:
 *
 * - L1 vCPU:        Memory write is logged to PML but not committed.
 *
 * - L1 test thread: Ignores the write because its last dirty ring entry
 *                   Resets the dirty ring which:
 *                     - Resets the A/D bits in EPT
 *                     - Issues tlb flush (invept), which is intercepted by L0
 *
 * - L0: frees the whole nested ept mmu root as the response to invept,
 *       and thus ensures that when memory write is retried, it will fault again
 *
 * - L1 vCPU:        Same memory write is logged to the PML but not committed again.
 *
 * - L1 test thread: Ignores the write because its last dirty ring entry (again)
 *                   Resets the dirty ring which:
 *                     - Resets the A/D bits in EPT (again)
 *                     - Issues tlb flush (again) which is intercepted by L0
 *
 * ...
 *
 * N times
 *
 * - L1 vCPU:        Memory write is logged in the PML and then committed.
 *                   Lots of other memory writes are logged and committed.
 * ...
 *
 * - L1 test thread: Sees the memory write along with other memory writes
 *                   in the dirty ring, and since the write is usually not
 *                   the last entry in the dirty-ring and has a very outdated
 *                   iteration, the test fails.
 *
 *
 * Note that this is only possible when the write was the last log entry
 * write during iteration N-1, thus remember last iteration last log entry
 * and also don't fail when it is reported in the next iteration, together with
 * an outdated iteration count.
 */
static uint64_t dirty_ring_prev_iteration_last_page;

enum log_mode_t {
	/* Only use KVM_GET_DIRTY_LOG for logging */
	LOG_MODE_DIRTY_LOG = 0,

	/* Use both KVM_[GET|CLEAR]_DIRTY_LOG for logging */
	LOG_MODE_CLEAR_LOG = 1,

	/* Use dirty ring for logging */
	LOG_MODE_DIRTY_RING = 2,

	LOG_MODE_NUM,

	/* Run all supported modes */
	LOG_MODE_ALL = LOG_MODE_NUM,
};

/* Mode of logging to test.  Default is to run all supported modes */
static enum log_mode_t host_log_mode_option = LOG_MODE_ALL;
/* Logging mode for current run */
static enum log_mode_t host_log_mode;
static pthread_t vcpu_thread;
static uint32_t test_dirty_ring_count = TEST_DIRTY_RING_COUNT;

static bool clear_log_supported(void)
{
	return kvm_has_cap(KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2);
}

static void clear_log_create_vm_done(struct kvm_vm *vm)
{
	u64 manual_caps;

	manual_caps = kvm_check_cap(KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2);
	TEST_ASSERT(manual_caps, "MANUAL_CAPS is zero!");
	manual_caps &= (KVM_DIRTY_LOG_MANUAL_PROTECT_ENABLE |
			KVM_DIRTY_LOG_INITIALLY_SET);
	vm_enable_cap(vm, KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2, manual_caps);
}

static void dirty_log_collect_dirty_pages(struct kvm_vcpu *vcpu, int slot,
					  void *bitmap, uint32_t num_pages,
					  uint32_t *unused)
{
	kvm_vm_get_dirty_log(vcpu->vm, slot, bitmap);
}

static void clear_log_collect_dirty_pages(struct kvm_vcpu *vcpu, int slot,
					  void *bitmap, uint32_t num_pages,
					  uint32_t *unused)
{
	kvm_vm_get_dirty_log(vcpu->vm, slot, bitmap);
	kvm_vm_clear_dirty_log(vcpu->vm, slot, bitmap, 0, num_pages);
}

/* Should only be called after a GUEST_SYNC */
static void vcpu_handle_sync_stop(void)
{
	if (READ_ONCE(vcpu_stop)) {
		sem_post(&sem_vcpu_stop);
		sem_wait(&sem_vcpu_cont);
	}
}

static void default_after_vcpu_run(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	TEST_ASSERT(get_ucall(vcpu, NULL) == UCALL_SYNC,
		    "Invalid guest sync status: exit_reason=%s",
		    exit_reason_str(run->exit_reason));

	vcpu_handle_sync_stop();
}

static bool dirty_ring_supported(void)
{
	return (kvm_has_cap(KVM_CAP_DIRTY_LOG_RING) ||
		kvm_has_cap(KVM_CAP_DIRTY_LOG_RING_ACQ_REL));
}

static void dirty_ring_create_vm_done(struct kvm_vm *vm)
{
	uint64_t pages;
	uint32_t limit;

	/*
	 * We rely on vcpu exit due to full dirty ring state. Adjust
	 * the ring buffer size to ensure we're able to reach the
	 * full dirty ring state.
	 */
	pages = (1ul << (DIRTY_MEM_BITS - vm->page_shift)) + 3;
	pages = vm_adjust_num_guest_pages(vm->mode, pages);
	if (vm->page_size < getpagesize())
		pages = vm_num_host_pages(vm->mode, pages);

	limit = 1 << (31 - __builtin_clz(pages));
	test_dirty_ring_count = 1 << (31 - __builtin_clz(test_dirty_ring_count));
	test_dirty_ring_count = min(limit, test_dirty_ring_count);
	pr_info("dirty ring count: 0x%x\n", test_dirty_ring_count);

	/*
	 * Switch to dirty ring mode after VM creation but before any
	 * of the vcpu creation.
	 */
	vm_enable_dirty_ring(vm, test_dirty_ring_count *
			     sizeof(struct kvm_dirty_gfn));
}

static inline bool dirty_gfn_is_dirtied(struct kvm_dirty_gfn *gfn)
{
	return smp_load_acquire(&gfn->flags) == KVM_DIRTY_GFN_F_DIRTY;
}

static inline void dirty_gfn_set_collected(struct kvm_dirty_gfn *gfn)
{
	smp_store_release(&gfn->flags, KVM_DIRTY_GFN_F_RESET);
}

static uint32_t dirty_ring_collect_one(struct kvm_dirty_gfn *dirty_gfns,
				       int slot, void *bitmap,
				       uint32_t num_pages, uint32_t *fetch_index)
{
	struct kvm_dirty_gfn *cur;
	uint32_t count = 0;

	while (true) {
		cur = &dirty_gfns[*fetch_index % test_dirty_ring_count];
		if (!dirty_gfn_is_dirtied(cur))
			break;
		TEST_ASSERT(cur->slot == slot, "Slot number didn't match: "
			    "%u != %u", cur->slot, slot);
		TEST_ASSERT(cur->offset < num_pages, "Offset overflow: "
			    "0x%llx >= 0x%x", cur->offset, num_pages);
		__set_bit_le(cur->offset, bitmap);
		dirty_ring_last_page = cur->offset;
		dirty_gfn_set_collected(cur);
		(*fetch_index)++;
		count++;
	}

	return count;
}

static void dirty_ring_collect_dirty_pages(struct kvm_vcpu *vcpu, int slot,
					   void *bitmap, uint32_t num_pages,
					   uint32_t *ring_buf_idx)
{
	uint32_t count, cleared;

	/* Only have one vcpu */
	count = dirty_ring_collect_one(vcpu_map_dirty_ring(vcpu),
				       slot, bitmap, num_pages,
				       ring_buf_idx);

	cleared = kvm_vm_reset_dirty_ring(vcpu->vm);

	/*
	 * Cleared pages should be the same as collected, as KVM is supposed to
	 * clear only the entries that have been harvested.
	 */
	TEST_ASSERT(cleared == count, "Reset dirty pages (%u) mismatch "
		    "with collected (%u)", cleared, count);
}

static void dirty_ring_after_vcpu_run(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	/* A ucall-sync or ring-full event is allowed */
	if (get_ucall(vcpu, NULL) == UCALL_SYNC) {
		vcpu_handle_sync_stop();
	} else if (run->exit_reason == KVM_EXIT_DIRTY_RING_FULL) {
		WRITE_ONCE(dirty_ring_vcpu_ring_full, true);
		vcpu_handle_sync_stop();
	} else {
		TEST_ASSERT(false, "Invalid guest sync status: "
			    "exit_reason=%s",
			    exit_reason_str(run->exit_reason));
	}
}

struct log_mode {
	const char *name;
	/* Return true if this mode is supported, otherwise false */
	bool (*supported)(void);
	/* Hook when the vm creation is done (before vcpu creation) */
	void (*create_vm_done)(struct kvm_vm *vm);
	/* Hook to collect the dirty pages into the bitmap provided */
	void (*collect_dirty_pages) (struct kvm_vcpu *vcpu, int slot,
				     void *bitmap, uint32_t num_pages,
				     uint32_t *ring_buf_idx);
	/* Hook to call when after each vcpu run */
	void (*after_vcpu_run)(struct kvm_vcpu *vcpu);
} log_modes[LOG_MODE_NUM] = {
	{
		.name = "dirty-log",
		.collect_dirty_pages = dirty_log_collect_dirty_pages,
		.after_vcpu_run = default_after_vcpu_run,
	},
	{
		.name = "clear-log",
		.supported = clear_log_supported,
		.create_vm_done = clear_log_create_vm_done,
		.collect_dirty_pages = clear_log_collect_dirty_pages,
		.after_vcpu_run = default_after_vcpu_run,
	},
	{
		.name = "dirty-ring",
		.supported = dirty_ring_supported,
		.create_vm_done = dirty_ring_create_vm_done,
		.collect_dirty_pages = dirty_ring_collect_dirty_pages,
		.after_vcpu_run = dirty_ring_after_vcpu_run,
	},
};

static void log_modes_dump(void)
{
	int i;

	printf("all");
	for (i = 0; i < LOG_MODE_NUM; i++)
		printf(", %s", log_modes[i].name);
	printf("\n");
}

static bool log_mode_supported(void)
{
	struct log_mode *mode = &log_modes[host_log_mode];

	if (mode->supported)
		return mode->supported();

	return true;
}

static void log_mode_create_vm_done(struct kvm_vm *vm)
{
	struct log_mode *mode = &log_modes[host_log_mode];

	if (mode->create_vm_done)
		mode->create_vm_done(vm);
}

static void log_mode_collect_dirty_pages(struct kvm_vcpu *vcpu, int slot,
					 void *bitmap, uint32_t num_pages,
					 uint32_t *ring_buf_idx)
{
	struct log_mode *mode = &log_modes[host_log_mode];

	TEST_ASSERT(mode->collect_dirty_pages != NULL,
		    "collect_dirty_pages() is required for any log mode!");
	mode->collect_dirty_pages(vcpu, slot, bitmap, num_pages, ring_buf_idx);
}

static void log_mode_after_vcpu_run(struct kvm_vcpu *vcpu)
{
	struct log_mode *mode = &log_modes[host_log_mode];

	if (mode->after_vcpu_run)
		mode->after_vcpu_run(vcpu);
}

static void *vcpu_worker(void *data)
{
	struct kvm_vcpu *vcpu = data;

	sem_wait(&sem_vcpu_cont);

	while (!READ_ONCE(host_quit)) {
		/* Let the guest dirty the random pages */
		vcpu_run(vcpu);
		log_mode_after_vcpu_run(vcpu);
	}

	return NULL;
}

static void vm_dirty_log_verify(enum vm_guest_mode mode, unsigned long **bmap)
{
	uint64_t page, nr_dirty_pages = 0, nr_clean_pages = 0;
	uint64_t step = vm_num_host_pages(mode, 1);

	for (page = 0; page < host_num_pages; page += step) {
		uint64_t val = *(uint64_t *)(host_test_mem + page * host_page_size);
		bool bmap0_dirty = __test_and_clear_bit_le(page, bmap[0]);

		/*
		 * Ensure both bitmaps are cleared, as a page can be written
		 * multiple times per iteration, i.e. can show up in both
		 * bitmaps, and the dirty ring is additive, i.e. doesn't purge
		 * bitmap entries from previous collections.
		 */
		if (__test_and_clear_bit_le(page, bmap[1]) || bmap0_dirty) {
			nr_dirty_pages++;

			/*
			 * If the page is dirty, the value written to memory
			 * should be the current iteration number.
			 */
			if (val == iteration)
				continue;

			if (host_log_mode == LOG_MODE_DIRTY_RING) {
				/*
				 * The last page in the ring from previous
				 * iteration can be written with the value
				 * from the previous iteration, as the value to
				 * be written may be cached in a CPU register.
				 */
				if (page == dirty_ring_prev_iteration_last_page &&
				    val == iteration - 1)
					continue;

				/*
				 * Any value from a previous iteration is legal
				 * for the last entry, as the write may not yet
				 * have retired, i.e. the page may hold whatever
				 * it had before this iteration started.
				 */
				if (page == dirty_ring_last_page &&
				    val < iteration)
					continue;
			} else if (!val && iteration == 1 && bmap0_dirty) {
				/*
				 * When testing get+clear, the dirty bitmap
				 * starts with all bits set, and so the first
				 * iteration can observe a "dirty" page that
				 * was never written, but only in the first
				 * bitmap (collecting the bitmap also clears
				 * all dirty pages).
				 */
				continue;
			}

			TEST_FAIL("Dirty page %lu value (%lu) != iteration (%lu) "
				  "(last = %lu, prev_last = %lu)",
				  page, val, iteration, dirty_ring_last_page,
				  dirty_ring_prev_iteration_last_page);
		} else {
			nr_clean_pages++;
			/*
			 * If cleared, the value written can be any
			 * value smaller than the iteration number.
			 */
			TEST_ASSERT(val < iteration,
				    "Clear page %lu value (%lu) >= iteration (%lu) "
				    "(last = %lu, prev_last = %lu)",
				    page, val, iteration, dirty_ring_last_page,
				    dirty_ring_prev_iteration_last_page);
		}
	}

	pr_info("Iteration %2ld: dirty: %-6lu clean: %-6lu writes: %-6lu\n",
		iteration, nr_dirty_pages, nr_clean_pages, nr_writes);

	host_dirty_count += nr_dirty_pages;
	host_clear_count += nr_clean_pages;
}

static struct kvm_vm *create_vm(enum vm_guest_mode mode, struct kvm_vcpu **vcpu,
				uint64_t extra_mem_pages, void *guest_code)
{
	struct kvm_vm *vm;

	pr_info("Testing guest mode: %s\n", vm_guest_mode_string(mode));

	vm = __vm_create(VM_SHAPE(mode), 1, extra_mem_pages);

	log_mode_create_vm_done(vm);
	*vcpu = vm_vcpu_add(vm, 0, guest_code);
	return vm;
}

struct test_params {
	unsigned long iterations;
	unsigned long interval;
	uint64_t phys_offset;
};

static void run_test(enum vm_guest_mode mode, void *arg)
{
	struct test_params *p = arg;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	unsigned long *bmap[2];
	uint32_t ring_buf_idx = 0;
	int sem_val;

	if (!log_mode_supported()) {
		print_skip("Log mode '%s' not supported",
			   log_modes[host_log_mode].name);
		return;
	}

	/*
	 * We reserve page table for 2 times of extra dirty mem which
	 * will definitely cover the original (1G+) test range.  Here
	 * we do the calculation with 4K page size which is the
	 * smallest so the page number will be enough for all archs
	 * (e.g., 64K page size guest will need even less memory for
	 * page tables).
	 */
	vm = create_vm(mode, &vcpu,
		       2ul << (DIRTY_MEM_BITS - PAGE_SHIFT_4K), guest_code);

	guest_page_size = vm->page_size;
	/*
	 * A little more than 1G of guest page sized pages.  Cover the
	 * case where the size is not aligned to 64 pages.
	 */
	guest_num_pages = (1ul << (DIRTY_MEM_BITS - vm->page_shift)) + 3;
	guest_num_pages = vm_adjust_num_guest_pages(mode, guest_num_pages);

	host_page_size = getpagesize();
	host_num_pages = vm_num_host_pages(mode, guest_num_pages);

	if (!p->phys_offset) {
		guest_test_phys_mem = (vm->max_gfn - guest_num_pages) *
				      guest_page_size;
		guest_test_phys_mem = align_down(guest_test_phys_mem, host_page_size);
	} else {
		guest_test_phys_mem = p->phys_offset;
	}

#ifdef __s390x__
	/* Align to 1M (segment size) */
	guest_test_phys_mem = align_down(guest_test_phys_mem, 1 << 20);

	/*
	 * The workaround in guest_code() to write all pages prior to the first
	 * iteration isn't compatible with the dirty ring, as the dirty ring
	 * support relies on the vCPU to actually stop when vcpu_stop is set so
	 * that the vCPU doesn't hang waiting for the dirty ring to be emptied.
	 */
	TEST_ASSERT(host_log_mode != LOG_MODE_DIRTY_RING,
		    "Test needs to be updated to support s390 dirty ring");
#endif

	pr_info("guest physical test memory offset: 0x%lx\n", guest_test_phys_mem);

	bmap[0] = bitmap_zalloc(host_num_pages);
	bmap[1] = bitmap_zalloc(host_num_pages);

	/* Add an extra memory slot for testing dirty logging */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    guest_test_phys_mem,
				    TEST_MEM_SLOT_INDEX,
				    guest_num_pages,
				    KVM_MEM_LOG_DIRTY_PAGES);

	/* Do mapping for the dirty track memory slot */
	virt_map(vm, guest_test_virt_mem, guest_test_phys_mem, guest_num_pages);

	/* Cache the HVA pointer of the region */
	host_test_mem = addr_gpa2hva(vm, (vm_paddr_t)guest_test_phys_mem);

	/* Export the shared variables to the guest */
	sync_global_to_guest(vm, host_page_size);
	sync_global_to_guest(vm, guest_page_size);
	sync_global_to_guest(vm, guest_test_virt_mem);
	sync_global_to_guest(vm, guest_num_pages);

	host_dirty_count = 0;
	host_clear_count = 0;
	WRITE_ONCE(host_quit, false);

	/*
	 * Ensure the previous iteration didn't leave a dangling semaphore, i.e.
	 * that the main task and vCPU worker were synchronized and completed
	 * verification of all iterations.
	 */
	sem_getvalue(&sem_vcpu_stop, &sem_val);
	TEST_ASSERT_EQ(sem_val, 0);
	sem_getvalue(&sem_vcpu_cont, &sem_val);
	TEST_ASSERT_EQ(sem_val, 0);

	TEST_ASSERT_EQ(vcpu_stop, false);

	pthread_create(&vcpu_thread, NULL, vcpu_worker, vcpu);

	for (iteration = 1; iteration <= p->iterations; iteration++) {
		unsigned long i;

		sync_global_to_guest(vm, iteration);

		WRITE_ONCE(nr_writes, 0);
		sync_global_to_guest(vm, nr_writes);

		dirty_ring_prev_iteration_last_page = dirty_ring_last_page;
		WRITE_ONCE(dirty_ring_vcpu_ring_full, false);

		sem_post(&sem_vcpu_cont);

		/*
		 * Let the vCPU run beyond the configured interval until it has
		 * performed the minimum number of writes.  This verifies the
		 * guest is making forward progress, e.g. isn't stuck because
		 * of a KVM bug, and puts a firm floor on test coverage.
		 */
		for (i = 0; i < p->interval || nr_writes < TEST_MIN_WRITES_PER_ITERATION; i++) {
			/*
			 * Sleep in 1ms chunks to keep the interval math simple
			 * and so that the test doesn't run too far beyond the
			 * specified interval.
			 */
			usleep(1000);

			sync_global_from_guest(vm, nr_writes);

			/*
			 * Reap dirty pages while the guest is running so that
			 * dirty ring full events are resolved, i.e. so that a
			 * larger interval doesn't always end up with a vCPU
			 * that's effectively blocked.  Collecting while the
			 * guest is running also verifies KVM doesn't lose any
			 * state.
			 *
			 * For bitmap modes, KVM overwrites the entire bitmap,
			 * i.e. collecting the bitmaps is destructive.  Collect
			 * the bitmap only on the first pass, otherwise this
			 * test would lose track of dirty pages.
			 */
			if (i && host_log_mode != LOG_MODE_DIRTY_RING)
				continue;

			/*
			 * For the dirty ring, empty the ring on subsequent
			 * passes only if the ring was filled at least once,
			 * to verify KVM's handling of a full ring (emptying
			 * the ring on every pass would make it unlikely the
			 * vCPU would ever fill the fing).
			 */
			if (i && !READ_ONCE(dirty_ring_vcpu_ring_full))
				continue;

			log_mode_collect_dirty_pages(vcpu, TEST_MEM_SLOT_INDEX,
						     bmap[0], host_num_pages,
						     &ring_buf_idx);
		}

		/*
		 * Stop the vCPU prior to collecting and verifying the dirty
		 * log.  If the vCPU is allowed to run during collection, then
		 * pages that are written during this iteration may be missed,
		 * i.e. collected in the next iteration.  And if the vCPU is
		 * writing memory during verification, pages that this thread
		 * sees as clean may be written with this iteration's value.
		 */
		WRITE_ONCE(vcpu_stop, true);
		sync_global_to_guest(vm, vcpu_stop);
		sem_wait(&sem_vcpu_stop);

		/*
		 * Clear vcpu_stop after the vCPU thread has acknowledge the
		 * stop request and is waiting, i.e. is definitely not running!
		 */
		WRITE_ONCE(vcpu_stop, false);
		sync_global_to_guest(vm, vcpu_stop);

		/*
		 * Sync the number of writes performed before verification, the
		 * info will be printed along with the dirty/clean page counts.
		 */
		sync_global_from_guest(vm, nr_writes);

		/*
		 * NOTE: for dirty ring, it's possible that we didn't stop at
		 * GUEST_SYNC but instead we stopped because ring is full;
		 * that's okay too because ring full means we're only missing
		 * the flush of the last page, and since we handle the last
		 * page specially verification will succeed anyway.
		 */
		log_mode_collect_dirty_pages(vcpu, TEST_MEM_SLOT_INDEX,
					     bmap[1], host_num_pages,
					     &ring_buf_idx);
		vm_dirty_log_verify(mode, bmap);
	}

	WRITE_ONCE(host_quit, true);
	sem_post(&sem_vcpu_cont);

	pthread_join(vcpu_thread, NULL);

	pr_info("Total bits checked: dirty (%lu), clear (%lu)\n",
		host_dirty_count, host_clear_count);

	free(bmap[0]);
	free(bmap[1]);
	kvm_vm_free(vm);
}

static void help(char *name)
{
	puts("");
	printf("usage: %s [-h] [-i iterations] [-I interval] "
	       "[-p offset] [-m mode]\n", name);
	puts("");
	printf(" -c: hint to dirty ring size, in number of entries\n");
	printf("     (only useful for dirty-ring test; default: %"PRIu32")\n",
	       TEST_DIRTY_RING_COUNT);
	printf(" -i: specify iteration counts (default: %"PRIu64")\n",
	       TEST_HOST_LOOP_N);
	printf(" -I: specify interval in ms (default: %"PRIu64" ms)\n",
	       TEST_HOST_LOOP_INTERVAL);
	printf(" -p: specify guest physical test memory offset\n"
	       "     Warning: a low offset can conflict with the loaded test code.\n");
	printf(" -M: specify the host logging mode "
	       "(default: run all log modes).  Supported modes: \n\t");
	log_modes_dump();
	guest_modes_help();
	puts("");
	exit(0);
}

int main(int argc, char *argv[])
{
	struct test_params p = {
		.iterations = TEST_HOST_LOOP_N,
		.interval = TEST_HOST_LOOP_INTERVAL,
	};
	int opt, i;

	sem_init(&sem_vcpu_stop, 0, 0);
	sem_init(&sem_vcpu_cont, 0, 0);

	guest_modes_append_default();

	while ((opt = getopt(argc, argv, "c:hi:I:p:m:M:")) != -1) {
		switch (opt) {
		case 'c':
			test_dirty_ring_count = strtol(optarg, NULL, 10);
			break;
		case 'i':
			p.iterations = strtol(optarg, NULL, 10);
			break;
		case 'I':
			p.interval = strtol(optarg, NULL, 10);
			break;
		case 'p':
			p.phys_offset = strtoull(optarg, NULL, 0);
			break;
		case 'm':
			guest_modes_cmdline(optarg);
			break;
		case 'M':
			if (!strcmp(optarg, "all")) {
				host_log_mode_option = LOG_MODE_ALL;
				break;
			}
			for (i = 0; i < LOG_MODE_NUM; i++) {
				if (!strcmp(optarg, log_modes[i].name)) {
					pr_info("Setting log mode to: '%s'\n",
						optarg);
					host_log_mode_option = i;
					break;
				}
			}
			if (i == LOG_MODE_NUM) {
				printf("Log mode '%s' invalid. Please choose "
				       "from: ", optarg);
				log_modes_dump();
				exit(1);
			}
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	TEST_ASSERT(p.iterations > 0, "Iterations must be greater than zero");
	TEST_ASSERT(p.interval > 0, "Interval must be greater than zero");

	pr_info("Test iterations: %"PRIu64", interval: %"PRIu64" (ms)\n",
		p.iterations, p.interval);

	if (host_log_mode_option == LOG_MODE_ALL) {
		/* Run each log mode */
		for (i = 0; i < LOG_MODE_NUM; i++) {
			pr_info("Testing Log Mode '%s'\n", log_modes[i].name);
			host_log_mode = i;
			for_each_guest_mode(run_test, &p);
		}
	} else {
		host_log_mode = host_log_mode_option;
		for_each_guest_mode(run_test, &p);
	}

	return 0;
}
