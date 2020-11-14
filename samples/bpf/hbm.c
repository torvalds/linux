// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * Example program for Host Bandwidth Managment
 *
 * This program loads a cgroup skb BPF program to enforce cgroup output
 * (egress) or input (ingress) bandwidth limits.
 *
 * USAGE: hbm [-d] [-l] [-n <id>] [-r <rate>] [-s] [-t <secs>] [-w] [-h] [prog]
 *   Where:
 *    -d	Print BPF trace debug buffer
 *    -l	Also limit flows doing loopback
 *    -n <#>	To create cgroup \"/hbm#\" and attach prog
 *		Default is /hbm1
 *    --no_cn   Do not return cn notifications
 *    -r <rate>	Rate limit in Mbps
 *    -s	Get HBM stats (marked, dropped, etc.)
 *    -t <time>	Exit after specified seconds (default is 0)
 *    -w	Work conserving flag. cgroup can increase its bandwidth
 *		beyond the rate limit specified while there is available
 *		bandwidth. Current implementation assumes there is only
 *		NIC (eth0), but can be extended to support multiple NICs.
 *		Currrently only supported for egress.
 *    -h	Print this info
 *    prog	BPF program file name. Name defaults to hbm_out_kern.o
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/unistd.h>
#include <linux/compiler.h>

#include <linux/bpf.h>
#include <bpf/bpf.h>
#include <getopt.h>

#include "bpf_load.h"
#include "bpf_rlimit.h"
#include "cgroup_helpers.h"
#include "hbm.h"
#include "bpf_util.h"
#include <bpf/libbpf.h>

bool outFlag = true;
int minRate = 1000;		/* cgroup rate limit in Mbps */
int rate = 1000;		/* can grow if rate conserving is enabled */
int dur = 1;
bool stats_flag;
bool loopback_flag;
bool debugFlag;
bool work_conserving_flag;
bool no_cn_flag;
bool edt_flag;

static void Usage(void);
static void read_trace_pipe2(void);
static void do_error(char *msg, bool errno_flag);

#define DEBUGFS "/sys/kernel/debug/tracing/"

struct bpf_object *obj;
int bpfprog_fd;
int cgroup_storage_fd;

static void read_trace_pipe2(void)
{
	int trace_fd;
	FILE *outf;
	char *outFname = "hbm_out.log";

	trace_fd = open(DEBUGFS "trace_pipe", O_RDONLY, 0);
	if (trace_fd < 0) {
		printf("Error opening trace_pipe\n");
		return;
	}

//	Future support of ingress
//	if (!outFlag)
//		outFname = "hbm_in.log";
	outf = fopen(outFname, "w");

	if (outf == NULL)
		printf("Error creating %s\n", outFname);

	while (1) {
		static char buf[4097];
		ssize_t sz;

		sz = read(trace_fd, buf, sizeof(buf) - 1);
		if (sz > 0) {
			buf[sz] = 0;
			puts(buf);
			if (outf != NULL) {
				fprintf(outf, "%s\n", buf);
				fflush(outf);
			}
		}
	}
}

static void do_error(char *msg, bool errno_flag)
{
	if (errno_flag)
		printf("ERROR: %s, errno: %d\n", msg, errno);
	else
		printf("ERROR: %s\n", msg);
	exit(1);
}

static int prog_load(char *prog)
{
	struct bpf_prog_load_attr prog_load_attr = {
		.prog_type = BPF_PROG_TYPE_CGROUP_SKB,
		.file = prog,
		.expected_attach_type = BPF_CGROUP_INET_EGRESS,
	};
	int map_fd;
	struct bpf_map *map;

	int ret = 0;

	if (access(prog, O_RDONLY) < 0) {
		printf("Error accessing file %s: %s\n", prog, strerror(errno));
		return 1;
	}
	if (bpf_prog_load_xattr(&prog_load_attr, &obj, &bpfprog_fd))
		ret = 1;
	if (!ret) {
		map = bpf_object__find_map_by_name(obj, "queue_stats");
		map_fd = bpf_map__fd(map);
		if (map_fd < 0) {
			printf("Map not found: %s\n", strerror(map_fd));
			ret = 1;
		}
	}

	if (ret) {
		printf("ERROR: bpf_prog_load_xattr failed for: %s\n", prog);
		printf("  Output from verifier:\n%s\n------\n", bpf_log_buf);
		ret = -1;
	} else {
		ret = map_fd;
	}

	return ret;
}

