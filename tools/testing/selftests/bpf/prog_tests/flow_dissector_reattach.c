// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for attaching, detaching, and replacing flow_dissector BPF program.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/bpf.h>
#include <bpf/bpf.h>

#include "test_progs.h"

static int init_net = -1;

static __u32 query_attached_prog_id(int netns)
{
	__u32 prog_ids[1] = {};
	__u32 prog_cnt = ARRAY_SIZE(prog_ids);
	int err;

	err = bpf_prog_query(netns, BPF_FLOW_DISSECTOR, 0, NULL,
			     prog_ids, &prog_cnt);
	if (CHECK_FAIL(err)) {
		perror("bpf_prog_query");
		return 0;
	}

	return prog_cnt == 1 ? prog_ids[0] : 0;
}

static bool prog_is_attached(int netns)
{
	return query_attached_prog_id(netns) > 0;
}

static int load_prog(enum bpf_prog_type type)
{
	struct bpf_insn prog[] = {
		BPF_MOV64_IMM(BPF_REG_0, BPF_OK),
		BPF_EXIT_INSN(),
	};
	int fd;

	fd = bpf_load_program(type, prog, ARRAY_SIZE(prog), "GPL", 0, NULL, 0);
	if (CHECK_FAIL(fd < 0))
		perror("bpf_load_program");

	return fd;
}

static __u32 query_prog_id(int prog)
{
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	int err;

	err = bpf_obj_get_info_by_fd(prog, &info, &info_len);
	if (CHECK_FAIL(err || info_len != sizeof(info))) {
		perror("bpf_obj_get_info_by_fd");
		return 0;
	}

	return info.id;
}

static int unshare_net(int old_net)
{
	int err, new_net;

	err = unshare(CLONE_NEWNET);
	if (CHECK_FAIL(err)) {
		perror("unshare(CLONE_NEWNET)");
		return -1;
	}
	new_net = open("/proc/self/ns/net", O_RDONLY);
	if (CHECK_FAIL(new_net < 0)) {
		perror("open(/proc/self/ns/net)");
		setns(old_net, CLONE_NEWNET);
		return -1;
	}
	return new_net;
}

static void test_prog_attach_prog_attach(int netns, int prog1, int prog2)
{
	int err;

	err = bpf_prog_attach(prog1, 0, BPF_FLOW_DISSECTOR, 0);
	if (CHECK_FAIL(err)) {
		perror("bpf_prog_attach(prog1)");
		return;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	/* Expect success when attaching a different program */
	err = bpf_prog_attach(prog2, 0, BPF_FLOW_DISSECTOR, 0);
	if (CHECK_FAIL(err)) {
		perror("bpf_prog_attach(prog2) #1");
		goto out_detach;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog2));

	/* Expect failure when attaching the same program twice */
	err = bpf_prog_attach(prog2, 0, BPF_FLOW_DISSECTOR, 0);
	if (CHECK_FAIL(!err || errno != EINVAL))
		perror("bpf_prog_attach(prog2) #2");
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog2));

out_detach:
	err = bpf_prog_detach2(prog2, 0, BPF_FLOW_DISSECTOR);
	if (CHECK_FAIL(err))
		perror("bpf_prog_detach");
	CHECK_FAIL(prog_is_attached(netns));
}

static void test_link_create_link_create(int netns, int prog1, int prog2)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, opts);
	int link1, link2;

	link1 = bpf_link_create(prog1, netns, BPF_FLOW_DISSECTOR, &opts);
	if (CHECK_FAIL(link < 0)) {
		perror("bpf_link_create(prog1)");
		return;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	/* Expect failure creating link when another link exists */
	errno = 0;
	link2 = bpf_link_create(prog2, netns, BPF_FLOW_DISSECTOR, &opts);
	if (CHECK_FAIL(link2 >= 0 || errno != E2BIG))
		perror("bpf_prog_attach(prog2) expected E2BIG");
	if (link2 >= 0)
		close(link2);
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	close(link1);
	CHECK_FAIL(prog_is_attached(netns));
}

