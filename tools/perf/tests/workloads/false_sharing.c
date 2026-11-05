// SPDX-License-Identifier: GPL-2.0
/*
 * False-sharing demo for data type profiling, shaped as a TCP
 * connection: a read-mostly identity shares a cacheline with per-packet
 * rx counters (the false-sharing line), a second line has packet-path
 * private tx and congestion control counters, and a third the connection
 * config.
 *
 * 'perf mem record' of this workload followed by 'perf report -s type'
 * (see tests/shell/data_type_profiling.sh) resolves the accesses to
 * struct net_conn members, showing the rx counters and the read-mostly
 * identity sharing cacheline 0.
 */
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <linux/compiler.h>
#include "../tests.h"

struct net_conn {
	/* cacheline 0: identity (read-mostly) + rx counters (per packet) */
	uint32_t	saddr;		/*   0 */
	uint32_t	daddr;		/*   4 */
	uint16_t	sport;		/*   8 */
	uint16_t	dport;		/*  10 */
	uint8_t		state;		/*  12: 1 == ESTABLISHED */
	uint8_t		protocol;	/*  13: 6 == TCP */
	uint16_t	__pad0;		/*  14 */
	uint64_t	bytes_rx;	/*  16: every packet */
	uint64_t	packets_rx;	/*  24: every packet */
	uint32_t	rx_queue;	/*  32: backlog depth, fluctuates */
	uint8_t		__pad1[24];	/*  36..59 */
	uint32_t	last_ack;	/*  60: written per ACK */
	/* cacheline 1: tx + congestion control (packet-path private) */
	uint64_t	bytes_tx;	/*  64: every packet */
	uint64_t	packets_tx;	/*  72: every packet */
	uint32_t	cwnd;		/*  80: on every ACK */
	uint32_t	ssthresh;	/*  84: on loss */
	uint32_t	rtt_us;		/*  88: on every ACK */
	uint32_t	retrans;	/*  92: on timeout */
	uint32_t	__pad2[8];	/*  96..127 */
	/* cacheline 2: config, set at setup, read by everybody */
	uint16_t	mss;		/* 128 */
	uint8_t		snd_wscale;	/* 130 */
	uint8_t		rcv_wscale;	/* 131 */
	uint32_t	keepalive_int;	/* 132 */
	uint32_t	mark;		/* 136: firewall mark */
	uint32_t	priority;	/* 140: traffic class */
	uint32_t	__pad3[12];	/* 144..191 */
} __attribute__((aligned(64)));

/* Volatile so every iteration really loads and stores. */
static volatile struct net_conn conn;
/* Keeps the reader checksums alive after the threads join. */
static volatile unsigned long fs_sink;

static volatile sig_atomic_t done;

/*
 * One cacheline each: sum before cpu, or the implicit padding after cpu
 * pushes the struct past 64 bytes, and aligning it to a cacheline then
 * rounds it up to 128.
 */
struct fs_reader {
	pthread_t	thread;
	unsigned long	sum;
	int		cpu;
	char		__pad[64 - sizeof(pthread_t) - sizeof(unsigned long) - sizeof(int)];
} __attribute__((aligned(64)));

static void sighandler(int sig __maybe_unused)
{
	done = 1;
}

