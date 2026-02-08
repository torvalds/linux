// SPDX-License-Identifier: GPL-2.0-or-later

#define _GNU_SOURCE
#include "kselftest_harness.h"
#include <linux/prctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/perf_event.h>
#include "vm_util.h"
#include <linux/mman.h>

FIXTURE(merge)
{
	unsigned int page_size;
	char *carveout;
	struct procmap_fd procmap;
};

static char *map_carveout(unsigned int page_size)
{
	return mmap(NULL, 30 * page_size, PROT_NONE,
		    MAP_ANON | MAP_PRIVATE, -1, 0);
}

static pid_t do_fork(struct procmap_fd *procmap)
{
	pid_t pid = fork();

	if (pid == -1)
		return -1;
	if (pid != 0) {
		wait(NULL);
		return pid;
	}

	/* Reopen for child. */
	if (close_procmap(procmap))
		return -1;
	if (open_self_procmap(procmap))
		return -1;

	return 0;
}

FIXTURE_SETUP(merge)
{
	self->page_size = psize();
	/* Carve out PROT_NONE region to map over. */
	self->carveout = map_carveout(self->page_size);
	ASSERT_NE(self->carveout, MAP_FAILED);
	/* Setup PROCMAP_QUERY interface. */
	ASSERT_EQ(open_self_procmap(&self->procmap), 0);
}

FIXTURE_TEARDOWN(merge)
{
	ASSERT_EQ(munmap(self->carveout, 30 * self->page_size), 0);
	/* May fail for parent of forked process. */
	close_procmap(&self->procmap);
	/*
	 * Clear unconditionally, as some tests set this. It is no issue if this
	 * fails (KSM may be disabled for instance).
	 */
	prctl(PR_SET_MEMORY_MERGE, 0, 0, 0, 0);
}

FIXTURE(merge_with_fork)
{
	unsigned int page_size;
	char *carveout;
	struct procmap_fd procmap;
};

FIXTURE_VARIANT(merge_with_fork)
{
	bool forked;
};

FIXTURE_VARIANT_ADD(merge_with_fork, forked)
{
	.forked = true,
};

FIXTURE_VARIANT_ADD(merge_with_fork, unforked)
{
	.forked = false,
};

FIXTURE_SETUP(merge_with_fork)
{
	self->page_size = psize();
	self->carveout = map_carveout(self->page_size);
	ASSERT_NE(self->carveout, MAP_FAILED);
	ASSERT_EQ(open_self_procmap(&self->procmap), 0);
}

FIXTURE_TEARDOWN(merge_with_fork)
{
	ASSERT_EQ(munmap(self->carveout, 30 * self->page_size), 0);
	ASSERT_EQ(close_procmap(&self->procmap), 0);
	/* See above. */
	prctl(PR_SET_MEMORY_MERGE, 0, 0, 0, 0);
}

TEST_F(merge, mprotect_unfaulted_left)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr;

	/*
	 * Map 10 pages of R/W memory within. MAP_NORESERVE so we don't hit
	 * merge failure due to lack of VM_ACCOUNT flag by mistake.
	 *
	 * |-----------------------|
	 * |       unfaulted       |
	 * |-----------------------|
	 */
	ptr = mmap(&carveout[page_size], 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	/*
	 * Now make the first 5 pages read-only, splitting the VMA:
	 *
	 *      RO          RW
	 * |-----------|-----------|
	 * | unfaulted | unfaulted |
	 * |-----------|-----------|
	 */
	ASSERT_EQ(mprotect(ptr, 5 * page_size, PROT_READ), 0);
	/*
	 * Fault in the first of the last 5 pages so it gets an anon_vma and
	 * thus the whole VMA becomes 'faulted':
	 *
	 *      RO          RW
	 * |-----------|-----------|
	 * | unfaulted |  faulted  |
	 * |-----------|-----------|
	 */
	ptr[5 * page_size] = 'x';
	/*
	 * Now mprotect() the RW region read-only, we should merge (though for
	 * ~15 years we did not! :):
	 *
	 *             RO
	 * |-----------------------|
	 * |        faulted        |
	 * |-----------------------|
	 */
	ASSERT_EQ(mprotect(&ptr[5 * page_size], 5 * page_size, PROT_READ), 0);

	/* Assert that the merge succeeded using PROCMAP_QUERY. */
	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 10 * page_size);
}

TEST_F(merge, mprotect_unfaulted_right)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr;

	/*
	 * |-----------------------|
	 * |       unfaulted       |
	 * |-----------------------|
	 */
	ptr = mmap(&carveout[page_size], 10 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	/*
	 * Now make the last 5 pages read-only, splitting the VMA:
	 *
	 *      RW          RO
	 * |-----------|-----------|
	 * | unfaulted | unfaulted |
	 * |-----------|-----------|
	 */
	ASSERT_EQ(mprotect(&ptr[5 * page_size], 5 * page_size, PROT_READ), 0);
	/*
	 * Fault in the first of the first 5 pages so it gets an anon_vma and
	 * thus the whole VMA becomes 'faulted':
	 *
	 *      RW          RO
	 * |-----------|-----------|
	 * |  faulted  | unfaulted |
	 * |-----------|-----------|
	 */
	ptr[0] = 'x';
	/*
	 * Now mprotect() the RW region read-only, we should merge:
	 *
	 *             RO
	 * |-----------------------|
	 * |        faulted        |
	 * |-----------------------|
	 */
	ASSERT_EQ(mprotect(ptr, 5 * page_size, PROT_READ), 0);

	/* Assert that the merge succeeded using PROCMAP_QUERY. */
	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 10 * page_size);
}

