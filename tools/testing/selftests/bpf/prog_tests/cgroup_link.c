// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "cgroup_helpers.h"
#include "testing_helpers.h"
#include "test_cgroup_link.skel.h"

static __u32 duration = 0;
#define PING_CMD	"ping -q -c1 -w1 127.0.0.1 > /dev/null"

static struct test_cgroup_link *skel = NULL;

int ping_and_check(int exp_calls, int exp_alt_calls)
{
	skel->bss->calls = 0;
	skel->bss->alt_calls = 0;
	CHECK_FAIL(system(PING_CMD));
	if (CHECK(skel->bss->calls != exp_calls, "call_cnt",
		  "exp %d, got %d\n", exp_calls, skel->bss->calls))
		return -EINVAL;
	if (CHECK(skel->bss->alt_calls != exp_alt_calls, "alt_call_cnt",
		  "exp %d, got %d\n", exp_alt_calls, skel->bss->alt_calls))
		return -EINVAL;
	return 0;
}

void test_cgroup_link(void)
{
	struct {
		const char *path;
		int fd;
	} cgs[] = {
		{ "/cg1" },
		{ "/cg1/cg2" },
		{ "/cg1/cg2/cg3" },
		{ "/cg1/cg2/cg3/cg4" },
	};
	int last_cg = ARRAY_SIZE(cgs) - 1, cg_nr = ARRAY_SIZE(cgs);
	DECLARE_LIBBPF_OPTS(bpf_link_update_opts, link_upd_opts);
	struct bpf_link *links[ARRAY_SIZE(cgs)] = {}, *tmp_link;
	__u32 prog_ids[ARRAY_SIZE(cgs)], prog_cnt = 0, attach_flags, prog_id;
	struct bpf_link_info info;
	int i = 0, err, prog_fd;
	bool detach_legacy = false;

	skel = test_cgroup_link__open_and_load();
	if (CHECK(!skel, "skel_open_load", "failed to open/load skeleton\n"))
		return;
	prog_fd = bpf_program__fd(skel->progs.egress);

	err = setup_cgroup_environment();
	if (CHECK(err, "cg_init", "failed: %d\n", err))
		goto cleanup;

	for (i = 0; i < cg_nr; i++) {
		cgs[i].fd = create_and_get_cgroup(cgs[i].path);
		if (!ASSERT_GE(cgs[i].fd, 0, "cg_create"))
			goto cleanup;
	}

	err = join_cgroup(cgs[last_cg].path);
	if (CHECK(err, "cg_join", "fail: %d\n", err))
		goto cleanup;

	for (i = 0; i < cg_nr; i++) {
		links[i] = bpf_program__attach_cgroup(skel->progs.egress,
						      cgs[i].fd);
		if (!ASSERT_OK_PTR(links[i], "cg_attach"))
			goto cleanup;
	}

	ping_and_check(cg_nr, 0);

	/* query the number of effective progs and attach flags in root cg */
	err = bpf_prog_query(cgs[0].fd, BPF_CGROUP_INET_EGRESS,
			     BPF_F_QUERY_EFFECTIVE, &attach_flags, NULL,
			     &prog_cnt);
	CHECK_FAIL(err);
	CHECK_FAIL(attach_flags != BPF_F_ALLOW_MULTI);
	if (CHECK(prog_cnt != 1, "effect_cnt", "exp %d, got %d\n", 1, prog_cnt))
		goto cleanup;

	/* query the number of effective progs in last cg */
	err = bpf_prog_query(cgs[last_cg].fd, BPF_CGROUP_INET_EGRESS,
			     BPF_F_QUERY_EFFECTIVE, NULL, NULL,
			     &prog_cnt);
	CHECK_FAIL(err);
	CHECK_FAIL(attach_flags != BPF_F_ALLOW_MULTI);
	if (CHECK(prog_cnt != cg_nr, "effect_cnt", "exp %d, got %d\n",
		  cg_nr, prog_cnt))
		goto cleanup;

	/* query the effective prog IDs in last cg */
	err = bpf_prog_query(cgs[last_cg].fd, BPF_CGROUP_INET_EGRESS,
			     BPF_F_QUERY_EFFECTIVE, &attach_flags,
			     prog_ids, &prog_cnt);
	CHECK_FAIL(err);
	CHECK_FAIL(attach_flags != BPF_F_ALLOW_MULTI);
	if (CHECK(prog_cnt != cg_nr, "effect_cnt", "exp %d, got %d\n",
		  cg_nr, prog_cnt))
		goto cleanup;
	for (i = 1; i < prog_cnt; i++) {
		CHECK(prog_ids[i - 1] != prog_ids[i], "prog_id_check",
		      "idx %d, prev id %d, cur id %d\n",
		      i, prog_ids[i - 1], prog_ids[i]);
	}

	/* detach bottom program and ping again */
	bpf_link__destroy(links[last_cg]);
	links[last_cg] = NULL;

	ping_and_check(cg_nr - 1, 0);

	/* mix in with non link-based multi-attachments */
	err = bpf_prog_attach(prog_fd, cgs[last_cg].fd,
			      BPF_CGROUP_INET_EGRESS, BPF_F_ALLOW_MULTI);
	if (CHECK(err, "cg_attach_legacy", "errno=%d\n", errno))
		goto cleanup;
	detach_legacy = true;

	links[last_cg] = bpf_program__attach_cgroup(skel->progs.egress,
						    cgs[last_cg].fd);
	if (!ASSERT_OK_PTR(links[last_cg], "cg_attach"))
		goto cleanup;

	ping_and_check(cg_nr + 1, 0);

	/* detach link */
	bpf_link__destroy(links[last_cg]);
	links[last_cg] = NULL;

	/* detach legacy */
	err = bpf_prog_detach2(prog_fd, cgs[last_cg].fd, BPF_CGROUP_INET_EGRESS);
	if (CHECK(err, "cg_detach_legacy", "errno=%d\n", errno))
		goto cleanup;
	detach_legacy = false;

	/* attach legacy exclusive prog attachment */
	err = bpf_prog_attach(prog_fd, cgs[last_cg].fd,
			      BPF_CGROUP_INET_EGRESS, 0);
	if (CHECK(err, "cg_attach_exclusive", "errno=%d\n", errno))
		goto cleanup;
	detach_legacy = true;

	/* attempt to mix in with multi-attach bpf_link */
	tmp_link = bpf_program__attach_cgroup(skel->progs.egress,
					      cgs[last_cg].fd);
	if (!ASSERT_ERR_PTR(tmp_link, "cg_attach_fail")) {
		bpf_link__destroy(tmp_link);
		goto cleanup;
	}

	ping_and_check(cg_nr, 0);

	/* detach */
	err = bpf_prog_detach2(prog_fd, cgs[last_cg].fd, BPF_CGROUP_INET_EGRESS);
	if (CHECK(err, "cg_detach_legacy", "errno=%d\n", errno))
		goto cleanup;
	detach_legacy = false;

	ping_and_check(cg_nr - 1, 0);

	/* attach back link-based one */
	links[last_cg] = bpf_program__attach_cgroup(skel->progs.egress,
						    cgs[last_cg].fd);
	if (!ASSERT_OK_PTR(links[last_cg], "cg_attach"))
		goto cleanup;

	ping_and_check(cg_nr, 0);

	/* check legacy exclusive prog can't be attached */
	err = bpf_prog_attach(prog_fd, cgs[last_cg].fd,
			      BPF_CGROUP_INET_EGRESS, 0);
	if (CHECK(!err, "cg_attach_exclusive", "unexpected success")) {
		bpf_prog_detach2(prog_fd, cgs[last_cg].fd, BPF_CGROUP_INET_EGRESS);
		goto cleanup;
	}

	/* replace BPF programs inside their links for all but first link */
	for (i = 1; i < cg_nr; i++) {
		err = bpf_link__update_program(links[i], skel->progs.egress_alt);
		if (CHECK(err, "prog_upd", "link #%d\n", i))
			goto cleanup;
	}

	ping_and_check(1, cg_nr - 1);

	/* Attempt program update with wrong expected BPF program */
	link_upd_opts.old_prog_fd = bpf_program__fd(skel->progs.egress_alt);
	link_upd_opts.flags = BPF_F_REPLACE;
	err = bpf_link_update(bpf_link__fd(links[0]),
			      bpf_program__fd(skel->progs.egress_alt),
			      &link_upd_opts);
	if (CHECK(err == 0 || errno != EPERM, "prog_cmpxchg1",
		  "unexpectedly succeeded, err %d, errno %d\n", err, -errno))
		goto cleanup;

	/* Compare-exchange single link program from egress to egress_alt */
	link_upd_opts.old_prog_fd = bpf_program__fd(skel->progs.egress);
	link_upd_opts.flags = BPF_F_REPLACE;
	err = bpf_link_update(bpf_link__fd(links[0]),
			      bpf_program__fd(skel->progs.egress_alt),
			      &link_upd_opts);
	if (CHECK(err, "prog_cmpxchg2", "errno %d\n", -errno))
		goto cleanup;

	/* ping */
	ping_and_check(0, cg_nr);

	/* close cgroup FDs before detaching links */
	for (i = 0; i < cg_nr; i++) {
		if (cgs[i].fd > 0) {
			close(cgs[i].fd);
			cgs[i].fd = -1;
		}
	}

	/* BPF programs should still get called */
	ping_and_check(0, cg_nr);

	prog_id = link_info_prog_id(links[0], &info);
	CHECK(prog_id == 0, "link_info", "failed\n");
	CHECK(info.cgroup.cgroup_id == 0, "cgroup_id", "unexpected %llu\n", info.cgroup.cgroup_id);

	err = bpf_link__detach(links[0]);
	if (CHECK(err, "link_detach", "failed %d\n", err))
		goto cleanup;

	/* cgroup_id should be zero in link_info */
	prog_id = link_info_prog_id(links[0], &info);
	CHECK(prog_id == 0, "link_info", "failed\n");
	CHECK(info.cgroup.cgroup_id != 0, "cgroup_id", "unexpected %llu\n", info.cgroup.cgroup_id);

	/* First BPF program shouldn't be called anymore */
	ping_and_check(0, cg_nr - 1);

	/* leave cgroup and remove them, don't detach programs */
	cleanup_cgroup_environment();

	/* BPF programs should have been auto-detached */
	ping_and_check(0, 0);

cleanup:
	if (detach_legacy)
		bpf_prog_detach2(prog_fd, cgs[last_cg].fd,
				 BPF_CGROUP_INET_EGRESS);

	for (i = 0; i < cg_nr; i++) {
		bpf_link__destroy(links[i]);
	}
	test_cgroup_link__destroy(skel);

	for (i = 0; i < cg_nr; i++) {
		if (cgs[i].fd > 0)
			close(cgs[i].fd);
	}
	cleanup_cgroup_environment();
}
