/* 
 * Copyright (c) 2008, Neville-Neil Consulting
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: George V. Neville-Neil
 *
 * Purpose: This program uses libpcap to read packets from the network
 * of a specific ethertype (default is 0x8822) and reflects them back
 * out the same interface with their destination and source mac
 * addresses reversed.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <unistd.h>
#include <stdlib.h>
#include <strings.h>

#include <pcap-int.h>
#include <pcap.h>
#include <net/ethernet.h>

#define ETHER_TYPE_TEST "0x8822"
#define SNAPLEN 96
#define MAXPROG 128

char errbuf[PCAP_ERRBUF_SIZE];

void usage(char* message) {
	if (message != NULL)
		printf ("error: %s\n", message);
	printf("usage: ether_reflect -i interface -e ethertype "
	       "-a address -t timeout -p -d\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int ch;
	int debug = 0, promisc = 0;
	int timeout = 100; 
	bpf_u_int32 localnet=0, netmask=0;
	unsigned int error = 0;
	char *interface = NULL;
	char *proto = ETHER_TYPE_TEST;
	char in_string[MAXPROG];
	char tmp[ETHER_ADDR_LEN];
	char addr[ETHER_ADDR_LEN];
	char *user_addr = NULL;
	pcap_t *capture;
	struct bpf_program program;
	struct pcap_pkthdr *header;
	unsigned char *packet = NULL;

	while ((ch = getopt(argc, argv, "a:e:i:t:pd")) != -1) {
		switch (ch) {
		case 'a':
			user_addr = optarg;
			break;
		case 'e':
			proto = optarg;
			break;
		case 'i':
			interface = optarg;
			break;
		case 'p':
			promisc = 1;
			break;
		case 't':
			timeout = atoi(optarg);
			break;
		case 'd':
			debug = 1;
			break;
		case '?':
		default:
			usage("invalid arguments");
		}
	}
	argc -= optind;
	argv += optind;

	if (interface == NULL)
		usage("You must specify an interface");

	if (user_addr != NULL)
		ether_aton_r(user_addr, (struct ether_addr *)&tmp);

	if ((capture = pcap_open_live(interface, SNAPLEN, promisc, timeout, 
				      &errbuf[0])) == NULL)
		usage(errbuf);

	snprintf(&in_string[0], MAXPROG, "ether proto %s\n", proto);

	if (pcap_lookupnet(interface, &localnet, &netmask, errbuf) < 0)
		usage(errbuf);

	if (pcap_compile(capture, &program, in_string, 1, netmask) < 0)
		usage(errbuf);

	if (pcap_setfilter(capture, &program) < 0)
		usage(errbuf);

	if (pcap_setdirection(capture, PCAP_D_IN) < 0)
		usage(errbuf);

	while (1) {
		error = pcap_next_ex(capture, &header, 
				     (const unsigned char **)&packet);
		if (error == 0)
			continue;
		if (error == -1)
			usage("packet read error");
		if (error == -2)
			usage("savefile?  invalid!");

		if (debug) {
			printf ("got packet of %d length\n", header->len);
			printf ("header %s\n", 
				ether_ntoa((const struct ether_addr*)
					   &packet[0]));
			printf ("header %s\n", 
				ether_ntoa((const struct ether_addr*)
					   &packet[ETHER_ADDR_LEN]));
		}
		
		/*
		 * If the user did not supply an address then we simply
		 * reverse the source and destination addresses.
		 */
		if (user_addr == NULL) {
			bcopy(packet, &tmp, ETHER_ADDR_LEN);
			bcopy(&packet[ETHER_ADDR_LEN], packet, ETHER_ADDR_LEN);
			bcopy(&tmp, &packet[ETHER_ADDR_LEN], ETHER_ADDR_LEN);
		} else {
			bcopy(&tmp, packet, ETHER_ADDR_LEN);
		}
		if (pcap_inject(capture, packet, header->len) < 0)
			if (debug)
				pcap_perror(capture, "pcap_inject");
	}
}