TEST_F(merge, mprotect_unfaulted_both)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr;

	/*
	 * |-----------------------|
	 * |       unfaulted       |
	 * |-----------------------|
	 */
	ptr = mmap(&carveout[2 * page_size], 9 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	/*
	 * Now make the first and last 3 pages read-only, splitting the VMA:
	 *
	 *      RO          RW          RO
	 * |-----------|-----------|-----------|
	 * | unfaulted | unfaulted | unfaulted |
	 * |-----------|-----------|-----------|
	 */
	ASSERT_EQ(mprotect(ptr, 3 * page_size, PROT_READ), 0);
	ASSERT_EQ(mprotect(&ptr[6 * page_size], 3 * page_size, PROT_READ), 0);
	/*
	 * Fault in the first of the middle 3 pages so it gets an anon_vma and
	 * thus the whole VMA becomes 'faulted':
	 *
	 *      RO          RW          RO
	 * |-----------|-----------|-----------|
	 * | unfaulted |  faulted  | unfaulted |
	 * |-----------|-----------|-----------|
	 */
	ptr[3 * page_size] = 'x';
	/*
	 * Now mprotect() the RW region read-only, we should merge:
	 *
	 *             RO
	 * |-----------------------|
	 * |        faulted        |
	 * |-----------------------|
	 */
	ASSERT_EQ(mprotect(&ptr[3 * page_size], 3 * page_size, PROT_READ), 0);

	/* Assert that the merge succeeded using PROCMAP_QUERY. */
	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 9 * page_size);
}

TEST_F(merge, mprotect_faulted_left_unfaulted_right)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr;

	/*
	 * |-----------------------|
	 * |       unfaulted       |
	 * |-----------------------|
	 */
	ptr = mmap(&carveout[2 * page_size], 9 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	/*
	 * Now make the last 3 pages read-only, splitting the VMA:
	 *
	 *             RW               RO
	 * |-----------------------|-----------|
	 * |       unfaulted       | unfaulted |
	 * |-----------------------|-----------|
	 */
	ASSERT_EQ(mprotect(&ptr[6 * page_size], 3 * page_size, PROT_READ), 0);
	/*
	 * Fault in the first of the first 6 pages so it gets an anon_vma and
	 * thus the whole VMA becomes 'faulted':
	 *
	 *             RW               RO
	 * |-----------------------|-----------|
	 * |       unfaulted       | unfaulted |
	 * |-----------------------|-----------|
	 */
	ptr[0] = 'x';
	/*
	 * Now make the first 3 pages read-only, splitting the VMA:
	 *
	 *      RO          RW          RO
	 * |-----------|-----------|-----------|
	 * |  faulted  |  faulted  | unfaulted |
	 * |-----------|-----------|-----------|
	 */
	ASSERT_EQ(mprotect(ptr, 3 * page_size, PROT_READ), 0);
	/*
	 * Now mprotect() the RW region read-only, we should merge:
	 *
	 *             RO
	 * |-----------------------|
	 * |        faulted        |
	 * |-----------------------|
	 */
	ASSERT_EQ(mprotect(&ptr[3 * page_size], 3 * page_size, PROT_READ), 0);

	/* Assert that the merge succeeded using PROCMAP_QUERY. */
	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 9 * page_size);
}

TEST_F(merge, mprotect_unfaulted_left_faulted_right)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr;

	/*
	 * |-----------------------|
	 * |       unfaulted       |
	 * |-----------------------|
	 */
	ptr = mmap(&carveout[2 * page_size], 9 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	/*
	 * Now make the first 3 pages read-only, splitting the VMA:
	 *
	 *      RO                RW
	 * |-----------|-----------------------|
	 * | unfaulted |       unfaulted       |
	 * |-----------|-----------------------|
	 */
	ASSERT_EQ(mprotect(ptr, 3 * page_size, PROT_READ), 0);
	/*
	 * Fault in the first of the last 6 pages so it gets an anon_vma and
	 * thus the whole VMA becomes 'faulted':
	 *
	 *      RO                RW
	 * |-----------|-----------------------|
	 * | unfaulted |        faulted        |
	 * |-----------|-----------------------|
	 */
	ptr[3 * page_size] = 'x';
	/*
	 * Now make the last 3 pages read-only, splitting the VMA:
	 *
	 *      RO          RW          RO
	 * |-----------|-----------|-----------|
	 * | unfaulted |  faulted  |  faulted  |
	 * |-----------|-----------|-----------|
	 */
	ASSERT_EQ(mprotect(&ptr[6 * page_size], 3 * page_size, PROT_READ), 0);
	/*
	 * Now mprotect() the RW region read-only, we should merge:
	 *
	 *             RO
	 * |-----------------------|
	 * |        faulted        |
	 * |-----------------------|
	 */
	ASSERT_EQ(mprotect(&ptr[3 * page_size], 3 * page_size, PROT_READ), 0);

	/* Assert that the merge succeeded using PROCMAP_QUERY. */
	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 9 * page_size);
}

