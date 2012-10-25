/*
 *  tools/testing/selftests/epoll/test_epoll.c
 *
 *  Copyright 2012 Adobe Systems Incorporated
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Paton J. Lewis <palewis@adobe.com>
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>

/*
 * A pointer to an epoll_item_private structure will be stored in the epoll
 * item's event structure so that we can get access to the epoll_item_private
 * data after calling epoll_wait:
 */
struct epoll_item_private {
	int index;  /* Position of this struct within the epoll_items array. */
	int fd;
	uint32_t events;
	pthread_mutex_t mutex;  /* Guards the following variables... */
	int stop;
	int status;  /* Stores any error encountered while handling item. */
	/* The following variable allows us to test whether we have encountered
	   a problem while attempting to cancel and delete the associated
	   event. When the test program exits, 'deleted' should be exactly
	   one. If it is greater than one, then the failed test reflects a real
	   world situation where we would have tried to access the epoll item's
	   private data after deleting it: */
	int deleted;
};

struct epoll_item_private *epoll_items;

/*
 * Delete the specified item from the epoll set. In a real-world secneario this
 * is where we would free the associated data structure, but in this testing
 * environment we retain the structure so that we can test for double-deletion:
 */
void delete_item(int index)
{
	__sync_fetch_and_add(&epoll_items[index].deleted, 1);
}

/*
 * A pointer to a read_thread_data structure will be passed as the argument to
 * each read thread:
 */
struct read_thread_data {
	int stop;
	int status;  /* Indicates any error encountered by the read thread. */
	int epoll_set;
};

/*
 * The function executed by the read threads:
 */
void *read_thread_function(void *function_data)
{
	struct read_thread_data *thread_data =
		(struct read_thread_data *)function_data;
	struct epoll_event event_data;
	struct epoll_item_private *item_data;
	char socket_data;

	/* Handle events until we encounter an error or this thread's 'stop'
	   condition is set: */
	while (1) {
		int result = epoll_wait(thread_data->epoll_set,
					&event_data,
					1,	/* Number of desired events */
					1000);  /* Timeout in ms */
		if (result < 0) {
			/* Breakpoints signal all threads. Ignore that while
			   debugging: */
			if (errno == EINTR)
				continue;
			thread_data->status = errno;
			return 0;
		} else if (thread_data->stop)
			return 0;
		else if (result == 0)  /* Timeout */
			continue;

		/* We need the mutex here because checking for the stop
		   condition and re-enabling the epoll item need to be done
		   together as one atomic operation when EPOLL_CTL_DISABLE is
		   available: */
		item_data = (struct epoll_item_private *)event_data.data.ptr;
		pthread_mutex_lock(&item_data->mutex);

		/* Remove the item from the epoll set if we want to stop
		   handling that event: */
		if (item_data->stop)
			delete_item(item_data->index);
		else {
			/* Clear the data that was written to the other end of
			   our non-blocking socket: */
			do {
				if (read(item_data->fd, &socket_data, 1) < 1) {
					if ((errno == EAGAIN) ||
					    (errno == EWOULDBLOCK))
						break;
					else
						goto error_unlock;
				}
			} while (item_data->events & EPOLLET);

			/* The item was one-shot, so re-enable it: */
			event_data.events = item_data->events;
			if (epoll_ctl(thread_data->epoll_set,
						  EPOLL_CTL_MOD,
						  item_data->fd,
						  &event_data) < 0)
				goto error_unlock;
		}

		pthread_mutex_unlock(&item_data->mutex);
	}

error_unlock:
	thread_data->status = item_data->status = errno;
	pthread_mutex_unlock(&item_data->mutex);
	return 0;
}

/*
 * A pointer to a write_thread_data structure will be passed as the argument to
 * the write thread:
 */
struct write_thread_data {
	int stop;
	int status;  /* Indicates any error encountered by the write thread. */
	int n_fds;
	int *fds;
};

/*
 * The function executed by the write thread. It writes a single byte to each
 * socket in turn until the stop condition for this thread is set. If writing to
 * a socket would block (i.e. errno was EAGAIN), we leave that socket alone for
 * the moment and just move on to the next socket in the list. We don't care
 * about the order in which we deliver events to the epoll set. In fact we don't
 * care about the data we're writing to the pipes at all; we just want to
 * trigger epoll events:
 */
void *write_thread_function(void *function_data)
{
	const char data = 'X';
	int index;
	struct write_thread_data *thread_data =
		(struct write_thread_data *)function_data;
	while (!thread_data->stop)
		for (index = 0;
		     !thread_data->stop && (index < thread_data->n_fds);
		     ++index)
			if ((write(thread_data->fds[index], &data, 1) < 1) &&
				(errno != EAGAIN) &&
				(errno != EWOULDBLOCK)) {
				thread_data->status = errno;
				return;
			}
}

/*
 * Arguments are currently ignored:
 */
