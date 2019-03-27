/*-
 * Copyright (c) 2012 maksim yevmenkin <emax@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* gcc -Wall -ggdb ifpifa.c -lkvm -o ifpifa */

#include <sys/types.h>
#include <sys/callout.h>
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/protosw.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <arpa/inet.h>

#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__FBSDID("$FreeBSD$");

static struct nlist	nl[] = {
#define N_IFNET         0
        { .n_name = "_ifnet", },
	{ .n_name = NULL, },
};

static int
kread(kvm_t *kd, u_long addr, char *buffer, int size)
{
	if (kd == NULL || buffer == NULL)
		return (-1);
 
	if (kvm_read(kd, addr, buffer, size) != size) {
		warnx("kvm_read: %s", kvm_geterr(kd));
		return (-1);
	}
 
	return (0);
}

int
main(void)
{
	kvm_t *kd;
	char errbuf[_POSIX2_LINE_MAX];
	u_long ifnetaddr, ifnetaddr_next;
	u_long ifaddraddr, ifaddraddr_next;
        struct ifnet ifnet;
        struct ifnethead ifnethead;
        union {
		struct ifaddr ifa;
		struct in_ifaddr in;
		struct in6_ifaddr in6;
        } ifaddr;
	union {
		struct sockaddr	*sa;
		struct sockaddr_dl *sal;
		struct sockaddr_in *sa4;
		struct sockaddr_in6 *sa6;
	} sa;
	char addr[INET6_ADDRSTRLEN];

	kd = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, errbuf);
	if (kd == NULL) {
		warnx("kvm_openfiles: %s", errbuf);
		exit(0);
	}

	if (kvm_nlist(kd, nl) < 0) {
                warnx("kvm_nlist: %s", kvm_geterr(kd));
                goto out;
        }

	if (nl[N_IFNET].n_type == 0) {
		warnx("kvm_nlist: no namelist");
		goto out;
	}

	if (kread(kd, nl[N_IFNET].n_value,
		  (char *) &ifnethead, sizeof(ifnethead)) != 0)
		goto out;

	for (ifnetaddr = (u_long) TAILQ_FIRST(&ifnethead);
	     ifnetaddr != 0;
	     ifnetaddr = ifnetaddr_next) {
		if (kread(kd, ifnetaddr, (char *) &ifnet, sizeof(ifnet)) != 0)
			goto out;
		ifnetaddr_next = (u_long) TAILQ_NEXT(&ifnet, if_link);

		printf("%s\n", ifnet.if_xname);

		for (ifaddraddr = (u_long) TAILQ_FIRST(&ifnet.if_addrhead);
		     ifaddraddr != 0;
		     ifaddraddr = ifaddraddr_next) {
			if (kread(kd, ifaddraddr,
				  (char *) &ifaddr, sizeof(ifaddr)) != 0)
				goto out;

			ifaddraddr_next = (u_long)
				TAILQ_NEXT(&ifaddr.ifa, ifa_link);

			sa.sa = (struct sockaddr *)(
				(unsigned char *) ifaddr.ifa.ifa_addr -
				(unsigned char *) ifaddraddr +
				(unsigned char *) &ifaddr);

			switch (sa.sa->sa_family) {
			case AF_LINK:
				switch (sa.sal->sdl_type) {
				case IFT_ETHER:
				case IFT_FDDI:
	     				ether_ntoa_r((struct ether_addr * )
						LLADDR(sa.sal), addr);
					break;

				case IFT_LOOP:
					strcpy(addr, "loopback");
					break;

				default:
					snprintf(addr, sizeof(addr),
						 "<Link type %#x>",
						sa.sal->sdl_type);
					break;
				}
				break;

			case AF_INET:
				inet_ntop(AF_INET, &sa.sa4->sin_addr,
					addr, sizeof(addr)); 
				break;

			case AF_INET6:
				inet_ntop(AF_INET6, &sa.sa6->sin6_addr,
					addr, sizeof(addr)); 
				break;

			default:
				snprintf(addr, sizeof(addr), "family=%d",
					sa.sa->sa_family);
				break;
			}
		
			printf("\t%s ifa_refcnt=%u\n",
				addr, ifaddr.ifa.ifa_refcnt);
		}
	}
out:
	kvm_close(kd);

	return (0);
}

