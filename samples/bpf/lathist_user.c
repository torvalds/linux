// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2013-2015 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2015 BMW Car IT GmbH
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#define MAX_ENTRIES	20
#define MAX_CPU		4
#define MAX_STARS	40

struct cpu_hist {
	long data[MAX_ENTRIES];
	long max;
};

static struct cpu_hist cpu_hist[MAX_CPU];

static void stars(char *str, long val, long max, int width)
{
	int i;

	for (i = 0; i < (width * val / max) - 1 && i < width - 1; i++)
		str[i] = '*';
	if (val > max)
		str[i - 1] = '+';
	str[i] = '\0';
}

static void print_hist(void)
{
	char starstr[MAX_STARS];
	struct cpu_hist *hist;
	int i, j;

	/* clear screen */
	printf("\033[2J");

	for (j = 0; j < MAX_CPU; j++) {
		hist = &cpu_hist[j];

		/* ignore CPUs without data (maybe offline?) */
		if (hist->max == 0)
			continue;

		printf("CPU %d\n", j);
		printf("      latency        : count     distribution\n");
		for (i = 1; i <= MAX_ENTRIES; i++) {
			stars(starstr, hist->data[i - 1], hist->max, MAX_STARS);
			printf("%8ld -> %-8ld : %-8ld |%-*s|\n",
				(1l << i) >> 1, (1l << i) - 1,
				hist->data[i - 1], MAX_STARS, starstr);
		}
	}
}

static void get_data(int fd)
{
	long key, value;
	int c, i;

	for (i = 0; i < MAX_CPU; i++)
		cpu_hist[i].max = 0;

	for (c = 0; c < MAX_CPU; c++) {
		for (i = 0; i < MAX_ENTRIES; i++) {
			key = c * MAX_ENTRIES + i;
			bpf_map_lookup_elem(fd, &key, &value);

			cpu_hist[c].data[i] = value;
			if (value > cpu_hist[c].max)
				cpu_hist[c].max = value;
		}
	}
}

int main(int argc, char **argv)
{
	struct bpf_link *links[2];
	struct bpf_program *prog;
	struct bpf_object *obj;
	char filename[256];
	int map_fd, i = 0;

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

	map_fd = bpf_object__find_map_fd_by_name(obj, "my_lat");
	if (map_fd < 0) {
		fprintf(stderr, "ERROR: finding a map in obj file failed\n");
		goto cleanup;
	}

	bpf_object__for_each_program(prog, obj) {
		links[i] = bpf_program__attach(prog);
		if (libbpf_get_error(links[i])) {
			fprintf(stderr, "ERROR: bpf_program__attach failed\n");
			links[i] = NULL;
			goto cleanup;
		}
		i++;
	}

	while (1) {
		get_data(map_fd);
		print_hist();
		sleep(5);
	}

cleanup:
	for (i--; i >= 0; i--)
		bpf_link__destroy(links[i]);

	bpf_object__close(obj);
	return 0;
}
