// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Google LLC.
 */
#include <pthread.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <linux/align.h>
#include <linux/kvm.h>
#include <linux/sizes.h>

#include "kvm_util.h"
#include "kvm_syscalls.h"
#include "kselftest_harness.h"
#include "test_util.h"
#include "ucall_common.h"

FIXTURE(gmem_conversions) {
	struct kvm_vcpu *vcpu;
	int gmem_fd;
	/* HVA of the first byte of the memory mmap()-ed from gmem_fd. */
	char *mem;
};

typedef FIXTURE_DATA(gmem_conversions) test_data_t;

FIXTURE_SETUP(gmem_conversions) { }

static size_t page_size;

static void guest_do_rmw(void);
#define GUEST_MEMFD_SHARING_TEST_GVA 0x90000000ULL

/*
 * Defer setup until the individual test is invoked so that tests can specify
 * the number of pages and flags for the guest_memfd instance.
 */
static void gmem_conversions_do_setup(test_data_t *t, int nr_pages,
				      int gmem_flags)
{
	const struct vm_shape shape = {
		.mode = VM_MODE_DEFAULT,
		.type = KVM_X86_SW_PROTECTED_VM,
	};
	/*
	 * Use high GPA above APIC_DEFAULT_PHYS_BASE to avoid clashing with
	 * APIC_DEFAULT_PHYS_BASE.
	 */
	const gpa_t gpa = SZ_4G;
	const u32 slot = 1;
	struct kvm_vm *vm;

	vm = __vm_create_shape_with_one_vcpu(shape, &t->vcpu, nr_pages, guest_do_rmw);

	vm_mem_add(vm, VM_MEM_SRC_SHMEM, gpa, slot, nr_pages,
		   KVM_MEM_GUEST_MEMFD, -1, 0, gmem_flags);

	t->gmem_fd = kvm_slot_to_fd(vm, slot);
	t->mem = addr_gpa2hva(vm, gpa);
	virt_map(vm, GUEST_MEMFD_SHARING_TEST_GVA, gpa, nr_pages);
}

static void gmem_conversions_do_teardown(test_data_t *t)
{
	/* Use NULL to avoid second free in FIXTURE_TEARDOWN (multipage tests). */
	if (!t->vcpu)
		return;

	/* No need to close gmem_fd, it's owned by the VM structure. */
	kvm_vm_free(t->vcpu->vm);
	t->vcpu = NULL;
}

FIXTURE_TEARDOWN(gmem_conversions)
{
	gmem_conversions_do_teardown(self);
}

/*
 * In these test definition macros, __nr_pages and nr_pages is used to set up
 * the total number of pages in the guest_memfd under test. This will be
 * available in the test definitions as nr_pages.
 */

#define __GMEM_CONVERSION_TEST(test, __nr_pages, flags)				\
static void __gmem_conversions_##test(test_data_t *t, int nr_pages);		\
										\
TEST_F(gmem_conversions, test)							\
{										\
	const int nr = (__nr_pages);						\
										\
	gmem_conversions_do_setup(self, nr, flags);				\
	__gmem_conversions_##test(self, nr);					\
}										\
static void __gmem_conversions_##test(test_data_t *t, int nr_pages)		\

#define GMEM_CONVERSION_TEST(test, __nr_pages, flags)				\
	__GMEM_CONVERSION_TEST(test, __nr_pages, (flags) | GUEST_MEMFD_FLAG_MMAP)

#define __GMEM_CONVERSION_TEST_INIT_PRIVATE(test, __nr_pages)			\
	GMEM_CONVERSION_TEST(test, __nr_pages, 0)

#define GMEM_CONVERSION_TEST_INIT_PRIVATE(test)					\
	__GMEM_CONVERSION_TEST_INIT_PRIVATE(test, 1)

#define __GMEM_CONVERSION_TEST_INIT_SHARED(test, __nr_pages)			\
	GMEM_CONVERSION_TEST(test, __nr_pages, GUEST_MEMFD_FLAG_INIT_SHARED)

#define GMEM_CONVERSION_TEST_INIT_SHARED(test)					\
	__GMEM_CONVERSION_TEST_INIT_SHARED(test, 1)

/*
 * Repeats test over nr_pages in a guest_memfd of size nr_pages, providing each
 * test iteration with test_page, the index of the page under test in
 * guest_memfd. test_page takes values 0..(nr_pages - 1) inclusive.
 */
#define GMEM_CONVERSION_MULTIPAGE_TEST_INIT_SHARED(test, __nr_pages)		\
static void __gmem_conversions_multipage_##test(test_data_t *t, int nr_pages,	\
						const int test_page);		\
										\
