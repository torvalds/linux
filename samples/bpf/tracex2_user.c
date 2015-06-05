#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <linux/bpf.h>
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

static void print_hist(int fd)
{
	int key;
	long value;
	long data[MAX_INDEX] = {};
	char starstr[MAX_STARS];
	int i;
	int max_ind = -1;
	long max_value = 0;

	for (key = 0; key < MAX_INDEX; key++) {
		bpf_lookup_elem(fd, &key, &value);
		data[key] = value;
		if (value && key > max_ind)
			max_ind = key;
		if (value > max_value)
			max_value = value;
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