static void test_prog_attach_link_create(int netns, int prog1, int prog2)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, opts);
	int err, link;

	err = bpf_prog_attach(prog1, 0, BPF_FLOW_DISSECTOR, 0);
	if (CHECK_FAIL(err)) {
		perror("bpf_prog_attach(prog1)");
		return;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	/* Expect failure creating link when prog attached */
	errno = 0;
	link = bpf_link_create(prog2, netns, BPF_FLOW_DISSECTOR, &opts);
	if (CHECK_FAIL(link >= 0 || errno != EEXIST))
		perror("bpf_link_create(prog2) expected EEXIST");
	if (link >= 0)
		close(link);
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	err = bpf_prog_detach2(prog1, 0, BPF_FLOW_DISSECTOR);
	if (CHECK_FAIL(err))
		perror("bpf_prog_detach");
	CHECK_FAIL(prog_is_attached(netns));
}

static void test_link_create_prog_attach(int netns, int prog1, int prog2)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, opts);
	int err, link;

	link = bpf_link_create(prog1, netns, BPF_FLOW_DISSECTOR, &opts);
	if (CHECK_FAIL(link < 0)) {
		perror("bpf_link_create(prog1)");
		return;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	/* Expect failure attaching prog when link exists */
	errno = 0;
	err = bpf_prog_attach(prog2, 0, BPF_FLOW_DISSECTOR, 0);
	if (CHECK_FAIL(!err || errno != EEXIST))
		perror("bpf_prog_attach(prog2) expected EEXIST");
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	close(link);
	CHECK_FAIL(prog_is_attached(netns));
}

static void test_link_create_prog_detach(int netns, int prog1, int prog2)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, opts);
	int err, link;

	link = bpf_link_create(prog1, netns, BPF_FLOW_DISSECTOR, &opts);
	if (CHECK_FAIL(link < 0)) {
		perror("bpf_link_create(prog1)");
		return;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	/* Expect failure detaching prog when link exists */
	errno = 0;
	err = bpf_prog_detach2(prog1, 0, BPF_FLOW_DISSECTOR);
	if (CHECK_FAIL(!err || errno != EINVAL))
		perror("bpf_prog_detach expected EINVAL");
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	close(link);
	CHECK_FAIL(prog_is_attached(netns));
}

static void test_prog_attach_detach_query(int netns, int prog1, int prog2)
{
	int err;

	err = bpf_prog_attach(prog1, 0, BPF_FLOW_DISSECTOR, 0);
	if (CHECK_FAIL(err)) {
		perror("bpf_prog_attach(prog1)");
		return;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	err = bpf_prog_detach2(prog1, 0, BPF_FLOW_DISSECTOR);
	if (CHECK_FAIL(err)) {
		perror("bpf_prog_detach");
		return;
	}

	/* Expect no prog attached after successful detach */
	CHECK_FAIL(prog_is_attached(netns));
}

static void test_link_create_close_query(int netns, int prog1, int prog2)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, opts);
	int link;

	link = bpf_link_create(prog1, netns, BPF_FLOW_DISSECTOR, &opts);
	if (CHECK_FAIL(link < 0)) {
		perror("bpf_link_create(prog1)");
		return;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	close(link);
	/* Expect no prog attached after closing last link FD */
	CHECK_FAIL(prog_is_attached(netns));
}

static void test_link_update_no_old_prog(int netns, int prog1, int prog2)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, create_opts);
	DECLARE_LIBBPF_OPTS(bpf_link_update_opts, update_opts);
	int err, link;

	link = bpf_link_create(prog1, netns, BPF_FLOW_DISSECTOR, &create_opts);
	if (CHECK_FAIL(link < 0)) {
		perror("bpf_link_create(prog1)");
		return;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	/* Expect success replacing the prog when old prog not specified */
	update_opts.flags = 0;
	update_opts.old_prog_fd = 0;
	err = bpf_link_update(link, prog2, &update_opts);
	if (CHECK_FAIL(err))
		perror("bpf_link_update");
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog2));

	close(link);
	CHECK_FAIL(prog_is_attached(netns));
}

