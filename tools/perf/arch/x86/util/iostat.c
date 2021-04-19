// SPDX-License-Identifier: GPL-2.0
/*
 * perf iostat
 *
 * Copyright (C) 2020, Intel Corporation
 *
 * Authors: Alexander Antonov <alexander.antonov@linux.intel.com>
 */

#include <api/fs/fs.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <regex.h>
#include "util/cpumap.h"
#include "util/debug.h"
#include "util/iostat.h"
#include "util/counts.h"
#include "path.h"

struct iio_root_port {
	u32 domain;
	u8 bus;
	u8 die;
	u8 pmu_idx;
	int idx;
};

struct iio_root_ports_list {
	struct iio_root_port **rps;
	int nr_entries;
};

static void iio_root_port_show(FILE *output,
			       const struct iio_root_port * const rp)
{
	if (output && rp)
		fprintf(output, "S%d-uncore_iio_%d<%04x:%02x>\n",
			rp->die, rp->pmu_idx, rp->domain, rp->bus);
}

static struct iio_root_port *iio_root_port_new(u32 domain, u8 bus,
					       u8 die, u8 pmu_idx)
{
	struct iio_root_port *p = calloc(1, sizeof(*p));

	if (p) {
		p->domain = domain;
		p->bus = bus;
		p->die = die;
		p->pmu_idx = pmu_idx;
	}
	return p;
}

static void iio_root_ports_list_free(struct iio_root_ports_list *list)
{
	int idx;

	if (list) {
		for (idx = 0; idx < list->nr_entries; idx++)
			free(list->rps[idx]);
		free(list->rps);
		free(list);
	}
}

static struct iio_root_port *iio_root_port_find_by_notation(
	const struct iio_root_ports_list * const list, u32 domain, u8 bus)
{
	int idx;
	struct iio_root_port *rp;

	if (list) {
		for (idx = 0; idx < list->nr_entries; idx++) {
			rp = list->rps[idx];
			if (rp && rp->domain == domain && rp->bus == bus)
				return rp;
		}
	}
	return NULL;
}

static int iio_root_ports_list_insert(struct iio_root_ports_list *list,
				      struct iio_root_port * const rp)
{
	struct iio_root_port **tmp_buf;

	if (list && rp) {
		rp->idx = list->nr_entries++;
		tmp_buf = realloc(list->rps,
				  list->nr_entries * sizeof(*list->rps));
		if (!tmp_buf) {
			pr_err("Failed to realloc memory\n");
			return -ENOMEM;
		}
		tmp_buf[rp->idx] = rp;
		list->rps = tmp_buf;
	}
	return 0;
}
