/*-
 * Copyright (c) 2004 Robert N. M. Watson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/endian.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>		/* if_nametoindex() */
#include <sys/time.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>

/* program arguments */
struct _a {
	int s;
	int ipv6;
	struct timespec interval;
	int port, port_max;
	long duration;
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	int packet_len;
	void *packet;
};

static void
usage(void)
{

	fprintf(stderr,
	    "netsend [ip] [port[-port_max]] [payloadsize] [packet_rate] [duration]\n");
	exit(-1);
}

#define	MAX_RATE	100000000

static __inline void
timespec_add(struct timespec *tsa, struct timespec *tsb)
{

	tsa->tv_sec += tsb->tv_sec;
	tsa->tv_nsec += tsb->tv_nsec;
	if (tsa->tv_nsec >= 1000000000) {
		tsa->tv_sec++;
		tsa->tv_nsec -= 1000000000;
	}
}

static __inline int
timespec_ge(struct timespec *a, struct timespec *b)
{

	if (a->tv_sec > b->tv_sec)
		return (1);
	if (a->tv_sec < b->tv_sec)
		return (0);
	if (a->tv_nsec >= b->tv_nsec)
		return (1);
	return (0);
}

/*
 * Busy wait spinning until we reach (or slightly pass) the desired time.
 * Optionally return the current time as retrieved on the last time check
 * to the caller.  Optionally also increment a counter provided by the
 * caller each time we loop.
 */
static int
wait_time(struct timespec ts, struct timespec *wakeup_ts, long long *waited)
{
	struct timespec curtime;

	curtime.tv_sec = 0;
	curtime.tv_nsec = 0;

	if (clock_gettime(CLOCK_REALTIME, &curtime) == -1) {
		perror("clock_gettime");
		return (-1);
	}
#if 0
	if (timespec_ge(&curtime, &ts))
		printf("warning: wait_time missed deadline without spinning\n");
#endif
	while (timespec_ge(&ts, &curtime)) {
		if (waited != NULL)
			(*waited)++;
		if (clock_gettime(CLOCK_REALTIME, &curtime) == -1) {
			perror("clock_gettime");
			return (-1);
		}
	}
	if (wakeup_ts != NULL)
		*wakeup_ts = curtime;
	return (0);
}

/*
 * Calculate a second-aligned starting time for the packet stream.  Busy
 * wait between our calculated interval and dropping the provided packet
 * into the socket.  If we hit our duration limit, bail.
 * We sweep the ports from a->port to a->port_max included.
 * If the two ports are the same we connect() the socket upfront, which
 * almost halves the cost of the sendto() call.
 */
static int
timing_loop(struct _a *a)
{
	struct timespec nexttime, starttime, tmptime;
	long long waited;
	u_int32_t counter;
	long finishtime;
	long send_errors, send_calls;
	/* do not call gettimeofday more than every 20us */
	long minres_ns = 200000;
	int ic, gettimeofday_cycles;
	int cur_port;
	uint64_t n, ns;

	if (clock_getres(CLOCK_REALTIME, &tmptime) == -1) {
		perror("clock_getres");
		return (-1);
	}

	ns = a->interval.tv_nsec;
	if (timespec_ge(&tmptime, &a->interval))
		fprintf(stderr,
		    "warning: interval (%jd.%09ld) less than resolution (%jd.%09ld)\n",
		    (intmax_t)a->interval.tv_sec, a->interval.tv_nsec,
		    (intmax_t)tmptime.tv_sec, tmptime.tv_nsec);
		/* interval too short, limit the number of gettimeofday()
		 * calls, but also make sure there is at least one every
		 * some 100 packets.
		 */
	if ((long)ns < minres_ns/100)
		gettimeofday_cycles = 100;
	else
		gettimeofday_cycles = minres_ns/ns;
	fprintf(stderr,
	    "calling time every %d cycles\n", gettimeofday_cycles);

	if (clock_gettime(CLOCK_REALTIME, &starttime) == -1) {
		perror("clock_gettime");
		return (-1);
	}
	tmptime.tv_sec = 2;
	tmptime.tv_nsec = 0;
	timespec_add(&starttime, &tmptime);
	starttime.tv_nsec = 0;
	if (wait_time(starttime, NULL, NULL) == -1)
		return (-1);
	nexttime = starttime;
	finishtime = starttime.tv_sec + a->duration;

	send_errors = send_calls = 0;
	counter = 0;
	waited = 0;
	ic = gettimeofday_cycles;
	cur_port = a->port;
	if (a->port == a->port_max) {
		if (a->ipv6) {
			if (connect(a->s, (struct sockaddr *)&a->sin6, sizeof(a->sin6))) {
				perror("connect (ipv6)");
				return (-1);
			}
		} else {
			if (connect(a->s, (struct sockaddr *)&a->sin, sizeof(a->sin))) {
				perror("connect (ipv4)");
				return (-1);
			}
		}
	}
	while (1) {
		int ret;

		timespec_add(&nexttime, &a->interval);
		if (--ic <= 0) {
			ic = gettimeofday_cycles;
			if (wait_time(nexttime, &tmptime, &waited) == -1)
				return (-1);
		}
		/*
		 * We maintain and, if there's room, send a counter.  Note
		 * that even if the error is purely local, we still increment
		 * the counter, so missing sequence numbers on the receive
		 * side should not be assumed to be packets lost in transit.
		 * For example, if the UDP socket gets back an ICMP from a
		 * previous send, the error will turn up the current send
		 * operation, causing the current sequence number also to be
		 * skipped.
		 * The counter is incremented only on the initial port number,
		 * so all destinations will see the same set of packets.
		 */
		if (cur_port == a->port && a->packet_len >= 4) {
			be32enc(a->packet, counter);
			counter++;
		}
		if (a->port == a->port_max) { /* socket already bound */
			ret = send(a->s, a->packet, a->packet_len, 0);
		} else {
			a->sin.sin_port = htons(cur_port++);
			if (cur_port > a->port_max)
				cur_port = a->port;
			if (a->ipv6) {
			ret = sendto(a->s, a->packet, a->packet_len, 0,
			    (struct sockaddr *)&a->sin6, sizeof(a->sin6));
			} else {
			ret = sendto(a->s, a->packet, a->packet_len, 0,
				(struct sockaddr *)&a->sin, sizeof(a->sin));
			}
		}
		if (ret < 0)
			send_errors++;
		send_calls++;
		if (a->duration != 0 && tmptime.tv_sec >= finishtime)
			goto done;
	}

done:
	if (clock_gettime(CLOCK_REALTIME, &tmptime) == -1) {
		perror("clock_gettime");
		return (-1);
	}

	printf("\n");
	printf("start:             %jd.%09ld\n", (intmax_t)starttime.tv_sec,
	    starttime.tv_nsec);
	printf("finish:            %jd.%09ld\n", (intmax_t)tmptime.tv_sec,
	    tmptime.tv_nsec);
	printf("send calls:        %ld\n", send_calls);
	printf("send errors:       %ld\n", send_errors);
	printf("approx send rate:  %ld pps\n", (send_calls - send_errors) /
	    a->duration);
	n = send_calls - send_errors;
	if (n > 0) {
		ns = (tmptime.tv_sec - starttime.tv_sec) * 1000000000UL +
			(tmptime.tv_nsec - starttime.tv_nsec);
		n = ns / n;
	}
	printf("time/packet:       %u ns\n", (u_int)n);
	printf("approx error rate: %ld\n", (send_errors / send_calls));
	printf("waited:            %lld\n", waited);
	printf("approx waits/sec:  %lld\n", (long long)(waited / a->duration));
	printf("approx wait rate:  %lld\n", (long long)(waited / send_calls));

	return (0);
}

