// SPDX-License-Identifier: GPL-2.0
/*
 * KVM userfaultfd util
 * Adapted from demand_paging_test.c
 *
 * Copyright (C) 2018, Red Hat, Inc.
 * Copyright (C) 2019-2022 Google LLC
 */

#define _GNU_SOURCE /* for pipe2 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include <linux/userfaultfd.h>
#include <sys/syscall.h>

#include "kvm_util.h"
#include "test_util.h"
#include "memstress.h"
#include "userfaultfd_util.h"

#ifdef __NR_userfaultfd

static void *uffd_handler_thread_fn(void *arg)
{
	struct uffd_desc *uffd_desc = (struct uffd_desc *)arg;
	int uffd = uffd_desc->uffd;
	int pipefd = uffd_desc->pipefds[0];
	useconds_t delay = uffd_desc->delay;
	int64_t pages = 0;
	struct timespec start;
	struct timespec ts_diff;

	clock_gettime(CLOCK_MONOTONIC, &start);
	while (1) {
		struct uffd_msg msg;
		struct pollfd pollfd[2];
		char tmp_chr;
		int r;

		pollfd[0].fd = uffd;
		pollfd[0].events = POLLIN;
		pollfd[1].fd = pipefd;
		pollfd[1].events = POLLIN;

		r = poll(pollfd, 2, -1);
		switch (r) {
		case -1:
			pr_info("poll err");
			continue;
		case 0:
			continue;
		case 1:
			break;
		default:
			pr_info("Polling uffd returned %d", r);
			return NULL;
		}

		if (pollfd[0].revents & POLLERR) {
			pr_info("uffd revents has POLLERR");
			return NULL;
		}

		if (pollfd[1].revents & POLLIN) {
			r = read(pollfd[1].fd, &tmp_chr, 1);
			TEST_ASSERT(r == 1,
				    "Error reading pipefd in UFFD thread");
			break;
		}

		if (!(pollfd[0].revents & POLLIN))
			continue;

		r = read(uffd, &msg, sizeof(msg));
		if (r == -1) {
			if (errno == EAGAIN)
				continue;
			pr_info("Read of uffd got errno %d\n", errno);
			return NULL;
		}

		if (r != sizeof(msg)) {
			pr_info("Read on uffd returned unexpected size: %d bytes", r);
			return NULL;
		}

		if (!(msg.event & UFFD_EVENT_PAGEFAULT))
			continue;

		if (delay)
			usleep(delay);
		r = uffd_desc->handler(uffd_desc->uffd_mode, uffd, &msg);
		if (r < 0)
			return NULL;
		pages++;
	}

	ts_diff = timespec_elapsed(start);
	PER_VCPU_DEBUG("userfaulted %ld pages over %ld.%.9lds. (%f/sec)\n",
		       pages, ts_diff.tv_sec, ts_diff.tv_nsec,
		       pages / ((double)ts_diff.tv_sec + (double)ts_diff.tv_nsec / NSEC_PER_SEC));

	return NULL;
}

struct uffd_desc *uffd_setup_demand_paging(int uffd_mode, useconds_t delay,
					   void *hva, uint64_t len,
					   uffd_handler_t handler)
{
	struct uffd_desc *uffd_desc;
	bool is_minor = (uffd_mode == UFFDIO_REGISTER_MODE_MINOR);
	int uffd;
	struct uffdio_api uffdio_api;
	struct uffdio_register uffdio_register;
	uint64_t expected_ioctls = ((uint64_t) 1) << _UFFDIO_COPY;
	int ret;

	PER_PAGE_DEBUG("Userfaultfd %s mode, faults resolved with %s\n",
		       is_minor ? "MINOR" : "MISSING",
		       is_minor ? "UFFDIO_CONINUE" : "UFFDIO_COPY");

	uffd_desc = malloc(sizeof(struct uffd_desc));
	TEST_ASSERT(uffd_desc, "malloc failed");

	/* In order to get minor faults, prefault via the alias. */
	if (is_minor)
		expected_ioctls = ((uint64_t) 1) << _UFFDIO_CONTINUE;

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	TEST_ASSERT(uffd >= 0, "uffd creation failed, errno: %d", errno);

	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	TEST_ASSERT(ioctl(uffd, UFFDIO_API, &uffdio_api) != -1,
		    "ioctl UFFDIO_API failed: %" PRIu64,
		    (uint64_t)uffdio_api.api);

	uffdio_register.range.start = (uint64_t)hva;
	uffdio_register.range.len = len;
	uffdio_register.mode = uffd_mode;
	TEST_ASSERT(ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) != -1,
		    "ioctl UFFDIO_REGISTER failed");
	TEST_ASSERT((uffdio_register.ioctls & expected_ioctls) ==
		    expected_ioctls, "missing userfaultfd ioctls");

	ret = pipe2(uffd_desc->pipefds, O_CLOEXEC | O_NONBLOCK);
	TEST_ASSERT(!ret, "Failed to set up pipefd");

	uffd_desc->uffd_mode = uffd_mode;
	uffd_desc->uffd = uffd;
	uffd_desc->delay = delay;
	uffd_desc->handler = handler;
	pthread_create(&uffd_desc->thread, NULL, uffd_handler_thread_fn,
		       uffd_desc);

	PER_VCPU_DEBUG("Created uffd thread for HVA range [%p, %p)\n",
		       hva, hva + len);

	return uffd_desc;
}

void uffd_stop_demand_paging(struct uffd_desc *uffd)
{
	char c = 0;
	int ret;

	ret = write(uffd->pipefds[1], &c, 1);
	TEST_ASSERT(ret == 1, "Unable to write to pipefd");

	ret = pthread_join(uffd->thread, NULL);
	TEST_ASSERT(ret == 0, "Pthread_join failed.");

	close(uffd->uffd);

	close(uffd->pipefds[1]);
	close(uffd->pipefds[0]);

	free(uffd);
}

#endif /* __NR_userfaultfd */