static int run_bpf_prog(char *prog, int cg_id)
{
	int map_fd;
	int rc = 0;
	int key = 0;
	int cg1 = 0;
	int type = BPF_CGROUP_INET_EGRESS;
	char cg_dir[100];
	struct hbm_queue_stats qstats = {0};

	sprintf(cg_dir, "/hbm%d", cg_id);
	map_fd = prog_load(prog);
	if (map_fd  == -1)
		return 1;

	if (setup_cgroup_environment()) {
		printf("ERROR: setting cgroup environment\n");
		goto err;
	}
	cg1 = create_and_get_cgroup(cg_dir);
	if (!cg1) {
		printf("ERROR: create_and_get_cgroup\n");
		goto err;
	}
	if (join_cgroup(cg_dir)) {
		printf("ERROR: join_cgroup\n");
		goto err;
	}

	qstats.rate = rate;
	qstats.stats = stats_flag ? 1 : 0;
	qstats.loopback = loopback_flag ? 1 : 0;
	qstats.no_cn = no_cn_flag ? 1 : 0;
	if (bpf_map_update_elem(map_fd, &key, &qstats, BPF_ANY)) {
		printf("ERROR: Could not update map element\n");
		goto err;
	}

	if (!outFlag)
		type = BPF_CGROUP_INET_INGRESS;
	if (bpf_prog_attach(bpfprog_fd, cg1, type, 0)) {
		printf("ERROR: bpf_prog_attach fails!\n");
		log_err("Attaching prog");
		goto err;
	}

	if (work_conserving_flag) {
		struct timeval t0, t_last, t_new;
		FILE *fin;
		unsigned long long last_eth_tx_bytes, new_eth_tx_bytes;
		signed long long last_cg_tx_bytes, new_cg_tx_bytes;
		signed long long delta_time, delta_bytes, delta_rate;
		int delta_ms;
#define DELTA_RATE_CHECK 10000		/* in us */
#define RATE_THRESHOLD 9500000000	/* 9.5 Gbps */

		bpf_map_lookup_elem(map_fd, &key, &qstats);
		if (gettimeofday(&t0, NULL) < 0)
			do_error("gettimeofday failed", true);
		t_last = t0;
		fin = fopen("/sys/class/net/eth0/statistics/tx_bytes", "r");
		if (fscanf(fin, "%llu", &last_eth_tx_bytes) != 1)
			do_error("fscanf fails", false);
		fclose(fin);
		last_cg_tx_bytes = qstats.bytes_total;
		while (true) {
			usleep(DELTA_RATE_CHECK);
			if (gettimeofday(&t_new, NULL) < 0)
				do_error("gettimeofday failed", true);
			delta_ms = (t_new.tv_sec - t0.tv_sec) * 1000 +
				(t_new.tv_usec - t0.tv_usec)/1000;
			if (delta_ms > dur * 1000)
				break;
			delta_time = (t_new.tv_sec - t_last.tv_sec) * 1000000 +
				(t_new.tv_usec - t_last.tv_usec);
			if (delta_time == 0)
				continue;
			t_last = t_new;
			fin = fopen("/sys/class/net/eth0/statistics/tx_bytes",
				    "r");
			if (fscanf(fin, "%llu", &new_eth_tx_bytes) != 1)
				do_error("fscanf fails", false);
			fclose(fin);
			printf("  new_eth_tx_bytes:%llu\n",
			       new_eth_tx_bytes);
			bpf_map_lookup_elem(map_fd, &key, &qstats);
			new_cg_tx_bytes = qstats.bytes_total;
			delta_bytes = new_eth_tx_bytes - last_eth_tx_bytes;
			last_eth_tx_bytes = new_eth_tx_bytes;
			delta_rate = (delta_bytes * 8000000) / delta_time;
			printf("%5d - eth_rate:%.1fGbps cg_rate:%.3fGbps",
			       delta_ms, delta_rate/1000000000.0,
			       rate/1000.0);
			if (delta_rate < RATE_THRESHOLD) {
				/* can increase cgroup rate limit, but first
				 * check if we are using the current limit.
				 * Currently increasing by 6.25%, unknown
				 * if that is the optimal rate.
				 */
				int rate_diff100;

				delta_bytes = new_cg_tx_bytes -
					last_cg_tx_bytes;
				last_cg_tx_bytes = new_cg_tx_bytes;
				delta_rate = (delta_bytes * 8000000) /
					delta_time;
				printf(" rate:%.3fGbps",
				       delta_rate/1000000000.0);
				rate_diff100 = (((long long)rate)*1000000 -
						     delta_rate) * 100 /
					(((long long) rate) * 1000000);
				printf("  rdiff:%d", rate_diff100);
				if (rate_diff100  <= 3) {
					rate += (rate >> 4);
					if (rate > RATE_THRESHOLD / 1000000)
						rate = RATE_THRESHOLD / 1000000;
					qstats.rate = rate;
					printf(" INC\n");
				} else {
					printf("\n");
				}
			} else {
				/* Need to decrease cgroup rate limit.
				 * Currently decreasing by 12.5%, unknown
				 * if that is optimal
				 */
				printf(" DEC\n");
				rate -= (rate >> 3);
				if (rate < minRate)
					rate = minRate;
				qstats.rate = rate;
			}
			if (bpf_map_update_elem(map_fd, &key, &qstats, BPF_ANY))
				do_error("update map element fails", false);
		}
	} else {
		sleep(dur);
	}
	// Get stats!
	if (stats_flag && bpf_map_lookup_elem(map_fd, &key, &qstats)) {
		char fname[100];
		FILE *fout;

		if (!outFlag)
			sprintf(fname, "hbm.%d.in", cg_id);
		else
			sprintf(fname, "hbm.%d.out", cg_id);
		fout = fopen(fname, "w");
		fprintf(fout, "id:%d\n", cg_id);
		fprintf(fout, "ERROR: Could not lookup queue_stats\n");
	} else if (stats_flag && qstats.lastPacketTime >
		   qstats.firstPacketTime) {
		long long delta_us = (qstats.lastPacketTime -
				      qstats.firstPacketTime)/1000;
		unsigned int rate_mbps = ((qstats.bytes_total -
					   qstats.bytes_dropped) * 8 /
					  delta_us);
		double percent_pkts, percent_bytes;
		char fname[100];
		FILE *fout;
		int k;
		static const char *returnValNames[] = {
			"DROP_PKT",
			"ALLOW_PKT",
			"DROP_PKT_CWR",
			"ALLOW_PKT_CWR"
		};
#define RET_VAL_COUNT 4

// Future support of ingress
//		if (!outFlag)
//			sprintf(fname, "hbm.%d.in", cg_id);
//		else
		sprintf(fname, "hbm.%d.out", cg_id);
		fout = fopen(fname, "w");
		fprintf(fout, "id:%d\n", cg_id);
		fprintf(fout, "rate_mbps:%d\n", rate_mbps);
		fprintf(fout, "duration:%.1f secs\n",
			(qstats.lastPacketTime - qstats.firstPacketTime) /
			1000000000.0);
		fprintf(fout, "packets:%d\n", (int)qstats.pkts_total);
		fprintf(fout, "bytes_MB:%d\n", (int)(qstats.bytes_total /
						     1000000));
		fprintf(fout, "pkts_dropped:%d\n", (int)qstats.pkts_dropped);
		fprintf(fout, "bytes_dropped_MB:%d\n",
			(int)(qstats.bytes_dropped /
						       1000000));
		// Marked Pkts and Bytes
		percent_pkts = (qstats.pkts_marked * 100.0) /
			(qstats.pkts_total + 1);
		percent_bytes = (qstats.bytes_marked * 100.0) /
			(qstats.bytes_total + 1);
		fprintf(fout, "pkts_marked_percent:%6.2f\n", percent_pkts);
		fprintf(fout, "bytes_marked_percent:%6.2f\n", percent_bytes);

		// Dropped Pkts and Bytes
		percent_pkts = (qstats.pkts_dropped * 100.0) /
			(qstats.pkts_total + 1);
		percent_bytes = (qstats.bytes_dropped * 100.0) /
			(qstats.bytes_total + 1);
		fprintf(fout, "pkts_dropped_percent:%6.2f\n", percent_pkts);
		fprintf(fout, "bytes_dropped_percent:%6.2f\n", percent_bytes);

		// ECN CE markings
		percent_pkts = (qstats.pkts_ecn_ce * 100.0) /
			(qstats.pkts_total + 1);
		fprintf(fout, "pkts_ecn_ce:%6.2f (%d)\n", percent_pkts,
			(int)qstats.pkts_ecn_ce);

		// Average cwnd
		fprintf(fout, "avg cwnd:%d\n",
			(int)(qstats.sum_cwnd / (qstats.sum_cwnd_cnt + 1)));
		// Average rtt
		fprintf(fout, "avg rtt:%d\n",
			(int)(qstats.sum_rtt / (qstats.pkts_total + 1)));
		// Average credit
		if (edt_flag)
			fprintf(fout, "avg credit_ms:%.03f\n",
				(qstats.sum_credit /
				 (qstats.pkts_total + 1.0)) / 1000000.0);
		else
			fprintf(fout, "avg credit:%d\n",
				(int)(qstats.sum_credit /
				      (1500 * ((int)qstats.pkts_total ) + 1)));

		// Return values stats
		for (k = 0; k < RET_VAL_COUNT; k++) {
			percent_pkts = (qstats.returnValCount[k] * 100.0) /
				(qstats.pkts_total + 1);
			fprintf(fout, "%s:%6.2f (%d)\n", returnValNames[k],
				percent_pkts, (int)qstats.returnValCount[k]);
		}
		fclose(fout);
	}

	if (debugFlag)
		read_trace_pipe2();
	return rc;
err:
	rc = 1;

	if (cg1)
		close(cg1);
	cleanup_cgroup_environment();

	return rc;
}

