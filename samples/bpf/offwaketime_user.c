/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <linux/bpf.h>
#include <string.h>
#include <linux/perf_event.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/resource.h>
#include "libbpf.h"
#include "bpf_load.h"

#define MAX_SYMS 300000
#define PRINT_RAW_ADDR 0

static struct ksym {
	long addr;
	char *name;
} syms[MAX_SYMS];
static int sym_cnt;

static int ksym_cmp(const void *p1, const void *p2)
{
	return ((struct ksym *)p1)->addr - ((struct ksym *)p2)->addr;
}

static int load_kallsyms(void)
{
	FILE *f = fopen("/proc/kallsyms", "r");
	char func[256], buf[256];
	char symbol;
	void *addr;
	int i = 0;

	if (!f)
		return -ENOENT;

	while (!feof(f)) {
		if (!fgets(buf, sizeof(buf), f))
			break;
		if (sscanf(buf, "%p %c %s", &addr, &symbol, func) != 3)
			break;
		if (!addr)
			continue;
		syms[i].addr = (long) addr;
		syms[i].name = strdup(func);
		i++;
	}
	sym_cnt = i;
	qsort(syms, sym_cnt, sizeof(struct ksym), ksym_cmp);
	return 0;
}

static void *search(long key)
{
	int start = 0, end = sym_cnt;
	int result;

	while (start < end) {
		size_t mid = start + (end - start) / 2;

		result = key - syms[mid].addr;
		if (result < 0)
			end = mid;
		else if (result > 0)
			start = mid + 1;
		else
			return &syms[mid];
	}

	if (start >= 1 && syms[start - 1].addr < key &&
	    key < syms[start].addr)
		/* valid ksym */
		return &syms[start - 1];

	/* out of range. return _stext */
	return &syms[0];
}

static void print_ksym(__u64 addr)
{
	struct ksym *sym;

	if (!addr)
		return;
	sym = search(addr);
	if (PRINT_RAW_ADDR)
		printf("%s/%llx;", sym->name, addr);
	else
		printf("%s;", sym->name);
}

#define TASK_COMM_LEN 16

struct key_t {
	char waker[TASK_COMM_LEN];
	char target[TASK_COMM_LEN];
	__u32 wret;
	__u32 tret;
};

static void print_stack(struct key_t *key, __u64 count)
{
	__u64 ip[PERF_MAX_STACK_DEPTH] = {};
	static bool warned;
	int i;

	printf("%s;", key->target);
	if (bpf_lookup_elem(map_fd[3], &key->tret, ip) != 0) {
		printf("---;");
	} else {
		for (i = PERF_MAX_STACK_DEPTH - 1; i >= 0; i--)
			print_ksym(ip[i]);
	}
	printf("-;");
	if (bpf_lookup_elem(map_fd[3], &key->wret, ip) != 0) {
		printf("---;");
	} else {
		for (i = 0; i < PERF_MAX_STACK_DEPTH; i++)
			print_ksym(ip[i]);
	}
	printf(";%s %lld\n", key->waker, count);

	if ((key->tret == -EEXIST || key->wret == -EEXIST) && !warned) {
		printf("stackmap collisions seen. Consider increasing size\n");
		warned = true;
	} else if (((int)(key->tret) < 0 || (int)(key->wret) < 0)) {
		printf("err stackid %d %d\n", key->tret, key->wret);
	}
}

static void print_stacks(int fd)
{
	struct key_t key = {}, next_key;
	__u64 value;

	while (bpf_get_next_key(fd, &key, &next_key) == 0) {
		bpf_lookup_elem(fd, &next_key, &value);
		print_stack(&next_key, value);
		key = next_key;
	}
}

static void int_exit(int sig)
{
	print_stacks(map_fd[0]);
	exit(0);
}

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	char filename[256];
	int delay = 1;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	setrlimit(RLIMIT_MEMLOCK, &r);

	signal(SIGINT, int_exit);

	if (load_kallsyms()) {
		printf("failed to process /proc/kallsyms\n");
		return 2;
	}

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	if (argc > 1)
		delay = atoi(argv[1]);
	sleep(delay);
	print_stacks(map_fd[0]);

	return 0;
}
