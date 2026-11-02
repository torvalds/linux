// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <test_progs.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/user.h>
#ifndef PAGE_SIZE /* on some archs it comes in sys/user.h */
#define PAGE_SIZE getpagesize()
#endif

#include "cgroup_helpers.h"
#include "arena_memcg.skel.h"

#define CG_PATH		"/arena_memcg"

/* Reclaimable page cache the child builds up before it gets capped. */
#define RECLAIMABLE	(128 * 1024 * 1024)
/* Headroom left under memory.max, far less than the arena we fault in. */
#define HEADROOM	(8 * 1024 * 1024)
/* Arena to fault in; it only fits by reclaiming the page cache. */
#define ARENA_FAULT	(64 * 1024 * 1024)
/* Child exit code for "this environment cannot host the test". */
#define CHILD_UNSUPPORTED	9

static void dump_memcg(void)
{
	char buf[512];

	if (!read_cgroup_file(CG_PATH, "memory.max", buf, sizeof(buf)))
		fprintf(stderr, "memory.max: %s", buf);
	if (!read_cgroup_file(CG_PATH, "memory.peak", buf, sizeof(buf)))
		fprintf(stderr, "memory.peak: %s", buf);
	if (!read_cgroup_file(CG_PATH, "memory.events", buf, sizeof(buf)))
		fprintf(stderr, "memory.events:\n%s", buf);
}

/*
 * Fill the page cache with @size bytes of clean, reclaimable pages by
 * reading a sparse temp file, the way the cgroup selftests do. Returns the
 * fd, which must stay open: closing it drops the cache. Returns -EOPNOTSUPP
 * if the working directory cannot back such a file, -1 on error.
 */
static int alloc_pagecache(size_t size)
{
	struct statfs stfs;
	char buf[4096];
	size_t off;
	int fd;

	fd = open(".", O_TMPFILE | O_RDWR | O_EXCL, 0600);
	if (fd < 0)
		return errno == EOPNOTSUPP ? -EOPNOTSUPP : -1;
	/* tmpfs hands out shmem pages, which are not reclaimable without swap */
	if (fstatfs(fd, &stfs) || stfs.f_type == TMPFS_MAGIC) {
		close(fd);
		return -EOPNOTSUPP;
	}
	if (ftruncate(fd, size))
		goto err;
	for (off = 0; off < size; off += sizeof(buf))
		if (read(fd, buf, sizeof(buf)) < 0)
			goto err;
	return fd;
err:
	close(fd);
	return -1;
}

void serial_test_arena_memcg(void)
{
	int cgroup_fd = -1, status, err;
	const long ps = PAGE_SIZE;
	char buf[64];
	pid_t pid;

	err = setup_cgroup_environment();
	if (!ASSERT_OK(err, "setup_cgroup_environment"))
		goto out;

	cgroup_fd = create_and_get_cgroup(CG_PATH);
	if (!ASSERT_OK_FD(cgroup_fd, "create_and_get_cgroup"))
		goto out;

	/* No memory controller -> nothing to test. */
	if (read_cgroup_file(CG_PATH, "memory.current", buf, sizeof(buf))) {
		fprintf(stderr, "%s:SKIP:no memory controller or other env error\n",
			__func__);
		test__skip();
		goto out;
	}

	pid = fork();
	if (!ASSERT_GE(pid, 0, "fork"))
		goto out;
	if (pid == 0) {
		struct arena_memcg *cskel;
		__u32 i, npages;
		char *base;
		size_t sz;
		long cur;
		int fd;

		/*
		 * Everything runs in the child: the arena vma is VM_DONTCOPY so
		 * it does not survive fork(), and only the child should be under
		 * the limit. The work dir belongs to the parent, so use the
		 * _parent() helpers; errors come back as an exit code, ASSERT_*
		 * does not reach the parent from here.
		 */

		/* Step 1: join the memcg, so what follows is charged to it. */
		snprintf(buf, sizeof(buf), "%d", getpid());
		if (write_cgroup_file_parent(CG_PATH, "cgroup.procs", buf))
			_exit(2);

		/*
		 * Step 2: load the arena. A map is charged to whoever creates
		 * it, hence joining first.
		 */
		cskel = arena_memcg__open_and_load();
		if (!cskel)
			_exit(3);
		base = bpf_map__initial_value(cskel->maps.arena, &sz);
		if (!base)
			_exit(4);
		npages = ARENA_FAULT / ps;
		if (npages > bpf_map__max_entries(cskel->maps.arena))
			_exit(5);

		/* Step 3: make RECLAIMABLE bytes of clean page cache. */
		fd = alloc_pagecache(RECLAIMABLE);
		if (fd == -EOPNOTSUPP)
			_exit(CHILD_UNSUPPORTED);
		if (fd < 0)
			_exit(6);

		/*
		 * Step 4: set memory.max to what we use now plus HEADROOM. The
		 * page cache is already inside the limit, so only HEADROOM is
		 * left.
		 */
		if (read_cgroup_file_parent(CG_PATH, "memory.current", buf, sizeof(buf)))
			_exit(7);
		cur = strtol(buf, NULL, 10);
		snprintf(buf, sizeof(buf), "%ld", cur + HEADROOM);
		if (write_cgroup_file_parent(CG_PATH, "memory.max", buf))
			_exit(8);

		/*
		 * Step 5: fault ARENA_FAULT of arena in, much more than
		 * HEADROOM. Once it hits memory.max every further page has to
		 * come from reclaiming the page cache. With the fix the
		 * fault-in reclaims and all of it succeeds; without it the
		 * allocation cannot reclaim and we die on a valid address.
		 */
		for (i = 0; i < npages; i++)
			base[(size_t)i * ps] = 1;
		_exit(0); /* fd deliberately kept open until here */
	}

	if (!ASSERT_EQ(waitpid(pid, &status, 0), pid, "waitpid"))
		goto out;

	/* The working directory cannot hold a reclaimable page cache. */
	if (WIFEXITED(status) && WEXITSTATUS(status) == CHILD_UNSUPPORTED) {
		fprintf(stderr, "%s:SKIP:no disk-backed O_TMPFILE in cwd\n", __func__);
		test__skip();
		goto out;
	}

	/* A non-zero exit means the child failed to set up; the code says where. */
	if (WIFEXITED(status) && WEXITSTATUS(status)) {
		ASSERT_OK(WEXITSTATUS(status), "child setup");
		goto out;
	}

	/*
	 * With the fix the arena fault-in reclaims the page cache and every
	 * fault succeeds, so the child exits 0. Without it the allocation
	 * cannot reclaim, fails once the headroom is used up, and the child
	 * dies with SIGSEGV on a valid arena address.
	 */
	if (!ASSERT_TRUE(WIFEXITED(status) && !WEXITSTATUS(status),
			 "child faulted the arena in")) {
		if (WIFSIGNALED(status))
			fprintf(stderr, "child killed by signal %d\n", WTERMSIG(status));
		dump_memcg();
	}
out:
	if (cgroup_fd >= 0)
		close(cgroup_fd);
	cleanup_cgroup_environment();
}