TEST_F(merge, forked_target_vma)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr, *ptr2;
	pid_t pid;
	int i;

	/*
	 * |-----------|
	 * | unfaulted |
	 * |-----------|
	 */
	ptr = mmap(&carveout[page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/*
	 * Fault in process.
	 *
	 * |-----------|
	 * |  faulted  |
	 * |-----------|
	 */
	ptr[0] = 'x';

	pid = do_fork(&self->procmap);
	ASSERT_NE(pid, -1);
	if (pid != 0)
		return;

	/* unCOWing everything does not cause the AVC to go away. */
	for (i = 0; i < 5 * page_size; i += page_size)
		ptr[i] = 'x';

	/*
	 * Map in adjacent VMA in child.
	 *
	 *     forked
	 * |-----------|-----------|
	 * |  faulted  | unfaulted |
	 * |-----------|-----------|
	 *      ptr         ptr2
	 */
	ptr2 = mmap(&ptr[5 * page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr2, MAP_FAILED);

	/* Make sure not merged. */
	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 5 * page_size);
}

TEST_F(merge, forked_source_vma)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr, *ptr2;
	pid_t pid;
	int i;

	/*
	 * |-----------|------------|
	 * | unfaulted | <unmapped> |
	 * |-----------|------------|
	 */
	ptr = mmap(&carveout[page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/*
	 * Fault in process.
	 *
	 * |-----------|------------|
	 * |  faulted  | <unmapped> |
	 * |-----------|------------|
	 */
	ptr[0] = 'x';

	pid = do_fork(&self->procmap);
	ASSERT_NE(pid, -1);
	if (pid != 0)
		return;

	/* unCOWing everything does not cause the AVC to go away. */
	for (i = 0; i < 5 * page_size; i += page_size)
		ptr[i] = 'x';

	/*
	 * Map in adjacent VMA in child, ptr2 after ptr, but incompatible.
	 *
	 *   forked RW      RWX
	 * |-----------|-----------|
	 * |  faulted  | unfaulted |
	 * |-----------|-----------|
	 *      ptr        ptr2
	 */
	ptr2 = mmap(&carveout[6 * page_size], 5 * page_size, PROT_READ | PROT_WRITE | PROT_EXEC,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE, -1, 0);
	ASSERT_NE(ptr2, MAP_FAILED);

	/* Make sure not merged. */
	ASSERT_TRUE(find_vma_procmap(procmap, ptr2));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr2);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr2 + 5 * page_size);

	/*
	 * Now mprotect forked region to RWX so it becomes the source for the
	 * merge to unfaulted region:
	 *
	 *  forked RWX      RWX
	 * |-----------|-----------|
	 * |  faulted  | unfaulted |
	 * |-----------|-----------|
	 *      ptr         ptr2
	 *
	 * This should NOT result in a merge, as ptr was forked.
	 */
	ASSERT_EQ(mprotect(ptr, 5 * page_size, PROT_READ | PROT_WRITE | PROT_EXEC), 0);
	/* Again, make sure not merged. */
	ASSERT_TRUE(find_vma_procmap(procmap, ptr2));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr2);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr2 + 5 * page_size);
}

TEST_F(merge, handle_uprobe_upon_merged_vma)
{
	const size_t attr_sz = sizeof(struct perf_event_attr);
	unsigned int page_size = self->page_size;
	const char *probe_file = "./foo";
	char *carveout = self->carveout;
	struct perf_event_attr attr;
	unsigned long type;
	void *ptr1, *ptr2;
	int fd;

	fd = open(probe_file, O_RDWR|O_CREAT, 0600);
	ASSERT_GE(fd, 0);

	ASSERT_EQ(ftruncate(fd, page_size), 0);
	if (read_sysfs("/sys/bus/event_source/devices/uprobe/type", &type) != 0) {
		SKIP(goto out, "Failed to read uprobe sysfs file, skipping");
	}

	memset(&attr, 0, attr_sz);
	attr.size = attr_sz;
	attr.type = type;
	attr.config1 = (__u64)(long)probe_file;
	attr.config2 = 0x0;

	ASSERT_GE(syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0), 0);

	ptr1 = mmap(&carveout[page_size], 10 * page_size, PROT_EXEC,
		    MAP_PRIVATE | MAP_FIXED, fd, 0);
	ASSERT_NE(ptr1, MAP_FAILED);

	ptr2 = mremap(ptr1, page_size, 2 * page_size,
		      MREMAP_MAYMOVE | MREMAP_FIXED, ptr1 + 5 * page_size);
	ASSERT_NE(ptr2, MAP_FAILED);

	ASSERT_NE(mremap(ptr2, page_size, page_size,
			 MREMAP_MAYMOVE | MREMAP_FIXED, ptr1), MAP_FAILED);

out:
	close(fd);
	remove(probe_file);
}

TEST_F(merge, ksm_merge)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr, *ptr2;
	int err;

	/*
	 * Map two R/W immediately adjacent to one another, they should
	 * trivially merge:
	 *
	 * |-----------|-----------|
	 * |    R/W    |    R/W    |
	 * |-----------|-----------|
	 *      ptr         ptr2
	 */

	ptr = mmap(&carveout[page_size], page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ptr2 = mmap(&carveout[2 * page_size], page_size,
		    PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr2, MAP_FAILED);
	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 2 * page_size);

	/* Unmap the second half of this merged VMA. */
	ASSERT_EQ(munmap(ptr2, page_size), 0);

	/* OK, now enable global KSM merge. We clear this on test teardown. */
	err = prctl(PR_SET_MEMORY_MERGE, 1, 0, 0, 0);
	if (err == -1) {
		int errnum = errno;

		/* Only non-failure case... */
		ASSERT_EQ(errnum, EINVAL);
		/* ...but indicates we should skip. */
		SKIP(return, "KSM memory merging not supported, skipping.");
	}

	/*
	 * Now map a VMA adjacent to the existing that was just made
	 * VM_MERGEABLE, this should merge as well.
	 */
	ptr2 = mmap(&carveout[2 * page_size], page_size,
		    PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr2, MAP_FAILED);
	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 2 * page_size);

	/* Now this VMA altogether. */
	ASSERT_EQ(munmap(ptr, 2 * page_size), 0);

	/* Try the same operation as before, asserting this also merges fine. */
	ptr = mmap(&carveout[page_size], page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ptr2 = mmap(&carveout[2 * page_size], page_size,
		    PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr2, MAP_FAILED);
	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 2 * page_size);
}

