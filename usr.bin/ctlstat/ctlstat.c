/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004, 2008, 2009 Silicon Graphics International Corp.
 * Copyright (c) 2017 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/users/kenm/FreeBSD-test2/usr.bin/ctlstat/ctlstat.c#4 $
 */
/*
 * CAM Target Layer statistics program
 *
 * Authors: Ken Merry <ken@FreeBSD.org>, Will Andrews <will@FreeBSD.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <sys/queue.h>
#include <sys/callout.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <ctype.h>
#include <bitstring.h>
#include <cam/scsi/scsi_all.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl_scsi_all.h>
#include <cam/ctl/ctl_util.h>
#include <cam/ctl/ctl_backend.h>
#include <cam/ctl/ctl_ioctl.h>

/*
 * The default amount of space we allocate for stats storage space.
 * We dynamically allocate more if needed.
 */
#define	CTL_STAT_NUM_ITEMS	256

static int ctl_stat_bits;

static const char *ctlstat_opts = "Cc:Ddhjl:n:p:tw:";
static const char *ctlstat_usage = "Usage:  ctlstat [-CDdjht] [-l lunnum]"
				   "[-c count] [-n numdevs] [-w wait]\n";

struct ctl_cpu_stats {
	uint64_t user;
	uint64_t nice;
	uint64_t system;
	uint64_t intr;
	uint64_t idle;
};

typedef enum {
	CTLSTAT_MODE_STANDARD,
	CTLSTAT_MODE_DUMP,
	CTLSTAT_MODE_JSON,
} ctlstat_mode_types;

#define	CTLSTAT_FLAG_CPU		(1 << 0)
#define	CTLSTAT_FLAG_HEADER		(1 << 1)
#define	CTLSTAT_FLAG_FIRST_RUN		(1 << 2)
#define	CTLSTAT_FLAG_TOTALS		(1 << 3)
#define	CTLSTAT_FLAG_DMA_TIME		(1 << 4)
#define	CTLSTAT_FLAG_TIME_VALID		(1 << 5)
#define	CTLSTAT_FLAG_MASK		(1 << 6)
#define	CTLSTAT_FLAG_LUNS		(1 << 7)
#define	CTLSTAT_FLAG_PORTS		(1 << 8)
#define	F_CPU(ctx) ((ctx)->flags & CTLSTAT_FLAG_CPU)
#define	F_HDR(ctx) ((ctx)->flags & CTLSTAT_FLAG_HEADER)
#define	F_FIRST(ctx) ((ctx)->flags & CTLSTAT_FLAG_FIRST_RUN)
#define	F_TOTALS(ctx) ((ctx)->flags & CTLSTAT_FLAG_TOTALS)
#define	F_DMA(ctx) ((ctx)->flags & CTLSTAT_FLAG_DMA_TIME)
#define	F_TIMEVAL(ctx) ((ctx)->flags & CTLSTAT_FLAG_TIME_VALID)
#define	F_MASK(ctx) ((ctx)->flags & CTLSTAT_FLAG_MASK)
#define	F_LUNS(ctx) ((ctx)->flags & CTLSTAT_FLAG_LUNS)
#define	F_PORTS(ctx) ((ctx)->flags & CTLSTAT_FLAG_PORTS)

struct ctlstat_context {
	ctlstat_mode_types mode;
	int flags;
	struct ctl_io_stats *cur_stats, *prev_stats;
	struct ctl_io_stats cur_total_stats[3], prev_total_stats[3];
	struct timespec cur_time, prev_time;
	struct ctl_cpu_stats cur_cpu, prev_cpu;
	uint64_t cur_total_jiffies, prev_total_jiffies;
	uint64_t cur_idle, prev_idle;
	bitstr_t *item_mask;
	int cur_items, prev_items;
	int cur_alloc, prev_alloc;
	int numdevs;
	int header_interval;
};