static void test_link_update_replace_old_prog(int netns, int prog1, int prog2)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, create_opts);
	DECLARE_LIBBPF_OPTS(bpf_link_update_opts, update_opts);
	int err, link;

	link = bpf_link_create(prog1, netns, BPF_FLOW_DISSECTOR, &create_opts);
	if (CHECK_FAIL(link < 0)) {
		perror("bpf_link_create(prog1)");
		return;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	/* Expect success F_REPLACE and old prog specified to succeed */
	update_opts.flags = BPF_F_REPLACE;
	update_opts.old_prog_fd = prog1;
	err = bpf_link_update(link, prog2, &update_opts);
	if (CHECK_FAIL(err))
		perror("bpf_link_update");
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog2));

	close(link);
	CHECK_FAIL(prog_is_attached(netns));
}

static void test_link_update_same_prog(int netns, int prog1, int prog2)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, create_opts);
	DECLARE_LIBBPF_OPTS(bpf_link_update_opts, update_opts);
	int err, link;

	link = bpf_link_create(prog1, netns, BPF_FLOW_DISSECTOR, &create_opts);
	if (CHECK_FAIL(link < 0)) {
		perror("bpf_link_create(prog1)");
		return;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	/* Expect success updating the prog with the same one */
	update_opts.flags = 0;
	update_opts.old_prog_fd = 0;
	err = bpf_link_update(link, prog1, &update_opts);
	if (CHECK_FAIL(err))
		perror("bpf_link_update");
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	close(link);
	CHECK_FAIL(prog_is_attached(netns));
}

