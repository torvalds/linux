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
#include <sys/time.h>

#include <netinet/in.h>
#include <netdb.h>			/* getaddrinfo */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>			/* close */

static void
usage(void)
{

	fprintf(stderr, "netblast [ip] [port] [payloadsize] [duration]\n");
	exit(-1);
}

static int	global_stop_flag;

static void
signal_handler(int signum __unused)
{

	global_stop_flag = 1;
}

/*
 * Loop that blasts packets: begin by recording time information, resetting
 * stats.  Set the interval timer for when we want to wake up.  Then go.
 * SIGALRM will set a flag indicating it's time to stop.  Note that there's
 * some overhead to the signal and timer setup, so the smaller the duration,
 * the higher the relative overhead.
 */
static int
blast_loop(int s, long duration, u_char *packet, u_int packet_len)
{
	struct timespec starttime, tmptime;
	struct itimerval it;
	u_int32_t counter;
	int send_errors, send_calls;

	if (signal(SIGALRM, signal_handler) == SIG_ERR) {
		perror("signal");
		return (-1);
	}

	if (clock_getres(CLOCK_REALTIME, &tmptime) == -1) {
		perror("clock_getres");
		return (-1);
	}

	if (clock_gettime(CLOCK_REALTIME, &starttime) == -1) {
		perror("clock_gettime");
		return (-1);
	}

	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 0;
	it.it_value.tv_sec = duration;
	it.it_value.tv_usec = 0;

	if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
		perror("setitimer");
		return (-1);
	}

	send_errors = send_calls = 0;
	counter = 0;
	while (global_stop_flag == 0) {
		/*
		 * We maintain and, if there's room, send a counter.  Note
		 * that even if the error is purely local, we still increment
		 * the counter, so missing sequence numbers on the receive
		 * side should not be assumed to be packets lost in transit.
		 * For example, if the UDP socket gets back an ICMP from a
		 * previous send, the error will turn up the current send
		 * operation, causing the current sequence number also to be
		 * skipped.
		 */
		if (packet_len >= 4) {
			be32enc(packet, counter);
			counter++;
		}
		if (send(s, packet, packet_len, 0) < 0)
			send_errors++;
		send_calls++;
	}

	if (clock_gettime(CLOCK_REALTIME, &tmptime) == -1) {
		perror("clock_gettime");
		return (-1);
	}

	printf("\n");
	printf("start:             %zd.%09lu\n", starttime.tv_sec,
	    starttime.tv_nsec);
	printf("finish:            %zd.%09lu\n", tmptime.tv_sec,
	    tmptime.tv_nsec);
	printf("send calls:        %d\n", send_calls);
	printf("send errors:       %d\n", send_errors);
	printf("approx send rate:  %ld\n", (send_calls - send_errors) /
	    duration);
	printf("approx error rate: %d\n", (send_errors / send_calls));

	return (0);
}

int
main(int argc, char *argv[])
{
	long payloadsize, duration;
	struct addrinfo hints, *res, *res0;
	char *dummy, *packet;
	int port, s, error;
	const char *cause = NULL;

	if (argc != 5)
		usage();

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	port = strtoul(argv[2], &dummy, 10);
	if (port < 1 || port > 65535 || *dummy != '\0') {
		fprintf(stderr, "Invalid port number: %s\n", argv[2]);
		usage();
		/*NOTREACHED*/
	}

	payloadsize = strtoul(argv[3], &dummy, 10);
	if (payloadsize < 0 || *dummy != '\0')
		usage();
	if (payloadsize > 32768) {
		fprintf(stderr, "payloadsize > 32768\n");
		return (-1);
		/*NOTREACHED*/
	}

	duration = strtoul(argv[4], &dummy, 10);
	if (duration < 0 || *dummy != '\0') {
		fprintf(stderr, "Invalid duration time: %s\n", argv[4]);
		usage();
		/*NOTREACHED*/
	}

	packet = malloc(payloadsize);
	if (packet == NULL) {
		perror("malloc");
		return (-1);
		/*NOTREACHED*/
	}

	bzero(packet, payloadsize);
	error = getaddrinfo(argv[1],argv[2], &hints, &res0);
	if (error) {
		perror(gai_strerror(error));
		return (-1);
		/*NOTREACHED*/
	}
	s = -1;
	for (res = res0; res; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype, 0);
		if (s < 0) {
			cause = "socket";
			continue;
		}

		if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
			cause = "connect";
			close(s);
			s = -1;
			continue;
		}

		break;  /* okay we got one */
	}
	if (s < 0) {
		perror(cause);
		return (-1);
		/*NOTREACHED*/
	}

	freeaddrinfo(res0);

	return (blast_loop(s, duration, packet, payloadsize));

}
