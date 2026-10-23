// SPDX-License-Identifier: GPL-2.0
#define __SANE_USERSPACE_TYPES__ // Use ll64
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <mm/gup_test.h>
#include "vm_util.h"
#include "kselftest_harness.h"

#define MB (1UL << 20)

/* Just the flags we need, copied from the kernel internals. */
#define FOLL_WRITE	0x01	/* check pte is writable */

/* Page counts exercising single, THP-batch, partial, and full-mapping GUP. */
static const int nr_pages_list[] = { 1, 512, 123, -1 };

#define GUP_TEST_FILE "/sys/kernel/debug/gup_test"
#define NR_HUGETLB_PAGES 2

static unsigned long hp_size;

FIXTURE(gup_test)
{
	int gup_fd;
	char *addr;
	unsigned long size;
};

FIXTURE_VARIANT(gup_test)
{
	bool thp;
	bool hugetlb;
	bool write;
	bool shared;
};

FIXTURE_VARIANT_ADD(gup_test, private_write)
{
	.thp = false,
	.hugetlb = false,
	.write = true,
	.shared = false,
};

FIXTURE_VARIANT_ADD(gup_test, private_read)
{
	.thp = false,
	.hugetlb = false,
	.write = false,
	.shared = false,
};

FIXTURE_VARIANT_ADD(gup_test, private_write_thp)
{
	.thp = true,
	.hugetlb = false,
	.write = true,
	.shared = false,
};

FIXTURE_VARIANT_ADD(gup_test, private_read_thp)
{
	.thp = true,
	.hugetlb = false,
	.write = false,
	.shared = false,
};

FIXTURE_VARIANT_ADD(gup_test, private_write_hugetlb)
{
	.thp = false,
	.hugetlb = true,
	.write = true,
	.shared = false,
};

FIXTURE_VARIANT_ADD(gup_test, private_read_hugetlb)
{
	.thp = false,
	.hugetlb = true,
	.write = false,
	.shared = false,
};

FIXTURE_VARIANT_ADD(gup_test, shared_write)
{
	.thp = false,
	.hugetlb = false,
	.write = true,
	.shared = true,
};

FIXTURE_VARIANT_ADD(gup_test, shared_read)
{
	.thp = false,
	.hugetlb = false,
	.write = false,
	.shared = true,
};

FIXTURE_VARIANT_ADD(gup_test, shared_write_thp)
{
	.thp = true,
	.hugetlb = false,
	.write = true,
	.shared = true,
};

FIXTURE_VARIANT_ADD(gup_test, shared_read_thp)
{
	.thp = true,
	.hugetlb = false,
	.write = false,
	.shared = true,
};

FIXTURE_VARIANT_ADD(gup_test, shared_write_hugetlb)
{
	.thp = false,
	.hugetlb = true,
	.write = true,
	.shared = true,
};

FIXTURE_VARIANT_ADD(gup_test, shared_read_hugetlb)
{
	.thp = false,
	.hugetlb = true,
	.write = false,
	.shared = true,
};

FIXTURE_SETUP(gup_test)
{
	int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
	char *p;

	self->size = 128 * MB;

	if (variant->hugetlb) {
		if (!hp_size)
			SKIP(return, "HugeTLB not available\n");

		if (hugetlb_free_default_pages() < NR_HUGETLB_PAGES)
			SKIP(return, "Not enough huge pages\n");

		self->size = NR_HUGETLB_PAGES * hp_size;
		mmap_flags |= MAP_HUGETLB;
	}

	if (variant->shared)
		mmap_flags = (mmap_flags & ~MAP_PRIVATE) | MAP_SHARED;

	/* gup_fd has to be >= 0. Already checked in main() */
	self->gup_fd = open(GUP_TEST_FILE, O_RDWR);
	ASSERT_GE(self->gup_fd, 0);

	self->addr = mmap(NULL, self->size, PROT_READ | PROT_WRITE,
			  mmap_flags, -1, 0);

	ASSERT_NE(self->addr, MAP_FAILED) {
		int err = errno;

		close(self->gup_fd);
		TH_LOG("mmap failed: %s", strerror(err));
	}

	if (variant->thp)
		madvise(self->addr, self->size, MADV_HUGEPAGE);
	else if (!variant->hugetlb)
		madvise(self->addr, self->size, MADV_NOHUGEPAGE);

	for (p = self->addr; (unsigned long)p < (unsigned long)self->addr
			+ self->size; p += psize())
		p[0] = 0;
}

FIXTURE_TEARDOWN(gup_test)
{
	munmap(self->addr, self->size);
	close(self->gup_fd);
}

static void run_gup_cmd(struct __test_metadata *_metadata,
		FIXTURE_DATA(gup_test) *self,
		const FIXTURE_VARIANT(gup_test) *variant,
		unsigned long command)
{
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(nr_pages_list); i++) {
		struct gup_test gup = {
			.addr = (unsigned long)self->addr,
			.size = self->size,
			.nr_pages_per_call = nr_pages_list[i] < 0 ?
				self->size / psize() : nr_pages_list[i],
			.gup_flags = variant->write ? FOLL_WRITE : 0,
		};

		TH_LOG("nr_pages_per_call=%u", gup.nr_pages_per_call);
		ASSERT_EQ(ioctl(self->gup_fd, command, &gup), 0);
		ASSERT_EQ(gup.size, self->size);
	}
}

TEST_F(gup_test, get_user_pages)
{
	run_gup_cmd(_metadata, self, variant, GUP_BASIC_TEST);
}

TEST_F(gup_test, pin_user_pages)
{
	run_gup_cmd(_metadata, self, variant, PIN_BASIC_TEST);
}

TEST_F(gup_test, get_user_pages_fast)
{
	run_gup_cmd(_metadata, self, variant, GUP_FAST_BENCHMARK);
}

TEST_F(gup_test, pin_user_pages_fast)
{
	run_gup_cmd(_metadata, self, variant, PIN_FAST_BENCHMARK);
}

TEST_F(gup_test, pin_user_pages_longterm)
{
	run_gup_cmd(_metadata, self, variant, PIN_LONGTERM_BENCHMARK);
}

int main(int argc, char **argv)
{
	const int fd = open(GUP_TEST_FILE, O_RDWR);

	if (fd == -1) {
		ksft_print_header();
		if (errno == EACCES)
			ksft_exit_skip("Please run this test as root\n");
		if (errno == ENOENT) {
			DIR *debugfs = opendir("/sys/kernel/debug");

			if (!debugfs)
				ksft_exit_skip("Mount debugfs at /sys/kernel/debug\n");
			closedir(debugfs);
			ksft_exit_skip("Check CONFIG_GUP_TEST in kernel config\n");
		}
		ksft_exit_fail_msg("Failed to open %s: %s\n", GUP_TEST_FILE, strerror(errno));
	}
	close(fd);

	hp_size = default_huge_page_size();
	if (hp_size)
		hugetlb_setup_default(NR_HUGETLB_PAGES);

	return test_harness_run(argc, argv);
}