TEST_F(gmem_conversions, test)							\
{										\
	const u64 flags = GUEST_MEMFD_FLAG_MMAP | GUEST_MEMFD_FLAG_INIT_SHARED; \
	const int nr = (__nr_pages);						\
	int i;									\
										\
	for (i = 0; i < nr; ++i) {						\
		gmem_conversions_do_setup(self, nr, flags);			\
		__gmem_conversions_multipage_##test(self, nr, i);		\
		gmem_conversions_do_teardown(self);				\
	}									\
}										\
static void __gmem_conversions_multipage_##test(test_data_t *t, int nr_pages,	\
						const int test_page)

struct guest_check_data {
	void *mem;
	char expected_val;
	char write_val;
};
static struct guest_check_data guest_data;

static void guest_do_rmw(void)
{
	for (;;) {
		char *mem = READ_ONCE(guest_data.mem);

		GUEST_ASSERT_EQ(READ_ONCE(*mem), READ_ONCE(guest_data.expected_val));
		WRITE_ONCE(*mem, READ_ONCE(guest_data.write_val));

		GUEST_SYNC(0);
	}
}

static void run_guest_do_rmw(struct kvm_vcpu *vcpu, u64 pgoff,
			     char expected_val, char write_val)
{
	struct ucall uc;
	int r;

	guest_data.mem = (void *)GUEST_MEMFD_SHARING_TEST_GVA + pgoff * page_size;
	guest_data.expected_val = expected_val;
	guest_data.write_val = write_val;
	sync_global_to_guest(vcpu->vm, guest_data);

	do {
		r = __vcpu_run(vcpu);
	} while (r == -1 && errno == EINTR);

	TEST_ASSERT_EQ(r, 0);

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
	case UCALL_SYNC:
		break;
	default:
		TEST_FAIL("Unexpected ucall %lu", uc.cmd);
	}
}

static void host_do_rmw(char *mem, u64 pgoff, char expected_val,
			char write_val)
{
	TEST_ASSERT_EQ(READ_ONCE(mem[pgoff * page_size]), expected_val);
	WRITE_ONCE(mem[pgoff * page_size], write_val);
}

static void test_private(test_data_t *t, u64 pgoff, char starting_val,
			 char write_val)
{
	TEST_EXPECT_SIGBUS(WRITE_ONCE(t->mem[pgoff * page_size], write_val));
	run_guest_do_rmw(t->vcpu, pgoff, starting_val, write_val);
	TEST_EXPECT_SIGBUS(READ_ONCE(t->mem[pgoff * page_size]));
}

static void test_convert_to_private(test_data_t *t, u64 pgoff,
				    char starting_val, char write_val)
{
	gmem_set_private(t->gmem_fd, pgoff * page_size, page_size);
	test_private(t, pgoff, starting_val, write_val);
}

static void test_shared(test_data_t *t, u64 pgoff, char starting_val,
			char host_write_val, char write_val)
{
	host_do_rmw(t->mem, pgoff, starting_val, host_write_val);
	run_guest_do_rmw(t->vcpu, pgoff, host_write_val, write_val);
	TEST_ASSERT_EQ(READ_ONCE(t->mem[pgoff * page_size]), write_val);
}

static void test_convert_to_shared(test_data_t *t, u64 pgoff,
				   char starting_val, char host_write_val,
				   char write_val)
{
	gmem_set_shared(t->gmem_fd, pgoff * page_size, page_size);
	test_shared(t, pgoff, starting_val, host_write_val, write_val);
}

GMEM_CONVERSION_TEST_INIT_PRIVATE(init_private)
{
	test_private(t, 0, 0, 'A');
	test_convert_to_shared(t, 0, 'A', 'B', 'C');
	test_convert_to_private(t, 0, 'C', 'E');
}

GMEM_CONVERSION_TEST_INIT_SHARED(init_shared)
{
	test_shared(t, 0, 0, 'A', 'B');
	test_convert_to_private(t, 0, 'B', 'C');
	test_convert_to_shared(t, 0, 'C', 'D', 'E');
}

