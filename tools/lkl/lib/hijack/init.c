/*
 * system calls hijack code
 * Copyright (c) 2015 Hajime Tazaki
 *
 * Author: Hajime Tazaki <tazaki@sfc.wide.ad.jp>
 *
 * Note: some of the code is picked from rumpkernel, written by Antti Kantee.
 */

#include <stdio.h>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <lkl.h>
#include <lkl_host.h>
#include <lkl_config.h>

#include "xlate.h"
#include "init.h"

#define __USE_GNU
#include <dlfcn.h>

#define _GNU_SOURCE
#include <sched.h>

/* Mount points are named after filesystem types so they should never
 * be longer than ~6 characters. */
#define MAX_FSTYPE_LEN 50

static void PinToCpus(const cpu_set_t* cpus)
{
	if (sched_setaffinity(0, sizeof(cpu_set_t), cpus)) {
		perror("sched_setaffinity");
	}
}

static void PinToFirstCpu(const cpu_set_t* cpus)
{
	int j;
	cpu_set_t pinto;
	CPU_ZERO(&pinto);
	for (j = 0; j < CPU_SETSIZE; j++) {
		if (CPU_ISSET(j, cpus)) {
			lkl_printf("LKL: Pin To CPU %d\n", j);
			CPU_SET(j, &pinto);
			PinToCpus(&pinto);
			return;
		}
	}
}

int lkl_debug, lkl_running;

static struct lkl_config *cfg;

static int config_load(void)
{
	int len, ret = -1;
	char *buf;
	int fd;
	char *path = getenv("LKL_HIJACK_CONFIG_FILE");

	cfg = (struct lkl_config *)malloc(sizeof(struct lkl_config));
	if (!cfg) {
		perror("config malloc");
		return -1;
	}
	memset(cfg, 0, sizeof(struct lkl_config));

	ret = lkl_load_config_env(cfg);
	if (ret < 0)
		return ret;

	if (path)
		fd = open(path, O_RDONLY, 0);
	else if (access("lkl-hijack.json", R_OK) == 0)
		fd = open("lkl-hijack.json", O_RDONLY, 0);
	else
		return 0;
	if (fd < 0) {
		fprintf(stderr, "config_file open %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	len = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	if (len < 0) {
		perror("config size check (lseek)");
		return -1;
	} else if (len == 0) {
		return 0;
	}
	buf = (char *)malloc(len * sizeof(char) + 1);
	if (!buf) {
		perror("config buf malloc");
		return -1;
	}
	memset(buf, 0, len * sizeof(char) + 1);
	ret = read(fd, buf, len);
	if (ret < 0) {
		perror("config file read");
		free(buf);
		return -1;
	}
	ret = lkl_load_config_json(cfg, buf);
	free(buf);
	return ret;
}

void
__hijack_init(void)
{
	int ret, i, dev_null;
	int single_cpu_mode = 0;
	cpu_set_t ori_cpu;

	ret = config_load();
	if (ret < 0)
		return;

	ret = lkl_init(&lkl_host_ops);
	if (ret) {
		fprintf(stderr, "can't init lkl: %s\n", lkl_strerror(ret));
		return;
	}

	/* reflect pre-configuration */
	lkl_load_config_pre(cfg);

	/* hijack library specific configurations */
	if (cfg->debug)
		lkl_register_dbg_handler();

	if (lkl_debug & 0x200) {
		char c;

		printf("press 'enter' to continue\n");
		if (scanf("%c", &c) <= 0) {
			fprintf(stderr, "scanf() fails\n");
			return;
		}
	}
	if (cfg->single_cpu) {
		single_cpu_mode = atoi(cfg->single_cpu);
		switch (single_cpu_mode) {
			case 0:
			case 1:
			case 2: break;
			default:
				fprintf(stderr, "single cpu mode must be 0~2.\n");
				single_cpu_mode = 0;
				break;
		}
	}

	if (single_cpu_mode) {
		if (sched_getaffinity(0, sizeof(cpu_set_t), &ori_cpu)) {
			perror("sched_getaffinity");
			single_cpu_mode = 0;
		}
	}

	/* Pin to a single cpu.
	 * Any children thread created after it are pinned to the same CPU.
	 */
	if (single_cpu_mode == 2)
		PinToFirstCpu(&ori_cpu);

	if (single_cpu_mode == 1)
		PinToFirstCpu(&ori_cpu);

#ifdef __ANDROID__
	struct sigaction sa;

	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	if (sigaction(32, &sa, 0) == -1) {
		perror("sigaction");
		exit(1);
	}
#endif

	ret = lkl_start_kernel(cfg->boot_cmdline);
	if (ret) {
		fprintf(stderr, "can't start kernel: %s\n", lkl_strerror(ret));
		lkl_cleanup();
		return;
	}

	lkl_running = 1;

	/* initialize epoll manage list */
	memset(dual_fds, -1, sizeof(int) * LKL_FD_OFFSET);

	/* restore cpu affinity */
	if (single_cpu_mode)
		PinToCpus(&ori_cpu);

	ret = lkl_set_fd_limit(65535);
	if (ret)
		fprintf(stderr, "lkl_set_fd_limit failed: %s\n",
			lkl_strerror(ret));

	/* fillup FDs up to LKL_FD_OFFSET */
	ret = lkl_sys_mknod("/dev_null", LKL_S_IFCHR | 0600, LKL_MKDEV(1, 3));
	dev_null = lkl_sys_open("/dev_null", LKL_O_RDONLY, 0);
	if (dev_null < 0) {
		fprintf(stderr, "failed to open /dev/null: %s\n",
				lkl_strerror(dev_null));
		return;
	}

	for (i = 1; i < LKL_FD_OFFSET; i++)
		lkl_sys_dup(dev_null);

	/* lo iff_up */
	lkl_if_up(1);

	/* reflect post-configuration */
	lkl_load_config_post(cfg);
}

void __attribute__((constructor))
hijack_init(void)
{
	if (getenv("LKL_HIJACK_ZPOLINE"))
		return;

	return __hijack_init();
}

void
__hijack_fini(void)
{
	int i;
	int err;

	/* The following pauses the kernel before exiting allowing one
	 * to debug or collect stattistics/diagnosis info from it.
	 */
	if (lkl_debug & 0x100) {
		while (1)
			pause();
	}

	if (!cfg)
		return;

	lkl_unload_config(cfg);
	free(cfg);

	if (!lkl_running)
		return;

	for (i = 0; i < LKL_FD_OFFSET; i++)
		lkl_sys_close(i);

	err = lkl_sys_halt();
	if (err)
		fprintf(stderr, "lkl_sys_halt: %s\n", lkl_strerror(err));

	lkl_cleanup();
}

void __attribute__((destructor))
hijack_fini(void)
{
	if (getenv("LKL_HIJACK_ZPOLINE"))
		return;

	return __hijack_fini();
}
