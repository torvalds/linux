// SPDX-License-Identifier: GPL-2.0+
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include "ptrace.h"

char data[16];

/* Overlapping address range */
volatile __u64 *ptrace_data1 = (__u64 *)&data[0];
volatile __u64 *perf_data1 = (__u64 *)&data[4];

/* Non-overlapping address range */
volatile __u64 *ptrace_data2 = (__u64 *)&data[0];
volatile __u64 *perf_data2 = (__u64 *)&data[8];

static unsigned long pid_max_addr(void)
{
	FILE *fp;
	char *line, *c;
	char addr[100];
	size_t len = 0;

	fp = fopen("/proc/kallsyms", "r");
	if (!fp) {
		printf("Failed to read /proc/kallsyms. Exiting..\n");
		exit(EXIT_FAILURE);
	}

	while (getline(&line, &len, fp) != -1) {
		if (!strstr(line, "pid_max") || strstr(line, "pid_max_max") ||
		    strstr(line, "pid_max_min"))
			continue;

		strncpy(addr, line, len < 100 ? len : 100);
		c = strchr(addr, ' ');
		*c = '\0';
		return strtoul(addr, &c, 16);
	}
	fclose(fp);
	printf("Could not find pid_max. Exiting..\n");
	exit(EXIT_FAILURE);
	return -1;
}

static void perf_user_event_attr_set(struct perf_event_attr *attr, __u64 addr, __u64 len)
{
	memset(attr, 0, sizeof(struct perf_event_attr));
	attr->type           = PERF_TYPE_BREAKPOINT;
	attr->size           = sizeof(struct perf_event_attr);
	attr->bp_type        = HW_BREAKPOINT_R;
	attr->bp_addr        = addr;
	attr->bp_len         = len;
	attr->exclude_kernel = 1;
	attr->exclude_hv     = 1;
}

static void perf_kernel_event_attr_set(struct perf_event_attr *attr)
{
	memset(attr, 0, sizeof(struct perf_event_attr));
	attr->type           = PERF_TYPE_BREAKPOINT;
	attr->size           = sizeof(struct perf_event_attr);
	attr->bp_type        = HW_BREAKPOINT_R;
	attr->bp_addr        = pid_max_addr();
	attr->bp_len         = sizeof(unsigned long);
	attr->exclude_user   = 1;
	attr->exclude_hv     = 1;
}

static int perf_cpu_event_open(int cpu, __u64 addr, __u64 len)
{
	struct perf_event_attr attr;

	perf_user_event_attr_set(&attr, addr, len);
	return syscall(__NR_perf_event_open, &attr, -1, cpu, -1, 0);
}

static int perf_thread_event_open(pid_t child_pid, __u64 addr, __u64 len)
{
	struct perf_event_attr attr;

	perf_user_event_attr_set(&attr, addr, len);
	return syscall(__NR_perf_event_open, &attr, child_pid, -1, -1, 0);
}

static int perf_thread_cpu_event_open(pid_t child_pid, int cpu, __u64 addr, __u64 len)
{
	struct perf_event_attr attr;

	perf_user_event_attr_set(&attr, addr, len);
	return syscall(__NR_perf_event_open, &attr, child_pid, cpu, -1, 0);
}

static int perf_thread_kernel_event_open(pid_t child_pid)
{
	struct perf_event_attr attr;

	perf_kernel_event_attr_set(&attr);
	return syscall(__NR_perf_event_open, &attr, child_pid, -1, -1, 0);
}

static int perf_cpu_kernel_event_open(int cpu)
{
	struct perf_event_attr attr;

	perf_kernel_event_attr_set(&attr);
	return syscall(__NR_perf_event_open, &attr, -1, cpu, -1, 0);
}

static int child(void)
{
	int ret;

	ret = ptrace(PTRACE_TRACEME, 0, NULL, 0);
	if (ret) {
		printf("Error: PTRACE_TRACEME failed\n");
		return 0;
	}
	kill(getpid(), SIGUSR1); /* --> parent (SIGUSR1) */

	return 0;
}

static void ptrace_ppc_hw_breakpoint(struct ppc_hw_breakpoint *info, int type,
				     __u64 addr, int len)
{
	info->version = 1;
	info->trigger_type = type;
	info->condition_mode = PPC_BREAKPOINT_CONDITION_NONE;
	info->addr = addr;
	info->addr2 = addr + len;
	info->condition_value = 0;
	if (!len)
		info->addr_mode = PPC_BREAKPOINT_MODE_EXACT;
	else
		info->addr_mode = PPC_BREAKPOINT_MODE_RANGE_INCLUSIVE;
}