TEST_F(merge, mremap_unfaulted_to_faulted)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr, *ptr2;

	/*
	 * Map two distinct areas:
	 *
	 * |-----------|  |-----------|
	 * | unfaulted |  | unfaulted |
	 * |-----------|  |-----------|
	 *      ptr            ptr2
	 */
	ptr = mmap(&carveout[page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ptr2 = mmap(&carveout[7 * page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr2, MAP_FAILED);

	/* Offset ptr2 further away. */
	ptr2 = sys_mremap(ptr2, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, ptr2 + page_size * 1000);
	ASSERT_NE(ptr2, MAP_FAILED);

	/*
	 * Fault in ptr:
	 *                \
	 * |-----------|  /  |-----------|
	 * |  faulted  |  \  | unfaulted |
	 * |-----------|  /  |-----------|
	 *      ptr       \       ptr2
	 */
	ptr[0] = 'x';

	/*
	 * Now move ptr2 adjacent to ptr:
	 *
	 * |-----------|-----------|
	 * |  faulted  | unfaulted |
	 * |-----------|-----------|
	 *      ptr         ptr2
	 *
	 * It should merge:
	 *
	 * |----------------------|
	 * |       faulted        |
	 * |----------------------|
	 *            ptr
	 */
	ptr2 = sys_mremap(ptr2, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, &ptr[5 * page_size]);
	ASSERT_NE(ptr2, MAP_FAILED);

	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 10 * page_size);
}

TEST_F(merge, mremap_unfaulted_behind_faulted)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr, *ptr2;

	/*
	 * Map two distinct areas:
	 *
	 * |-----------|  |-----------|
	 * | unfaulted |  | unfaulted |
	 * |-----------|  |-----------|
	 *      ptr            ptr2
	 */
	ptr = mmap(&carveout[6 * page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ptr2 = mmap(&carveout[14 * page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr2, MAP_FAILED);

	/* Offset ptr2 further away. */
	ptr2 = sys_mremap(ptr2, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, ptr2 + page_size * 1000);
	ASSERT_NE(ptr2, MAP_FAILED);

	/*
	 * Fault in ptr:
	 *                \
	 * |-----------|  /  |-----------|
	 * |  faulted  |  \  | unfaulted |
	 * |-----------|  /  |-----------|
	 *      ptr       \       ptr2
	 */
	ptr[0] = 'x';

	/*
	 * Now move ptr2 adjacent, but behind, ptr:
	 *
	 * |-----------|-----------|
	 * | unfaulted |  faulted  |
	 * |-----------|-----------|
	 *      ptr2        ptr
	 *
	 * It should merge:
	 *
	 * |----------------------|
	 * |       faulted        |
	 * |----------------------|
	 *            ptr2
	 */
	ptr2 = sys_mremap(ptr2, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, &carveout[page_size]);
	ASSERT_NE(ptr2, MAP_FAILED);

	ASSERT_TRUE(find_vma_procmap(procmap, ptr2));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr2);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr2 + 10 * page_size);
}

TEST_F(merge, mremap_unfaulted_between_faulted)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr, *ptr2, *ptr3;

	/*
	 * Map three distinct areas:
	 *
	 * |-----------|  |-----------|  |-----------|
	 * | unfaulted |  | unfaulted |  | unfaulted |
	 * |-----------|  |-----------|  |-----------|
	 *      ptr            ptr2           ptr3
	 */
	ptr = mmap(&carveout[page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ptr2 = mmap(&carveout[7 * page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr2, MAP_FAILED);
	ptr3 = mmap(&carveout[14 * page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr3, MAP_FAILED);

	/* Offset ptr3 further away. */
	ptr3 = sys_mremap(ptr3, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, ptr3 + page_size * 2000);
	ASSERT_NE(ptr3, MAP_FAILED);

	/* Offset ptr2 further away. */
	ptr2 = sys_mremap(ptr2, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, ptr2 + page_size * 1000);
	ASSERT_NE(ptr2, MAP_FAILED);

	/*
	 * Fault in ptr, ptr3:
	 *                \                 \
	 * |-----------|  /  |-----------|  /  |-----------|
	 * |  faulted  |  \  | unfaulted |  \  |  faulted  |
	 * |-----------|  /  |-----------|  /  |-----------|
	 *      ptr       \       ptr2      \       ptr3
	 */
	ptr[0] = 'x';
	ptr3[0] = 'x';

	/*
	 * Move ptr3 back into place, leaving a place for ptr2:
	 *                                        \
	 * |-----------|           |-----------|  /  |-----------|
	 * |  faulted  |           |  faulted  |  \  | unfaulted |
	 * |-----------|           |-----------|  /  |-----------|
	 *      ptr                     ptr3      \       ptr2
	 */
	ptr3 = sys_mremap(ptr3, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, &ptr[10 * page_size]);
	ASSERT_NE(ptr3, MAP_FAILED);

	/*
	 * Finally, move ptr2 into place:
	 *
	 * |-----------|-----------|-----------|
	 * |  faulted  | unfaulted |  faulted  |
	 * |-----------|-----------|-----------|
	 *      ptr        ptr2         ptr3
	 *
	 * It should merge, but only ptr, ptr2:
	 *
	 * |-----------------------|-----------|
	 * |        faulted        | unfaulted |
	 * |-----------------------|-----------|
	 */
	ptr2 = sys_mremap(ptr2, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, &ptr[5 * page_size]);
	ASSERT_NE(ptr2, MAP_FAILED);

	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 10 * page_size);

	ASSERT_TRUE(find_vma_procmap(procmap, ptr3));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr3);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr3 + 5 * page_size);
}

TEST_F(merge, mremap_unfaulted_between_faulted_unfaulted)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr, *ptr2, *ptr3;

	/*
	 * Map three distinct areas:
	 *
	 * |-----------|  |-----------|  |-----------|
	 * | unfaulted |  | unfaulted |  | unfaulted |
	 * |-----------|  |-----------|  |-----------|
	 *      ptr            ptr2           ptr3
	 */
	ptr = mmap(&carveout[page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ptr2 = mmap(&carveout[7 * page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr2, MAP_FAILED);
	ptr3 = mmap(&carveout[14 * page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr3, MAP_FAILED);

	/* Offset ptr3 further away. */
	ptr3 = sys_mremap(ptr3, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, ptr3 + page_size * 2000);
	ASSERT_NE(ptr3, MAP_FAILED);


	/* Offset ptr2 further away. */
	ptr2 = sys_mremap(ptr2, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, ptr2 + page_size * 1000);
	ASSERT_NE(ptr2, MAP_FAILED);

	/*
	 * Fault in ptr:
	 *                \                 \
	 * |-----------|  /  |-----------|  /  |-----------|
	 * |  faulted  |  \  | unfaulted |  \  | unfaulted |
	 * |-----------|  /  |-----------|  /  |-----------|
	 *      ptr       \       ptr2      \       ptr3
	 */
	ptr[0] = 'x';

	/*
	 * Move ptr3 back into place, leaving a place for ptr2:
	 *                                        \
	 * |-----------|           |-----------|  /  |-----------|
	 * |  faulted  |           | unfaulted |  \  | unfaulted |
	 * |-----------|           |-----------|  /  |-----------|
	 *      ptr                     ptr3      \       ptr2
	 */
	ptr3 = sys_mremap(ptr3, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, &ptr[10 * page_size]);
	ASSERT_NE(ptr3, MAP_FAILED);

	/*
	 * Finally, move ptr2 into place:
	 *
	 * |-----------|-----------|-----------|
	 * |  faulted  | unfaulted | unfaulted |
	 * |-----------|-----------|-----------|
	 *      ptr        ptr2         ptr3
	 *
	 * It should merge:
	 *
	 * |-----------------------------------|
	 * |              faulted              |
	 * |-----------------------------------|
	 */
	ptr2 = sys_mremap(ptr2, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, &ptr[5 * page_size]);
	ASSERT_NE(ptr2, MAP_FAILED);

	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 15 * page_size);
}

TEST_F(merge, mremap_unfaulted_between_correctly_placed_faulted)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr, *ptr2;

	/*
	 * Map one larger area:
	 *
	 * |-----------------------------------|
	 * |            unfaulted              |
	 * |-----------------------------------|
	 */
	ptr = mmap(&carveout[page_size], 15 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/*
	 * Fault in ptr:
	 *
	 * |-----------------------------------|
	 * |              faulted              |
	 * |-----------------------------------|
	 */
	ptr[0] = 'x';

	/*
	 * Unmap middle:
	 *
	 * |-----------|           |-----------|
	 * |  faulted  |           |  faulted  |
	 * |-----------|           |-----------|
	 *
	 * Now the faulted areas are compatible with each other (anon_vma the
	 * same, vma->vm_pgoff equal to virtual page offset).
	 */
	ASSERT_EQ(munmap(&ptr[5 * page_size], 5 * page_size), 0);

	/*
	 * Map a new area, ptr2:
	 *                                        \
	 * |-----------|           |-----------|  /  |-----------|
	 * |  faulted  |           |  faulted  |  \  | unfaulted |
	 * |-----------|           |-----------|  /  |-----------|
	 *      ptr                               \       ptr2
	 */
	ptr2 = mmap(&carveout[20 * page_size], 5 * page_size, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr2, MAP_FAILED);

	/*
	 * Finally, move ptr2 into place:
	 *
	 * |-----------|-----------|-----------|
	 * |  faulted  | unfaulted |  faulted  |
	 * |-----------|-----------|-----------|
	 *      ptr        ptr2         ptr3
	 *
	 * It should merge:
	 *
	 * |-----------------------------------|
	 * |              faulted              |
	 * |-----------------------------------|
	 */
	ptr2 = sys_mremap(ptr2, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, &ptr[5 * page_size]);
	ASSERT_NE(ptr2, MAP_FAILED);

	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 15 * page_size);
}

TEST_F(merge, mremap_correct_placed_faulted)
{
	unsigned int page_size = self->page_size;
	char *carveout = self->carveout;
	struct procmap_fd *procmap = &self->procmap;
	char *ptr, *ptr2, *ptr3;

	/*
	 * Map one larger area:
	 *
	 * |-----------------------------------|
	 * |            unfaulted              |
	 * |-----------------------------------|
	 */
	ptr = mmap(&carveout[page_size], 15 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/*
	 * Fault in ptr:
	 *
	 * |-----------------------------------|
	 * |              faulted              |
	 * |-----------------------------------|
	 */
	ptr[0] = 'x';

	/*
	 * Offset the final and middle 5 pages further away:
	 *                \                 \
	 * |-----------|  /  |-----------|  /  |-----------|
	 * |  faulted  |  \  |  faulted  |  \  |  faulted  |
	 * |-----------|  /  |-----------|  /  |-----------|
	 *      ptr       \       ptr2      \       ptr3
	 */
	ptr3 = &ptr[10 * page_size];
	ptr3 = sys_mremap(ptr3, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, ptr3 + page_size * 2000);
	ASSERT_NE(ptr3, MAP_FAILED);
	ptr2 = &ptr[5 * page_size];
	ptr2 = sys_mremap(ptr2, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, ptr2 + page_size * 1000);
	ASSERT_NE(ptr2, MAP_FAILED);

	/*
	 * Move ptr2 into its correct place:
	 *                            \
	 * |-----------|-----------|  /  |-----------|
	 * |  faulted  |  faulted  |  \  |  faulted  |
	 * |-----------|-----------|  /  |-----------|
	 *      ptr         ptr2      \       ptr3
	 *
	 * It should merge:
	 *                            \
	 * |-----------------------|  /  |-----------|
	 * |        faulted        |  \  |  faulted  |
	 * |-----------------------|  /  |-----------|
	 *            ptr             \       ptr3
	 */

	ptr2 = sys_mremap(ptr2, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, &ptr[5 * page_size]);
	ASSERT_NE(ptr2, MAP_FAILED);

	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 10 * page_size);

	/*
	 * Now move ptr out of place:
	 *                            \                 \
	 *             |-----------|  /  |-----------|  /  |-----------|
	 *             |  faulted  |  \  |  faulted  |  \  |  faulted  |
	 *             |-----------|  /  |-----------|  /  |-----------|
	 *                  ptr2      \       ptr       \       ptr3
	 */
	ptr = sys_mremap(ptr, 5 * page_size, 5 * page_size,
			 MREMAP_MAYMOVE | MREMAP_FIXED, ptr + page_size * 1000);
	ASSERT_NE(ptr, MAP_FAILED);

	/*
	 * Now move ptr back into place:
	 *                            \
	 * |-----------|-----------|  /  |-----------|
	 * |  faulted  |  faulted  |  \  |  faulted  |
	 * |-----------|-----------|  /  |-----------|
	 *      ptr         ptr2      \       ptr3
	 *
	 * It should merge:
	 *                            \
	 * |-----------------------|  /  |-----------|
	 * |        faulted        |  \  |  faulted  |
	 * |-----------------------|  /  |-----------|
	 *            ptr             \       ptr3
	 */
	ptr = sys_mremap(ptr, 5 * page_size, 5 * page_size,
			 MREMAP_MAYMOVE | MREMAP_FIXED, &carveout[page_size]);
	ASSERT_NE(ptr, MAP_FAILED);

	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 10 * page_size);

	/*
	 * Now move ptr out of place again:
	 *                            \                 \
	 *             |-----------|  /  |-----------|  /  |-----------|
	 *             |  faulted  |  \  |  faulted  |  \  |  faulted  |
	 *             |-----------|  /  |-----------|  /  |-----------|
	 *                  ptr2      \       ptr       \       ptr3
	 */
	ptr = sys_mremap(ptr, 5 * page_size, 5 * page_size,
			 MREMAP_MAYMOVE | MREMAP_FIXED, ptr + page_size * 1000);
	ASSERT_NE(ptr, MAP_FAILED);

	/*
	 * Now move ptr3 back into place:
	 *                                        \
	 *             |-----------|-----------|  /  |-----------|
	 *             |  faulted  |  faulted  |  \  |  faulted  |
	 *             |-----------|-----------|  /  |-----------|
	 *                  ptr2        ptr3      \       ptr
	 *
	 * It should merge:
	 *                                        \
	 *             |-----------------------|  /  |-----------|
	 *             |        faulted        |  \  |  faulted  |
	 *             |-----------------------|  /  |-----------|
	 *                        ptr2            \       ptr
	 */
	ptr3 = sys_mremap(ptr3, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, &ptr2[5 * page_size]);
	ASSERT_NE(ptr3, MAP_FAILED);

	ASSERT_TRUE(find_vma_procmap(procmap, ptr2));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr2);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr2 + 10 * page_size);

	/*
	 * Now move ptr back into place:
	 *
	 * |-----------|-----------------------|
	 * |  faulted  |        faulted        |
	 * |-----------|-----------------------|
	 *      ptr               ptr2
	 *
	 * It should merge:
	 *
	 * |-----------------------------------|
	 * |              faulted              |
	 * |-----------------------------------|
	 *                  ptr
	 */
	ptr = sys_mremap(ptr, 5 * page_size, 5 * page_size,
			 MREMAP_MAYMOVE | MREMAP_FIXED, &carveout[page_size]);
	ASSERT_NE(ptr, MAP_FAILED);

	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 15 * page_size);

	/*
	 * Now move ptr2 out of the way:
	 *                                        \
	 * |-----------|           |-----------|  /  |-----------|
	 * |  faulted  |           |  faulted  |  \  |  faulted  |
	 * |-----------|           |-----------|  /  |-----------|
	 *      ptr                     ptr3      \       ptr2
	 */
	ptr2 = sys_mremap(ptr2, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, ptr2 + page_size * 1000);
	ASSERT_NE(ptr2, MAP_FAILED);

	/*
	 * Now move it back:
	 *
	 * |-----------|-----------|-----------|
	 * |  faulted  |  faulted  |  faulted  |
	 * |-----------|-----------|-----------|
	 *      ptr         ptr2        ptr3
	 *
	 * It should merge:
	 *
	 * |-----------------------------------|
	 * |              faulted              |
	 * |-----------------------------------|
	 *                  ptr
	 */
	ptr2 = sys_mremap(ptr2, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, &ptr[5 * page_size]);
	ASSERT_NE(ptr2, MAP_FAILED);

	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 15 * page_size);

	/*
	 * Move ptr3 out of place:
	 *                                        \
	 * |-----------------------|              /  |-----------|
	 * |        faulted        |              \  |  faulted  |
	 * |-----------------------|              /  |-----------|
	 *            ptr                         \       ptr3
	 */
	ptr3 = sys_mremap(ptr3, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, ptr3 + page_size * 1000);
	ASSERT_NE(ptr3, MAP_FAILED);

	/*
	 * Now move it back:
	 *
	 * |-----------|-----------|-----------|
	 * |  faulted  |  faulted  |  faulted  |
	 * |-----------|-----------|-----------|
	 *      ptr         ptr2        ptr3
	 *
	 * It should merge:
	 *
	 * |-----------------------------------|
	 * |              faulted              |
	 * |-----------------------------------|
	 *                  ptr
	 */
	ptr3 = sys_mremap(ptr3, 5 * page_size, 5 * page_size,
			  MREMAP_MAYMOVE | MREMAP_FIXED, &ptr[10 * page_size]);
	ASSERT_NE(ptr3, MAP_FAILED);

	ASSERT_TRUE(find_vma_procmap(procmap, ptr));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr);
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr + 15 * page_size);
}

