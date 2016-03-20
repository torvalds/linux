/* Copyright (c) 2013-2015 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2015 BMW Car IT GmbH
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
#include "libbpf.h"
#include "bpf_load.h"

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
			bpf_lookup_elem(fd, &key, &value);

			cpu_hist[c].data[i] = value;
			if (value > cpu_hist[c].max)
				cpu_hist[c].max = value;
		}
	}
}

int main(int argc, char **argv)
{
	char filename[256];

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	while (1) {
		get_data(map_fd[1]);
		print_hist();
		sleep(5);
	}

	return 0;
}
