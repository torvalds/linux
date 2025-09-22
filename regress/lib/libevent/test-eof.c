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
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
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
#include <evutil.h>

int test_okay = 1;
int called = 0;

static void
read_cb(int fd, short event, void *arg)
{
	char buf[256];
	int len;

	len = recv(fd, buf, sizeof(buf), 0);

	printf("%s: read %d%s\n", __func__,
	    len, len ? "" : " - means EOF");

	if (len) {
		if (!called)
			event_add(arg, NULL);
	} else if (called == 1)
		test_okay = 0;

	called++;
}

#ifndef SHUT_WR
#define SHUT_WR 1
#endif

int
main (int argc, char **argv)
{
	struct event ev;
	const char *test = "test string";
	int pair[2];

	if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == -1)
		return (1);

	
	send(pair[0], test, strlen(test)+1, 0);
	shutdown(pair[0], SHUT_WR);

	/* Initalize the event library */
	event_init();

	/* Initalize one event */
	event_set(&ev, pair[1], EV_READ, read_cb, &ev);

	event_add(&ev, NULL);

	event_dispatch();

	return (test_okay);
}

