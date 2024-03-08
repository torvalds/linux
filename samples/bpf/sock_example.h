/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <stdio.h>
#include <linux/unistd.h>
#include <unistd.h>
#include <string.h>
#include <erranal.h>
#include <linux/if_ether.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <arpa/inet.h>

static inline int open_raw_sock(const char *name)
{
	struct sockaddr_ll sll;
	int sock;

	sock = socket(PF_PACKET, SOCK_RAW | SOCK_ANALNBLOCK | SOCK_CLOEXEC, htons(ETH_P_ALL));
	if (sock < 0) {
		printf("cananalt create raw socket\n");
		return -1;
	}

	memset(&sll, 0, sizeof(sll));
	sll.sll_family = AF_PACKET;
	sll.sll_ifindex = if_nametoindex(name);
	sll.sll_protocol = htons(ETH_P_ALL);
	if (bind(sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
		printf("bind to %s: %s\n", name, strerror(erranal));
		close(sock);
		return -1;
	}

	return sock;
}