static int ptrace_open(pid_t child_pid, __u64 wp_addr, int len)
{
	struct ppc_hw_breakpoint info;

	ptrace_ppc_hw_breakpoint(&info, PPC_BREAKPOINT_TRIGGER_RW, wp_addr, len);
	return ptrace(PPC_PTRACE_SETHWDEBUG, child_pid, 0, &info);
}

static int test1(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int ret = 0;

	/* Test:
	 * if (new per thread event by ptrace)
	 *	if (existing cpu event by perf)
	 *		if (addr range overlaps)
	 *			fail;
	 */

	perf_fd = perf_cpu_event_open(0, (__u64)perf_data1, sizeof(*perf_data1));
	if (perf_fd < 0)
		return -1;

	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data1, sizeof(*ptrace_data1));
	if (ptrace_fd > 0 || errno != ENOSPC)
		ret = -1;

	close(perf_fd);
	return ret;
}

static int test2(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int ret = 0;

	/* Test:
	 * if (new per thread event by ptrace)
	 *	if (existing cpu event by perf)
	 *		if (addr range does not overlaps)
	 *			allow;
	 */

	perf_fd = perf_cpu_event_open(0, (__u64)perf_data2, sizeof(*perf_data2));
	if (perf_fd < 0)
		return -1;

	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data2, sizeof(*ptrace_data2));
	if (ptrace_fd < 0) {
		ret = -1;
		goto perf_close;
	}
	ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, ptrace_fd);

perf_close:
	close(perf_fd);
	return ret;
}

static int test3(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int ret = 0;

	/* Test:
	 * if (new per thread event by ptrace)
	 *	if (existing thread event by perf on the same thread)
	 *		if (addr range overlaps)
	 *			fail;
	 */
	perf_fd = perf_thread_event_open(child_pid, (__u64)perf_data1,
					 sizeof(*perf_data1));
	if (perf_fd < 0)
		return -1;

	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data1, sizeof(*ptrace_data1));
	if (ptrace_fd > 0 || errno != ENOSPC)
		ret = -1;

	close(perf_fd);
	return ret;
}

static int test4(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int ret = 0;

	/* Test:
	 * if (new per thread event by ptrace)
	 *	if (existing thread event by perf on the same thread)
	 *		if (addr range does not overlaps)
	 *			fail;
	 */
	perf_fd = perf_thread_event_open(child_pid, (__u64)perf_data2,
					 sizeof(*perf_data2));
	if (perf_fd < 0)
		return -1;

	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data2, sizeof(*ptrace_data2));
	if (ptrace_fd < 0) {
		ret = -1;
		goto perf_close;
	}
	ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, ptrace_fd);

perf_close:
	close(perf_fd);
	return ret;
}

static int test5(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int cpid;
	int ret = 0;

	/* Test:
	 * if (new per thread event by ptrace)
	 *	if (existing thread event by perf on the different thread)
	 *		allow;
	 */
	cpid = fork();
	if (!cpid) {
		/* Temporary Child */
		pause();
		exit(EXIT_SUCCESS);
	}

	perf_fd = perf_thread_event_open(cpid, (__u64)perf_data1, sizeof(*perf_data1));
	if (perf_fd < 0) {
		ret = -1;
		goto kill_child;
	}

	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data1, sizeof(*ptrace_data1));
	if (ptrace_fd < 0) {
		ret = -1;
		goto perf_close;
	}

	ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, ptrace_fd);
perf_close:
	close(perf_fd);
kill_child:
	kill(cpid, SIGINT);
	return ret;
}

static int test6(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int ret = 0;

	/* Test:
	 * if (new per thread kernel event by perf)
	 *	if (existing thread event by ptrace on the same thread)
	 *		allow;
	 * -- OR --
	 * if (new per cpu kernel event by perf)
	 *	if (existing thread event by ptrace)
	 *		allow;
	 */
	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data1, sizeof(*ptrace_data1));
	if (ptrace_fd < 0)
		return -1;

	perf_fd = perf_thread_kernel_event_open(child_pid);
	if (perf_fd < 0) {
		ret = -1;
		goto ptrace_close;
	}
	close(perf_fd);

	perf_fd = perf_cpu_kernel_event_open(0);
	if (perf_fd < 0) {
		ret = -1;
		goto ptrace_close;
	}
	close(perf_fd);

