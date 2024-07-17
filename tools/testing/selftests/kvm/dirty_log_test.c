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

/* How many pages to dirty for each guest loop */
#define TEST_PAGES_PER_LOOP		1024

/* How many host loops to run (one KVM_GET_DIRTY_LOG for each loop) */
#define TEST_HOST_LOOP_N		32UL

/* Interval for each host loop (ms) */
#define TEST_HOST_LOOP_INTERVAL		10UL

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
	int i;

	/*
	 * On s390x, all pages of a 1M segment are initially marked as dirty
	 * when a page of the segment is written to for the very first time.
	 * To compensate this specialty in this test, we need to touch all
	 * pages during the first iteration.
	 */
	for (i = 0; i < guest_num_pages; i++) {
		addr = guest_test_virt_mem + i * guest_page_size;
		vcpu_arch_put_guest(*(uint64_t *)addr, READ_ONCE(iteration));
	}

	while (true) {
		for (i = 0; i < TEST_PAGES_PER_LOOP; i++) {
			addr = guest_test_virt_mem;
			addr += (guest_random_u64(&guest_rng) % guest_num_pages)
				* guest_page_size;
			addr = align_down(addr, host_page_size);

			vcpu_arch_put_guest(*(uint64_t *)addr, READ_ONCE(iteration));
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
static uint64_t host_track_next_count;

/* Whether dirty ring reset is requested, or finished */
static sem_t sem_vcpu_stop;
static sem_t sem_vcpu_cont;
/*
 * This is only set by main thread, and only cleared by vcpu thread.  It is
 * used to request vcpu thread to stop at the next GUEST_SYNC, since GUEST_SYNC
 * is the only place that we'll guarantee both "dirty bit" and "dirty data"
 * will match.  E.g., SIG_IPI won't guarantee that if the vcpu is interrupted
 * after setting dirty bit but before the data is written.
 */
static atomic_t vcpu_sync_stop_requested;
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
static uint64_t dirty_ring_last_page;

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

static void vcpu_kick(void)
{
	pthread_kill(vcpu_thread, SIG_IPI);
}

/*
 * In our test we do signal tricks, let's use a better version of
 * sem_wait to avoid signal interrupts
 */
static void sem_wait_until(sem_t *sem)
{
	int ret;

	do
		ret = sem_wait(sem);
	while (ret == -1 && errno == EINTR);
}

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
	if (atomic_read(&vcpu_sync_stop_requested)) {
		/* It means main thread is sleeping waiting */
		atomic_set(&vcpu_sync_stop_requested, false);
		sem_post(&sem_vcpu_stop);
		sem_wait_until(&sem_vcpu_cont);
	}
}

static void default_after_vcpu_run(struct kvm_vcpu *vcpu, int ret, int err)
{
	struct kvm_run *run = vcpu->run;

	TEST_ASSERT(ret == 0 || (ret == -1 && err == EINTR),
		    "vcpu run failed: errno=%d", err);

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
		//pr_info("fetch 0x%x page %llu\n", *fetch_index, cur->offset);
		__set_bit_le(cur->offset, bitmap);
		dirty_ring_last_page = cur->offset;
		dirty_gfn_set_collected(cur);
		(*fetch_index)++;
		count++;
	}

	return count;
}

static void dirty_ring_wait_vcpu(void)
{
	/* This makes sure that hardware PML cache flushed */
	vcpu_kick();
	sem_wait_until(&sem_vcpu_stop);
}

static void dirty_ring_continue_vcpu(void)
{
	pr_info("Notifying vcpu to continue\n");
	sem_post(&sem_vcpu_cont);
}

static void dirty_ring_collect_dirty_pages(struct kvm_vcpu *vcpu, int slot,
					   void *bitmap, uint32_t num_pages,
					   uint32_t *ring_buf_idx)
{
	uint32_t count = 0, cleared;
	bool continued_vcpu = false;

	dirty_ring_wait_vcpu();

	if (!dirty_ring_vcpu_ring_full) {
		/*
		 * This is not a ring-full event, it's safe to allow
		 * vcpu to continue
		 */
		dirty_ring_continue_vcpu();
		continued_vcpu = true;
	}

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

	if (!continued_vcpu) {
		TEST_ASSERT(dirty_ring_vcpu_ring_full,
			    "Didn't continue vcpu even without ring full");
		dirty_ring_continue_vcpu();
	}

	pr_info("Iteration %ld collected %u pages\n", iteration, count);
}