#ifndef min
#define	min(x,y)	(((x) < (y)) ? (x) : (y))
#endif

static void usage(int error);
static int getstats(int fd, int *alloc_items, int *num_items,
    struct ctl_io_stats **xstats, struct timespec *cur_time, int *time_valid);
static int getcpu(struct ctl_cpu_stats *cpu_stats);
static void compute_stats(struct ctl_io_stats *cur_stats,
			  struct ctl_io_stats *prev_stats,
			  long double etime, long double *mbsec,
			  long double *kb_per_transfer,
			  long double *transfers_per_second,
			  long double *ms_per_transfer,
			  long double *ms_per_dma,
			  long double *dmas_per_second);

static void
usage(int error)
{
	fputs(ctlstat_usage, error ? stderr : stdout);
}

static int
getstats(int fd, int *alloc_items, int *num_items, struct ctl_io_stats **stats,
	 struct timespec *cur_time, int *flags)
{
	struct ctl_get_io_stats get_stats;
	int more_space_count = 0;

	if (*alloc_items == 0)
		*alloc_items = CTL_STAT_NUM_ITEMS;
retry:
	if (*stats == NULL)
		*stats = malloc(sizeof(**stats) * *alloc_items);

	memset(&get_stats, 0, sizeof(get_stats));
	get_stats.alloc_len = *alloc_items * sizeof(**stats);
	memset(*stats, 0, get_stats.alloc_len);
	get_stats.stats = *stats;

	if (ioctl(fd, (*flags & CTLSTAT_FLAG_PORTS) ? CTL_GET_PORT_STATS :
	    CTL_GET_LUN_STATS, &get_stats) == -1)
		err(1, "CTL_GET_*_STATS ioctl returned error");

	switch (get_stats.status) {
	case CTL_SS_OK:
		break;
	case CTL_SS_ERROR:
		err(1, "CTL_GET_*_STATS ioctl returned CTL_SS_ERROR");
		break;
	case CTL_SS_NEED_MORE_SPACE:
		if (more_space_count >= 2)
			errx(1, "CTL_GET_*_STATS returned NEED_MORE_SPACE again");
		*alloc_items = get_stats.num_items * 5 / 4;
		free(*stats);
		*stats = NULL;
		more_space_count++;
		goto retry;
		break; /* NOTREACHED */
	default:
		errx(1, "CTL_GET_*_STATS ioctl returned unknown status %d",
		     get_stats.status);
		break;
	}

	*num_items = get_stats.fill_len / sizeof(**stats);
	cur_time->tv_sec = get_stats.timestamp.tv_sec;
	cur_time->tv_nsec = get_stats.timestamp.tv_nsec;
	if (get_stats.flags & CTL_STATS_FLAG_TIME_VALID)
		*flags |= CTLSTAT_FLAG_TIME_VALID;
	else
		*flags &= ~CTLSTAT_FLAG_TIME_VALID;

	return (0);
}

static int
getcpu(struct ctl_cpu_stats *cpu_stats)
{
	long cp_time[CPUSTATES];
	size_t cplen;

	cplen = sizeof(cp_time);

	if (sysctlbyname("kern.cp_time", &cp_time, &cplen, NULL, 0) == -1) {
		warn("sysctlbyname(kern.cp_time...) failed");
		return (1);
	}

	cpu_stats->user = cp_time[CP_USER];
	cpu_stats->nice = cp_time[CP_NICE];
	cpu_stats->system = cp_time[CP_SYS];
	cpu_stats->intr = cp_time[CP_INTR];
	cpu_stats->idle = cp_time[CP_IDLE];

	return (0);
}

