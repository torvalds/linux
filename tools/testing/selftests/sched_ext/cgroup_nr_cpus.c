// SPDX-License-Identifier: GPL-2.0
/*
 * Verify that scx_bpf_cgroup_nr_cpus() reports the number of CPUs in a
 * cgroup's effective cpuset, including inherited and updated cpusets.
 *
 * Copyright (c) 2026 NVIDIA Corporation.
 */

#define _GNU_SOURCE
#include <bpf/bpf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/limits.h>
#include <scx/common.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cgroup_nr_cpus.bpf.skel.h"
#include "cgroup_util.h"
#include "scx_test.h"

/*
 * Hierarchy under the cgroup2 root, all with the cpu controller enabled so
 * that ops.cgroup_init() runs for each of them:
 *
 *   parent		cpuset enabled by the root, enables cpuset for its children
 *   parent/child	owns a cpuset
 *   parent/child/leaf	no cpuset of its own, inherits child's
 */
struct cgroup_nr_cpus_ctx {
	struct cgroup_nr_cpus *skel;
	struct bpf_link *link;
	char root[PATH_MAX];
	char parent[PATH_MAX];
	char child[PATH_MAX];
	char leaf[PATH_MAX];
	bool parent_created;
	bool child_created;
	bool leaf_created;
};

static int join_path(char *dst, size_t dst_size, const char *parent, const char *name)
{
	int ret;

	ret = snprintf(dst, dst_size, "%s/%s", parent, name);
	if (ret < 0 || (size_t)ret >= dst_size)
		return -ENAMETOOLONG;
	return 0;
}

static u64 cgroup_id(const char *path)
{
	union {
		u64 id;
		unsigned char bytes[8];
	} id = {};
	struct file_handle *handle;
	int mount_id, ret;

	handle = calloc(1, sizeof(*handle) + sizeof(id));
	if (!handle)
		return 0;
	handle->handle_bytes = sizeof(id);
	ret = name_to_handle_at(AT_FDCWD, path, handle, &mount_id, 0);
	if (!ret && handle->handle_bytes == sizeof(id))
		memcpy(id.bytes, handle->f_handle, sizeof(id));
	free(handle);

	return ret ? 0 : id.id;
}

/*
 * Parse a cpulist such as "0-3,8,10-11". Return the number of CPUs and the
 * lowest and highest CPU in @first and @last, or -errno on failure.
 */
static int parse_cpulist(const char *cpulist, u32 *first, u32 *last)
{
	const char *p = cpulist;
	u32 lowest = UINT_MAX, highest = 0;
	int count = 0;

	while (*p && *p != '\n') {
		unsigned long start, end_cpu;
		char *end;

		errno = 0;
		start = strtoul(p, &end, 10);
		if (errno || end == p || start > INT_MAX)
			return -EINVAL;
		end_cpu = start;
		p = end;
		if (*p == '-') {
			end_cpu = strtoul(p + 1, &end, 10);
			if (errno || end == p + 1 || end_cpu > INT_MAX || end_cpu < start)
				return -EINVAL;
			p = end;
		}
		if (end_cpu - start + 1 > (unsigned long)(INT_MAX - count))
			return -EOVERFLOW;
		if (start < lowest)
			lowest = start;
		if (end_cpu > highest)
			highest = end_cpu;
		count += end_cpu - start + 1;
		if (*p == ',')
			p++;
		else if (*p && *p != '\n')
			return -EINVAL;
	}

	if (first)
		*first = lowest;
	if (last)
		*last = highest;
	return count;
}

/*
 * Number of CPUs in @cgroup's cpuset.cpus.effective, or -errno.
 *
 * A sparse cpulist can exceed a page on large systems. cg_read() does a single
 * bounded read, so size the buffer for the worst case of NR_CPUS=8192 and
 * reject a read that fills it rather than parsing a truncated list.
 */
static int effective_nr_cpus(const char *cgroup, u32 *first, u32 *last)
{
	static char buf[65536];

	if (cg_read(cgroup, "cpuset.cpus.effective", buf, sizeof(buf)))
		return -EIO;
	if (strlen(buf) >= sizeof(buf) - 1)
		return -EOVERFLOW;
	return parse_cpulist(buf, first, last);
}

/* Run the SYSCALL program to sample scx_bpf_cgroup_nr_cpus() for @path. */
static int kfunc_nr_cpus(struct cgroup_nr_cpus_ctx *ctx, const char *path, s64 *nr_cpus)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	u64 cgid;
	int err;

	cgid = cgroup_id(path);
	if (!cgid) {
		SCX_ERR("Failed to read cgroup ID of %s", path);
		return -ENOENT;
	}

	ctx->skel->bss->query_cgid = cgid;
	err = bpf_prog_test_run_opts(bpf_program__fd(ctx->skel->progs.cgroup_nr_cpus_read),
				     &topts);
	if (err || topts.retval) {
		SCX_ERR("BPF_PROG_RUN failed for %s (err=%d retval=%d)",
			path, err, (int)topts.retval);
		return err ?: -EIO;
	}

	*nr_cpus = ctx->skel->data->query_nr_cpus;
	return 0;
}

