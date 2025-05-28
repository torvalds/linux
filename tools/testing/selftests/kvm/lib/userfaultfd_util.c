// SPDX-License-Identifier: GPL-2.0
/*
 * KVM userfaultfd util
 * Adapted from demand_paging_test.c
 *
 * Copyright (C) 2018, Red Hat, Inc.
 * Copyright (C) 2019-2022 Google LLC
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include <linux/userfaultfd.h>
#include <sys/epoll.h>
#include <sys/syscall.h>

#include "kvm_util.h"
#include "test_util.h"
#include "memstress.h"
#include "userfaultfd_util.h"

#ifdef __NR_userfaultfd

static void *uffd_handler_thread_fn(void *arg)
{
	struct uffd_reader_args *reader_args = (struct uffd_reader_args *)arg;
	int uffd = reader_args->uffd;
	int64_t pages = 0;
	struct timespec start;
	struct timespec ts_diff;
	struct epoll_event evt;
	int epollfd;

	epollfd = epoll_create(1);
	TEST_ASSERT(epollfd >= 0, "Failed to create epollfd.");

	evt.events = EPOLLIN | EPOLLEXCLUSIVE;
	evt.data.u32 = 0;
	TEST_ASSERT(!epoll_ctl(epollfd, EPOLL_CTL_ADD, uffd, &evt),
		    "Failed to add uffd to epollfd");

	evt.events = EPOLLIN;
	evt.data.u32 = 1;
	TEST_ASSERT(!epoll_ctl(epollfd, EPOLL_CTL_ADD, reader_args->pipe, &evt),
		    "Failed to add pipe to epollfd");

	clock_gettime(CLOCK_MONOTONIC, &start);
	while (1) {
		struct uffd_msg msg;
		int r;

		r = epoll_wait(epollfd, &evt, 1, -1);
		TEST_ASSERT(r == 1,
			    "Unexpected number of events (%d) from epoll, errno = %d",
			    r, errno);

		if (evt.data.u32 == 1) {
			char tmp_chr;

			TEST_ASSERT(!(evt.events & (EPOLLERR | EPOLLHUP)),
				    "Reader thread received EPOLLERR or EPOLLHUP on pipe.");
			r = read(reader_args->pipe, &tmp_chr, 1);
			TEST_ASSERT(r == 1,
				    "Error reading pipefd in uffd reader thread");
			break;
		}

		TEST_ASSERT(!(evt.events & (EPOLLERR | EPOLLHUP)),
			    "Reader thread received EPOLLERR or EPOLLHUP on uffd.");

		r = read(uffd, &msg, sizeof(msg));
		if (r == -1) {
			TEST_ASSERT(errno == EAGAIN,
				    "Error reading from UFFD: errno = %d", errno);
			continue;
		}

		TEST_ASSERT(r == sizeof(msg),
			    "Read on uffd returned unexpected number of bytes (%d)", r);

		if (!(msg.event & UFFD_EVENT_PAGEFAULT))
			continue;

		if (reader_args->delay)
			usleep(reader_args->delay);
		r = reader_args->handler(reader_args->uffd_mode, uffd, &msg);
		TEST_ASSERT(r >= 0,
			    "Reader thread handler fn returned negative value %d", r);
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
					   uint64_t num_readers,
					   uffd_handler_t handler)
{
	struct uffd_desc *uffd_desc;
	bool is_minor = (uffd_mode == UFFDIO_REGISTER_MODE_MINOR);
	int uffd;
	struct uffdio_api uffdio_api;
	struct uffdio_register uffdio_register;
	uint64_t expected_ioctls = ((uint64_t) 1) << _UFFDIO_COPY;
	int ret, i;

	PER_PAGE_DEBUG("Userfaultfd %s mode, faults resolved with %s\n",
		       is_minor ? "MINOR" : "MISSING",
		       is_minor ? "UFFDIO_CONTINUE" : "UFFDIO_COPY");

	uffd_desc = malloc(sizeof(struct uffd_desc));
	TEST_ASSERT(uffd_desc, "Failed to malloc uffd descriptor");

	uffd_desc->pipefds = calloc(sizeof(int), num_readers);
	TEST_ASSERT(uffd_desc->pipefds, "Failed to alloc pipes");

	uffd_desc->readers = calloc(sizeof(pthread_t), num_readers);
	TEST_ASSERT(uffd_desc->readers, "Failed to alloc reader threads");

	uffd_desc->reader_args = calloc(sizeof(struct uffd_reader_args), num_readers);
	TEST_ASSERT(uffd_desc->reader_args, "Failed to alloc reader_args");

	uffd_desc->num_readers = num_readers;

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

	uffd_desc->uffd = uffd;
	for (i = 0; i < uffd_desc->num_readers; ++i) {
		int pipes[2];

		ret = pipe2((int *) &pipes, O_CLOEXEC | O_NONBLOCK);
		TEST_ASSERT(!ret, "Failed to set up pipefd %i for uffd_desc %p",
			    i, uffd_desc);

		uffd_desc->pipefds[i] = pipes[1];

		uffd_desc->reader_args[i].uffd_mode = uffd_mode;
		uffd_desc->reader_args[i].uffd = uffd;
		uffd_desc->reader_args[i].delay = delay;
		uffd_desc->reader_args[i].handler = handler;
		uffd_desc->reader_args[i].pipe = pipes[0];

		pthread_create(&uffd_desc->readers[i], NULL, uffd_handler_thread_fn,
			       &uffd_desc->reader_args[i]);

		PER_VCPU_DEBUG("Created uffd thread %i for HVA range [%p, %p)\n",
			       i, hva, hva + len);
	}

	return uffd_desc;
}

void uffd_stop_demand_paging(struct uffd_desc *uffd)
{
	char c = 0;
	int i;

	for (i = 0; i < uffd->num_readers; ++i)
		TEST_ASSERT(write(uffd->pipefds[i], &c, 1) == 1,
			    "Unable to write to pipefd %i for uffd_desc %p", i, uffd);

	for (i = 0; i < uffd->num_readers; ++i)
		TEST_ASSERT(!pthread_join(uffd->readers[i], NULL),
			    "Pthread_join failed on reader %i for uffd_desc %p", i, uffd);

	close(uffd->uffd);

	for (i = 0; i < uffd->num_readers; ++i) {
		close(uffd->pipefds[i]);
		close(uffd->reader_args[i].pipe);
	}

	free(uffd->pipefds);
	free(uffd->readers);
	free(uffd->reader_args);
	free(uffd);
}

#endif /* __NR_userfaultfd */