static void
compute_stats(struct ctl_io_stats *cur_stats,
	      struct ctl_io_stats *prev_stats, long double etime,
	      long double *mbsec, long double *kb_per_transfer,
	      long double *transfers_per_second, long double *ms_per_transfer,
	      long double *ms_per_dma, long double *dmas_per_second)
{
	uint64_t total_bytes = 0, total_operations = 0, total_dmas = 0;
	struct bintime total_time_bt, total_dma_bt;
	struct timespec total_time_ts, total_dma_ts;
	int i;

	bzero(&total_time_bt, sizeof(total_time_bt));
	bzero(&total_dma_bt, sizeof(total_dma_bt));
	bzero(&total_time_ts, sizeof(total_time_ts));
	bzero(&total_dma_ts, sizeof(total_dma_ts));
	for (i = 0; i < CTL_STATS_NUM_TYPES; i++) {
		total_bytes += cur_stats->bytes[i];
		total_operations += cur_stats->operations[i];
		total_dmas += cur_stats->dmas[i];
		bintime_add(&total_time_bt, &cur_stats->time[i]);
		bintime_add(&total_dma_bt, &cur_stats->dma_time[i]);
		if (prev_stats != NULL) {
			total_bytes -= prev_stats->bytes[i];
			total_operations -= prev_stats->operations[i];
			total_dmas -= prev_stats->dmas[i];
			bintime_sub(&total_time_bt, &prev_stats->time[i]);
			bintime_sub(&total_dma_bt, &prev_stats->dma_time[i]);
		}
	}

	*mbsec = total_bytes;
	*mbsec /= 1024 * 1024;
	if (etime > 0.0)
		*mbsec /= etime;
	else
		*mbsec = 0;
	*kb_per_transfer = total_bytes;
	*kb_per_transfer /= 1024;
	if (total_operations > 0)
		*kb_per_transfer /= total_operations;
	else
		*kb_per_transfer = 0;
	*transfers_per_second = total_operations;
	*dmas_per_second = total_dmas;
	if (etime > 0.0) {
		*transfers_per_second /= etime;
		*dmas_per_second /= etime;
	} else {
		*transfers_per_second = 0;
		*dmas_per_second = 0;
	}

	bintime2timespec(&total_time_bt, &total_time_ts);
	bintime2timespec(&total_dma_bt, &total_dma_ts);
	if (total_operations > 0) {
		/*
		 * Convert the timespec to milliseconds.
		 */
		*ms_per_transfer = total_time_ts.tv_sec * 1000;
		*ms_per_transfer += total_time_ts.tv_nsec / 1000000;
		*ms_per_transfer /= total_operations;
	} else
		*ms_per_transfer = 0;

	if (total_dmas > 0) {
		/*
		 * Convert the timespec to milliseconds.
		 */
		*ms_per_dma = total_dma_ts.tv_sec * 1000;
		*ms_per_dma += total_dma_ts.tv_nsec / 1000000;
		*ms_per_dma /= total_dmas;
	} else
		*ms_per_dma = 0;
}

/* The dump_stats() and json_stats() functions perform essentially the same
 * purpose, but dump the statistics in different formats.  JSON is more
 * conducive to programming, however.
 */

#define	PRINT_BINTIME(bt) \
	printf("%jd.%06ju", (intmax_t)(bt).sec, \
	       (uintmax_t)(((bt).frac >> 32) * 1000000 >> 32))
static const char *iotypes[] = {"NO IO", "READ", "WRITE"};

static void
ctlstat_dump(struct ctlstat_context *ctx)
{
	int iotype, i, n;
	struct ctl_io_stats *stats = ctx->cur_stats;

	for (i = n = 0; i < ctx->cur_items;i++) {
		if (F_MASK(ctx) && bit_test(ctx->item_mask,
		    (int)stats[i].item) == 0)
			continue;
		printf("%s %d\n", F_PORTS(ctx) ? "port" : "lun", stats[i].item);
		for (iotype = 0; iotype < CTL_STATS_NUM_TYPES; iotype++) {
			printf("  io type %d (%s)\n", iotype, iotypes[iotype]);
			printf("   bytes %ju\n", (uintmax_t)
			    stats[i].bytes[iotype]);
			printf("   operations %ju\n", (uintmax_t)
			    stats[i].operations[iotype]);
			printf("   dmas %ju\n", (uintmax_t)
			    stats[i].dmas[iotype]);
			printf("   io time ");
			PRINT_BINTIME(stats[i].time[iotype]);
			printf("\n   dma time ");
			PRINT_BINTIME(stats[i].dma_time[iotype]);
			printf("\n");
		}
		if (++n >= ctx->numdevs)
			break;
	}
}

