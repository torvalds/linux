// SPDX-License-Identifier: GPL-2.0
/*
 * Test the 'L' (loader substitution) flag of binfmt_misc. A matched
 * binary runs as the MAIN image - a fully native exec - with the
 * registered interpreter substituted for its PT_INTERP. The payload
 * (binfmt_loader_payload) asserts the native identity from inside.
 *
 * The substitute is a copy of the system loader found via our own
 * PT_INTERP; magic matching pokes a marker into the ELF header's
 * e_ident padding, which kernel and loader ignore.
 *
 * Needs root for the registration; no bpf toolchain involved.
 */
#define _GNU_SOURCE
#include <elf.h>
#include <link.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "binfmt_misc_common.h"
#include "kselftest_harness.h"

#define ENTRY		"test_loader"
#define INTERP_PATH	"/tmp/binfmt_loader_interp"
#define MOVED_PATH	INTERP_PATH ".moved"
#define TARGET_PATH	"/tmp/binfmt_loader_target.ldrtest"
#define STATIC_PATH	"/tmp/binfmt_loader_static.ldrtest"
#define FOREIGN_PATH	"/tmp/binfmt_loader_foreign.ldrtest"
#define SCRIPT_PATH	"/tmp/binfmt_loader_script.ldrtest"
#define M_RULE		":" ENTRY ":M:9:" LOADER_MARKER "::" INTERP_PATH ":L"
#define E_RULE		":" ENTRY ":E::ldrtest::" INTERP_PATH ":L"
#define FL_RULE		":" ENTRY ":E::ldrtest::" INTERP_PATH ":FL"

/* Execute the binary from an inaccessible O_CLOEXEC memfd. */
static int run_memfd(const char *path)
{
	int status;
	pid_t pid;

	pid = fork();
	if (pid == 0) {
		char *argv[] = { PAYLOAD_ARGV0, PAYLOAD_ARG1, PAYLOAD_ARG2, NULL };
		char buf[4096];
		int in, mfd;
		ssize_t n;

		mfd = memfd_create("loader-test", MFD_CLOEXEC);
		in = open(path, O_RDONLY);
		if (mfd < 0 || in < 0)
			_exit(125);
		while ((n = read(in, buf, sizeof(buf))) > 0)
			if (write(mfd, buf, n) != n)
				_exit(125);
		close(in);
		setenv("BINFMT_TEST_MEMFD", "1", 1);
		unsetenv("BINFMT_TEST_BINARY");
		syscall(SYS_execveat, mfd, "", argv, environ, AT_EMPTY_PATH);
		_exit(126);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

/*
 * The differentiator against the transparent mode: at PTRACE_EVENT_EXEC
 * the identity is already complete - exe, auxv and the stat code markers
 * are mutually consistent with no window a debugger could observe.
 */
static int ptrace_probe(const char *target)
{
	unsigned long auxv[2 * 64], base = 0, entry = 0, at_flags = 0;
	unsigned long start_code = 0, end_code = 0;
	int status, fd, execfd_seen = 0, failed = 0;
	char path[64], buf[PATH_MAX];
	ssize_t n;
	pid_t pid;
	int i;

	pid = fork();
	if (pid == 0) {
		ptrace(PTRACE_TRACEME, 0, NULL, NULL);
		raise(SIGSTOP);
		execl(target, PAYLOAD_ARGV0, PAYLOAD_ARG1, PAYLOAD_ARG2, (char *)NULL);
		_exit(126);
	}
	if (pid < 0)
		return -1;
	if (waitpid(pid, &status, 0) != pid || !WIFSTOPPED(status))
		goto fail_kill;
	if (ptrace(PTRACE_SETOPTIONS, pid, NULL, (void *)PTRACE_O_TRACEEXEC))
		goto fail_kill;
	if (ptrace(PTRACE_CONT, pid, NULL, NULL))
		goto fail_kill;
	if (waitpid(pid, &status, 0) != pid || !WIFSTOPPED(status) ||
	    status >> 8 != (SIGTRAP | (PTRACE_EVENT_EXEC << 8))) {
		fprintf(stderr, "no exec stop (status %#x)\n", status);
		goto fail_kill;
	}

	snprintf(path, sizeof(path), "/proc/%d/exe", pid);
	n = readlink(path, buf, sizeof(buf) - 1);
	if (n <= 0) {
		failed = 1;
	} else {
		buf[n] = '\0';
		if (strcmp(buf, target)) {
			fprintf(stderr, "exe at exec stop: %s\n", buf);
			failed = 1;
		}
	}

	snprintf(path, sizeof(path), "/proc/%d/auxv", pid);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		n = -1;
	} else {
		n = read(fd, auxv, sizeof(auxv));
		close(fd);
	}
	if (n <= 0) {
		failed = 1;
		n = 0;
	}
	for (i = 0; i + 1 < (int)(n / sizeof(unsigned long)); i += 2) {
		switch (auxv[i]) {
		case AT_BASE:
			base = auxv[i + 1];
			break;
		case AT_ENTRY:
			entry = auxv[i + 1];
			break;
		case AT_FLAGS:
			at_flags = auxv[i + 1];
			break;
		case AT_EXECFD:
			execfd_seen = 1;
			break;
		}
	}

	if (stat_codes(pid, &start_code, &end_code))
		failed = 1;

	if (!base || execfd_seen || at_flags) {
		fprintf(stderr, "auxv at exec stop not native\n");
		failed = 1;
	}
	if (!start_code || entry < start_code || entry >= end_code) {
		fprintf(stderr, "auxv/stat inconsistent at exec stop\n");
		failed = 1;
	}

	if (ptrace(PTRACE_CONT, pid, NULL, NULL))
		goto fail_kill;
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status))
		failed = 1;
	return failed ? -1 : 0;

