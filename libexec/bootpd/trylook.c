/*
 * trylook.c - test program for lookup.c
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>

#include "report.h"
#include "lookup.h"

extern char *ether_ntoa();
extern char *inet_ntoa();

int debug = 0;
char *progname;

void
main(argc, argv)
	int argc;
	char **argv;
{
	int i;
	struct in_addr in;
	char *a;
	u_char *hwa;

	progname = argv[0];			/* for report */

	for (i = 1; i < argc; i++) {

		/* Host name */
		printf("%s:", argv[i]);

		/* IP addr */
		if (lookup_ipa(argv[i], &in.s_addr))
			a = "?";
		else
			a = inet_ntoa(in);
		printf(" ipa=%s", a);

		/* Ether addr */
		printf(" hwa=");
		hwa = lookup_hwa(argv[i], 1);
		if (!hwa)
			printf("?\n");
		else {
			int i;
			for (i = 0; i < 6; i++)
				printf(":%x", hwa[i] & 0xFF);
			putchar('\n');
		}

	}
	exit(0);
}