static void
ctlstat_json(struct ctlstat_context *ctx) {
	int iotype, i, n;
	struct ctl_io_stats *stats = ctx->cur_stats;

	printf("{\"%s\":[", F_PORTS(ctx) ? "ports" : "luns");
	for (i = n = 0; i < ctx->cur_items; i++) {
		if (F_MASK(ctx) && bit_test(ctx->item_mask,
		    (int)stats[i].item) == 0)
			continue;
		printf("{\"num\":%d,\"io\":[",
		    stats[i].item);
		for (iotype = 0; iotype < CTL_STATS_NUM_TYPES; iotype++) {
			printf("{\"type\":\"%s\",", iotypes[iotype]);
			printf("\"bytes\":%ju,", (uintmax_t)
			    stats[i].bytes[iotype]);
			printf("\"operations\":%ju,", (uintmax_t)
			    stats[i].operations[iotype]);
			printf("\"dmas\":%ju,", (uintmax_t)
			    stats[i].dmas[iotype]);
			printf("\"io time\":");
			PRINT_BINTIME(stats[i].time[iotype]);
			printf(",\"dma time\":");
			PRINT_BINTIME(stats[i].dma_time[iotype]);
			printf("}");
			if (iotype < (CTL_STATS_NUM_TYPES - 1))
				printf(","); /* continue io array */
		}
		printf("]}");
		if (++n >= ctx->numdevs)
			break;
		if (i < (ctx->cur_items - 1))
			printf(","); /* continue lun array */
	}
	printf("]}");
}