fail_kill:
	kill(pid, SIGKILL);
	waitpid(pid, &status, 0);
	return -1;
}

FIXTURE(loader) {
	bool have_static;
};

FIXTURE_SETUP(loader)
{
	unsigned short foreign_machine = 0xdead;
	char src[PATH_MAX], loader[PATH_MAX];

	if (getuid() != 0)
		SKIP(return, "test must be run as root");
	if (!binfmt_misc_available())
		SKIP(return, "no binfmt_misc");
	if (find_loader(loader, sizeof(loader)))
		SKIP(return, "cannot determine own PT_INTERP");

	ASSERT_EQ(copy_file(loader, INTERP_PATH), 0);

	ASSERT_EQ(artifact_path(src, sizeof(src), "binfmt_loader_payload"), 0);
	ASSERT_EQ(copy_file(src, TARGET_PATH), 0);
	ASSERT_EQ(patch_file(TARGET_PATH, EI_PAD, LOADER_MARKER,
			     strlen(LOADER_MARKER)), 0);

	/* The same payload with a machine type this kernel cannot load. */
	ASSERT_EQ(copy_file(src, FOREIGN_PATH), 0);
	ASSERT_EQ(patch_file(FOREIGN_PATH, EI_PAD, LOADER_MARKER,
			     strlen(LOADER_MARKER)), 0);
	ASSERT_EQ(patch_file(FOREIGN_PATH, offsetof(ElfW(Ehdr), e_machine),
			     &foreign_machine, sizeof(foreign_machine)), 0);

	self->have_static =
		artifact_path(src, sizeof(src), "binfmt_loader_payload_static") == 0 &&
		copy_file(src, STATIC_PATH) == 0;

	setenv("BINFMT_TEST_BINARY", TARGET_PATH, 1);
	setenv("BINFMT_TEST_INTERP", INTERP_PATH, 1);

	/* Everything below needs the flag; find out once. */
	if (write_reg(E_RULE)) {
		ASSERT_EQ(errno, EINVAL);
		SKIP(return, "kernel without the 'L' flag");
	}
	unregister(ENTRY);
}

FIXTURE_TEARDOWN(loader)
{
	unregister(ENTRY);
	if (access(MOVED_PATH, F_OK) == 0)
		rename(MOVED_PATH, INTERP_PATH);
	unlink(TARGET_PATH);
	unlink(STATIC_PATH);
	unlink(FOREIGN_PATH);
	unlink(SCRIPT_PATH);
	unlink(INTERP_PATH);
}

/* Grammar sanity check: the same entry without 'L' has to register. */
TEST_F(loader, plain_entry_registers)
{
	ASSERT_EQ(write_reg(":" ENTRY ":E::ldrtest::" INTERP_PATH ":"), 0);
}

/* 'L' is a native exec: every classic-dispatch flag is rejected. */
TEST_F(loader, rejects_classic_flags)
{
	static const char * const combos[] = { "LT", "LP", "LC", "LO" };
	char rule[PATH_MAX];
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(combos); i++) {
		int rc;

		snprintf(rule, sizeof(rule),
			 ":" ENTRY ":E::ldrtest::" INTERP_PATH ":%s", combos[i]);
		rc = write_reg(rule);
		EXPECT_EQ(rc, -1)
			TH_LOG("'%s' was not rejected", combos[i]);
		if (rc == 0) {
			unregister(ENTRY);
			continue;
		}
		EXPECT_EQ(errno, EINVAL);
	}
}