static void dirty_ring_after_vcpu_run(struct kvm_vcpu *vcpu, int ret, int err)
{
	struct kvm_run *run = vcpu->run;

	/* A ucall-sync or ring-full event is allowed */
	if (get_ucall(vcpu, NULL) == UCALL_SYNC) {
		/* We should allow this to continue */
		;
	} else if (run->exit_reason == KVM_EXIT_DIRTY_RING_FULL ||
		   (ret == -1 && err == EINTR)) {
		/* Update the flag first before pause */
		WRITE_ONCE(dirty_ring_vcpu_ring_full,
			   run->exit_reason == KVM_EXIT_DIRTY_RING_FULL);
		sem_post(&sem_vcpu_stop);
		pr_info("vcpu stops because %s...\n",
			dirty_ring_vcpu_ring_full ?
			"dirty ring is full" : "vcpu is kicked out");
		sem_wait_until(&sem_vcpu_cont);
		pr_info("vcpu continues now.\n");
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
	void (*after_vcpu_run)(struct kvm_vcpu *vcpu, int ret, int err);
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

/*
 * We use this bitmap to track some pages that should have its dirty
 * bit set in the _next_ iteration.  For example, if we detected the
 * page value changed to current iteration but at the same time the
 * page bit is cleared in the latest bitmap, then the system must
 * report that write in the next get dirty log call.
 */
static unsigned long *host_bmap_track;

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

static void log_mode_after_vcpu_run(struct kvm_vcpu *vcpu, int ret, int err)
{
	struct log_mode *mode = &log_modes[host_log_mode];

	if (mode->after_vcpu_run)
		mode->after_vcpu_run(vcpu, ret, err);
}

static void *vcpu_worker(void *data)
{
	int ret;
	struct kvm_vcpu *vcpu = data;
	uint64_t pages_count = 0;
	struct kvm_signal_mask *sigmask = alloca(offsetof(struct kvm_signal_mask, sigset)
						 + sizeof(sigset_t));
	sigset_t *sigset = (sigset_t *) &sigmask->sigset;

	/*
	 * SIG_IPI is unblocked atomically while in KVM_RUN.  It causes the
	 * ioctl to return with -EINTR, but it is still pending and we need
	 * to accept it with the sigwait.
	 */
	sigmask->len = 8;
	pthread_sigmask(0, NULL, sigset);
	sigdelset(sigset, SIG_IPI);
	vcpu_ioctl(vcpu, KVM_SET_SIGNAL_MASK, sigmask);

	sigemptyset(sigset);
	sigaddset(sigset, SIG_IPI);

	while (!READ_ONCE(host_quit)) {
		/* Clear any existing kick signals */
		pages_count += TEST_PAGES_PER_LOOP;
		/* Let the guest dirty the random pages */
		ret = __vcpu_run(vcpu);
		if (ret == -1 && errno == EINTR) {
			int sig = -1;
			sigwait(sigset, &sig);
			assert(sig == SIG_IPI);
		}
		log_mode_after_vcpu_run(vcpu, ret, errno);
	}

	pr_info("Dirtied %"PRIu64" pages\n", pages_count);

	return NULL;
}

static void vm_dirty_log_verify(enum vm_guest_mode mode, unsigned long *bmap)
{
	uint64_t step = vm_num_host_pages(mode, 1);
	uint64_t page;
	uint64_t *value_ptr;
	uint64_t min_iter = 0;

	for (page = 0; page < host_num_pages; page += step) {
		value_ptr = host_test_mem + page * host_page_size;

		/* If this is a special page that we were tracking... */
		if (__test_and_clear_bit_le(page, host_bmap_track)) {
			host_track_next_count++;
			TEST_ASSERT(test_bit_le(page, bmap),
				    "Page %"PRIu64" should have its dirty bit "
				    "set in this iteration but it is missing",
				    page);
		}

		if (__test_and_clear_bit_le(page, bmap)) {
			bool matched;

			host_dirty_count++;

			/*
			 * If the bit is set, the value written onto
			 * the corresponding page should be either the
			 * previous iteration number or the current one.
			 */
			matched = (*value_ptr == iteration ||
				   *value_ptr == iteration - 1);

			if (host_log_mode == LOG_MODE_DIRTY_RING && !matched) {
				if (*value_ptr == iteration - 2 && min_iter <= iteration - 2) {
					/*
					 * Short answer: this case is special
					 * only for dirty ring test where the
					 * page is the last page before a kvm
					 * dirty ring full in iteration N-2.
					 *
					 * Long answer: Assuming ring size R,
					 * one possible condition is:
					 *
					 *      main thr       vcpu thr
					 *      --------       --------
					 *    iter=1
					 *                   write 1 to page 0~(R-1)
					 *                   full, vmexit
					 *    collect 0~(R-1)
					 *    kick vcpu
					 *                   write 1 to (R-1)~(2R-2)
					 *                   full, vmexit
					 *    iter=2
					 *    collect (R-1)~(2R-2)
					 *    kick vcpu
					 *                   write 1 to (2R-2)
					 *                   (NOTE!!! "1" cached in cpu reg)
					 *                   write 2 to (2R-1)~(3R-3)
					 *                   full, vmexit
					 *    iter=3
					 *    collect (2R-2)~(3R-3)
					 *    (here if we read value on page
					 *     "2R-2" is 1, while iter=3!!!)
					 *
					 * This however can only happen once per iteration.
					 */
					min_iter = iteration - 1;
					continue;
				} else if (page == dirty_ring_last_page) {
					/*
					 * Please refer to comments in
					 * dirty_ring_last_page.
					 */
					continue;
				}
			}

			TEST_ASSERT(matched,
				    "Set page %"PRIu64" value %"PRIu64
				    " incorrect (iteration=%"PRIu64")",
				    page, *value_ptr, iteration);
		} else {
			host_clear_count++;
			/*
			 * If cleared, the value written can be any
			 * value smaller or equals to the iteration
			 * number.  Note that the value can be exactly
			 * (iteration-1) if that write can happen
			 * like this:
			 *
			 * (1) increase loop count to "iteration-1"
			 * (2) write to page P happens (with value
			 *     "iteration-1")
			 * (3) get dirty log for "iteration-1"; we'll
			 *     see that page P bit is set (dirtied),
			 *     and not set the bit in host_bmap_track
			 * (4) increase loop count to "iteration"
			 *     (which is current iteration)
			 * (5) get dirty log for current iteration,
			 *     we'll see that page P is cleared, with
			 *     value "iteration-1".
			 */
			TEST_ASSERT(*value_ptr <= iteration,
				    "Clear page %"PRIu64" value %"PRIu64
				    " incorrect (iteration=%"PRIu64")",
				    page, *value_ptr, iteration);
			if (*value_ptr == iteration) {
				/*
				 * This page is _just_ modified; it
				 * should report its dirtyness in the
				 * next run
				 */
				__set_bit_le(page, host_bmap_track);
			}
		}
	}
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
	unsigned long *bmap;
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
#endif

	pr_info("guest physical test memory offset: 0x%lx\n", guest_test_phys_mem);

	bmap = bitmap_zalloc(host_num_pages);
	host_bmap_track = bitmap_zalloc(host_num_pages);

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

	/* Start the iterations */
	iteration = 1;
	sync_global_to_guest(vm, iteration);
	WRITE_ONCE(host_quit, false);
	host_dirty_count = 0;
	host_clear_count = 0;
	host_track_next_count = 0;
	WRITE_ONCE(dirty_ring_vcpu_ring_full, false);

	/*
	 * Ensure the previous iteration didn't leave a dangling semaphore, i.e.
	 * that the main task and vCPU worker were synchronized and completed
	 * verification of all iterations.
	 */
	sem_getvalue(&sem_vcpu_stop, &sem_val);
	TEST_ASSERT_EQ(sem_val, 0);
	sem_getvalue(&sem_vcpu_cont, &sem_val);
	TEST_ASSERT_EQ(sem_val, 0);

	pthread_create(&vcpu_thread, NULL, vcpu_worker, vcpu);

	while (iteration < p->iterations) {
		/* Give the vcpu thread some time to dirty some pages */
		usleep(p->interval * 1000);
		log_mode_collect_dirty_pages(vcpu, TEST_MEM_SLOT_INDEX,
					     bmap, host_num_pages,
					     &ring_buf_idx);

		/*
		 * See vcpu_sync_stop_requested definition for details on why
		 * we need to stop vcpu when verify data.
		 */
		atomic_set(&vcpu_sync_stop_requested, true);
		sem_wait_until(&sem_vcpu_stop);
		/*
		 * NOTE: for dirty ring, it's possible that we didn't stop at
		 * GUEST_SYNC but instead we stopped because ring is full;
		 * that's okay too because ring full means we're only missing
		 * the flush of the last page, and since we handle the last
		 * page specially verification will succeed anyway.
		 */
		assert(host_log_mode == LOG_MODE_DIRTY_RING ||
		       atomic_read(&vcpu_sync_stop_requested) == false);
		vm_dirty_log_verify(mode, bmap);

		/*
		 * Set host_quit before sem_vcpu_cont in the final iteration to
		 * ensure that the vCPU worker doesn't resume the guest.  As
		 * above, the dirty ring test may stop and wait even when not
		 * explicitly request to do so, i.e. would hang waiting for a
		 * "continue" if it's allowed to resume the guest.
		 */
		if (++iteration == p->iterations)
			WRITE_ONCE(host_quit, true);

		sem_post(&sem_vcpu_cont);
		sync_global_to_guest(vm, iteration);
	}

	pthread_join(vcpu_thread, NULL);

	pr_info("Total bits checked: dirty (%"PRIu64"), clear (%"PRIu64"), "
		"track_next (%"PRIu64")\n", host_dirty_count, host_clear_count,
		host_track_next_count);

	free(bmap);
	free(host_bmap_track);
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
	sigset_t sigset;

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

	TEST_ASSERT(p.iterations > 2, "Iterations must be greater than two");
	TEST_ASSERT(p.interval > 0, "Interval must be greater than zero");

	pr_info("Test iterations: %"PRIu64", interval: %"PRIu64" (ms)\n",
		p.iterations, p.interval);

	srandom(time(0));

	/* Ensure that vCPU threads start with SIG_IPI blocked.  */
	sigemptyset(&sigset);
	sigaddset(&sigset, SIG_IPI);
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);

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