TEST_F(merge_with_fork, mremap_faulted_to_unfaulted_prev)
{
	struct procmap_fd *procmap = &self->procmap;
	unsigned int page_size = self->page_size;
	unsigned long offset;
	char *ptr_a, *ptr_b;

	/*
	 * mremap() such that A and B merge:
	 *
	 *                             |------------|
	 *                             |    \       |
	 *           |-----------|     |    /  |---------|
	 *           | unfaulted |     v    \  | faulted |
	 *           |-----------|          /  |---------|
	 *                 B                \       A
	 */

	/* Map VMA A into place. */
	ptr_a = mmap(&self->carveout[page_size + 3 * page_size],
		     3 * page_size,
		     PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr_a, MAP_FAILED);
	/* Fault it in. */
	ptr_a[0] = 'x';

	if (variant->forked) {
		pid_t pid = do_fork(&self->procmap);

		ASSERT_NE(pid, -1);
		if (pid != 0)
			return;
	}

	/*
	 * Now move it out of the way so we can place VMA B in position,
	 * unfaulted.
	 */
	ptr_a = mremap(ptr_a, 3 * page_size, 3 * page_size,
		       MREMAP_FIXED | MREMAP_MAYMOVE, &self->carveout[20 * page_size]);
	ASSERT_NE(ptr_a, MAP_FAILED);

	/* Map VMA B into place. */
	ptr_b = mmap(&self->carveout[page_size], 3 * page_size,
		     PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr_b, MAP_FAILED);

	/*
	 * Now move VMA A into position with MREMAP_DONTUNMAP to catch incorrect
	 * anon_vma propagation.
	 */
	ptr_a = mremap(ptr_a, 3 * page_size, 3 * page_size,
		       MREMAP_FIXED | MREMAP_MAYMOVE | MREMAP_DONTUNMAP,
		       &self->carveout[page_size + 3 * page_size]);
	ASSERT_NE(ptr_a, MAP_FAILED);

	/* The VMAs should have merged, if not forked. */
	ASSERT_TRUE(find_vma_procmap(procmap, ptr_b));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr_b);

	offset = variant->forked ? 3 * page_size : 6 * page_size;
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr_b + offset);
}

