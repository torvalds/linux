// SPDX-License-Identifier: GPL-2.0
#include <linux/unistd.h>
#include <linux/bpf.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#include <bpf/bpf.h>
#include "bpf_util.h"

#define MAX_INDEX 64
#define MAX_STARS 38

char bpf_log_buf[BPF_LOG_BUF_SIZE];

static void stars(char *str, long val, long max, int width)
{
	int i;

	for (i = 0; i < (width * val / max) - 1 && i < width - 1; i++)
		str[i] = '*';
	if (val > max)
		str[i - 1] = '+';
	str[i] = '\0';
}

int main(int argc, char **argv)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	const char *map_filename = "/sys/fs/bpf/tc/globals/lwt_len_hist_map";
	uint64_t values[nr_cpus], sum, max_value = 0, data[MAX_INDEX] = {};
	uint64_t key = 0, next_key, max_key = 0;
	char starstr[MAX_STARS];
	int i, map_fd;

	map_fd = bpf_obj_get(map_filename);
	if (map_fd < 0) {
		fprintf(stderr, "bpf_obj_get(%s): %s(%d)\n",
			map_filename, strerror(errno), errno);
		return -1;
	}

	while (bpf_map_get_next_key(map_fd, &key, &next_key) == 0) {
		if (next_key >= MAX_INDEX) {
			fprintf(stderr, "Key %lu out of bounds\n", next_key);
			continue;
		}

		bpf_map_lookup_elem(map_fd, &next_key, values);

		sum = 0;
		for (i = 0; i < nr_cpus; i++)
			sum += values[i];

		data[next_key] = sum;
		if (sum && next_key > max_key)
			max_key = next_key;

		if (sum > max_value)
			max_value = sum;

		key = next_key;
	}

	for (i = 1; i <= max_key + 1; i++) {
		stars(starstr, data[i - 1], max_value, MAX_STARS);
		printf("%8ld -> %-8ld : %-8ld |%-*s|\n",
		       (1l << i) >> 1, (1l << i) - 1, data[i - 1],
		       MAX_STARS, starstr);
	}

	close(map_fd);

	return 0;
}
