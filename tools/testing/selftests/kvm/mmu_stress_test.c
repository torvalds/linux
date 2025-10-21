// SPDX-License-Identifier: GPL-2.0
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
#include <linux/sizes.h>

#include "kvm_util.h"
#include "test_util.h"
#include "guest_modes.h"
#include "processor.h"
#include "ucall_common.h"

static bool mprotect_ro_done;
static bool all_vcpus_hit_ro_fault;

static void guest_code(uint64_t start_gpa, uint64_t end_gpa, uint64_t stride)
{
	uint64_t gpa;
	int i;

	for (i = 0; i < 2; i++) {
		for (gpa = start_gpa; gpa < end_gpa; gpa += stride)
			vcpu_arch_put_guest(*((volatile uint64_t *)gpa), gpa);
		GUEST_SYNC(i);
	}

	for (gpa = start_gpa; gpa < end_gpa; gpa += stride)
		*((volatile uint64_t *)gpa);
	GUEST_SYNC(2);

	/*
	 * Write to the region while mprotect(PROT_READ) is underway.  Keep
	 * looping until the memory is guaranteed to be read-only and a fault
	 * has occurred, otherwise vCPUs may complete their writes and advance
	 * to the next stage prematurely.
	 *
	 * For architectures that support skipping the faulting instruction,
	 * generate the store via inline assembly to ensure the exact length
	 * of the instruction is known and stable (vcpu_arch_put_guest() on
	 * fixed-length architectures should work, but the cost of paranoia
	 * is low in this case).  For x86, hand-code the exact opcode so that
	 * there is no room for variability in the generated instruction.
	 */
	do {
		for (gpa = start_gpa; gpa < end_gpa; gpa += stride)
#ifdef __x86_64__
			asm volatile(".byte 0x48,0x89,0x00" :: "a"(gpa) : "memory"); /* mov %rax, (%rax) */
#elif defined(__aarch64__)
			asm volatile("str %0, [%0]" :: "r" (gpa) : "memory");
#else
			vcpu_arch_put_guest(*((volatile uint64_t *)gpa), gpa);
#endif
	} while (!READ_ONCE(mprotect_ro_done) || !READ_ONCE(all_vcpus_hit_ro_fault));

	/*
	 * Only architectures that write the entire range can explicitly sync,
	 * as other architectures will be stuck on the write fault.
	 */
#if defined(__x86_64__) || defined(__aarch64__)
	GUEST_SYNC(3);
#endif

	for (gpa = start_gpa; gpa < end_gpa; gpa += stride)
		vcpu_arch_put_guest(*((volatile uint64_t *)gpa), gpa);
	GUEST_SYNC(4);

	GUEST_ASSERT(0);
}

struct vcpu_info {
	struct kvm_vcpu *vcpu;
	uint64_t start_gpa;
	uint64_t end_gpa;
};

static int nr_vcpus;
static atomic_t rendezvous;
static atomic_t nr_ro_faults;

static void rendezvous_with_boss(void)
{
	int orig = atomic_read(&rendezvous);

	if (orig > 0) {
		atomic_dec_and_test(&rendezvous);
		while (atomic_read(&rendezvous) > 0)
			cpu_relax();
	} else {
		atomic_inc(&rendezvous);
		while (atomic_read(&rendezvous) < 0)
			cpu_relax();
	}
}

static void assert_sync_stage(struct kvm_vcpu *vcpu, int stage)
{
	struct ucall uc;

	TEST_ASSERT_EQ(get_ucall(vcpu, &uc), UCALL_SYNC);
	TEST_ASSERT_EQ(uc.args[1], stage);
}

static void run_vcpu(struct kvm_vcpu *vcpu, int stage)
{
	vcpu_run(vcpu);
	assert_sync_stage(vcpu, stage);
}