TEST_F(merge_with_fork, mremap_faulted_to_unfaulted_next)
{
	struct procmap_fd *procmap = &self->procmap;
	unsigned int page_size = self->page_size;
	unsigned long offset;
	char *ptr_a, *ptr_b;

	/*
	 * mremap() such that A and B merge:
	 *
	 *      |---------------------------|
	 *      |                   \       |
	 *      |    |-----------|  /  |---------|
	 *      v    | unfaulted |  \  | faulted |
	 *           |-----------|  /  |---------|
	 *                 B        \       A
	 *
	 * Then unmap VMA A to trigger the bug.
	 */

	/* Map VMA A into place. */
	ptr_a = mmap(&self->carveout[page_size], 3 * page_size,
		     PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr_a, MAP_FAILED);
	/* Fault it in. */
	ptr_a[0] = 'x';

	if (variant->forked) {
		pid_t pid = do_fork(&self->procmap);

		ASSERT_NE(pid, -1);
		if (pid != 0)
			return;
	}

	/*
	 * Now move it out of the way so we can place VMA B in position,
	 * unfaulted.
	 */
	ptr_a = mremap(ptr_a, 3 * page_size, 3 * page_size,
		       MREMAP_FIXED | MREMAP_MAYMOVE, &self->carveout[20 * page_size]);
	ASSERT_NE(ptr_a, MAP_FAILED);

	/* Map VMA B into place. */
	ptr_b = mmap(&self->carveout[page_size + 3 * page_size], 3 * page_size,
		     PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr_b, MAP_FAILED);

	/*
	 * Now move VMA A into position with MREMAP_DONTUNMAP to catch incorrect
	 * anon_vma propagation.
	 */
	ptr_a = mremap(ptr_a, 3 * page_size, 3 * page_size,
		       MREMAP_FIXED | MREMAP_MAYMOVE | MREMAP_DONTUNMAP,
		       &self->carveout[page_size]);
	ASSERT_NE(ptr_a, MAP_FAILED);

	/* The VMAs should have merged, if not forked. */
	ASSERT_TRUE(find_vma_procmap(procmap, ptr_a));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr_a);
	offset = variant->forked ? 3 * page_size : 6 * page_size;
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr_a + offset);
}

