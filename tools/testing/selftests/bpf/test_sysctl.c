// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/filter.h>

#include <bpf/bpf.h>

#include "bpf_rlimit.h"
#include "bpf_util.h"
#include "cgroup_helpers.h"

#define CG_PATH			"/foo"
#define MAX_INSNS		512

char bpf_log_buf[BPF_LOG_BUF_SIZE];

struct sysctl_test {
	const char *descr;
	struct bpf_insn	insns[MAX_INSNS];
	enum bpf_attach_type attach_type;
	const char *sysctl;
	int open_flags;
	const char *newval;
	enum {
		LOAD_REJECT,
		ATTACH_REJECT,
		OP_EPERM,
		SUCCESS,
	} result;
};

static struct sysctl_test tests[] = {
	{
		.descr = "sysctl wrong attach_type",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = 0,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.result = ATTACH_REJECT,
	},
	{
		.descr = "sysctl:read allow all",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		.descr = "sysctl:read deny all",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.result = OP_EPERM,
	},
	{
		.descr = "ctx:write sysctl:read read ok",
		.insns = {
			/* If (write) */
			BPF_LDX_MEM(BPF_W, BPF_REG_7, BPF_REG_1,
				    offsetof(struct bpf_sysctl, write)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 1, 2),

			/* return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_A(1),

			/* else return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.result = SUCCESS,
	},
	{
		.descr = "ctx:write sysctl:write read ok",
		.insns = {
			/* If (write) */
			BPF_LDX_MEM(BPF_B, BPF_REG_7, BPF_REG_1,
				    offsetof(struct bpf_sysctl, write)),
			BPF_JMP_IMM(BPF_JNE, BPF_REG_7, 1, 2),

			/* return DENY; */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_JMP_A(1),

			/* else return ALLOW; */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/domainname",
		.open_flags = O_WRONLY,
		.newval = "(none)", /* same as default, should fail anyway */
		.result = OP_EPERM,
	},
	{
		.descr = "ctx:write sysctl:read write reject",
		.insns = {
			/* write = X */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sysctl, write)),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SYSCTL,
		.sysctl = "kernel/ostype",
		.open_flags = O_RDONLY,
		.result = LOAD_REJECT,
	},
};

static size_t probe_prog_length(const struct bpf_insn *fp)
{
	size_t len;

	for (len = MAX_INSNS - 1; len > 0; --len)
		if (fp[len].code != 0 || fp[len].imm != 0)
			break;
	return len + 1;
}

static int load_sysctl_prog(struct sysctl_test *test, const char *sysctl_path)
{
	struct bpf_insn *prog = test->insns;
	struct bpf_load_program_attr attr;
	int ret;

	memset(&attr, 0, sizeof(struct bpf_load_program_attr));
	attr.prog_type = BPF_PROG_TYPE_CGROUP_SYSCTL;
	attr.insns = prog;
	attr.insns_cnt = probe_prog_length(attr.insns);
	attr.license = "GPL";

	ret = bpf_load_program_xattr(&attr, bpf_log_buf, BPF_LOG_BUF_SIZE);
	if (ret < 0 && test->result != LOAD_REJECT) {
		log_err(">>> Loading program error.\n"
			">>> Verifier output:\n%s\n-------\n", bpf_log_buf);
	}

	return ret;
}

static int access_sysctl(const char *sysctl_path,
			 const struct sysctl_test *test)
{
	int err = 0;
	int fd;

	fd = open(sysctl_path, test->open_flags | O_CLOEXEC);
	if (fd < 0)
		return fd;

	if (test->open_flags == O_RDONLY) {
		char buf[128];

		if (read(fd, buf, sizeof(buf)) == -1)
			goto err;
	} else if (test->open_flags == O_WRONLY) {
		if (!test->newval) {
			log_err("New value for sysctl is not set");
			goto err;
		}
		if (write(fd, test->newval, strlen(test->newval)) == -1)
			goto err;
	} else {
		log_err("Unexpected sysctl access: neither read nor write");
		goto err;
	}

	goto out;
err:
	err = -1;
out:
	close(fd);
	return err;
}

static int run_test_case(int cgfd, struct sysctl_test *test)
{
	enum bpf_attach_type atype = test->attach_type;
	char sysctl_path[128];
	int progfd = -1;
	int err = 0;

	printf("Test case: %s .. ", test->descr);

	snprintf(sysctl_path, sizeof(sysctl_path), "/proc/sys/%s",
		 test->sysctl);

	progfd = load_sysctl_prog(test, sysctl_path);
	if (progfd < 0) {
		if (test->result == LOAD_REJECT)
			goto out;
		else
			goto err;
	}

	if (bpf_prog_attach(progfd, cgfd, atype, BPF_F_ALLOW_OVERRIDE) == -1) {
		if (test->result == ATTACH_REJECT)
			goto out;
		else
			goto err;
	}

	if (access_sysctl(sysctl_path, test) == -1) {
		if (test->result == OP_EPERM && errno == EPERM)
			goto out;
		else
			goto err;
	}

	if (test->result != SUCCESS) {
		log_err("Unexpected failure");
		goto err;
	}

	goto out;
err:
	err = -1;
out:
	/* Detaching w/o checking return code: best effort attempt. */
	if (progfd != -1)
		bpf_prog_detach(cgfd, atype);
	close(progfd);
	printf("[%s]\n", err ? "FAIL" : "PASS");
	return err;
}

static int run_tests(int cgfd)
{
	int passes = 0;
	int fails = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(tests); ++i) {
		if (run_test_case(cgfd, &tests[i]))
			++fails;
		else
			++passes;
	}
	printf("Summary: %d PASSED, %d FAILED\n", passes, fails);
	return fails ? -1 : 0;
}

int main(int argc, char **argv)
{
	int cgfd = -1;
	int err = 0;

	if (setup_cgroup_environment())
		goto err;

	cgfd = create_and_get_cgroup(CG_PATH);
	if (cgfd < 0)
		goto err;

	if (join_cgroup(CG_PATH))
		goto err;

	if (run_tests(cgfd))
		goto err;

	goto out;
err:
	err = -1;
out:
	close(cgfd);
	cleanup_cgroup_environment();
	return err;
}