static void
ctlstat_standard(struct ctlstat_context *ctx) {
	long double etime;
	uint64_t delta_jiffies, delta_idle;
	long double cpu_percentage;
	int i, j, n;

	cpu_percentage = 0;

	if (F_CPU(ctx) && (getcpu(&ctx->cur_cpu) != 0))
		errx(1, "error returned from getcpu()");

	etime = ctx->cur_time.tv_sec - ctx->prev_time.tv_sec +
	    (ctx->prev_time.tv_nsec - ctx->cur_time.tv_nsec) * 1e-9;

	if (F_CPU(ctx)) {
		ctx->prev_total_jiffies = ctx->cur_total_jiffies;
		ctx->cur_total_jiffies = ctx->cur_cpu.user +
		    ctx->cur_cpu.nice + ctx->cur_cpu.system +
		    ctx->cur_cpu.intr + ctx->cur_cpu.idle;
		delta_jiffies = ctx->cur_total_jiffies;
		if (F_FIRST(ctx) == 0)
			delta_jiffies -= ctx->prev_total_jiffies;
		ctx->prev_idle = ctx->cur_idle;
		ctx->cur_idle = ctx->cur_cpu.idle;
		delta_idle = ctx->cur_idle - ctx->prev_idle;

		cpu_percentage = delta_jiffies - delta_idle;
		cpu_percentage /= delta_jiffies;
		cpu_percentage *= 100;
	}

	if (F_HDR(ctx)) {
		ctx->header_interval--;
		if (ctx->header_interval <= 0) {
			if (F_CPU(ctx))
				fprintf(stdout, " CPU");
			if (F_TOTALS(ctx)) {
				fprintf(stdout, "%s     Read       %s"
					"    Write       %s    Total\n",
					(F_TIMEVAL(ctx) != 0) ? "      " : "",
					(F_TIMEVAL(ctx) != 0) ? "      " : "",
					(F_TIMEVAL(ctx) != 0) ? "      " : "");
				n = 3;
			} else {
				for (i = n = 0; i < min(ctl_stat_bits,
				     ctx->cur_items); i++) {
					int item;

					/*
					 * Obviously this won't work with
					 * LUN numbers greater than a signed
					 * integer.
					 */
					item = (int)ctx->cur_stats[i].item;

					if (F_MASK(ctx) &&
					    bit_test(ctx->item_mask, item) == 0)
						continue;
					fprintf(stdout, "%15.6s%d %s",
					    F_PORTS(ctx) ? "port" : "lun", item,
					    (F_TIMEVAL(ctx) != 0) ? "     " : "");
					if (++n >= ctx->numdevs)
						break;
				}
				fprintf(stdout, "\n");
			}
			if (F_CPU(ctx))
				fprintf(stdout, "    ");
			for (i = 0; i < n; i++)
				fprintf(stdout, "%s KB/t   %s MB/s",
					(F_TIMEVAL(ctx) != 0) ? "    ms" : "",
					(F_DMA(ctx) == 0) ? "tps" : "dps");
			fprintf(stdout, "\n");
			ctx->header_interval = 20;
		}
	}

	if (F_CPU(ctx))
		fprintf(stdout, "%3.0Lf%%", cpu_percentage);
	if (F_TOTALS(ctx) != 0) {
		long double mbsec[3];
		long double kb_per_transfer[3];
		long double transfers_per_sec[3];
		long double ms_per_transfer[3];
		long double ms_per_dma[3];
		long double dmas_per_sec[3];

		for (i = 0; i < 3; i++) 
			ctx->prev_total_stats[i] = ctx->cur_total_stats[i];

		memset(&ctx->cur_total_stats, 0, sizeof(ctx->cur_total_stats));

		/* Use macros to make the next loop more readable. */
#define	ADD_STATS_BYTES(st, i, j) \
	ctx->cur_total_stats[st].bytes[j] += \
	    ctx->cur_stats[i].bytes[j]
#define	ADD_STATS_OPERATIONS(st, i, j) \
	ctx->cur_total_stats[st].operations[j] += \
	    ctx->cur_stats[i].operations[j]
#define	ADD_STATS_DMAS(st, i, j) \
	ctx->cur_total_stats[st].dmas[j] += \
	    ctx->cur_stats[i].dmas[j]
#define	ADD_STATS_TIME(st, i, j) \
	bintime_add(&ctx->cur_total_stats[st].time[j], \
	    &ctx->cur_stats[i].time[j])
#define	ADD_STATS_DMA_TIME(st, i, j) \
	bintime_add(&ctx->cur_total_stats[st].dma_time[j], \
	    &ctx->cur_stats[i].dma_time[j])

		for (i = 0; i < ctx->cur_items; i++) {
			if (F_MASK(ctx) && bit_test(ctx->item_mask,
			    (int)ctx->cur_stats[i].item) == 0)
				continue;
			for (j = 0; j < CTL_STATS_NUM_TYPES; j++) {
				ADD_STATS_BYTES(2, i, j);
				ADD_STATS_OPERATIONS(2, i, j);
				ADD_STATS_DMAS(2, i, j);
				ADD_STATS_TIME(2, i, j);
				ADD_STATS_DMA_TIME(2, i, j);
			}
			ADD_STATS_BYTES(0, i, CTL_STATS_READ);
			ADD_STATS_OPERATIONS(0, i, CTL_STATS_READ);
			ADD_STATS_DMAS(0, i, CTL_STATS_READ);
			ADD_STATS_TIME(0, i, CTL_STATS_READ);
			ADD_STATS_DMA_TIME(0, i, CTL_STATS_READ);

			ADD_STATS_BYTES(1, i, CTL_STATS_WRITE);
			ADD_STATS_OPERATIONS(1, i, CTL_STATS_WRITE);
			ADD_STATS_DMAS(1, i, CTL_STATS_WRITE);
			ADD_STATS_TIME(1, i, CTL_STATS_WRITE);
			ADD_STATS_DMA_TIME(1, i, CTL_STATS_WRITE);
		}

		for (i = 0; i < 3; i++) {
			compute_stats(&ctx->cur_total_stats[i],
				F_FIRST(ctx) ? NULL : &ctx->prev_total_stats[i],
				etime, &mbsec[i], &kb_per_transfer[i],
				&transfers_per_sec[i],
				&ms_per_transfer[i], &ms_per_dma[i],
				&dmas_per_sec[i]);
			if (F_DMA(ctx) != 0)
				fprintf(stdout, " %5.1Lf",
					ms_per_dma[i]);
			else if (F_TIMEVAL(ctx) != 0)
				fprintf(stdout, " %5.1Lf",
					ms_per_transfer[i]);
			fprintf(stdout, " %4.0Lf %5.0Lf %4.0Lf",
				kb_per_transfer[i],
				(F_DMA(ctx) == 0) ? transfers_per_sec[i] :
				dmas_per_sec[i], mbsec[i]);
		}
	} else {
		for (i = n = 0; i < min(ctl_stat_bits, ctx->cur_items); i++) {
			long double mbsec, kb_per_transfer;
			long double transfers_per_sec;
			long double ms_per_transfer;
			long double ms_per_dma;
			long double dmas_per_sec;

			if (F_MASK(ctx) && bit_test(ctx->item_mask,
			    (int)ctx->cur_stats[i].item) == 0)
				continue;
			for (j = 0; j < ctx->prev_items; j++) {
				if (ctx->prev_stats[j].item ==
				    ctx->cur_stats[i].item)
					break;
			}
			if (j >= ctx->prev_items)
				j = -1;
			compute_stats(&ctx->cur_stats[i],
			    j >= 0 ? &ctx->prev_stats[j] : NULL,
			    etime, &mbsec, &kb_per_transfer,
			    &transfers_per_sec, &ms_per_transfer,
			    &ms_per_dma, &dmas_per_sec);
			if (F_DMA(ctx))
				fprintf(stdout, " %5.1Lf",
					ms_per_dma);
			else if (F_TIMEVAL(ctx) != 0)
				fprintf(stdout, " %5.1Lf",
					ms_per_transfer);
			fprintf(stdout, " %4.0Lf %5.0Lf %4.0Lf",
				kb_per_transfer, (F_DMA(ctx) == 0) ?
				transfers_per_sec : dmas_per_sec, mbsec);
			if (++n >= ctx->numdevs)
				break;
		}
	}
}