GMEM_CONVERSION_MULTIPAGE_TEST_INIT_SHARED(indexing, 4)
{
	int i;

	/* Get a char that varies with both i and n. */
#define combine(x, n) (((x) << 4) + (n))
#define i_(n) (combine(i, n))
#define t_(n) (combine(test_page, n))

	/*
	 * Start with the highest index, to catch any errors when, perhaps, the
	 * first page is returned even for the last index.
	 */
	for (i = nr_pages - 1; i >= 0; --i)
		test_shared(t, i, 0, i_(0), i_(2));

	test_convert_to_private(t, test_page, t_(2), t_(3));

	for (i = 0; i < nr_pages; ++i) {
		if (i == test_page)
			test_private(t, test_page, t_(3), t_(4));
		else
			test_shared(t, i, i_(2), i_(3), i_(4));
	}

	test_convert_to_shared(t, test_page, t_(4), t_(5), t_(6));

	for (i = 0; i < nr_pages; ++i) {
		char expected = i == test_page ? t_(6) : i_(4);

		test_shared(t, i, expected, i_(7), i_(8));
	}

#undef t_
#undef i_
#undef combine
}

/*
 * Test that even if there are no folios yet, conversion requests are recorded
 * in guest_memfd.
 */
GMEM_CONVERSION_TEST_INIT_SHARED(before_allocation_shared)
{
	test_convert_to_private(t, 0, 0, 'A');
}

GMEM_CONVERSION_TEST_INIT_PRIVATE(before_allocation_private)
{
	test_convert_to_shared(t, 0, 0, 'A', 'B');
}

/*
 * Test that when some of the folios in the conversion range are allocated,
 * conversion requests are handled correctly in guest_memfd.  Vary the ranges
 * allocated before conversion, using test_page, to cover various layouts of
 * contiguous allocated and unallocated regions.
 */
GMEM_CONVERSION_MULTIPAGE_TEST_INIT_SHARED(unallocated_folios, 8)
{
	const int second_page_to_fault = 4;
	int i;

	/*
	 * Fault 2 of the pages to test filemap range operations except when
	 * test_page == second_page_to_fault.
	 */
	host_do_rmw(t->mem, test_page, 0, 'A');
	if (test_page != second_page_to_fault)
		host_do_rmw(t->mem, second_page_to_fault, 0, 'A');

	gmem_set_private(t->gmem_fd, 0, nr_pages * page_size);
	for (i = 0; i < nr_pages; ++i) {
		char expected = (i == test_page || i == second_page_to_fault) ? 'A' : 0;

		test_private(t, i, expected, 'B');
	}

	for (i = 0; i < nr_pages; ++i)
		test_convert_to_shared(t, i, 'B', 'C', 'D');
}

/* Truncation should not affect shared/private status. */
GMEM_CONVERSION_TEST_INIT_SHARED(truncate)
{
	host_do_rmw(t->mem, 0, 0, 'A');
	kvm_fallocate(t->gmem_fd, FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE, 0, page_size);
	host_do_rmw(t->mem, 0, 0, 'A');

	test_convert_to_private(t, 0, 'A', 'B');

	kvm_fallocate(t->gmem_fd, FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE, 0, page_size);
	test_private(t, 0, 0, 'A');
}

