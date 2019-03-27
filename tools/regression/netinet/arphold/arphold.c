/*
 * Copyright (c) 2010 Hudson River Trading LLC
 * Written by George Neville-Neil gnn@freebsd.org
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
 * Description: The following is a test of the arp entry packet queues
 * which replaced the single packet hold entry that existed in the BSDs
 * since time immemorial.  The test process is:
 *
 * 1) Find out the current system limit (maxhold)
 * 2) Using an IP address for which we do not yet have an entry
 *    load up an ARP entry packet queue with exactly that many packets.
 * 3) Check the arp dropped stat to make sure that we have not dropped
 *    any packets as yet.
 * 4) Add one more packet to the queue.
 * 5) Make sure that only one packet was dropped.
 *
 * CAVEAT: The ARP timer will flush the queue after 1 second so it is
 * important not to run this code in a fast loop or the test will
 * fail.
 *
 * $FreeBSD$
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if_arp.h>

#define MSG_SIZE 1024
#define PORT 6969

int
main(int argc, char **argv)
{

	int sock;
	int maxhold;
	size_t size = sizeof(maxhold);
	struct sockaddr_in dest;
	char message[MSG_SIZE];
	struct arpstat arpstat;
	size_t len = sizeof(arpstat);
	unsigned long dropped = 0;

	memset(&message, 1, sizeof(message));

	if (sysctlbyname("net.link.ether.inet.maxhold", &maxhold, &size,
			 NULL, 0) < 0) {
		perror("not ok 1 - sysctlbyname failed");
		exit(1);
	}
	    
#ifdef DEBUG
	printf("maxhold is %d\n", maxhold);
#endif /* DEBUG */

	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		perror("not ok 1 - could not open socket");
		exit(1);
	}

	bzero(&dest, sizeof(dest));
	if (inet_pton(AF_INET, argv[1], &dest.sin_addr.s_addr) != 1) {
		perror("not ok 1 - could not parse address");
		exit(1);
	}
	dest.sin_len = sizeof(dest);
	dest.sin_family = AF_INET;
	dest.sin_port = htons(PORT);

	if (sysctlbyname("net.link.ether.arp.stats", &arpstat, &len,
			 NULL, 0) < 0) {
		perror("not ok 1 - could not get initial arp stats");
		exit(1);
	}

	dropped = arpstat.dropped;
#ifdef DEBUG
	printf("dropped before %ld\n", dropped);
#endif /* DEBUG */

	/* 
	 * Load up the queue in the ARP entry to the maximum.
	 * We should not drop any packets at this point. 
	 */

	while (maxhold > 0) {
		if (sendto(sock, message, sizeof(message), 0,
			   (struct sockaddr *)&dest, sizeof(dest)) < 0) {
			perror("not ok 1 - could not send packet");
			exit(1);
		}
		maxhold--;
	}

	if (sysctlbyname("net.link.ether.arp.stats", &arpstat, &len,
			 NULL, 0) < 0) {
		perror("not ok 1 - could not get new arp stats");
		exit(1);
	}

#ifdef DEBUG
	printf("dropped after %ld\n", arpstat.dropped);
#endif /* DEBUG */

	if (arpstat.dropped != dropped) {
		printf("not ok 1 - Failed, drops changed:"
		       "before %ld after %ld\n", dropped, arpstat.dropped);
		exit(1);
	}
	
	dropped = arpstat.dropped;

	/* Now add one extra and make sure it is dropped. */
	if (sendto(sock, message, sizeof(message), 0,
		   (struct sockaddr *)&dest, sizeof(dest)) < 0) {
		perror("not ok 1 - could not send packet");
		exit(1);
	}

	if (sysctlbyname("net.link.ether.arp.stats", &arpstat, &len,
			 NULL, 0) < 0) {
		perror("not ok 1 - could not get new arp stats");
		exit(1);
	}

	if (arpstat.dropped != (dropped + 1)) {
		printf("not ok 1 - Failed to drop one packet: before"
		       " %ld after %ld\n", dropped, arpstat.dropped);
		exit(1);
	}

	printf("ok\n");
	return (0);
}