static void test_link_update_invalid_opts(int netns, int prog1, int prog2)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, create_opts);
	DECLARE_LIBBPF_OPTS(bpf_link_update_opts, update_opts);
	int err, link;

	link = bpf_link_create(prog1, netns, BPF_FLOW_DISSECTOR, &create_opts);
	if (CHECK_FAIL(link < 0)) {
		perror("bpf_link_create(prog1)");
		return;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	/* Expect update to fail w/ old prog FD but w/o F_REPLACE*/
	errno = 0;
	update_opts.flags = 0;
	update_opts.old_prog_fd = prog1;
	err = bpf_link_update(link, prog2, &update_opts);
	if (CHECK_FAIL(!err || errno != EINVAL)) {
		perror("bpf_link_update expected EINVAL");
		goto out_close;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	/* Expect update to fail on old prog FD mismatch */
	errno = 0;
	update_opts.flags = BPF_F_REPLACE;
	update_opts.old_prog_fd = prog2;
	err = bpf_link_update(link, prog2, &update_opts);
	if (CHECK_FAIL(!err || errno != EPERM)) {
		perror("bpf_link_update expected EPERM");
		goto out_close;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	/* Expect update to fail for invalid old prog FD */
	errno = 0;
	update_opts.flags = BPF_F_REPLACE;
	update_opts.old_prog_fd = -1;
	err = bpf_link_update(link, prog2, &update_opts);
	if (CHECK_FAIL(!err || errno != EBADF)) {
		perror("bpf_link_update expected EBADF");
		goto out_close;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	/* Expect update to fail with invalid flags */
	errno = 0;
	update_opts.flags = BPF_F_ALLOW_MULTI;
	update_opts.old_prog_fd = 0;
	err = bpf_link_update(link, prog2, &update_opts);
	if (CHECK_FAIL(!err || errno != EINVAL))
		perror("bpf_link_update expected EINVAL");
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

out_close:
	close(link);
	CHECK_FAIL(prog_is_attached(netns));
}

static void test_link_update_invalid_prog(int netns, int prog1, int prog2)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, create_opts);
	DECLARE_LIBBPF_OPTS(bpf_link_update_opts, update_opts);
	int err, link, prog3;

	link = bpf_link_create(prog1, netns, BPF_FLOW_DISSECTOR, &create_opts);
	if (CHECK_FAIL(link < 0)) {
		perror("bpf_link_create(prog1)");
		return;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	/* Expect failure when new prog FD is not valid */
	errno = 0;
	update_opts.flags = 0;
	update_opts.old_prog_fd = 0;
	err = bpf_link_update(link, -1, &update_opts);
	if (CHECK_FAIL(!err || errno != EBADF)) {
		perror("bpf_link_update expected EINVAL");
		goto out_close_link;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	prog3 = load_prog(BPF_PROG_TYPE_SOCKET_FILTER);
	if (prog3 < 0)
		goto out_close_link;

	/* Expect failure when new prog FD type doesn't match */
	errno = 0;
	update_opts.flags = 0;
	update_opts.old_prog_fd = 0;
	err = bpf_link_update(link, prog3, &update_opts);
	if (CHECK_FAIL(!err || errno != EINVAL))
		perror("bpf_link_update expected EINVAL");
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	close(prog3);
out_close_link:
	close(link);
	CHECK_FAIL(prog_is_attached(netns));
}

static void test_link_update_netns_gone(int netns, int prog1, int prog2)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, create_opts);
	DECLARE_LIBBPF_OPTS(bpf_link_update_opts, update_opts);
	int err, link, old_net;

	old_net = netns;
	netns = unshare_net(old_net);
	if (netns < 0)
		return;

	link = bpf_link_create(prog1, netns, BPF_FLOW_DISSECTOR, &create_opts);
	if (CHECK_FAIL(link < 0)) {
		perror("bpf_link_create(prog1)");
		return;
	}
	CHECK_FAIL(query_attached_prog_id(netns) != query_prog_id(prog1));

	close(netns);
	err = setns(old_net, CLONE_NEWNET);
	if (CHECK_FAIL(err)) {
		perror("setns(CLONE_NEWNET)");
		close(link);
		return;
	}

	/* Expect failure when netns destroyed */
	errno = 0;
	update_opts.flags = 0;
	update_opts.old_prog_fd = 0;
	err = bpf_link_update(link, prog2, &update_opts);
	if (CHECK_FAIL(!err || errno != ENOLINK))
		perror("bpf_link_update");

	close(link);
}

static void test_link_get_info(int netns, int prog1, int prog2)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, create_opts);
	DECLARE_LIBBPF_OPTS(bpf_link_update_opts, update_opts);
	struct bpf_link_info info = {};
	struct stat netns_stat = {};
	__u32 info_len, link_id;
	int err, link, old_net;

	old_net = netns;
	netns = unshare_net(old_net);
	if (netns < 0)
		return;

	err = fstat(netns, &netns_stat);
	if (CHECK_FAIL(err)) {
		perror("stat(netns)");
		goto out_resetns;
	}

	link = bpf_link_create(prog1, netns, BPF_FLOW_DISSECTOR, &create_opts);
	if (CHECK_FAIL(link < 0)) {
		perror("bpf_link_create(prog1)");
		goto out_resetns;
	}

	info_len = sizeof(info);
	err = bpf_obj_get_info_by_fd(link, &info, &info_len);
	if (CHECK_FAIL(err)) {
		perror("bpf_obj_get_info");
		goto out_unlink;
	}
	CHECK_FAIL(info_len != sizeof(info));

	/* Expect link info to be sane and match prog and netns details */
	CHECK_FAIL(info.type != BPF_LINK_TYPE_NETNS);
	CHECK_FAIL(info.id == 0);
	CHECK_FAIL(info.prog_id != query_prog_id(prog1));
	CHECK_FAIL(info.netns.netns_ino != netns_stat.st_ino);
	CHECK_FAIL(info.netns.attach_type != BPF_FLOW_DISSECTOR);

	update_opts.flags = 0;
	update_opts.old_prog_fd = 0;
	err = bpf_link_update(link, prog2, &update_opts);
	if (CHECK_FAIL(err)) {
		perror("bpf_link_update(prog2)");
		goto out_unlink;
	}

	link_id = info.id;
	info_len = sizeof(info);
	err = bpf_obj_get_info_by_fd(link, &info, &info_len);
	if (CHECK_FAIL(err)) {
		perror("bpf_obj_get_info");
		goto out_unlink;
	}
	CHECK_FAIL(info_len != sizeof(info));

	/* Expect no info change after update except in prog id */
	CHECK_FAIL(info.type != BPF_LINK_TYPE_NETNS);
	CHECK_FAIL(info.id != link_id);
	CHECK_FAIL(info.prog_id != query_prog_id(prog2));
	CHECK_FAIL(info.netns.netns_ino != netns_stat.st_ino);
	CHECK_FAIL(info.netns.attach_type != BPF_FLOW_DISSECTOR);

	/* Leave netns link is attached to and close last FD to it */
	err = setns(old_net, CLONE_NEWNET);
	if (CHECK_FAIL(err)) {
		perror("setns(NEWNET)");
		goto out_unlink;
	}
	close(netns);
	old_net = -1;
	netns = -1;

	info_len = sizeof(info);
	err = bpf_obj_get_info_by_fd(link, &info, &info_len);
	if (CHECK_FAIL(err)) {
		perror("bpf_obj_get_info");
		goto out_unlink;
	}
	CHECK_FAIL(info_len != sizeof(info));

	/* Expect netns_ino to change to 0 */
	CHECK_FAIL(info.type != BPF_LINK_TYPE_NETNS);
	CHECK_FAIL(info.id != link_id);
	CHECK_FAIL(info.prog_id != query_prog_id(prog2));
	CHECK_FAIL(info.netns.netns_ino != 0);
	CHECK_FAIL(info.netns.attach_type != BPF_FLOW_DISSECTOR);