static void Usage(void)
{
	printf("This program loads a cgroup skb BPF program to enforce\n"
	       "cgroup output (egress) bandwidth limits.\n\n"
	       "USAGE: hbm [-o] [-d]  [-l] [-n <id>] [--no_cn] [-r <rate>]\n"
	       "           [-s] [-t <secs>] [-w] [-h] [prog]\n"
	       "  Where:\n"
	       "    -o         indicates egress direction (default)\n"
	       "    -d         print BPF trace debug buffer\n"
	       "    --edt      use fq's Earliest Departure Time\n"
	       "    -l         also limit flows using loopback\n"
	       "    -n <#>     to create cgroup \"/hbm#\" and attach prog\n"
	       "               Default is /hbm1\n"
	       "    --no_cn    disable CN notifications\n"
	       "    -r <rate>  Rate in Mbps\n"
	       "    -s         Update HBM stats\n"
	       "    -t <time>  Exit after specified seconds (default is 0)\n"
	       "    -w	       Work conserving flag. cgroup can increase\n"
	       "               bandwidth beyond the rate limit specified\n"
	       "               while there is available bandwidth. Current\n"
	       "               implementation assumes there is only eth0\n"
	       "               but can be extended to support multiple NICs\n"
	       "    -h         print this info\n"
	       "    prog       BPF program file name. Name defaults to\n"
	       "                 hbm_out_kern.o\n");
}

