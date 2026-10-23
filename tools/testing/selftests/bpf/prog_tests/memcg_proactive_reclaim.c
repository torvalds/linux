// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <limits.h>
#include <linux/magic.h>
#include <sys/statfs.h>
#include <unistd.h>

#include "cgroup_helpers.h"
#include "memcg_proactive_reclaim.skel.h"

#define CG_PATH "/memcg_proactive_reclaim"

/*
 * Large enough that a single reclaim pass cannot come close to it, so that
 * "reclaimed less than was asked for" is not page-size noise: one pass is
 * capped at MEMCG_CHARGE_BATCH pages, which is 256 KiB on 4K pages but 4 MiB
 * on 64K pages.
 */
#define FILE_SIZE (32 * 1024 * 1024UL)
#define BUF_SIZE (64 * 1024)

struct reclaim_args {
	__u64 cgroup_id;
	__u64 size;
};

/*
 * The data file has to sit on a writable regular filesystem: tmpfs pages are
 * charged as shmem, so whether they can be reclaimed at all depends on swap
 * being available, and a read-only mount cannot host the file at all (EROFS
 * applies to root too). /tmp is tmpfs on many systems, and test_progs is
 * routinely run from a tmpfs working directory, so both candidates need the
 * check.
 */
static const char *workload_dir(void)
{
	static const char * const dirs[] = { "/tmp", "." };
	struct statfs st;
	int i;

	for (i = 0; i < ARRAY_SIZE(dirs); i++)
		if (!statfs(dirs[i], &st) && st.f_type != TMPFS_MAGIC &&
		    st.f_type != RAMFS_MAGIC && !access(dirs[i], W_OK))
			return dirs[i];

	return NULL;
}

void test_memcg_proactive_reclaim(void)
{
	struct memcg_proactive_reclaim *skel = NULL;
	struct reclaim_args args = {};

	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .ctx_in = &args,
		    .ctx_size_in = sizeof(args));

	char data_file[PATH_MAX];
	static char buf[BUF_SIZE];
	__u64 cgroup_id;
	const char *dir;
	off_t off;
	int cg_fd = -1, data_fd = -1, err;

	/*
	 * Both candidate directories can be unusable: tmpfs (initramfs-based
	 * CI, for instance) or a read-only mount. That is an environment
	 * limitation, not a test failure.
	 */
	dir = workload_dir();
	if (!dir) {
		test__skip();
		return;
	}

	snprintf(data_file, sizeof(data_file),
		 "%s/memcg_proactive_reclaim_XXXXXX", dir);
	data_fd = mkstemp(data_file);
	if (!ASSERT_GE(data_fd, 0, "mkstemp"))
		return;

	cg_fd = cgroup_setup_and_join(CG_PATH);
	if (!ASSERT_OK_FD(cg_fd, "cgroup_setup_and_join"))
		goto out;

	cgroup_id = get_cgroup_id(CG_PATH);
	if (!ASSERT_GT(cgroup_id, 0, "get_cgroup_id"))
		goto out;

	skel = memcg_proactive_reclaim__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto out;

	/* Charge FILE_SIZE of clean page cache to the cgroup. */
	for (off = 0; off < (off_t)FILE_SIZE; off += sizeof(buf))
		if (!ASSERT_EQ(write(data_fd, buf, sizeof(buf)),
			       (ssize_t)sizeof(buf), "write"))
			goto out;
	if (!ASSERT_OK(fsync(data_fd), "fsync"))
		goto out;

	args.cgroup_id = cgroup_id;
	args.size = FILE_SIZE;
	skel->bss->reclaimed = 0;
	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.memcg_proactive_reclaim),
				     &opts);
	if (!ASSERT_OK(err, "test_run"))
		goto out;
	if (!ASSERT_EQ(opts.retval, 0, "retval"))
		goto out;

	/*
	 * A single call is a single bounded pass: it reclaims something, but
	 * stops well short of the requested size instead of retrying until the
	 * goal is reached the way a write to memory.reclaim does.
	 */
	ASSERT_GT(skel->bss->reclaimed, 0, "reclaimed");
	ASSERT_LT(skel->bss->reclaimed, (__s64)FILE_SIZE, "single pass");

out:
	if (skel)
		memcg_proactive_reclaim__destroy(skel);
	if (cg_fd >= 0)
		close(cg_fd);
	if (data_fd >= 0) {
		close(data_fd);
		unlink(data_file);
	}
	cleanup_cgroup_environment();
}