static bool check_nr_cpus(struct cgroup_nr_cpus_ctx *ctx, const char *path, int expected,
			  const char *what)
{
	s64 nr_cpus;

	if (kfunc_nr_cpus(ctx, path, &nr_cpus))
		return false;
	if (nr_cpus != expected) {
		SCX_ERR("%s: expected %d CPUs, got %lld", what, expected,
			(long long)nr_cpus);
		return false;
	}
	return true;
}

/*
 * Like check_nr_cpus() but tolerate a transient mismatch. A cpuset css being
 * disabled stays attached to its cgroup until it's asynchronously offlined, and
 * cpuset_num_cpus() keeps reporting its stale mask until then.
 */
static bool wait_nr_cpus(struct cgroup_nr_cpus_ctx *ctx, const char *path, int expected,
			 const char *what)
{
	s64 nr_cpus = -1;
	int i;

	for (i = 0; i < 1000; i++) {
		if (kfunc_nr_cpus(ctx, path, &nr_cpus))
			return false;
		if (nr_cpus == expected)
			return true;
		usleep(1000);
	}
	SCX_ERR("%s: expected %d CPUs, got %lld", what, expected, (long long)nr_cpus);
	return false;
}

static bool controller_enabled(const char *cgroup, const char *file, const char *controller)
{
	char buf[4096], *saveptr, *token;

	if (cg_read(cgroup, file, buf, sizeof(buf)))
		return false;
	for (token = strtok_r(buf, "\n ", &saveptr); token;
	     token = strtok_r(NULL, "\n ", &saveptr))
		if (!strcmp(token, controller))
			return true;
	return false;
}

static void cleanup_ctx(struct cgroup_nr_cpus_ctx *ctx)
{
	bpf_link__destroy(ctx->link);
	cgroup_nr_cpus__destroy(ctx->skel);
	if (ctx->leaf_created)
		cg_destroy(ctx->leaf);
	if (ctx->child_created)
		cg_destroy(ctx->child);
	if (ctx->parent_created)
		cg_destroy(ctx->parent);
}

/*
 * Enable @controller in the root's subtree_control if needed. Like the cgroup
 * selftests, leave it enabled afterwards: the root is shared, and another
 * manager may start relying on the controller while the test runs.
 */
static enum scx_test_status enable_controller(const char *root, const char *controller)
{
	char value[32];

	if (controller_enabled(root, "cgroup.subtree_control", controller))
		return SCX_TEST_PASS;
	if (!controller_enabled(root, "cgroup.controllers", controller))
		return SCX_TEST_SKIP;

	snprintf(value, sizeof(value), "+%s", controller);
	if (cg_write(root, "cgroup.subtree_control", value))
		return SCX_TEST_SKIP;
	return SCX_TEST_PASS;
}

static enum scx_test_status setup_cgroups(struct cgroup_nr_cpus_ctx *ctx)
{
	enum scx_test_status status;
	char name[64];

	if (cg_find_unified_root(ctx->root, sizeof(ctx->root), NULL))
		return SCX_TEST_SKIP;

	status = enable_controller(ctx->root, "cpu");
	if (status != SCX_TEST_PASS)
		return status;
	status = enable_controller(ctx->root, "cpuset");
	if (status != SCX_TEST_PASS)
		return status;

	snprintf(name, sizeof(name), "scx_nr_cpus_%d", getpid());
	if (join_path(ctx->parent, sizeof(ctx->parent), ctx->root, name) ||
	    join_path(ctx->child, sizeof(ctx->child), ctx->parent, "child") ||
	    join_path(ctx->leaf, sizeof(ctx->leaf), ctx->child, "leaf")) {
		SCX_ERR("Cgroup path is too long");
		return SCX_TEST_FAIL;
	}

