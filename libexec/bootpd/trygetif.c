/*
 * trygetif.c - test program for getif.c
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/socket.h>

#if defined(SUNOS) || defined(SVR4)
#include <sys/sockio.h>
#endif

#ifdef _AIX32
#include <sys/time.h>	/* for struct timeval in net/if.h */
#endif
#include <net/if.h>				/* for struct ifreq */
#include <netinet/in.h>
#include <arpa/inet.h>			/* inet_ntoa */

#include <netdb.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#include "getif.h"

int debug = 0;
char *progname;

void
main(argc, argv)
	int argc;
	char **argv;
{
	struct hostent *hep;
	struct sockaddr_in *sip;	/* Interface address */
	struct ifreq *ifr;
	struct in_addr dst_addr;
	struct in_addr *dap;
	int s;

	progname = argv[0];			/* for report */

	dap = NULL;
	if (argc > 1) {
		dap = &dst_addr;
		if (isdigit(argv[1][0]))
			dst_addr.s_addr = inet_addr(argv[1]);
		else {
			hep = gethostbyname(argv[1]);
			if (!hep) {
				printf("gethostbyname(%s)\n", argv[1]);
				exit(1);
			}
			memcpy(&dst_addr, hep->h_addr, sizeof(dst_addr));
		}
	}
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		perror("socket open");
		exit(1);
	}
	ifr = getif(s, dap);
	if (!ifr) {
		printf("no interface for address\n");
		exit(1);
	}
	printf("Intf-name:%s\n", ifr->ifr_name);
	sip = (struct sockaddr_in *) &(ifr->ifr_addr);
	printf("Intf-addr:%s\n", inet_ntoa(sip->sin_addr));

	exit(0);
}