/*
 * Without 'F' the interpreter is opened when the binary is executed, so a
 * relative path would be resolved against the caller's working directory.
 */
TEST_F(loader, rejects_relative_interpreter)
{
	static const char * const flags[] = { "L", "C" };
	char rule[PATH_MAX];
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(flags); i++) {
		int rc;

		snprintf(rule, sizeof(rule),
			 ":" ENTRY ":E::ldrtest::binfmt_loader_interp:%s",
			 flags[i]);
		rc = write_reg(rule);
		EXPECT_EQ(rc, -1)
			TH_LOG("'%s' accepted a relative interpreter", flags[i]);
		if (rc == 0) {
			unregister(ENTRY);
			continue;
		}
		EXPECT_EQ(errno, EINVAL);
	}
}

TEST_F(loader, extension_matched)
{
	ASSERT_EQ(write_reg(E_RULE), 0);
	EXPECT_EQ(run_payload(TARGET_PATH), 0);
}

TEST_F(loader, magic_matched)
{
	ASSERT_EQ(write_reg(M_RULE), 0);
	EXPECT_EQ(run_payload(TARGET_PATH), 0);
}

/*
 * The differentiator against the transparent mode: at PTRACE_EVENT_EXEC the
 * identity is already complete, with no window a debugger could observe.
 */
TEST_F(loader, exec_stop_consistency)
{
	ASSERT_EQ(write_reg(E_RULE), 0);
	EXPECT_EQ(ptrace_probe(TARGET_PATH), 0);
}

/* A binary without PT_INTERP drops the override and runs natively. */
TEST_F(loader, static_binary_runs_natively)
{
	if (!self->have_static)
		SKIP(return, "no static payload built");

	ASSERT_EQ(write_reg(E_RULE), 0);
	setenv("BINFMT_TEST_BINARY", STATIC_PATH, 1);
	setenv("BINFMT_TEST_STATIC", "1", 1);
	EXPECT_EQ(run_payload(STATIC_PATH), 0);
	unsetenv("BINFMT_TEST_STATIC");
	setenv("BINFMT_TEST_BINARY", TARGET_PATH, 1);
}

/*
 * A '#!' file that matched an 'L' entry is claimed by binfmt_script, which
 * sits ahead of binfmt_elf. The substitute the entry staged has to be
 * released when the interpreter replaces the file, not leaked.
 */
TEST_F(loader, script_claims_the_file)
{
	static const char script[] = "#!/bin/sh\nexit 0\n";
	int fd;

	unlink(SCRIPT_PATH);
	fd = open(SCRIPT_PATH, O_WRONLY | O_CREAT | O_EXCL, 0755);
	ASSERT_GE(fd, 0);
	ASSERT_EQ(write(fd, script, sizeof(script) - 1),
		  (ssize_t)sizeof(script) - 1);
	ASSERT_EQ(close(fd), 0);

	ASSERT_EQ(write_reg(E_RULE), 0);
	EXPECT_EQ(run_payload(SCRIPT_PATH), 0);

	/* A leaked substitute keeps its write denial on the loader. */
	fd = open(INTERP_PATH, O_WRONLY);
	EXPECT_GE(fd, 0)
		TH_LOG("loader still write denied (errno %d)", errno);
	if (fd >= 0)
		close(fd);
}

/* Nothing needs the binary's path, so an inaccessible fd works. */
TEST_F(loader, inaccessible_memfd)
{
	ASSERT_EQ(write_reg(M_RULE), 0);
	EXPECT_EQ(run_memfd(TARGET_PATH), 0);
}

/* The whole exec of a wrong-arch binary fails as if unhandled. */
TEST_F(loader, foreign_arch_enoexec)
{
	ASSERT_EQ(write_reg(M_RULE), 0);
	EXPECT_EQ(run_payload(FOREIGN_PATH), RUN_ENOEXEC);
}

/* 'F' pre-opens the substitute, so it survives losing its path. */
TEST_F(loader, fixed_interpreter_survives_rename)
{
	ASSERT_EQ(write_reg(FL_RULE), 0);
	ASSERT_EQ(rename(INTERP_PATH, MOVED_PATH), 0);
	EXPECT_EQ(run_payload(TARGET_PATH), 0);
}

TEST_HARNESS_MAIN