static void pin_to_cpu(int cpu)
{
	cpu_set_t set;

	/* There may be no second CPU in a restricted cpuset. */
	if (cpu < 0)
		return;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	/* Best effort: in a restricted cpuset this fails and the thread runs unpinned. */
	pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

/*
 * Connection lookup, as a load balancer or 'ss' scrape would do it: the
 * reads go straight to the global, this file is built -O0 and a local
 * pointer would be reloaded from a stack slot, a form the data type
 * resolver does not track.
 */
static void *reader_fn(void *arg)
{
	struct fs_reader *r = arg;
	unsigned long sum = 0;

	pthread_setname_np(pthread_self(), "fs-reader");
	pin_to_cpu(r->cpu);

	while (!done) {
		sum += conn.saddr + conn.daddr + conn.sport + conn.dport +
		       conn.state + conn.protocol;
		sum += conn.mss + conn.snd_wscale + conn.rcv_wscale +
		       conn.keepalive_int + conn.mark + conn.priority;
	}
	r->sum = sum;
	return NULL;
}

static int false_sharing(int argc, const char **argv)
{
	double sec = 2.0;
	int nreaders = 0, nr_allowed = 0, err = 1;
	int *allowed = NULL, nallowed = 0;
	cpu_set_t set;
	int nr_mask_bits = sizeof(set) * 8 < CPU_SETSIZE ? sizeof(set) * 8 : CPU_SETSIZE;
	struct fs_reader *readers = NULL;
	int i, writer_cpu;
	unsigned long n = 0;

	pthread_setname_np(pthread_self(), "fs-writer");
	if (argc > 0)
		sec = atof(argv[0]);
	if (!(sec > 0.0)) {
		fprintf(stderr, "Error: seconds (%f) must be > 0\n", sec);
		return 1;
	}
	if (argc > 1)
		nreaders = atoi(argv[1]);

	/*
	 * A connection that just got established: identity and config fixed
	 * from here on, counters at zero.
	 */
	conn.saddr = 0x0a000001;	/* 10.0.0.1 */
	conn.daddr = 0x0a000002;	/* 10.0.0.2 */
	conn.sport = 54321;
	conn.dport = 443;
	conn.state = 1;			/* ESTABLISHED */
	conn.protocol = 6;		/* TCP */
	conn.mss = 1448;
	conn.snd_wscale = 7;
	conn.rcv_wscale = 7;
	conn.keepalive_int = 7200;
	conn.cwnd = 10;
	conn.ssthresh = 65535;
	conn.rtt_us = 50;

	/*
	 * Pin against the allowed set, restricted cpusets still spread the threads.
	 * The whole mask is looked at, not the CPU count: the count can be lower
	 * than the highest ID in it, as when a cpuset allows only high numbered
	 * CPUs, and then no allowed CPU would be found at all.
	 */
	if (sched_getaffinity(0, sizeof(set), &set) == 0) {
		for (i = 0; i < nr_mask_bits; i++) {
			if (!CPU_ISSET(i, &set))
				continue;
			nr_allowed++;
		}
		allowed = malloc(nr_allowed * sizeof(int));
		if (allowed == NULL) {
			fprintf(stderr, "Error: malloc failed for CPU list\n");
			return 1;
		}
		for (i = 0; i < nr_mask_bits; i++) {
			if (CPU_ISSET(i, &set))
				allowed[nallowed++] = i;
		}
	}
	if (nreaders <= 0) {
		/* By default leave one CPU for the packet path, up to 4 readers. */
		nreaders = nallowed > 1 ? nallowed - 1 : 1;
		if (nreaders > 4)
			nreaders = 4;
	}

	signal(SIGINT, sighandler);
	signal(SIGALRM, sighandler);

	readers = calloc(nreaders, sizeof(*readers));
	if (readers == NULL) {
		fprintf(stderr, "Error: calloc failed for %d readers\n", nreaders);
		goto out;
	}
	for (i = 0; i < nreaders; i++) {
		int cpu = nallowed > 1 ? allowed[(i + 1) % nallowed] : -1;

		readers[i].cpu = cpu;
		if (pthread_create(&readers[i].thread, NULL, reader_fn, &readers[i])) {
			fprintf(stderr, "Error: failed to create reader %d\n", i);
			done = 1; // Ensure started threads terminate.
			nreaders = i;
			goto out_join;
		}
	}
	writer_cpu = nallowed > 0 ? allowed[0] : -1;
	if (nallowed == 1)
		fprintf(stderr, "Warning: single CPU allowed, no cross-CPU traffic expected\n");
	if (writer_cpu >= 0)
		pin_to_cpu(writer_cpu);

	/*
	 * The packet path: receive, acknowledge, transmit, repeat; every 64th
	 * packet simulates a loss so ssthresh/retrans get sampled too.
	 */
	if (sec < 1.0) {
		useconds_t usecs = (useconds_t)(sec * 1000000.0);

		ualarm(usecs > 0 ? usecs : 1, 0);
	} else
		alarm((unsigned int)sec);
	while (!done) {
		conn.bytes_rx += conn.mss;
		conn.packets_rx++;
		conn.rx_queue = (uint32_t)(n & 0x3f);
		conn.last_ack = (uint32_t)n;
		conn.bytes_tx += conn.mss;
		conn.packets_tx++;
		conn.cwnd = 10 + (n & 15);
		conn.rtt_us = 50 + (n & 7);
		if ((n & 63) == 0) {
			conn.ssthresh = conn.cwnd / 2;
			conn.retrans++;
		}
		n++;
	}
	err = 0;
out_join:
	for (i = 0; i < nreaders; i++) {
		if (readers[i].thread) {
			pthread_join(readers[i].thread, /*retval=*/NULL);
			fs_sink += readers[i].sum;
		}
	}
	fs_sink += (unsigned long)(conn.bytes_rx + conn.bytes_tx + n);
	free(readers);
out:
	free(allowed);
	return err;
}

DEFINE_WORKLOAD(false_sharing);
