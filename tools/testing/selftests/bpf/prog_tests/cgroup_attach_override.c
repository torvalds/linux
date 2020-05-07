// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>

#include "cgroup_helpers.h"

#define FOO		"/foo"
#define BAR		"/foo/bar/"
#define PING_CMD	"ping -q -c1 -w1 127.0.0.1 > /dev/null"

static char bpf_log_buf[BPF_LOG_BUF_SIZE];

static int prog_load(int verdict)
{
	struct bpf_insn prog[] = {
		BPF_MOV64_IMM(BPF_REG_0, verdict), /* r0 = verdict */
		BPF_EXIT_INSN(),
	};
	size_t insns_cnt = sizeof(prog) / sizeof(struct bpf_insn);

	return bpf_load_program(BPF_PROG_TYPE_CGROUP_SKB,
			       prog, insns_cnt, "GPL", 0,
			       bpf_log_buf, BPF_LOG_BUF_SIZE);
}

void test_cgroup_attach_override(void)
{
	int drop_prog = -1, allow_prog = -1, foo = -1, bar = -1;
	__u32 duration = 0;

	allow_prog = prog_load(1);
	if (CHECK(allow_prog < 0, "prog_load_allow",
		  "verifier output:\n%s\n-------\n", bpf_log_buf))
		goto err;

	drop_prog = prog_load(0);
	if (CHECK(drop_prog < 0, "prog_load_drop",
		  "verifier output:\n%s\n-------\n", bpf_log_buf))
		goto err;

	foo = test__join_cgroup(FOO);
	if (CHECK(foo < 0, "cgroup_join_foo", "cgroup setup failed\n"))
		goto err;

	if (CHECK(bpf_prog_attach(drop_prog, foo, BPF_CGROUP_INET_EGRESS,
				  BPF_F_ALLOW_OVERRIDE),
		  "prog_attach_drop_foo_override",
		  "attach prog to %s failed, errno=%d\n", FOO, errno))
		goto err;

	if (CHECK(!system(PING_CMD), "ping_fail",
		  "ping unexpectedly succeeded\n"))
		goto err;

	bar = test__join_cgroup(BAR);
	if (CHECK(bar < 0, "cgroup_join_bar", "cgroup setup failed\n"))
		goto err;

	if (CHECK(!system(PING_CMD), "ping_fail",
		  "ping unexpectedly succeeded\n"))
		goto err;

	if (CHECK(bpf_prog_attach(allow_prog, bar, BPF_CGROUP_INET_EGRESS,
				  BPF_F_ALLOW_OVERRIDE),
		  "prog_attach_allow_bar_override",
		  "attach prog to %s failed, errno=%d\n", BAR, errno))
		goto err;

	if (CHECK(system(PING_CMD), "ping_ok", "ping failed\n"))
		goto err;

	if (CHECK(bpf_prog_detach(bar, BPF_CGROUP_INET_EGRESS),
		  "prog_detach_bar",
		  "detach prog from %s failed, errno=%d\n", BAR, errno))
		goto err;

	if (CHECK(!system(PING_CMD), "ping_fail",
		  "ping unexpectedly succeeded\n"))
		goto err;

	if (CHECK(bpf_prog_attach(allow_prog, bar, BPF_CGROUP_INET_EGRESS,
				  BPF_F_ALLOW_OVERRIDE),
		  "prog_attach_allow_bar_override",
		  "attach prog to %s failed, errno=%d\n", BAR, errno))
		goto err;

	if (CHECK(bpf_prog_detach(foo, BPF_CGROUP_INET_EGRESS),
		  "prog_detach_foo",
		  "detach prog from %s failed, errno=%d\n", FOO, errno))
		goto err;

	if (CHECK(system(PING_CMD), "ping_ok", "ping failed\n"))
		goto err;

	if (CHECK(bpf_prog_attach(allow_prog, bar, BPF_CGROUP_INET_EGRESS,
				  BPF_F_ALLOW_OVERRIDE),
		  "prog_attach_allow_bar_override",
		  "attach prog to %s failed, errno=%d\n", BAR, errno))
		goto err;

	if (CHECK(!bpf_prog_attach(allow_prog, bar, BPF_CGROUP_INET_EGRESS, 0),
		  "fail_prog_attach_allow_bar_none",
		  "attach prog to %s unexpectedly succeeded\n", BAR))
		goto err;

	if (CHECK(bpf_prog_detach(bar, BPF_CGROUP_INET_EGRESS),
		  "prog_detach_bar",
		  "detach prog from %s failed, errno=%d\n", BAR, errno))
		goto err;

	if (CHECK(!bpf_prog_detach(foo, BPF_CGROUP_INET_EGRESS),
		  "fail_prog_detach_foo",
		  "double detach from %s unexpectedly succeeded\n", FOO))
		goto err;

	if (CHECK(bpf_prog_attach(allow_prog, foo, BPF_CGROUP_INET_EGRESS, 0),
		  "prog_attach_allow_foo_none",
		  "attach prog to %s failed, errno=%d\n", FOO, errno))
		goto err;

	if (CHECK(!bpf_prog_attach(allow_prog, bar, BPF_CGROUP_INET_EGRESS, 0),
		  "fail_prog_attach_allow_bar_none",
		  "attach prog to %s unexpectedly succeeded\n", BAR))
		goto err;

	if (CHECK(!bpf_prog_attach(allow_prog, bar, BPF_CGROUP_INET_EGRESS,
				   BPF_F_ALLOW_OVERRIDE),
		  "fail_prog_attach_allow_bar_override",
		  "attach prog to %s unexpectedly succeeded\n", BAR))
		goto err;

	if (CHECK(!bpf_prog_attach(allow_prog, foo, BPF_CGROUP_INET_EGRESS,
				   BPF_F_ALLOW_OVERRIDE),
		  "fail_prog_attach_allow_foo_override",
		  "attach prog to %s unexpectedly succeeded\n", FOO))
		goto err;

	if (CHECK(bpf_prog_attach(drop_prog, foo, BPF_CGROUP_INET_EGRESS, 0),
		  "prog_attach_drop_foo_none",
		  "attach prog to %s failed, errno=%d\n", FOO, errno))
		goto err;

err:
	close(foo);
	close(bar);
	close(allow_prog);
	close(drop_prog);
}