int
main(int argc, char *argv[])
{
	long rate, payloadsize, port;
	char *dummy;
	struct _a a;	/* arguments */
	struct addrinfo hints, *res, *ressave;

	bzero(&a, sizeof(a));

	if (argc != 6)
		usage();

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;

	if (getaddrinfo(argv[1], NULL, &hints, &res) != 0) {
		fprintf(stderr, "Couldn't resolv %s\n", argv[1]);
		return (-1);
	}
	ressave = res;
	while (res) {
		if (res->ai_family == AF_INET) {
			memcpy(&a.sin, res->ai_addr, res->ai_addrlen);
			a.ipv6 = 0;
			break;
		} else if (res->ai_family == AF_INET6) {
			memcpy(&a.sin6, res->ai_addr, res->ai_addrlen);
			a.ipv6 = 1;
			break;
		} 
		res = res->ai_next;
	}
	if (!res) {
		fprintf(stderr, "Couldn't resolv %s\n", argv[1]);
		exit(1);
	}
	freeaddrinfo(ressave);

	port = strtoul(argv[2], &dummy, 10);
	if (port < 1 || port > 65535)
		usage();
	if (*dummy != '\0' && *dummy != '-')
		usage();
	if (a.ipv6)
		a.sin6.sin6_port = htons(port);
	else
		a.sin.sin_port = htons(port);
	a.port = a.port_max = port;
	if (*dummy == '-') {	/* set high port */
		port = strtoul(dummy + 1, &dummy, 10);
		if (port < a.port || port > 65535)
			usage();
		a.port_max = port;
	}

	payloadsize = strtoul(argv[3], &dummy, 10);
	if (payloadsize < 0 || *dummy != '\0')
		usage();
	if (payloadsize > 32768) {
		fprintf(stderr, "payloadsize > 32768\n");
		return (-1);
	}
	a.packet_len = payloadsize;

	/*
	 * Specify an arbitrary limit.  It's exactly that, not selected by
	 * any particular strategy.  '0' is a special value meaning "blast",
	 * and avoids the cost of a timing loop.
	 */
	rate = strtoul(argv[4], &dummy, 10);
	if (rate < 0 || *dummy != '\0')
		usage();
	if (rate > MAX_RATE) {
		fprintf(stderr, "packet rate at most %d\n", MAX_RATE);
		return (-1);
	}

	a.duration = strtoul(argv[5], &dummy, 10);
	if (a.duration < 0 || *dummy != '\0')
		usage();

	a.packet = malloc(payloadsize);
	if (a.packet == NULL) {
		perror("malloc");
		return (-1);
	}
	bzero(a.packet, payloadsize);
	if (rate == 0) {
		a.interval.tv_sec = 0;
		a.interval.tv_nsec = 0;
	} else if (rate == 1) {
		a.interval.tv_sec = 1;
		a.interval.tv_nsec = 0;
	} else {
		a.interval.tv_sec = 0;
		a.interval.tv_nsec = ((1 * 1000000000) / rate);
	}

	printf("Sending packet of payload size %ld every %jd.%09lds for %ld "
	    "seconds\n", payloadsize, (intmax_t)a.interval.tv_sec,
	    a.interval.tv_nsec, a.duration);

	if (a.ipv6)
		a.s = socket(PF_INET6, SOCK_DGRAM, 0);
	else
		a.s = socket(PF_INET, SOCK_DGRAM, 0);
	if (a.s == -1) {
		perror("socket");
		return (-1);
	}

	return (timing_loop(&a));
}