TEST_F(merge_with_fork, mremap_faulted_to_unfaulted_prev_unfaulted_next)
{
	struct procmap_fd *procmap = &self->procmap;
	unsigned int page_size = self->page_size;
	unsigned long offset;
	char *ptr_a, *ptr_b, *ptr_c;

	/*
	 * mremap() with MREMAP_DONTUNMAP such that A, B and C merge:
	 *
	 *                  |---------------------------|
	 *                  |                   \       |
	 * |-----------|    |    |-----------|  /  |---------|
	 * | unfaulted |    v    | unfaulted |  \  | faulted |
	 * |-----------|         |-----------|  /  |---------|
	 *       A                     C        \        B
	 */

	/* Map VMA B into place. */
	ptr_b = mmap(&self->carveout[page_size + 3 * page_size], 3 * page_size,
		     PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr_b, MAP_FAILED);
	/* Fault it in. */
	ptr_b[0] = 'x';

	if (variant->forked) {
		pid_t pid = do_fork(&self->procmap);

		ASSERT_NE(pid, -1);
		if (pid != 0)
			return;
	}

	/*
	 * Now move it out of the way so we can place VMAs A, C in position,
	 * unfaulted.
	 */
	ptr_b = mremap(ptr_b, 3 * page_size, 3 * page_size,
		       MREMAP_FIXED | MREMAP_MAYMOVE, &self->carveout[20 * page_size]);
	ASSERT_NE(ptr_b, MAP_FAILED);

	/* Map VMA A into place. */

	ptr_a = mmap(&self->carveout[page_size], 3 * page_size,
		     PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr_a, MAP_FAILED);

	/* Map VMA C into place. */
	ptr_c = mmap(&self->carveout[page_size + 3 * page_size + 3 * page_size],
		     3 * page_size, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr_c, MAP_FAILED);

	/*
	 * Now move VMA B into position with MREMAP_DONTUNMAP to catch incorrect
	 * anon_vma propagation.
	 */
	ptr_b = mremap(ptr_b, 3 * page_size, 3 * page_size,
		       MREMAP_FIXED | MREMAP_MAYMOVE | MREMAP_DONTUNMAP,
		       &self->carveout[page_size + 3 * page_size]);
	ASSERT_NE(ptr_b, MAP_FAILED);

	/* The VMAs should have merged, if not forked. */
	ASSERT_TRUE(find_vma_procmap(procmap, ptr_a));
	ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr_a);
	offset = variant->forked ? 3 * page_size : 9 * page_size;
	ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr_a + offset);

	/* If forked, B and C should also not have merged. */
	if (variant->forked) {
		ASSERT_TRUE(find_vma_procmap(procmap, ptr_b));
		ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr_b);
		ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr_b + 3 * page_size);
	}
}