static void *vcpu_worker(void *data)
{
	struct kvm_sregs __maybe_unused sregs;
	struct vcpu_info *info = data;
	struct kvm_vcpu *vcpu = info->vcpu;
	struct kvm_vm *vm = vcpu->vm;
	int r;

	vcpu_args_set(vcpu, 3, info->start_gpa, info->end_gpa, vm->page_size);

	rendezvous_with_boss();

	/* Stage 0, write all of guest memory. */
	run_vcpu(vcpu, 0);
	rendezvous_with_boss();
#ifdef __x86_64__
	vcpu_sregs_get(vcpu, &sregs);
	/* Toggle CR0.WP to trigger a MMU context reset. */
	sregs.cr0 ^= X86_CR0_WP;
	vcpu_sregs_set(vcpu, &sregs);
#endif
	rendezvous_with_boss();

	/* Stage 1, re-write all of guest memory. */
	run_vcpu(vcpu, 1);
	rendezvous_with_boss();

	/* Stage 2, read all of guest memory, which is now read-only. */
	run_vcpu(vcpu, 2);

	/*
	 * Stage 3, write guest memory and verify KVM returns -EFAULT for once
	 * the mprotect(PROT_READ) lands.  Only architectures that support
	 * validating *all* of guest memory sync for this stage, as vCPUs will
	 * be stuck on the faulting instruction for other architectures.  Go to
	 * stage 3 without a rendezvous
	 */
	r = _vcpu_run(vcpu);
	TEST_ASSERT(r == -1 && errno == EFAULT,
		    "Expected EFAULT on write to RO memory, got r = %d, errno = %d", r, errno);

	atomic_inc(&nr_ro_faults);
	if (atomic_read(&nr_ro_faults) == nr_vcpus) {
		WRITE_ONCE(all_vcpus_hit_ro_fault, true);
		sync_global_to_guest(vm, all_vcpus_hit_ro_fault);
	}

#if defined(__x86_64__) || defined(__aarch64__)
	/*
	 * Verify *all* writes from the guest hit EFAULT due to the VMA now
	 * being read-only.  x86 and arm64 only at this time as skipping the
	 * instruction that hits the EFAULT requires advancing the program
	 * counter, which is arch specific and relies on inline assembly.
	 */
#ifdef __x86_64__
	vcpu->run->kvm_valid_regs = KVM_SYNC_X86_REGS;
#endif
	for (;;) {
		r = _vcpu_run(vcpu);
		if (!r)
			break;
		TEST_ASSERT_EQ(errno, EFAULT);
#if defined(__x86_64__)
		WRITE_ONCE(vcpu->run->kvm_dirty_regs, KVM_SYNC_X86_REGS);
		vcpu->run->s.regs.regs.rip += 3;
#elif defined(__aarch64__)
		vcpu_set_reg(vcpu, ARM64_CORE_REG(regs.pc),
			     vcpu_get_reg(vcpu, ARM64_CORE_REG(regs.pc)) + 4);
#endif

	}
	assert_sync_stage(vcpu, 3);
#endif /* __x86_64__ || __aarch64__ */
	rendezvous_with_boss();

	/*
	 * Stage 4.  Run to completion, waiting for mprotect(PROT_WRITE) to
	 * make the memory writable again.
	 */
	do {
		r = _vcpu_run(vcpu);
	} while (r && errno == EFAULT);
	TEST_ASSERT_EQ(r, 0);
	assert_sync_stage(vcpu, 4);
	rendezvous_with_boss();

	return NULL;
}

static pthread_t *spawn_workers(struct kvm_vm *vm, struct kvm_vcpu **vcpus,
				uint64_t start_gpa, uint64_t end_gpa)
{
	struct vcpu_info *info;
	uint64_t gpa, nr_bytes;
	pthread_t *threads;
	int i;

	threads = malloc(nr_vcpus * sizeof(*threads));
	TEST_ASSERT(threads, "Failed to allocate vCPU threads");

	info = malloc(nr_vcpus * sizeof(*info));
	TEST_ASSERT(info, "Failed to allocate vCPU gpa ranges");

	nr_bytes = ((end_gpa - start_gpa) / nr_vcpus) &
			~((uint64_t)vm->page_size - 1);
	TEST_ASSERT(nr_bytes, "C'mon, no way you have %d CPUs", nr_vcpus);

	for (i = 0, gpa = start_gpa; i < nr_vcpus; i++, gpa += nr_bytes) {
		info[i].vcpu = vcpus[i];
		info[i].start_gpa = gpa;
		info[i].end_gpa = gpa + nr_bytes;
		pthread_create(&threads[i], NULL, vcpu_worker, &info[i]);
	}
	return threads;
}

