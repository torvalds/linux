// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <stdlib.h>
#include <asm/ptrace.h>
#include <linux/elf.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include "../../kselftest.h"
#include "v_helpers.h"

int parent_set_val, child_set_val;

static long do_ptrace(enum __ptrace_request op, pid_t pid, long type, size_t size, void *data)
{
	struct iovec v_iovec = {
		.iov_len = size,
		.iov_base = data
	};

	return ptrace(op, pid, type, &v_iovec);
}

static int do_child(void)
{
	int out;

	if (ptrace(PTRACE_TRACEME, -1, NULL, NULL)) {
		ksft_perror("PTRACE_TRACEME failed\n");
		return EXIT_FAILURE;
	}

	asm volatile (".option push\n\t"
		".option	arch, +v\n\t"
		".option	norvc\n\t"
		"vsetivli	x0, 1, e32, m1, ta, ma\n\t"
		"vmv.s.x	v31, %[in]\n\t"
		"ebreak\n\t"
		"vmv.x.s	%[out], v31\n\t"
		".option pop\n\t"
		: [out] "=r" (out)
		: [in] "r" (child_set_val));

	if (out != parent_set_val)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

static void do_parent(pid_t child)
{
	int status;
	void *data = NULL;

	/* Attach to the child */
	while (waitpid(child, &status, 0)) {
		if (WIFEXITED(status)) {
			ksft_test_result(WEXITSTATUS(status) == 0, "SETREGSET vector\n");
			goto out;
		} else if (WIFSTOPPED(status) && (WSTOPSIG(status) == SIGTRAP)) {
			size_t size;
			void *data, *v31;
			struct __riscv_v_regset_state *v_regset_hdr;
			struct user_regs_struct *gpreg;

			size = sizeof(*v_regset_hdr);
			data = malloc(size);
			if (!data)
				goto out;
			v_regset_hdr = (struct __riscv_v_regset_state *)data;

			if (do_ptrace(PTRACE_GETREGSET, child, NT_RISCV_VECTOR, size, data))
				goto out;

			ksft_print_msg("vlenb %ld\n", v_regset_hdr->vlenb);
			data = realloc(data, size + v_regset_hdr->vlenb * 32);
			if (!data)
				goto out;
			v_regset_hdr = (struct __riscv_v_regset_state *)data;
			v31 = (void *)(data + size + v_regset_hdr->vlenb * 31);
			size += v_regset_hdr->vlenb * 32;

			if (do_ptrace(PTRACE_GETREGSET, child, NT_RISCV_VECTOR, size, data))
				goto out;

			ksft_test_result(*(int *)v31 == child_set_val, "GETREGSET vector\n");

			*(int *)v31 = parent_set_val;
			if (do_ptrace(PTRACE_SETREGSET, child, NT_RISCV_VECTOR, size, data))
				goto out;

			/* move the pc forward */
			size = sizeof(*gpreg);
			data = realloc(data, size);
			gpreg = (struct user_regs_struct *)data;

			if (do_ptrace(PTRACE_GETREGSET, child, NT_PRSTATUS, size, data))
				goto out;

			gpreg->pc += 4;
			if (do_ptrace(PTRACE_SETREGSET, child, NT_PRSTATUS, size, data))
				goto out;
		}

		ptrace(PTRACE_CONT, child, NULL, NULL);
	}

out:
	free(data);
}

int main(void)
{
	pid_t child;

	ksft_set_plan(2);
	if (!is_vector_supported() && !is_xtheadvector_supported())
		ksft_exit_skip("Vector not supported\n");

	srandom(getpid());
	parent_set_val = rand();
	child_set_val = rand();

	child = fork();
	if (child < 0)
		ksft_exit_fail_msg("Fork failed %d\n", child);

	if (!child)
		return do_child();

	do_parent(child);

	ksft_finished();
}
