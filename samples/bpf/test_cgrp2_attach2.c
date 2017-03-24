/* eBPF example program:
 *
 * - Creates arraymap in kernel with 4 bytes keys and 8 byte values
 *
 * - Loads eBPF program
 *
 *   The eBPF program accesses the map passed in to store two pieces of
 *   information. The number of invocations of the program, which maps
 *   to the number of packets received, is stored to key 0. Key 1 is
 *   incremented on each iteration by the number of bytes stored in
 *   the skb.
 *
 * - Attaches the new program to a cgroup using BPF_PROG_ATTACH
 *
 * - Every second, reads map[0] and map[1] to see how many bytes and
 *   packets were seen on any socket of tasks in the given cgroup.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include <linux/bpf.h>

#include "libbpf.h"
#include "cgroup_helpers.h"

#define FOO		"/foo"
#define BAR		"/foo/bar/"
#define PING_CMD	"ping -c1 -w1 127.0.0.1"

char bpf_log_buf[BPF_LOG_BUF_SIZE];

static int prog_load(int verdict)
{
	int ret;
	struct bpf_insn prog[] = {
		BPF_MOV64_IMM(BPF_REG_0, verdict), /* r0 = verdict */
		BPF_EXIT_INSN(),
	};
	size_t insns_cnt = sizeof(prog) / sizeof(struct bpf_insn);

	ret = bpf_load_program(BPF_PROG_TYPE_CGROUP_SKB,
			       prog, insns_cnt, "GPL", 0,
			       bpf_log_buf, BPF_LOG_BUF_SIZE);

	if (ret < 0) {
		log_err("Loading program");
		printf("Output from verifier:\n%s\n-------\n", bpf_log_buf);
		return 0;
	}
	return ret;
}


int main(int argc, char **argv)
{
	int drop_prog, allow_prog, foo = 0, bar = 0, rc = 0;

	allow_prog = prog_load(1);
	if (!allow_prog)
		goto err;

	drop_prog = prog_load(0);
	if (!drop_prog)
		goto err;

	if (setup_cgroup_environment())
		goto err;

	/* Create cgroup /foo, get fd, and join it */
	foo = create_and_get_cgroup(FOO);
	if (!foo)
		goto err;

	if (join_cgroup(FOO))
		goto err;

	if (bpf_prog_attach(drop_prog, foo, BPF_CGROUP_INET_EGRESS, 1)) {
		log_err("Attaching prog to /foo");
		goto err;
	}

	printf("Attached DROP prog. This ping in cgroup /foo should fail...\n");
	assert(system(PING_CMD) != 0);

	/* Create cgroup /foo/bar, get fd, and join it */
	bar = create_and_get_cgroup(BAR);
	if (!bar)
		goto err;

	if (join_cgroup(BAR))
		goto err;

	printf("Attached DROP prog. This ping in cgroup /foo/bar should fail...\n");
	assert(system(PING_CMD) != 0);

	if (bpf_prog_attach(allow_prog, bar, BPF_CGROUP_INET_EGRESS, 1)) {
		log_err("Attaching prog to /foo/bar");
		goto err;
	}

	printf("Attached PASS prog. This ping in cgroup /foo/bar should pass...\n");
	assert(system(PING_CMD) == 0);

	if (bpf_prog_detach(bar, BPF_CGROUP_INET_EGRESS)) {
		log_err("Detaching program from /foo/bar");
		goto err;
	}

	printf("Detached PASS from /foo/bar while DROP is attached to /foo.\n"
	       "This ping in cgroup /foo/bar should fail...\n");
	assert(system(PING_CMD) != 0);

	if (bpf_prog_attach(allow_prog, bar, BPF_CGROUP_INET_EGRESS, 1)) {
		log_err("Attaching prog to /foo/bar");
		goto err;
	}

	if (bpf_prog_detach(foo, BPF_CGROUP_INET_EGRESS)) {
		log_err("Detaching program from /foo");
		goto err;
	}

	printf("Attached PASS from /foo/bar and detached DROP from /foo.\n"
	       "This ping in cgroup /foo/bar should pass...\n");
	assert(system(PING_CMD) == 0);

	if (bpf_prog_attach(allow_prog, bar, BPF_CGROUP_INET_EGRESS, 1)) {
		log_err("Attaching prog to /foo/bar");
		goto err;
	}

	if (!bpf_prog_attach(allow_prog, bar, BPF_CGROUP_INET_EGRESS, 0)) {
		errno = 0;
		log_err("Unexpected success attaching prog to /foo/bar");
		goto err;
	}

	if (bpf_prog_detach(bar, BPF_CGROUP_INET_EGRESS)) {
		log_err("Detaching program from /foo/bar");
		goto err;
	}

	if (!bpf_prog_detach(foo, BPF_CGROUP_INET_EGRESS)) {
		errno = 0;
		log_err("Unexpected success in double detach from /foo");
		goto err;
	}

	if (bpf_prog_attach(allow_prog, foo, BPF_CGROUP_INET_EGRESS, 0)) {
		log_err("Attaching non-overridable prog to /foo");
		goto err;
	}

	if (!bpf_prog_attach(allow_prog, bar, BPF_CGROUP_INET_EGRESS, 0)) {
		errno = 0;
		log_err("Unexpected success attaching non-overridable prog to /foo/bar");
		goto err;
	}

	if (!bpf_prog_attach(allow_prog, bar, BPF_CGROUP_INET_EGRESS, 1)) {
		errno = 0;
		log_err("Unexpected success attaching overridable prog to /foo/bar");
		goto err;
	}

	if (!bpf_prog_attach(allow_prog, foo, BPF_CGROUP_INET_EGRESS, 1)) {
		errno = 0;
		log_err("Unexpected success attaching overridable prog to /foo");
		goto err;
	}

	if (bpf_prog_attach(drop_prog, foo, BPF_CGROUP_INET_EGRESS, 0)) {
		log_err("Attaching different non-overridable prog to /foo");
		goto err;
	}

	goto out;

err:
	rc = 1;

out:
	close(foo);
	close(bar);
	cleanup_cgroup_environment();
	if (!rc)
		printf("PASS\n");
	else
		printf("FAIL\n");
	return rc;
}
