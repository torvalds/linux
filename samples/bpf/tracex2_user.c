#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <linux/bpf.h>
#include <string.h>
#include "libbpf.h"
#include "bpf_load.h"

#define MAX_INDEX	64
#define MAX_STARS	38

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
	struct hist_key key = {}, next_key;
	char starstr[MAX_STARS];
	long value;
	long data[MAX_INDEX] = {};
	int max_ind = -1;
	long max_value = 0;
	int i, ind;

	while (bpf_get_next_key(fd, &key, &next_key) == 0) {
		if (memcmp(&next_key, task, SIZE)) {
			key = next_key;
			continue;
		}
		bpf_lookup_elem(fd, &next_key, &value);
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

	while (bpf_get_next_key(fd, &key, &next_key) == 0) {
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
	char filename[256];
	long key, next_key, value;
	FILE *f;
	int i;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	signal(SIGINT, int_exit);

	/* start 'ping' in the background to have some kfree_skb events */
	f = popen("ping -c5 localhost", "r");
	(void) f;

	/* start 'dd' in the background to have plenty of 'write' syscalls */
	f = popen("dd if=/dev/zero of=/dev/null count=5000000", "r");
	(void) f;

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	for (i = 0; i < 5; i++) {
		key = 0;
		while (bpf_get_next_key(map_fd[0], &key, &next_key) == 0) {
			bpf_lookup_elem(map_fd[0], &next_key, &value);
			printf("location 0x%lx count %ld\n", next_key, value);
			key = next_key;
		}
		if (key)
			printf("\n");
		sleep(1);
	}
	print_hist(map_fd[1]);

	return 0;
}
