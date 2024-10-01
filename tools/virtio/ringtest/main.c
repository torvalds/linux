// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Red Hat, Inc.
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * Command line processing and common functions for ring benchmarking.
 */
#define _GNU_SOURCE
#include <getopt.h>
#include <pthread.h>
#include <assert.h>
#include <sched.h>
#include "main.h"
#include <sys/eventfd.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

int runcycles = 10000000;
int max_outstanding = INT_MAX;
int batch = 1;
int param = 0;

bool do_sleep = false;
bool do_relax = false;
bool do_exit = true;

unsigned ring_size = 256;

static int kickfd = -1;
static int callfd = -1;

void notify(int fd)
{
	unsigned long long v = 1;
	int r;

	vmexit();
	r = write(fd, &v, sizeof v);
	assert(r == sizeof v);
	vmentry();
}

void wait_for_notify(int fd)
{
	unsigned long long v = 1;
	int r;

	vmexit();
	r = read(fd, &v, sizeof v);
	assert(r == sizeof v);
	vmentry();
}

void kick(void)
{
	notify(kickfd);
}

void wait_for_kick(void)
{
	wait_for_notify(kickfd);
}

void call(void)
{
	notify(callfd);
}

void wait_for_call(void)
{
	wait_for_notify(callfd);
}

void set_affinity(const char *arg)
{
	cpu_set_t cpuset;
	int ret;
	pthread_t self;
	long int cpu;
	char *endptr;

	if (!arg)
		return;

	cpu = strtol(arg, &endptr, 0);
	assert(!*endptr);

	assert(cpu >= 0 && cpu < CPU_SETSIZE);

	self = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	ret = pthread_setaffinity_np(self, sizeof(cpu_set_t), &cpuset);
	assert(!ret);
}

void poll_used(void)
{
	while (used_empty())
		busy_wait();
}

static void __attribute__((__flatten__)) run_guest(void)
{
	int completed_before;
	int completed = 0;
	int started = 0;
	int bufs = runcycles;
	int spurious = 0;
	int r;
	unsigned len;
	void *buf;
	int tokick = batch;

	for (;;) {
		if (do_sleep)
			disable_call();
		completed_before = completed;
		do {
			if (started < bufs &&
			    started - completed < max_outstanding) {
				r = add_inbuf(0, "Buffer\n", "Hello, world!");
				if (__builtin_expect(r == 0, true)) {
					++started;
					if (!--tokick) {
						tokick = batch;
						if (do_sleep)
							kick_available();
					}

				}
			} else
				r = -1;

			/* Flush out completed bufs if any */
			if (get_buf(&len, &buf)) {
				++completed;
				if (__builtin_expect(completed == bufs, false))
					return;
				r = 0;
			}
		} while (r == 0);
		if (completed == completed_before)
			++spurious;
		assert(completed <= bufs);
		assert(started <= bufs);
		if (do_sleep) {
			if (used_empty() && enable_call())
				wait_for_call();
		} else {
			poll_used();
		}
	}
}

void poll_avail(void)
{
	while (avail_empty())
		busy_wait();
}

static void __attribute__((__flatten__)) run_host(void)
{
	int completed_before;
	int completed = 0;
	int spurious = 0;
	int bufs = runcycles;
	unsigned len;
	void *buf;

	for (;;) {
		if (do_sleep) {
			if (avail_empty() && enable_kick())
				wait_for_kick();
		} else {
			poll_avail();
		}
		if (do_sleep)
			disable_kick();
		completed_before = completed;
		while (__builtin_expect(use_buf(&len, &buf), true)) {
			if (do_sleep)
				call_used();
			++completed;
			if (__builtin_expect(completed == bufs, false))
				return;
		}
		if (completed == completed_before)
			++spurious;
		assert(completed <= bufs);
		if (completed == bufs)
			break;
	}
}

void *start_guest(void *arg)
{
	set_affinity(arg);
	run_guest();
	pthread_exit(NULL);
}

void *start_host(void *arg)
{
	set_affinity(arg);
	run_host();
	pthread_exit(NULL);
}