int main(int argc, char **argv)
{
	const int n_read_threads = 100;
	const int n_epoll_items = 500;
	int index;
	int epoll_set = epoll_create1(0);
	struct write_thread_data write_thread_data = {
		0, 0, n_epoll_items, malloc(n_epoll_items * sizeof(int))
	};
	struct read_thread_data *read_thread_data =
		malloc(n_read_threads * sizeof(struct read_thread_data));
	pthread_t *read_threads = malloc(n_read_threads * sizeof(pthread_t));
	pthread_t write_thread;

	printf("-----------------\n");
	printf("Runing test_epoll\n");
	printf("-----------------\n");

	epoll_items = malloc(n_epoll_items * sizeof(struct epoll_item_private));

	if (epoll_set < 0 || epoll_items == 0 || write_thread_data.fds == 0 ||
		read_thread_data == 0 || read_threads == 0)
		goto error;

	if (sysconf(_SC_NPROCESSORS_ONLN) < 2) {
		printf("Error: please run this test on a multi-core system.\n");
		goto error;
	}

	/* Create the socket pairs and epoll items: */
	for (index = 0; index < n_epoll_items; ++index) {
		int socket_pair[2];
		struct epoll_event event_data;
		if (socketpair(AF_UNIX,
			       SOCK_STREAM | SOCK_NONBLOCK,
			       0,
			       socket_pair) < 0)
			goto error;
		write_thread_data.fds[index] = socket_pair[0];
		epoll_items[index].index = index;
		epoll_items[index].fd = socket_pair[1];
		if (pthread_mutex_init(&epoll_items[index].mutex, NULL) != 0)
			goto error;
		/* We always use EPOLLONESHOT because this test is currently
		   structured to demonstrate the need for EPOLL_CTL_DISABLE,
		   which only produces useful information in the EPOLLONESHOT
		   case (without EPOLLONESHOT, calling epoll_ctl with
		   EPOLL_CTL_DISABLE will never return EBUSY). If support for
		   testing events without EPOLLONESHOT is desired, it should
		   probably be implemented in a separate unit test. */
		epoll_items[index].events = EPOLLIN | EPOLLONESHOT;
		if (index < n_epoll_items / 2)
			epoll_items[index].events |= EPOLLET;
		epoll_items[index].stop = 0;
		epoll_items[index].status = 0;
		epoll_items[index].deleted = 0;
		event_data.events = epoll_items[index].events;
		event_data.data.ptr = &epoll_items[index];
		if (epoll_ctl(epoll_set,
			      EPOLL_CTL_ADD,
			      epoll_items[index].fd,
			      &event_data) < 0)
			goto error;
	}

	/* Create and start the read threads: */
	for (index = 0; index < n_read_threads; ++index) {
		read_thread_data[index].stop = 0;
		read_thread_data[index].status = 0;
		read_thread_data[index].epoll_set = epoll_set;
		if (pthread_create(&read_threads[index],
				   NULL,
				   read_thread_function,
				   &read_thread_data[index]) != 0)
			goto error;
	}

	if (pthread_create(&write_thread,
			   NULL,
			   write_thread_function,
			   &write_thread_data) != 0)
		goto error;

	/* Cancel all event pollers: */
#ifdef EPOLL_CTL_DISABLE
	for (index = 0; index < n_epoll_items; ++index) {
		pthread_mutex_lock(&epoll_items[index].mutex);
		++epoll_items[index].stop;
		if (epoll_ctl(epoll_set,
			      EPOLL_CTL_DISABLE,
			      epoll_items[index].fd,
			      NULL) == 0)
			delete_item(index);
		else if (errno != EBUSY) {
			pthread_mutex_unlock(&epoll_items[index].mutex);
			goto error;
		}
		/* EBUSY means events were being handled; allow the other thread
		   to delete the item. */
		pthread_mutex_unlock(&epoll_items[index].mutex);
	}
#else
	for (index = 0; index < n_epoll_items; ++index) {
		pthread_mutex_lock(&epoll_items[index].mutex);
		++epoll_items[index].stop;
		pthread_mutex_unlock(&epoll_items[index].mutex);
		/* Wait in case a thread running read_thread_function is
		   currently executing code between epoll_wait and
		   pthread_mutex_lock with this item. Note that a longer delay
		   would make double-deletion less likely (at the expense of
		   performance), but there is no guarantee that any delay would
		   ever be sufficient. Note also that we delete all event
		   pollers at once for testing purposes, but in a real-world
		   environment we are likely to want to be able to cancel event
		   pollers at arbitrary times. Therefore we can't improve this
		   situation by just splitting this loop into two loops
		   (i.e. signal 'stop' for all items, sleep, and then delete all
		   items). We also can't fix the problem via EPOLL_CTL_DEL
		   because that command can't prevent the case where some other
		   thread is executing read_thread_function within the region
		   mentioned above: */
		usleep(1);
		pthread_mutex_lock(&epoll_items[index].mutex);
		if (!epoll_items[index].deleted)
			delete_item(index);
		pthread_mutex_unlock(&epoll_items[index].mutex);
	}
#endif

	/* Shut down the read threads: */
	for (index = 0; index < n_read_threads; ++index)
		__sync_fetch_and_add(&read_thread_data[index].stop, 1);
	for (index = 0; index < n_read_threads; ++index) {
		if (pthread_join(read_threads[index], NULL) != 0)
			goto error;
		if (read_thread_data[index].status)
			goto error;
	}

	/* Shut down the write thread: */
	__sync_fetch_and_add(&write_thread_data.stop, 1);
	if ((pthread_join(write_thread, NULL) != 0) || write_thread_data.status)
		goto error;

	/* Check for final error conditions: */
	for (index = 0; index < n_epoll_items; ++index) {
		if (epoll_items[index].status != 0)
			goto error;
		if (pthread_mutex_destroy(&epoll_items[index].mutex) < 0)
			goto error;
	}
	for (index = 0; index < n_epoll_items; ++index)
		if (epoll_items[index].deleted != 1) {
			printf("Error: item data deleted %1d times.\n",
				   epoll_items[index].deleted);
			goto error;
		}

	printf("[PASS]\n");
	return 0;

 error:
	printf("[FAIL]\n");
	return errno;
}