out_unlink:
	close(link);
out_resetns:
	if (old_net != -1)
		setns(old_net, CLONE_NEWNET);
	if (netns != -1)
		close(netns);
}

static void run_tests(int netns)
{
	struct test {
		const char *test_name;
		void (*test_func)(int netns, int prog1, int prog2);
	} tests[] = {
		{ "prog attach, prog attach",
		  test_prog_attach_prog_attach },
		{ "link create, link create",
		  test_link_create_link_create },
		{ "prog attach, link create",
		  test_prog_attach_link_create },
		{ "link create, prog attach",
		  test_link_create_prog_attach },
		{ "link create, prog detach",
		  test_link_create_prog_detach },
		{ "prog attach, detach, query",
		  test_prog_attach_detach_query },
		{ "link create, close, query",
		  test_link_create_close_query },
		{ "link update no old prog",
		  test_link_update_no_old_prog },
		{ "link update with replace old prog",
		  test_link_update_replace_old_prog },
		{ "link update with same prog",
		  test_link_update_same_prog },
		{ "link update invalid opts",
		  test_link_update_invalid_opts },
		{ "link update invalid prog",
		  test_link_update_invalid_prog },
		{ "link update netns gone",
		  test_link_update_netns_gone },
		{ "link get info",
		  test_link_get_info },
	};
	int i, progs[2] = { -1, -1 };
	char test_name[80];

	for (i = 0; i < ARRAY_SIZE(progs); i++) {
		progs[i] = load_prog(BPF_PROG_TYPE_FLOW_DISSECTOR);
		if (progs[i] < 0)
			goto out_close;
	}

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		snprintf(test_name, sizeof(test_name),
			 "flow dissector %s%s",
			 tests[i].test_name,
			 netns == init_net ? " (init_net)" : "");
		if (test__start_subtest(test_name))
			tests[i].test_func(netns, progs[0], progs[1]);
	}
out_close:
	for (i = 0; i < ARRAY_SIZE(progs); i++) {
		if (progs[i] >= 0)
			CHECK_FAIL(close(progs[i]));
	}
}

void test_flow_dissector_reattach(void)
{
	int err, new_net, saved_net;

	saved_net = open("/proc/self/ns/net", O_RDONLY);
	if (CHECK_FAIL(saved_net < 0)) {
		perror("open(/proc/self/ns/net");
		return;
	}

	init_net = open("/proc/1/ns/net", O_RDONLY);
	if (CHECK_FAIL(init_net < 0)) {
		perror("open(/proc/1/ns/net)");
		goto out_close;
	}

	err = setns(init_net, CLONE_NEWNET);
	if (CHECK_FAIL(err)) {
		perror("setns(/proc/1/ns/net)");
		goto out_close;
	}

	if (prog_is_attached(init_net)) {
		test__skip();
		printf("Can't test with flow dissector attached to init_net\n");
		goto out_setns;
	}

	/* First run tests in root network namespace */
	run_tests(init_net);

	/* Then repeat tests in a non-root namespace */
	new_net = unshare_net(init_net);
	if (new_net < 0)
		goto out_setns;
	run_tests(new_net);
	close(new_net);

out_setns:
	/* Move back to netns we started in. */
	err = setns(saved_net, CLONE_NEWNET);
	if (CHECK_FAIL(err))
		perror("setns(/proc/self/ns/net)");

out_close:
	close(init_net);
	close(saved_net);
}