int main(int argc, char **argv)
{
	char *prog = "hbm_out_kern.o";
	int  k;
	int cg_id = 1;
	char *optstring = "iodln:r:st:wh";
	struct option loptions[] = {
		{"no_cn", 0, NULL, 1},
		{"edt", 0, NULL, 2},
		{NULL, 0, NULL, 0}
	};

	while ((k = getopt_long(argc, argv, optstring, loptions, NULL)) != -1) {
		switch (k) {
		case 1:
			no_cn_flag = true;
			break;
		case 2:
			prog = "hbm_edt_kern.o";
			edt_flag = true;
			break;
		case'o':
			break;
		case 'd':
			debugFlag = true;
			break;
		case 'l':
			loopback_flag = true;
			break;
		case 'n':
			cg_id = atoi(optarg);
			break;
		case 'r':
			minRate = atoi(optarg) * 1.024;
			rate = minRate;
			break;
		case 's':
			stats_flag = true;
			break;
		case 't':
			dur = atoi(optarg);
			break;
		case 'w':
			work_conserving_flag = true;
			break;
		case '?':
			if (optopt == 'n' || optopt == 'r' || optopt == 't')
				fprintf(stderr,
					"Option -%c requires an argument.\n\n",
					optopt);
		case 'h':
			__fallthrough;
		default:
			Usage();
			return 0;
		}
	}

	if (optind < argc)
		prog = argv[optind];
	printf("HBM prog: %s\n", prog != NULL ? prog : "NULL");

	return run_bpf_prog(prog, cg_id);
}