/* Test that shared/private memory protections work and are seen from any process. */
GMEM_CONVERSION_TEST_INIT_SHARED(forked_accesses)
{
	enum test_state {
		STATE_INIT,
		STATE_CHECK_SHARED,
		STATE_DONE_CHECKING_SHARED,
		STATE_CHECK_PRIVATE,
		STATE_DONE_CHECKING_PRIVATE,
	};

	struct sync_state {
		pthread_mutex_t mutex;
		pthread_cond_t cond;
		enum test_state step;
	} *sync;

	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;
	pid_t child_pid, parent_pid;
	int status;

	sync = kvm_mmap(sizeof(*sync), PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(&sync->mutex, &mattr);
	pthread_mutexattr_destroy(&mattr);

	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
	pthread_cond_init(&sync->cond, &cattr);
	pthread_condattr_destroy(&cattr);

	sync->step = STATE_INIT;

#define TEST_STATE_AWAIT(__state)						\
	do {									\
		pthread_mutex_lock(&sync->mutex);				\
		while (sync->step != (__state)) {				\
			struct timespec ts, stop;				\
			int ret;						\
										\
			clock_gettime(CLOCK_REALTIME, &ts);			\
			stop = timespec_add_ns(ts, 100 * 1000000UL);		\
										\
			ret = pthread_cond_timedwait(&sync->cond, &sync->mutex, &stop); \
			if (ret == ETIMEDOUT) {					\
				bool alive = (child_pid == 0) ?			\
					     (getppid() == parent_pid) :		\
					     (waitpid(child_pid, NULL, WNOHANG) == 0); \
				TEST_ASSERT(alive, "Other process exited prematurely"); \
			} else {						\
				TEST_ASSERT(!ret, "pthread_cond_timedwait failed"); \
			}							\
		}								\
		pthread_mutex_unlock(&sync->mutex);				\
	} while (0)

#define TEST_STATE_SET(__state)							\
	do {									\
		pthread_mutex_lock(&sync->mutex);				\
		sync->step = (__state);						\
		pthread_cond_broadcast(&sync->cond);				\
		pthread_mutex_unlock(&sync->mutex);				\
	} while (0)

	parent_pid = getpid();
	child_pid = fork();
	TEST_ASSERT(child_pid != -1, "fork failed");

	if (child_pid == 0) {
		const char inconsequential = 0xdd;

		TEST_STATE_AWAIT(STATE_CHECK_SHARED);

		/*
		 * This maps the pages into the child process as well, and tests
		 * that the conversion process will unmap the guest_memfd memory
		 * from all processes.
		 */
		host_do_rmw(t->mem, 0, 0xB, 0xC);

		TEST_STATE_SET(STATE_DONE_CHECKING_SHARED);
		TEST_STATE_AWAIT(STATE_CHECK_PRIVATE);

		TEST_EXPECT_SIGBUS(READ_ONCE(t->mem[0]));
		TEST_EXPECT_SIGBUS(WRITE_ONCE(t->mem[0], inconsequential));

		TEST_STATE_SET(STATE_DONE_CHECKING_PRIVATE);
		exit(0);
	}

	test_shared(t, 0, 0, 0xA, 0xB);

	TEST_STATE_SET(STATE_CHECK_SHARED);
	TEST_STATE_AWAIT(STATE_DONE_CHECKING_SHARED);

	test_convert_to_private(t, 0, 0xC, 0xD);

	TEST_STATE_SET(STATE_CHECK_PRIVATE);
	TEST_STATE_AWAIT(STATE_DONE_CHECKING_PRIVATE);

	TEST_ASSERT_EQ(waitpid(child_pid, &status, 0), child_pid);
	TEST_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0,
		    "Child exited with unexpected status");

	pthread_mutex_destroy(&sync->mutex);
	pthread_cond_destroy(&sync->cond);
	kvm_munmap(sync, sizeof(*sync));

#undef TEST_STATE_SET
#undef TEST_STATE_AWAIT
}

static void test_convert_to_private_fails(test_data_t *t, u64 pgoff,
					  size_t nr_pages,
					  u64 expected_error_offset)
{
	/* +1 to make it anything but expected_error_offset. */
	u64 error_offset = expected_error_offset + 1;
	u64 offset = pgoff * page_size;
	int ret;

	do {
		ret = __gmem_set_private(t->gmem_fd, offset,
					 nr_pages * page_size, &error_offset);
	} while (ret == -1 && errno == EINTR);
	TEST_ASSERT(ret == -1 && errno == EAGAIN,
		    "Wanted EAGAIN on page %lu, got %d (ret = %d)", pgoff,
		    errno, ret);
	TEST_ASSERT_EQ(error_offset, expected_error_offset);
}

GMEM_CONVERSION_MULTIPAGE_TEST_INIT_SHARED(elevated_refcount, 4)
{
	int i;

	pin_pages(t->mem + test_page * page_size, page_size);

	for (i = 0; i < nr_pages; i++)
		test_shared(t, i, 0, 'A', 'B');

	/*
	 * Converting in bulk should fail as long any page in the range has
	 * unexpected refcounts.
	 */
	test_convert_to_private_fails(t, 0, nr_pages, test_page * page_size);

	for (i = 0; i < nr_pages; i++) {
		/*
		 * Converting page-wise should also fail as long any page in the
		 * range has unexpected refcounts.
		 */
		if (i == test_page)
			test_convert_to_private_fails(t, i, 1, test_page * page_size);
		else
			test_convert_to_private(t, i, 'B', 'C');
	}

	unpin_pages();

	gmem_set_private(t->gmem_fd, 0, nr_pages * page_size);

	for (i = 0; i < nr_pages; i++) {
		char expected = i == test_page ? 'B' : 'C';

		test_private(t, i, expected, 'D');
	}
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_check_cap(KVM_CAP_VM_TYPES) & BIT(KVM_X86_SW_PROTECTED_VM));
	TEST_REQUIRE(kvm_check_cap(KVM_CAP_GUEST_MEMFD_MEMORY_ATTRIBUTES) &
		     KVM_MEMORY_ATTRIBUTE_PRIVATE);

	page_size = getpagesize();

	return test_harness_run(argc, argv);
}
