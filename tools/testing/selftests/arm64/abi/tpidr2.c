// SPDX-License-Identifier: GPL-2.0-only

#include <linux/sched.h>
#include <linux/wait.h>

#define SYS_TPIDR2 "S3_3_C13_C0_5"

#define EXPECTED_TESTS 5

static void putstr(const char *str)
{
	write(1, str, strlen(str));
}

static void putnum(unsigned int num)
{
	char c;

	if (num / 10)
		putnum(num / 10);

	c = '0' + (num % 10);
	write(1, &c, 1);
}

static int tests_run;
static int tests_passed;
static int tests_failed;
static int tests_skipped;

static void set_tpidr2(uint64_t val)
{
	asm volatile (
		"msr	" SYS_TPIDR2 ", %0\n"
		:
		: "r"(val)
		: "cc");
}

static uint64_t get_tpidr2(void)
{
	uint64_t val;

	asm volatile (
		"mrs	%0, " SYS_TPIDR2 "\n"
		: "=r"(val)
		:
		: "cc");

	return val;
}

static void print_summary(void)
{
	if (tests_passed + tests_failed + tests_skipped != EXPECTED_TESTS)
		putstr("# UNEXPECTED TEST COUNT: ");

	putstr("# Totals: pass:");
	putnum(tests_passed);
	putstr(" fail:");
	putnum(tests_failed);
	putstr(" xfail:0 xpass:0 skip:");
	putnum(tests_skipped);
	putstr(" error:0\n");
}

/* Processes should start with TPIDR2 == 0 */
static int default_value(void)
{
	return get_tpidr2() == 0;
}

/* If we set TPIDR2 we should read that value */
static int write_read(void)
{
	set_tpidr2(getpid());

	return getpid() == get_tpidr2();
}

/* If we set a value we should read the same value after scheduling out */
static int write_sleep_read(void)
{
	set_tpidr2(getpid());

	msleep(100);

	return getpid() == get_tpidr2();
}

/*
 * If we fork the value in the parent should be unchanged and the
 * child should start with the same value and be able to set its own
 * value.
 */
static int write_fork_read(void)
{
	pid_t newpid, waiting, oldpid;
	int status;

	set_tpidr2(getpid());

	oldpid = getpid();
	newpid = fork();
	if (newpid == 0) {
		/* In child */
		if (get_tpidr2() != oldpid) {
			putstr("# TPIDR2 changed in child: ");
			putnum(get_tpidr2());
			putstr("\n");
			exit(0);
		}

		set_tpidr2(getpid());
		if (get_tpidr2() == getpid()) {
			exit(1);
		} else {
			putstr("# Failed to set TPIDR2 in child\n");
			exit(0);
		}
	}
	if (newpid < 0) {
		putstr("# fork() failed: -");
		putnum(-newpid);
		putstr("\n");
		return 0;
	}

	for (;;) {
		waiting = waitpid(newpid, &status, 0);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			putstr("# waitpid() failed: ");
			putnum(errno);
			putstr("\n");
			return 0;
		}
		if (waiting != newpid) {
			putstr("# waitpid() returned wrong PID\n");
			return 0;
		}

		if (!WIFEXITED(status)) {
			putstr("# child did not exit\n");
			return 0;
		}

		if (getpid() != get_tpidr2()) {
			putstr("# TPIDR2 corrupted in parent\n");
			return 0;
		}

		return WEXITSTATUS(status);
	}
}

/*
 * sys_clone() has a lot of per architecture variation so just define
 * it here rather than adding it to nolibc, plus the raw API is a
 * little more convenient for this test.
 */
static int sys_clone(unsigned long clone_flags, unsigned long newsp,
		     int *parent_tidptr, unsigned long tls,
		     int *child_tidptr)
{
	return my_syscall5(__NR_clone, clone_flags, newsp, parent_tidptr, tls,
			   child_tidptr);
}

/*
 * If we clone with CLONE_SETTLS then the value in the parent should
 * be unchanged and the child should start with zero and be able to
 * set its own value.
 */
static int write_clone_read(void)
{
	int parent_tid, child_tid;
	pid_t parent, waiting;
	int ret, status;

	parent = getpid();
	set_tpidr2(parent);

	ret = sys_clone(CLONE_SETTLS, 0, &parent_tid, 0, &child_tid);
	if (ret == -1) {
		putstr("# clone() failed\n");
		putnum(errno);
		putstr("\n");
		return 0;
	}

	if (ret == 0) {
		/* In child */
		if (get_tpidr2() != 0) {
			putstr("# TPIDR2 non-zero in child: ");
			putnum(get_tpidr2());
			putstr("\n");
			exit(0);
		}

		if (gettid() == 0)
			putstr("# Child TID==0\n");
		set_tpidr2(gettid());
		if (get_tpidr2() == gettid()) {
			exit(1);
		} else {
			putstr("# Failed to set TPIDR2 in child\n");
			exit(0);
		}
	}

	for (;;) {
		waiting = wait4(ret, &status, __WCLONE, NULL);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			putstr("# wait4() failed: ");
			putnum(errno);
			putstr("\n");
			return 0;
		}
		if (waiting != ret) {
			putstr("# wait4() returned wrong PID ");
			putnum(waiting);
			putstr("\n");
			return 0;
		}

		if (!WIFEXITED(status)) {
			putstr("# child did not exit\n");
			return 0;
		}

		if (parent != get_tpidr2()) {
			putstr("# TPIDR2 corrupted in parent\n");
			return 0;
		}

		return WEXITSTATUS(status);
	}
}

#define run_test(name)			     \
	if (name()) {			     \
		tests_passed++;		     \
	} else {			     \
		tests_failed++;		     \
		putstr("not ");		     \
	}				     \
	putstr("ok ");			     \
	putnum(++tests_run);		     \
	putstr(" " #name "\n");

#define skip_test(name)			     \
	tests_skipped++;		     \
	putstr("ok ");			     \
	putnum(++tests_run);		     \
	putstr(" # SKIP " #name "\n");

int main(int argc, char **argv)
{
	int ret, i;

	putstr("TAP version 13\n");
	putstr("1..");
	putnum(EXPECTED_TESTS);
	putstr("\n");

	putstr("# PID: ");
	putnum(getpid());
	putstr("\n");

	/*
	 * This test is run with nolibc which doesn't support hwcap and
	 * it's probably disproportionate to implement so instead check
	 * for the default vector length configuration in /proc.
	 */
	ret = open("/proc/sys/abi/sme_default_vector_length", O_RDONLY, 0);
	if (ret >= 0) {
		run_test(default_value);
		run_test(write_read);
		run_test(write_sleep_read);
		run_test(write_fork_read);
		run_test(write_clone_read);

	} else {
		putstr("# SME support not present\n");

		skip_test(default_value);
		skip_test(write_read);
		skip_test(write_sleep_read);
		skip_test(write_fork_read);
		skip_test(write_clone_read);
	}

	print_summary();

	return 0;
}