ptrace_close:
	ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, ptrace_fd);
	return ret;
}

static int test7(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int ret = 0;

	/* Test:
	 * if (new per thread event by perf)
	 *	if (existing thread event by ptrace on the same thread)
	 *		if (addr range overlaps)
	 *			fail;
	 */
	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data1, sizeof(*ptrace_data1));
	if (ptrace_fd < 0)
		return -1;

	perf_fd = perf_thread_event_open(child_pid, (__u64)perf_data1,
					 sizeof(*perf_data1));
	if (perf_fd > 0 || errno != ENOSPC)
		ret = -1;

	ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, ptrace_fd);
	return ret;
}

static int test8(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int ret = 0;

	/* Test:
	 * if (new per thread event by perf)
	 *	if (existing thread event by ptrace on the same thread)
	 *		if (addr range does not overlaps)
	 *			allow;
	 */
	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data2, sizeof(*ptrace_data2));
	if (ptrace_fd < 0)
		return -1;

	perf_fd = perf_thread_event_open(child_pid, (__u64)perf_data2,
					 sizeof(*perf_data2));
	if (perf_fd < 0) {
		ret = -1;
		goto ptrace_close;
	}
	close(perf_fd);

ptrace_close:
	ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, ptrace_fd);
	return ret;
}

static int test9(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int cpid;
	int ret = 0;

	/* Test:
	 * if (new per thread event by perf)
	 *	if (existing thread event by ptrace on the other thread)
	 *		allow;
	 */
	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data1, sizeof(*ptrace_data1));
	if (ptrace_fd < 0)
		return -1;

	cpid = fork();
	if (!cpid) {
		/* Temporary Child */
		pause();
		exit(EXIT_SUCCESS);
	}

	perf_fd = perf_thread_event_open(cpid, (__u64)perf_data1, sizeof(*perf_data1));
	if (perf_fd < 0) {
		ret = -1;
		goto kill_child;
	}
	close(perf_fd);

kill_child:
	kill(cpid, SIGINT);
	ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, ptrace_fd);
	return ret;
}

static int test10(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int ret = 0;

	/* Test:
	 * if (new per cpu event by perf)
	 *	if (existing thread event by ptrace on the same thread)
	 *		if (addr range overlaps)
	 *			fail;
	 */
	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data1, sizeof(*ptrace_data1));
	if (ptrace_fd < 0)
		return -1;

	perf_fd = perf_cpu_event_open(0, (__u64)perf_data1, sizeof(*perf_data1));
	if (perf_fd > 0 || errno != ENOSPC)
		ret = -1;

	ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, ptrace_fd);
	return ret;
}

static int test11(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int ret = 0;

	/* Test:
	 * if (new per cpu event by perf)
	 *	if (existing thread event by ptrace on the same thread)
	 *		if (addr range does not overlap)
	 *			allow;
	 */
	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data2, sizeof(*ptrace_data2));
	if (ptrace_fd < 0)
		return -1;

	perf_fd = perf_cpu_event_open(0, (__u64)perf_data2, sizeof(*perf_data2));
	if (perf_fd < 0) {
		ret = -1;
		goto ptrace_close;
	}
	close(perf_fd);

ptrace_close:
	ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, ptrace_fd);
	return ret;
}

static int test12(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int ret = 0;

	/* Test:
	 * if (new per thread and per cpu event by perf)
	 *	if (existing thread event by ptrace on the same thread)
	 *		if (addr range overlaps)
	 *			fail;
	 */
	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data1, sizeof(*ptrace_data1));
	if (ptrace_fd < 0)
		return -1;

	perf_fd = perf_thread_cpu_event_open(child_pid, 0, (__u64)perf_data1, sizeof(*perf_data1));
	if (perf_fd > 0 || errno != ENOSPC)
		ret = -1;

	ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, ptrace_fd);
	return ret;
}

static int test13(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int ret = 0;

	/* Test:
	 * if (new per thread and per cpu event by perf)
	 *	if (existing thread event by ptrace on the same thread)
	 *		if (addr range does not overlap)
	 *			allow;
	 */
	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data2, sizeof(*ptrace_data2));
	if (ptrace_fd < 0)
		return -1;

	perf_fd = perf_thread_cpu_event_open(child_pid, 0, (__u64)perf_data2, sizeof(*perf_data2));
	if (perf_fd < 0) {
		ret = -1;
		goto ptrace_close;
	}
	close(perf_fd);