	if (cg_create(ctx->parent)) {
		SCX_ERR("Failed to create cgroup %s", ctx->parent);
		return SCX_TEST_FAIL;
	}
	ctx->parent_created = true;
	if (cg_write(ctx->parent, "cgroup.subtree_control", "+cpu +cpuset")) {
		SCX_ERR("Failed to enable controllers in %s", ctx->parent);
		return SCX_TEST_FAIL;
	}
	if (cg_create(ctx->child)) {
		SCX_ERR("Failed to create cgroup %s", ctx->child);
		return SCX_TEST_FAIL;
	}
	ctx->child_created = true;
	if (cg_write(ctx->child, "cgroup.subtree_control", "+cpu")) {
		SCX_ERR("Failed to enable cpu in %s", ctx->child);
		return SCX_TEST_FAIL;
	}
	if (cg_create(ctx->leaf)) {
		SCX_ERR("Failed to create cgroup %s", ctx->leaf);
		return SCX_TEST_FAIL;
	}
	ctx->leaf_created = true;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *arg)
{
	struct cgroup_nr_cpus_ctx ctx = {};
	enum scx_test_status status;
	char value[32];
	u32 first, last;
	int nr_root, nr_child;

	(void)arg;

	/*
	 * SCX_ENUM_INIT() exits the process if vmlinux BTF can't be loaded, so
	 * run it before creating any cgroups that would then be left behind.
	 */
	ctx.skel = cgroup_nr_cpus__open();
	if (!ctx.skel) {
		SCX_ERR("Failed to open skel");
		return SCX_TEST_FAIL;
	}
	SCX_ENUM_INIT(ctx.skel);

	status = setup_cgroups(&ctx);
	if (status != SCX_TEST_PASS)
		goto out;
	status = SCX_TEST_FAIL;

	nr_root = effective_nr_cpus(ctx.root, NULL, NULL);
	nr_child = effective_nr_cpus(ctx.child, &first, &last);
	if (nr_root < 0 || nr_child < 0) {
		SCX_ERR("Failed to read effective cpusets");
		goto out;
	}
	/* The effective cpuset can be empty, e.g. under a partition root. */
	if (nr_child < 2) {
		status = SCX_TEST_SKIP;
		goto out;
	}

	ctx.skel->bss->init_cgid = cgroup_id(ctx.leaf);
	if (!ctx.skel->bss->init_cgid) {
		SCX_ERR("Failed to read cgroup ID of %s", ctx.leaf);
		goto out;
	}
	if (cgroup_nr_cpus__load(ctx.skel)) {
		SCX_ERR("Failed to load skel");
		goto out;
	}

	/* The kfunc must be callable without a scheduler attached. */
	if (!check_nr_cpus(&ctx, ctx.root, nr_root, "root") ||
	    !check_nr_cpus(&ctx, ctx.child, nr_child, "child") ||
	    !check_nr_cpus(&ctx, ctx.leaf, nr_child, "inherited leaf"))
		goto out;

	/* ops.cgroup_init() runs for existing cgroups when attaching. */
	ctx.link = bpf_map__attach_struct_ops(ctx.skel->maps.cgroup_nr_cpus_ops);
	if (!ctx.link) {
		SCX_ERR("Failed to attach scheduler");
		goto out;
	}
	if (ctx.skel->data->init_nr_cpus != nr_child) {
		SCX_ERR("ops.cgroup_init(): expected %d CPUs, got %lld", nr_child,
			(long long)ctx.skel->data->init_nr_cpus);
		goto out;
	}

	/* Non-contiguous cpuset, observed by the owner and by the inheritor. */
	if (nr_child > 2) {
		snprintf(value, sizeof(value), "%u,%u", first, last);
		if (cg_write(ctx.child, "cpuset.cpus", value)) {
			SCX_ERR("Failed to set cpuset.cpus=%s for %s", value, ctx.child);
			goto out;
		}
		if (!check_nr_cpus(&ctx, ctx.child, 2, "sparse child") ||
		    !check_nr_cpus(&ctx, ctx.leaf, 2, "sparse inherited leaf"))
			goto out;
	}

	snprintf(value, sizeof(value), "%u", first);
	if (cg_write(ctx.child, "cpuset.cpus", value)) {
		SCX_ERR("Failed to set cpuset.cpus=%s for %s", value, ctx.child);
		goto out;
	}
	if (!check_nr_cpus(&ctx, ctx.child, 1, "single-CPU child") ||
	    !check_nr_cpus(&ctx, ctx.leaf, 1, "single-CPU inherited leaf"))
		goto out;

	/*
	 * Disabling cpuset below @parent makes @child and @leaf inherit
	 * @parent's effective cpuset, which spans all of the root's CPUs.
	 */
	if (cg_write(ctx.parent, "cgroup.subtree_control", "-cpuset")) {
		SCX_ERR("Failed to disable cpuset in %s", ctx.parent);
		goto out;
	}
	nr_child = effective_nr_cpus(ctx.parent, NULL, NULL);
	if (nr_child < 0) {
		SCX_ERR("Failed to read effective cpuset of %s", ctx.parent);
		goto out;
	}
	if (!wait_nr_cpus(&ctx, ctx.child, nr_child, "child after cpuset disable") ||
	    !wait_nr_cpus(&ctx, ctx.leaf, nr_child, "leaf after cpuset disable"))
		goto out;

	if (UEI_EXITED(ctx.skel, uei)) {
		SCX_ERR("Scheduler exited unexpectedly");
		goto out;
	}

	status = SCX_TEST_PASS;
out:
	cleanup_ctx(&ctx);
	return status;
}

struct scx_test cgroup_nr_cpus = {
	.name = "cgroup_nr_cpus",
	.description = "Verify scx_bpf_cgroup_nr_cpus() reports effective cpuset CPU counts",
	.run = run,
};
REGISTER_SCX_TEST(&cgroup_nr_cpus)
