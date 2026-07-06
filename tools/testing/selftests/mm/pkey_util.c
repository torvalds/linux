// SPDX-License-Identifier: GPL-2.0-only
#define __SANE_USERSPACE_TYPES__
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "pkey-helpers.h"

int iteration_nr = 1;
int test_nr;
int dprint_in_signal;

#if CONTROL_TRACING > 0
static void cat_into_file(char *str, char *file)
{
	int fd = open(file, O_RDWR);
	int ret;

	dprintf2("%s(): writing '%s' to '%s'\n", __func__, str, file);
	/*
	 * these need to be raw because they are called under
	 * pkey_assert()
	 */
	if (fd < 0) {
		fprintf(stderr, "error opening '%s'\n", file);
		perror("error: ");
		exit(__LINE__);
	}

	ret = write(fd, str, strlen(str));
	if (ret != strlen(str)) {
		perror("write to file failed");
		fprintf(stderr, "filename: '%s' str: '%s'\n", file, str);
		exit(__LINE__);
	}
	close(fd);
}

static int warned_tracing;
static int tracing_root_ok(void)
{
	if (geteuid() != 0) {
		if (!warned_tracing)
			fprintf(stderr, "WARNING: not run as root, "
					"can not do tracing control\n");
		warned_tracing = 1;
		return 0;
	}
	return 1;
}
#endif

void tracing_on(void)
{
#if CONTROL_TRACING > 0
#define TRACEDIR "/sys/kernel/tracing"
	char pidstr[32];

	if (!tracing_root_ok())
		return;

	sprintf(pidstr, "%d", getpid());
	cat_into_file("0", TRACEDIR "/tracing_on");
	cat_into_file("\n", TRACEDIR "/trace");
	if (1) {
		cat_into_file("function_graph", TRACEDIR "/current_tracer");
		cat_into_file("1", TRACEDIR "/options/funcgraph-proc");
	} else {
		cat_into_file("nop", TRACEDIR "/current_tracer");
	}
	cat_into_file(pidstr, TRACEDIR "/set_ftrace_pid");
	cat_into_file("1", TRACEDIR "/tracing_on");
	dprintf1("enabled tracing\n");
#endif
}

void tracing_off(void)
{
#if CONTROL_TRACING > 0
	if (!tracing_root_ok())
		return;
	cat_into_file("0", "/sys/kernel/tracing/tracing_on");
#endif
}

void abort_hooks(void)
{
	fflush(stdout);
	fprintf(stderr, "running %s()...\n", __func__);
	tracing_off();
#ifdef SLEEP_ON_ABORT
	sleep(SLEEP_ON_ABORT);
#endif
}

int sys_pkey_alloc(unsigned long flags, unsigned long init_val)
{
	int ret = syscall(SYS_pkey_alloc, flags, init_val);
	dprintf1("%s(flags=%lx, init_val=%lx) syscall ret: %d errno: %d\n",
			__func__, flags, init_val, ret, errno);
	return ret;
}

int sys_pkey_free(unsigned long pkey)
{
	int ret = syscall(SYS_pkey_free, pkey);
	dprintf1("%s(pkey=%ld) syscall ret: %d\n", __func__, pkey, ret);
	return ret;
}

int sys_mprotect_pkey(void *ptr, size_t size, unsigned long orig_prot,
		unsigned long pkey)
{
	int sret;

	dprintf2("%s(0x%p, %zx, prot=%lx, pkey=%lx)\n", __func__,
			ptr, size, orig_prot, pkey);

	errno = 0;
	sret = syscall(__NR_pkey_mprotect, ptr, size, orig_prot, pkey);
	if (errno) {
		dprintf2("SYS_mprotect_key sret: %d\n", sret);
		dprintf2("SYS_mprotect_key prot: 0x%lx\n", orig_prot);
		dprintf2("SYS_mprotect_key failed, errno: %d\n", errno);
		if (DEBUG_LEVEL >= 2)
			perror("SYS_mprotect_pkey");
	}
	return sret;
}