static const char optstring[] = "";
static const struct option longopts[] = {
	{
		.name = "help",
		.has_arg = no_argument,
		.val = 'h',
	},
	{
		.name = "host-affinity",
		.has_arg = required_argument,
		.val = 'H',
	},
	{
		.name = "guest-affinity",
		.has_arg = required_argument,
		.val = 'G',
	},
	{
		.name = "ring-size",
		.has_arg = required_argument,
		.val = 'R',
	},
	{
		.name = "run-cycles",
		.has_arg = required_argument,
		.val = 'C',
	},
	{
		.name = "outstanding",
		.has_arg = required_argument,
		.val = 'o',
	},
	{
		.name = "batch",
		.has_arg = required_argument,
		.val = 'b',
	},
	{
		.name = "param",
		.has_arg = required_argument,
		.val = 'p',
	},
	{
		.name = "sleep",
		.has_arg = no_argument,
		.val = 's',
	},
	{
		.name = "relax",
		.has_arg = no_argument,
		.val = 'x',
	},
	{
		.name = "exit",
		.has_arg = no_argument,
		.val = 'e',
	},
	{
	}
};

static void help(void)
{
	fprintf(stderr, "Usage: <test> [--help]"
		" [--host-affinity H]"
		" [--guest-affinity G]"
		" [--ring-size R (default: %d)]"
		" [--run-cycles C (default: %d)]"
		" [--batch b]"
		" [--outstanding o]"
		" [--param p]"
		" [--sleep]"
		" [--relax]"
		" [--exit]"
		"\n",
		ring_size,
		runcycles);
}

int main(int argc, char **argv)
{
	int ret;
	pthread_t host, guest;
	void *tret;
	char *host_arg = NULL;
	char *guest_arg = NULL;
	char *endptr;
	long int c;

	kickfd = eventfd(0, 0);
	assert(kickfd >= 0);
	callfd = eventfd(0, 0);
	assert(callfd >= 0);

	for (;;) {
		int o = getopt_long(argc, argv, optstring, longopts, NULL);
		switch (o) {
		case -1:
			goto done;
		case '?':
			help();
			exit(2);
		case 'H':
			host_arg = optarg;
			break;
		case 'G':
			guest_arg = optarg;
			break;
		case 'R':
			ring_size = strtol(optarg, &endptr, 0);
			assert(ring_size && !(ring_size & (ring_size - 1)));
			assert(!*endptr);
			break;
		case 'C':
			c = strtol(optarg, &endptr, 0);
			assert(!*endptr);
			assert(c > 0 && c < INT_MAX);
			runcycles = c;
			break;
		case 'o':
			c = strtol(optarg, &endptr, 0);
			assert(!*endptr);
			assert(c > 0 && c < INT_MAX);
			max_outstanding = c;
			break;
		case 'p':
			c = strtol(optarg, &endptr, 0);
			assert(!*endptr);
			assert(c > 0 && c < INT_MAX);
			param = c;
			break;
		case 'b':
			c = strtol(optarg, &endptr, 0);
			assert(!*endptr);
			assert(c > 0 && c < INT_MAX);
			batch = c;
			break;
		case 's':
			do_sleep = true;
			break;
		case 'x':
			do_relax = true;
			break;
		case 'e':
			do_exit = true;
			break;
		default:
			help();
			exit(4);
			break;
		}
	}

	/* does nothing here, used to make sure all smp APIs compile */
	smp_acquire();
	smp_release();
	smp_mb();
done:

	if (batch > max_outstanding)
		batch = max_outstanding;

	if (optind < argc) {
		help();
		exit(4);
	}
	alloc_ring();

	ret = pthread_create(&host, NULL, start_host, host_arg);
	assert(!ret);
	ret = pthread_create(&guest, NULL, start_guest, guest_arg);
	assert(!ret);

	ret = pthread_join(guest, &tret);
	assert(!ret);
	ret = pthread_join(host, &tret);
	assert(!ret);
	return 0;
}
