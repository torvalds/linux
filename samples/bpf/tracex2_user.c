// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "bpf_util.h"

#define MAX_INDEX	64
#define MAX_STARS	38

/* my_map, my_hist_map */
static int map_fd[2];

static void stars(char *str, long val, long max, int width)
{
	int i;

	for (i = 0; i < (width * val / max) - 1 && i < width - 1; i++)
		str[i] = '*';
	if (val > max)
		str[i - 1] = '+';
	str[i] = '\0';
}

struct task {
	char comm[16];
	__u64 pid_tgid;
	__u64 uid_gid;
};

struct hist_key {
	struct task t;
	__u32 index;
};

#define SIZE sizeof(struct task)

static void print_hist_for_pid(int fd, void *task)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	struct hist_key key = {}, next_key;
	long values[nr_cpus];
	char starstr[MAX_STARS];
	long value;
	long data[MAX_INDEX] = {};
	int max_ind = -1;
	long max_value = 0;
	int i, ind;

	while (bpf_map_get_next_key(fd, &key, &next_key) == 0) {
		if (memcmp(&next_key, task, SIZE)) {
			key = next_key;
			continue;
		}
		bpf_map_lookup_elem(fd, &next_key, values);
		value = 0;
		for (i = 0; i < nr_cpus; i++)
			value += values[i];
		ind = next_key.index;
		data[ind] = value;
		if (value && ind > max_ind)
			max_ind = ind;
		if (value > max_value)
			max_value = value;
		key = next_key;
	}

	printf("           syscall write() stats\n");
	printf("     byte_size       : count     distribution\n");
	for (i = 1; i <= max_ind + 1; i++) {
		stars(starstr, data[i - 1], max_value, MAX_STARS);
		printf("%8ld -> %-8ld : %-8ld |%-*s|\n",
		       (1l << i) >> 1, (1l << i) - 1, data[i - 1],
		       MAX_STARS, starstr);
	}
}

static void print_hist(int fd)
{
	struct hist_key key = {}, next_key;
	static struct task tasks[1024];
	int task_cnt = 0;
	int i;

	while (bpf_map_get_next_key(fd, &key, &next_key) == 0) {
		int found = 0;

		for (i = 0; i < task_cnt; i++)
			if (memcmp(&tasks[i], &next_key, SIZE) == 0)
				found = 1;
		if (!found)
			memcpy(&tasks[task_cnt++], &next_key, SIZE);
		key = next_key;
	}

	for (i = 0; i < task_cnt; i++) {
		printf("\npid %d cmd %s uid %d\n",
		       (__u32) tasks[i].pid_tgid,
		       tasks[i].comm,
		       (__u32) tasks[i].uid_gid);
		print_hist_for_pid(fd, &tasks[i]);
	}

}

static void int_exit(int sig)
{
	print_hist(map_fd[1]);
	exit(0);
}

int main(int ac, char **argv)
{
	struct rlimit r = {1024*1024, RLIM_INFINITY};
	long key, next_key, value;
	struct bpf_link *links[2];
	struct bpf_program *prog;
	struct bpf_object *obj;
	char filename[256];
	int i, j = 0;
	FILE *f;

	if (setrlimit(RLIMIT_MEMLOCK, &r)) {
		perror("setrlimit(RLIMIT_MEMLOCK)");
		return 1;
	}

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	obj = bpf_object__open_file(filename, NULL);
	if (libbpf_get_error(obj)) {
		fprintf(stderr, "ERROR: opening BPF object file failed\n");
		return 0;
	}

	/* load BPF program */
	if (bpf_object__load(obj)) {
		fprintf(stderr, "ERROR: loading BPF object file failed\n");
		goto cleanup;
	}

	map_fd[0] = bpf_object__find_map_fd_by_name(obj, "my_map");
	map_fd[1] = bpf_object__find_map_fd_by_name(obj, "my_hist_map");
	if (map_fd[0] < 0 || map_fd[1] < 0) {
		fprintf(stderr, "ERROR: finding a map in obj file failed\n");
		goto cleanup;
	}

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	/* start 'ping' in the background to have some kfree_skb events */
	f = popen("ping -4 -c5 localhost", "r");
	(void) f;

	/* start 'dd' in the background to have plenty of 'write' syscalls */
	f = popen("dd if=/dev/zero of=/dev/null count=5000000", "r");
	(void) f;

	bpf_object__for_each_program(prog, obj) {
		links[j] = bpf_program__attach(prog);
		if (libbpf_get_error(links[j])) {
			fprintf(stderr, "ERROR: bpf_program__attach failed\n");
			links[j] = NULL;
			goto cleanup;
		}
		j++;
	}

	for (i = 0; i < 5; i++) {
		key = 0;
		while (bpf_map_get_next_key(map_fd[0], &key, &next_key) == 0) {
			bpf_map_lookup_elem(map_fd[0], &next_key, &value);
			printf("location 0x%lx count %ld\n", next_key, value);
			key = next_key;
		}
		if (key)
			printf("\n");
		sleep(1);
	}
	print_hist(map_fd[1]);

cleanup:
	for (j--; j >= 0; j--)
		bpf_link__destroy(links[j]);

	bpf_object__close(obj);
	return 0;
}
