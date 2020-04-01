// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>

#include "cgroup_helpers.h"

#define PING_CMD	"ping -q -c1 -w1 127.0.0.1 > /dev/null"

char bpf_log_buf[BPF_LOG_BUF_SIZE];

static int prog_load(void)
{
	struct bpf_insn prog[] = {
		BPF_MOV64_IMM(BPF_REG_0, 1), /* r0 = 1 */
		BPF_EXIT_INSN(),
	};
	size_t insns_cnt = sizeof(prog) / sizeof(struct bpf_insn);

	return bpf_load_program(BPF_PROG_TYPE_CGROUP_SKB,
			       prog, insns_cnt, "GPL", 0,
			       bpf_log_buf, BPF_LOG_BUF_SIZE);
}

void test_cgroup_attach_autodetach(void)
{
	__u32 duration = 0, prog_cnt = 4, attach_flags;
	int allow_prog[2] = {-1};
	__u32 prog_ids[2] = {0};
	void *ptr = NULL;
	int cg = 0, i;
	int attempts;

	for (i = 0; i < ARRAY_SIZE(allow_prog); i++) {
		allow_prog[i] = prog_load();
		if (CHECK(allow_prog[i] < 0, "prog_load",
			  "verifier output:\n%s\n-------\n", bpf_log_buf))
			goto err;
	}

	if (CHECK_FAIL(setup_cgroup_environment()))
		goto err;

	/* create a cgroup, attach two programs and remember their ids */
	cg = create_and_get_cgroup("/cg_autodetach");
	if (CHECK_FAIL(cg < 0))
		goto err;

	if (CHECK_FAIL(join_cgroup("/cg_autodetach")))
		goto err;

	for (i = 0; i < ARRAY_SIZE(allow_prog); i++)
		if (CHECK(bpf_prog_attach(allow_prog[i], cg,
					  BPF_CGROUP_INET_EGRESS,
					  BPF_F_ALLOW_MULTI),
			  "prog_attach", "prog[%d], errno=%d\n", i, errno))
			goto err;

	/* make sure that programs are attached and run some traffic */
	if (CHECK(bpf_prog_query(cg, BPF_CGROUP_INET_EGRESS, 0, &attach_flags,
				 prog_ids, &prog_cnt),
		  "prog_query", "errno=%d\n", errno))
		goto err;
	if (CHECK_FAIL(system(PING_CMD)))
		goto err;

	/* allocate some memory (4Mb) to pin the original cgroup */
	ptr = malloc(4 * (1 << 20));
	if (CHECK_FAIL(!ptr))
		goto err;

	/* close programs and cgroup fd */
	for (i = 0; i < ARRAY_SIZE(allow_prog); i++) {
		close(allow_prog[i]);
		allow_prog[i] = -1;
	}

	close(cg);
	cg = 0;

	/* leave the cgroup and remove it. don't detach programs */
	cleanup_cgroup_environment();

	/* wait for the asynchronous auto-detachment.
	 * wait for no more than 5 sec and give up.
	 */
	for (i = 0; i < ARRAY_SIZE(prog_ids); i++) {
		for (attempts = 5; attempts >= 0; attempts--) {
			int fd = bpf_prog_get_fd_by_id(prog_ids[i]);

			if (fd < 0)
				break;

			/* don't leave the fd open */
			close(fd);

			if (CHECK_FAIL(!attempts))
				goto err;

			sleep(1);
		}
	}

err:
	for (i = 0; i < ARRAY_SIZE(allow_prog); i++)
		if (allow_prog[i] >= 0)
			close(allow_prog[i]);
	if (cg)
		close(cg);
	free(ptr);
	cleanup_cgroup_environment();
}
