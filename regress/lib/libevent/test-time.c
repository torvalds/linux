/*
 * Compile with:
 * cc -I/usr/local/include -o time-test time-test.c -L/usr/local/lib -levent
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef WIN32
#include <winsock2.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>

#include <event.h>

int called = 0;

#define NEVENT	20000

struct event *ev[NEVENT];

static int
rand_int(int n)
{
#ifdef WIN32
	return (int)(rand() * n);
#else
	return (int)(random() % n);
#endif
}

static void
time_cb(int fd, short event, void *arg)
{
	struct timeval tv;
	int i, j;

	called++;

	if (called < 10*NEVENT) {
		for (i = 0; i < 10; i++) {
			j = rand_int(NEVENT);
			tv.tv_sec = 0;
			tv.tv_usec = rand_int(50000);
			if (tv.tv_usec % 2)
				evtimer_add(ev[j], &tv);
			else
				evtimer_del(ev[j]);
		}
	}
}

int
main (int argc, char **argv)
{
	struct timeval tv;
	int i;

	/* Initalize the event library */
	event_init();

	for (i = 0; i < NEVENT; i++) {
		ev[i] = malloc(sizeof(struct event));

		/* Initalize one event */
		evtimer_set(ev[i], time_cb, ev[i]);
		tv.tv_sec = 0;
		tv.tv_usec = rand_int(50000);
		evtimer_add(ev[i], &tv);
	}

	event_dispatch();

	return (called < NEVENT);
}