TEST_F(merge_with_fork, mremap_faulted_to_unfaulted_prev_faulted_next)
{
	struct procmap_fd *procmap = &self->procmap;
	unsigned int page_size = self->page_size;
	char *ptr_a, *ptr_b, *ptr_bc;

	/*
	 * mremap() with MREMAP_DONTUNMAP such that A, B and C merge:
	 *
	 *                  |---------------------------|
	 *                  |                   \       |
	 * |-----------|    |    |-----------|  /  |---------|
	 * | unfaulted |    v    |  faulted  |  \  | faulted |
	 * |-----------|         |-----------|  /  |---------|
	 *       A                     C        \       B
	 */

	/*
	 * Map VMA B and C into place. We have to map them together so their
	 * anon_vma is the same and the vma->vm_pgoff's are correctly aligned.
	 */
	ptr_bc = mmap(&self->carveout[page_size + 3 * page_size],
		      3 * page_size + 3 * page_size,
		     PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr_bc, MAP_FAILED);

	/* Fault it in. */
	ptr_bc[0] = 'x';

	if (variant->forked) {
		pid_t pid = do_fork(&self->procmap);

		ASSERT_NE(pid, -1);
		if (pid != 0)
			return;
	}

	/*
	 * Now move VMA B out the way (splitting VMA BC) so we can place VMA A
	 * in position, unfaulted, and leave the remainder of the VMA we just
	 * moved in place, faulted, as VMA C.
	 */
	ptr_b = mremap(ptr_bc, 3 * page_size, 3 * page_size,
		       MREMAP_FIXED | MREMAP_MAYMOVE, &self->carveout[20 * page_size]);
	ASSERT_NE(ptr_b, MAP_FAILED);

	/* Map VMA A into place. */
	ptr_a = mmap(&self->carveout[page_size], 3 * page_size,
		     PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	ASSERT_NE(ptr_a, MAP_FAILED);

	/*
	 * Now move VMA B into position with MREMAP_DONTUNMAP to catch incorrect
	 * anon_vma propagation.
	 */
	ptr_b = mremap(ptr_b, 3 * page_size, 3 * page_size,
		       MREMAP_FIXED | MREMAP_MAYMOVE | MREMAP_DONTUNMAP,
		       &self->carveout[page_size + 3 * page_size]);
	ASSERT_NE(ptr_b, MAP_FAILED);

	/* The VMAs should have merged. A,B,C if unforked, B, C if forked. */
	if (variant->forked) {
		ASSERT_TRUE(find_vma_procmap(procmap, ptr_b));
		ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr_b);
		ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr_b + 6 * page_size);
	} else {
		ASSERT_TRUE(find_vma_procmap(procmap, ptr_a));
		ASSERT_EQ(procmap->query.vma_start, (unsigned long)ptr_a);
		ASSERT_EQ(procmap->query.vma_end, (unsigned long)ptr_a + 9 * page_size);
	}
}

TEST_HARNESS_MAIN