static void rendezvous_with_vcpus(struct timespec *time, const char *name)
{
	int i, rendezvoused;

	pr_info("Waiting for vCPUs to finish %s...\n", name);

	rendezvoused = atomic_read(&rendezvous);
	for (i = 0; abs(rendezvoused) != 1; i++) {
		usleep(100);
		if (!(i & 0x3f))
			pr_info("\r%d vCPUs haven't rendezvoused...",
				abs(rendezvoused) - 1);
		rendezvoused = atomic_read(&rendezvous);
	}

	clock_gettime(CLOCK_MONOTONIC, time);

	/* Release the vCPUs after getting the time of the previous action. */
	pr_info("\rAll vCPUs finished %s, releasing...\n", name);
	if (rendezvoused > 0)
		atomic_set(&rendezvous, -nr_vcpus - 1);
	else
		atomic_set(&rendezvous, nr_vcpus + 1);
}

static void calc_default_nr_vcpus(void)
{
	cpu_set_t possible_mask;
	int r;

	r = sched_getaffinity(0, sizeof(possible_mask), &possible_mask);
	TEST_ASSERT(!r, "sched_getaffinity failed, errno = %d (%s)",
		    errno, strerror(errno));

	nr_vcpus = CPU_COUNT(&possible_mask) * 3/4;
	TEST_ASSERT(nr_vcpus > 0, "Uh, no CPUs?");
}

