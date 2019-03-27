/*
 * trygetea.c - test program for getether.c
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

#include "getether.h"

int debug = 0;
char *progname;

void
main(argc, argv)
	int argc;
	char **argv;
{
	u_char ea[16];				/* Ethernet address */
	int i;

	progname = argv[0];			/* for report */

	if (argc < 2) {
		printf("need interface name\n");
		exit(1);
	}
	if ((i = getether(argv[1], (char*)ea)) < 0) {
		printf("Could not get Ethernet address (rc=%d)\n", i);
		exit(1);
	}
	printf("Ether-addr");
	for (i = 0; i < 6; i++)
		printf(":%x", ea[i] & 0xFF);
	printf("\n");

	exit(0);
}
