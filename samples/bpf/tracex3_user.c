// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2013-2015 PLUMgrid, http://plumgrid.com
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <sys/resource.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "bpf_util.h"

#define SLOTS 100

static void clear_stats(int fd)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	__u64 values[nr_cpus];
	__u32 key;

	memset(values, 0, sizeof(values));
	for (key = 0; key < SLOTS; key++)
		bpf_map_update_elem(fd, &key, values, BPF_ANY);
}

const char *color[] = {
	"\033[48;5;255m",
	"\033[48;5;252m",
	"\033[48;5;250m",
	"\033[48;5;248m",
	"\033[48;5;246m",
	"\033[48;5;244m",
	"\033[48;5;242m",
	"\033[48;5;240m",
	"\033[48;5;238m",
	"\033[48;5;236m",
	"\033[48;5;234m",
	"\033[48;5;232m",
};
const int num_colors = ARRAY_SIZE(color);

const char nocolor[] = "\033[00m";

const char *sym[] = {
	" ",
	" ",
	".",
	".",
	"*",
	"*",
	"o",
	"o",
	"O",
	"O",
	"#",
	"#",
};

bool full_range = false;
bool text_only = false;

static void print_banner(void)
{
	if (full_range)
		printf("|1ns     |10ns     |100ns    |1us      |10us     |100us"
		       "    |1ms      |10ms     |100ms    |1s       |10s\n");
	else
		printf("|1us      |10us     |100us    |1ms      |10ms     "
		       "|100ms    |1s       |10s\n");
}

static void print_hist(int fd)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	__u64 total_events = 0;
	long values[nr_cpus];
	__u64 max_cnt = 0;
	__u64 cnt[SLOTS];
	__u64 value;
	__u32 key;
	int i;

	for (key = 0; key < SLOTS; key++) {
		bpf_map_lookup_elem(fd, &key, values);
		value = 0;
		for (i = 0; i < nr_cpus; i++)
			value += values[i];
		cnt[key] = value;
		total_events += value;
		if (value > max_cnt)
			max_cnt = value;
	}
	clear_stats(fd);
	for (key = full_range ? 0 : 29; key < SLOTS; key++) {
		int c = num_colors * cnt[key] / (max_cnt + 1);

		if (text_only)
			printf("%s", sym[c]);
		else
			printf("%s %s", color[c], nocolor);
	}
	printf(" # %lld\n", total_events);
}

int main(int ac, char **argv)
{
	struct bpf_link *links[2];
	struct bpf_program *prog;
	struct bpf_object *obj;
	char filename[256];
	int map_fd, i, j = 0;

	for (i = 1; i < ac; i++) {
		if (strcmp(argv[i], "-a") == 0) {
			full_range = true;
		} else if (strcmp(argv[i], "-t") == 0) {
			text_only = true;
		} else if (strcmp(argv[i], "-h") == 0) {
			printf("Usage:\n"
			       "  -a display wider latency range\n"
			       "  -t text only\n");
			return 1;
		}
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

	map_fd = bpf_object__find_map_fd_by_name(obj, "lat_map");
	if (map_fd < 0) {
		fprintf(stderr, "ERROR: finding a map in obj file failed\n");
		goto cleanup;
	}

	bpf_object__for_each_program(prog, obj) {
		links[j] = bpf_program__attach(prog);
		if (libbpf_get_error(links[j])) {
			fprintf(stderr, "ERROR: bpf_program__attach failed\n");
			links[j] = NULL;
			goto cleanup;
		}
		j++;
	}

	printf("  heatmap of IO latency\n");
	if (text_only)
		printf("  %s", sym[num_colors - 1]);
	else
		printf("  %s %s", color[num_colors - 1], nocolor);
	printf(" - many events with this latency\n");

	if (text_only)
		printf("  %s", sym[0]);
	else
		printf("  %s %s", color[0], nocolor);
	printf(" - few events\n");

	for (i = 0; ; i++) {
		if (i % 20 == 0)
			print_banner();
		print_hist(map_fd);
		sleep(2);
	}

cleanup:
	for (j--; j >= 0; j--)
		bpf_link__destroy(links[j]);

	bpf_object__close(obj);
	return 0;
}
