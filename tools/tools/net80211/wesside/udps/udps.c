/*-
 * Copyright (c) 2006, Andrea Bittau <a.bittau@cs.ucl.ac.uk>
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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <err.h>

int poll_rate = 5;
int pps = 10;

//#define INSANE

void own(int s, struct sockaddr_in* s_in) {
	char buf[64];
	int times = 10;
	int i;
	int delay = 10*1000;
	unsigned int sent = 0;
	struct timeval start, end;
	struct timespec ts;
	int dont_sleep_times = 1;
	int dont_sleep;

	delay = (int) ((double)1.0/pps*1000.0*1000.0);

	if (delay <= 5000) {
		dont_sleep_times = 10;
/*		
		printf("delay is %d... sleeping every %d packets\n",
			delay, dont_sleep_times);
*/			
		delay *= dont_sleep_times;

		delay = (int) (0.90*delay);
	}	
	
	dont_sleep = dont_sleep_times;
	times = poll_rate*pps;
//	times *= dont_sleep;



	ts.tv_sec = 0;
	ts.tv_nsec = delay*1000;

//	printf("times=%d delay=%d\n", times, delay);
	if (gettimeofday(&start, NULL) == -1) {
		perror("gettimeofday()");
		exit(1);
	}

	for(i = 0; i < times; i++) {
		if( sendto(s, buf, 6, 0, (struct sockaddr *)s_in, sizeof(*s_in)) != 6) {
			printf("messed up a bit\n");
			return;
		}

#ifndef INSANE

#if 0
		if (usleep(delay) == -1) {
			perror("usleep()");
			exit(1);
		}
#endif
		dont_sleep--;
		
		if (!dont_sleep) {
			if (nanosleep(&ts, NULL) == -1) {
				perror("nanosleep()");
				exit(1);
			}

			dont_sleep  = dont_sleep_times;
		}	
		
#endif	
		sent++;
	}

	if (gettimeofday(&end, NULL) == -1) {
		perror("gettimeofday()");
		exit(1);
	}

	printf ("Sent %.03f p/s\n", ((double)sent)/(((double)end.tv_sec) - start.tv_sec));

//	printf("Sent %d packets\n", i);
}

int main(int argc, char* argv[]) {
	int port = 6969;
	struct sockaddr_in s_in;
	int s;
	int rd;
	int len;
	char buf[64];
	struct timeval tv;
	int do_it = 0;
	fd_set rfds;
	char ip[17];

	if( argc > 1)
		pps = atoi(argv[1]);

	printf("Packets per second=%d\n", pps);	

	s = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if( s < 0)
		err(1, "socket()");

	s_in.sin_family = PF_INET;
	s_in.sin_port = htons(port);
	s_in.sin_addr.s_addr = INADDR_ANY;

	if( bind(s, (struct sockaddr*)&s_in, sizeof(s_in)) < 0) {
		perror("bind()");
		exit(1);
	}

	while(1) {
		assert(do_it >= 0);
		len = sizeof(struct sockaddr_in);

		memset(&tv, 0, sizeof(tv));
		tv.tv_usec = 1000*10;
		FD_ZERO(&rfds);
		FD_SET(s, &rfds);
		rd = select(s + 1, &rfds, NULL ,NULL ,&tv);
		if (rd == -1) {
			perror("select()");
			exit(1);
		}
		if (rd == 1 && FD_ISSET(s, &rfds)) {
			rd = recvfrom(s, buf, 64, 0, (struct sockaddr*)&s_in, &len);

			if(rd < 0) {
				perror("read died");
				exit(1);
			}

			if(rd == 5 && memcmp(buf, "sorbo", 5) == 0) {
				sprintf(ip, "%s", inet_ntoa(s_in.sin_addr));
				printf("Got signal from %s\n", ip);
#ifdef INSANE
				do_it = 10;
#else				
				do_it = 2;
#endif				
			}	
		}		

		if (do_it) {	
			printf("Sending stuff to %s\n", ip);

			own(s, &s_in);
			do_it--;

			if(do_it == 0)
			printf("Stopping send\n");
		}
	}
}