int
main(int argc, char **argv)
{
	int c;
	int count, waittime;
	int fd, retval;
	size_t size;
	struct ctlstat_context ctx;
	struct ctl_io_stats *tmp_stats;

	/* default values */
	retval = 0;
	waittime = 1;
	count = -1;
	memset(&ctx, 0, sizeof(ctx));
	ctx.numdevs = 3;
	ctx.mode = CTLSTAT_MODE_STANDARD;
	ctx.flags |= CTLSTAT_FLAG_CPU;
	ctx.flags |= CTLSTAT_FLAG_FIRST_RUN;
	ctx.flags |= CTLSTAT_FLAG_HEADER;

	size = sizeof(ctl_stat_bits);
	if (sysctlbyname("kern.cam.ctl.max_luns", &ctl_stat_bits, &size, NULL,
	    0) == -1) {
		/* Backward compatibility for where the sysctl wasn't exposed */
		ctl_stat_bits = 1024;
	}
	ctx.item_mask = bit_alloc(ctl_stat_bits);
	if (ctx.item_mask == NULL)
		err(1, "bit_alloc() failed");

	while ((c = getopt(argc, argv, ctlstat_opts)) != -1) {
		switch (c) {
		case 'C':
			ctx.flags &= ~CTLSTAT_FLAG_CPU;
			break;
		case 'c':
			count = atoi(optarg);
			break;
		case 'd':
			ctx.flags |= CTLSTAT_FLAG_DMA_TIME;
			break;
		case 'D':
			ctx.mode = CTLSTAT_MODE_DUMP;
			waittime = 30;
			break;
		case 'h':
			ctx.flags &= ~CTLSTAT_FLAG_HEADER;
			break;
		case 'j':
			ctx.mode = CTLSTAT_MODE_JSON;
			waittime = 30;
			break;
		case 'l': {
			int cur_lun;

			cur_lun = atoi(optarg);
			if (cur_lun > ctl_stat_bits)
				errx(1, "Invalid LUN number %d", cur_lun);

			if (!F_MASK(&ctx))
				ctx.numdevs = 1;
			else
				ctx.numdevs++;
			bit_set(ctx.item_mask, cur_lun);
			ctx.flags |= CTLSTAT_FLAG_MASK;
			ctx.flags |= CTLSTAT_FLAG_LUNS;
			break;
		}
		case 'n':
			ctx.numdevs = atoi(optarg);
			break;
		case 'p': {
			int cur_port;

			cur_port = atoi(optarg);
			if (cur_port > ctl_stat_bits)
				errx(1, "Invalid port number %d", cur_port);

			if (!F_MASK(&ctx))
				ctx.numdevs = 1;
			else
				ctx.numdevs++;
			bit_set(ctx.item_mask, cur_port);
			ctx.flags |= CTLSTAT_FLAG_MASK;
			ctx.flags |= CTLSTAT_FLAG_PORTS;
			break;
		}
		case 't':
			ctx.flags |= CTLSTAT_FLAG_TOTALS;
			break;
		case 'w':
			waittime = atoi(optarg);
			break;
		default:
			retval = 1;
			usage(retval);
			exit(retval);
			break;
		}
	}

	if (F_LUNS(&ctx) && F_PORTS(&ctx))
		errx(1, "Options -p and -l are exclusive.");

	if (!F_LUNS(&ctx) && !F_PORTS(&ctx)) {
		if (F_TOTALS(&ctx))
			ctx.flags |= CTLSTAT_FLAG_PORTS;
		else
			ctx.flags |= CTLSTAT_FLAG_LUNS;
	}

	if ((fd = open(CTL_DEFAULT_DEV, O_RDWR)) == -1)
		err(1, "cannot open %s", CTL_DEFAULT_DEV);

	for (;count != 0;) {
		tmp_stats = ctx.prev_stats;
		ctx.prev_stats = ctx.cur_stats;
		ctx.cur_stats = tmp_stats;
		c = ctx.prev_alloc;
		ctx.prev_alloc = ctx.cur_alloc;
		ctx.cur_alloc = c;
		c = ctx.prev_items;
		ctx.prev_items = ctx.cur_items;
		ctx.cur_items = c;
		ctx.prev_time = ctx.cur_time;
		ctx.prev_cpu = ctx.cur_cpu;
		if (getstats(fd, &ctx.cur_alloc, &ctx.cur_items,
		    &ctx.cur_stats, &ctx.cur_time, &ctx.flags) != 0)
			errx(1, "error returned from getstats()");

		switch(ctx.mode) {
		case CTLSTAT_MODE_STANDARD:
			ctlstat_standard(&ctx);
			break;
		case CTLSTAT_MODE_DUMP:
			ctlstat_dump(&ctx);
			break;
		case CTLSTAT_MODE_JSON:
			ctlstat_json(&ctx);
			break;
		default:
			break;
		}

		fprintf(stdout, "\n");
		fflush(stdout);
		ctx.flags &= ~CTLSTAT_FLAG_FIRST_RUN;
		if (count != 1)
			sleep(waittime);
		if (count > 0)
			count--;
	}

	exit (retval);
}

/*
 * vim: ts=8
 */