ptrace_close:
	ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, ptrace_fd);
	return ret;
}

static int test14(pid_t child_pid)
{
	int perf_fd;
	int ptrace_fd;
	int cpid;
	int ret = 0;

	/* Test:
	 * if (new per thread and per cpu event by perf)
	 *	if (existing thread event by ptrace on the other thread)
	 *		allow;
	 */
	ptrace_fd = ptrace_open(child_pid, (__u64)ptrace_data1, sizeof(*ptrace_data1));
	if (ptrace_fd < 0)
		return -1;

	cpid = fork();
	if (!cpid) {
		/* Temporary Child */
		pause();
		exit(EXIT_SUCCESS);
	}

	perf_fd = perf_thread_cpu_event_open(cpid, 0, (__u64)perf_data1,
					     sizeof(*perf_data1));
	if (perf_fd < 0) {
		ret = -1;
		goto kill_child;
	}
	close(perf_fd);

kill_child:
	kill(cpid, SIGINT);
	ptrace(PPC_PTRACE_DELHWDEBUG, child_pid, 0, ptrace_fd);
	return ret;
}

static int do_test(const char *msg, int (*fun)(pid_t arg), pid_t arg)
{
	int ret;

	ret = fun(arg);
	if (ret)
		printf("%s: Error\n", msg);
	else
		printf("%s: Ok\n", msg);
	return ret;
}

char *desc[14] = {
	"perf cpu event -> ptrace thread event (Overlapping)",
	"perf cpu event -> ptrace thread event (Non-overlapping)",
	"perf thread event -> ptrace same thread event (Overlapping)",
	"perf thread event -> ptrace same thread event (Non-overlapping)",
	"perf thread event -> ptrace other thread event",
	"ptrace thread event -> perf kernel event",
	"ptrace thread event -> perf same thread event (Overlapping)",
	"ptrace thread event -> perf same thread event (Non-overlapping)",
	"ptrace thread event -> perf other thread event",
	"ptrace thread event -> perf cpu event (Overlapping)",
	"ptrace thread event -> perf cpu event (Non-overlapping)",
	"ptrace thread event -> perf same thread & cpu event (Overlapping)",
	"ptrace thread event -> perf same thread & cpu event (Non-overlapping)",
	"ptrace thread event -> perf other thread & cpu event",
};

static int test(pid_t child_pid)
{
	int ret = TEST_PASS;

	ret |= do_test(desc[0], test1, child_pid);
	ret |= do_test(desc[1], test2, child_pid);
	ret |= do_test(desc[2], test3, child_pid);
	ret |= do_test(desc[3], test4, child_pid);
	ret |= do_test(desc[4], test5, child_pid);
	ret |= do_test(desc[5], test6, child_pid);
	ret |= do_test(desc[6], test7, child_pid);
	ret |= do_test(desc[7], test8, child_pid);
	ret |= do_test(desc[8], test9, child_pid);
	ret |= do_test(desc[9], test10, child_pid);
	ret |= do_test(desc[10], test11, child_pid);
	ret |= do_test(desc[11], test12, child_pid);
	ret |= do_test(desc[12], test13, child_pid);
	ret |= do_test(desc[13], test14, child_pid);

	return ret;
}

static void get_dbginfo(pid_t child_pid, struct ppc_debug_info *dbginfo)
{
	if (ptrace(PPC_PTRACE_GETHWDBGINFO, child_pid, NULL, dbginfo)) {
		perror("Can't get breakpoint info");
		exit(-1);
	}
}

static int ptrace_perf_hwbreak(void)
{
	int ret;
	pid_t child_pid;
	struct ppc_debug_info dbginfo;

	child_pid = fork();
	if (!child_pid)
		return child();

	/* parent */
	wait(NULL); /* <-- child (SIGUSR1) */

	get_dbginfo(child_pid, &dbginfo);
	SKIP_IF_MSG(dbginfo.num_data_bps <= 1, "Not enough data watchpoints (need at least 2)");

	ret = perf_cpu_event_open(0, (__u64)perf_data1, sizeof(*perf_data1));
	SKIP_IF_MSG(ret < 0, "perf_event_open syscall failed");
	close(ret);

	ret = test(child_pid);

	ptrace(PTRACE_CONT, child_pid, NULL, 0);
	return ret;
}

int main(int argc, char *argv[])
{
	return test_harness(ptrace_perf_hwbreak, "ptrace-perf-hwbreak");
}