int main(int argc, char *argv[])
{
	/*
	 * Skip the first 4gb and slot0.  slot0 maps <1gb and is used to back
	 * the guest's code, stack, and page tables.  Because selftests creates
	 * an IRQCHIP, a.k.a. a local APIC, KVM creates an internal memslot
	 * just below the 4gb boundary.  This test could create memory at
	 * 1gb-3gb,but it's simpler to skip straight to 4gb.
	 */
	const uint64_t start_gpa = SZ_4G;
	const int first_slot = 1;

	struct timespec time_start, time_run1, time_reset, time_run2, time_ro, time_rw;
	uint64_t max_gpa, gpa, slot_size, max_mem, i;
	int max_slots, slot, opt, fd;
	bool hugepages = false;
	struct kvm_vcpu **vcpus;
	pthread_t *threads;
	struct kvm_vm *vm;
	void *mem;

	/*
	 * Default to 2gb so that maxing out systems with MAXPHADDR=46, which
	 * are quite common for x86, requires changing only max_mem (KVM allows
	 * 32k memslots, 32k * 2gb == ~64tb of guest memory).
	 */
	slot_size = SZ_2G;

	max_slots = kvm_check_cap(KVM_CAP_NR_MEMSLOTS);
	TEST_ASSERT(max_slots > first_slot, "KVM is broken");

	/* All KVM MMUs should be able to survive a 128gb guest. */
	max_mem = 128ull * SZ_1G;

	calc_default_nr_vcpus();

	while ((opt = getopt(argc, argv, "c:h:m:s:H")) != -1) {
		switch (opt) {
		case 'c':
			nr_vcpus = atoi_positive("Number of vCPUs", optarg);
			break;
		case 'm':
			max_mem = 1ull * atoi_positive("Memory size", optarg) * SZ_1G;
			break;
		case 's':
			slot_size = 1ull * atoi_positive("Slot size", optarg) * SZ_1G;
			break;
		case 'H':
			hugepages = true;
			break;
		case 'h':
		default:
			printf("usage: %s [-c nr_vcpus] [-m max_mem_in_gb] [-s slot_size_in_gb] [-H]\n", argv[0]);
			exit(1);
		}
	}

	vcpus = malloc(nr_vcpus * sizeof(*vcpus));
	TEST_ASSERT(vcpus, "Failed to allocate vCPU array");

	vm = __vm_create_with_vcpus(VM_SHAPE_DEFAULT, nr_vcpus,
#ifdef __x86_64__
				    max_mem / SZ_1G,
#else
				    max_mem / vm_guest_mode_params[VM_MODE_DEFAULT].page_size,
#endif
				    guest_code, vcpus);

	max_gpa = vm->max_gfn << vm->page_shift;
	TEST_ASSERT(max_gpa > (4 * slot_size), "MAXPHYADDR <4gb ");

	fd = kvm_memfd_alloc(slot_size, hugepages);
	mem = kvm_mmap(slot_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd);

	TEST_ASSERT(!madvise(mem, slot_size, MADV_NOHUGEPAGE), "madvise() failed");

	/* Pre-fault the memory to avoid taking mmap_sem on guest page faults. */
	for (i = 0; i < slot_size; i += vm->page_size)
		((uint8_t *)mem)[i] = 0xaa;

	gpa = 0;
	for (slot = first_slot; slot < max_slots; slot++) {
		gpa = start_gpa + ((slot - first_slot) * slot_size);
		if (gpa + slot_size > max_gpa)
			break;

		if ((gpa - start_gpa) >= max_mem)
			break;

		vm_set_user_memory_region(vm, slot, 0, gpa, slot_size, mem);

#ifdef __x86_64__
		/* Identity map memory in the guest using 1gb pages. */
		for (i = 0; i < slot_size; i += SZ_1G)
			__virt_pg_map(vm, gpa + i, gpa + i, PG_LEVEL_1G);
#else
		for (i = 0; i < slot_size; i += vm->page_size)
			virt_pg_map(vm, gpa + i, gpa + i);
#endif
	}

	atomic_set(&rendezvous, nr_vcpus + 1);
	threads = spawn_workers(vm, vcpus, start_gpa, gpa);

	free(vcpus);
	vcpus = NULL;

	pr_info("Running with %lugb of guest memory and %u vCPUs\n",
		(gpa - start_gpa) / SZ_1G, nr_vcpus);

	rendezvous_with_vcpus(&time_start, "spawning");
	rendezvous_with_vcpus(&time_run1, "run 1");
	rendezvous_with_vcpus(&time_reset, "reset");
	rendezvous_with_vcpus(&time_run2, "run 2");

	mprotect(mem, slot_size, PROT_READ);
	mprotect_ro_done = true;
	sync_global_to_guest(vm, mprotect_ro_done);

	rendezvous_with_vcpus(&time_ro, "mprotect RO");
	mprotect(mem, slot_size, PROT_READ | PROT_WRITE);
	rendezvous_with_vcpus(&time_rw, "mprotect RW");

	time_rw    = timespec_sub(time_rw,     time_ro);
	time_ro    = timespec_sub(time_ro,     time_run2);
	time_run2  = timespec_sub(time_run2,   time_reset);
	time_reset = timespec_sub(time_reset,  time_run1);
	time_run1  = timespec_sub(time_run1,   time_start);

	pr_info("run1 = %ld.%.9lds, reset = %ld.%.9lds, run2 = %ld.%.9lds, "
		"ro = %ld.%.9lds, rw = %ld.%.9lds\n",
		time_run1.tv_sec, time_run1.tv_nsec,
		time_reset.tv_sec, time_reset.tv_nsec,
		time_run2.tv_sec, time_run2.tv_nsec,
		time_ro.tv_sec, time_ro.tv_nsec,
		time_rw.tv_sec, time_rw.tv_nsec);

	/*
	 * Delete even numbered slots (arbitrary) and unmap the first half of
	 * the backing (also arbitrary) to verify KVM correctly drops all
	 * references to the removed regions.
	 */
	for (slot = (slot - 1) & ~1ull; slot >= first_slot; slot -= 2)
		vm_set_user_memory_region(vm, slot, 0, 0, 0, NULL);

	kvm_munmap(mem, slot_size / 2);

	/* Sanity check that the vCPUs actually ran. */
	for (i = 0; i < nr_vcpus; i++)
		pthread_join(threads[i], NULL);

	/*
	 * Deliberately exit without deleting the remaining memslots or closing
	 * kvm_fd to test cleanup via mmu_notifier.release.
	 */
}
