/*	$OpenBSD: vis_test.c,v 1.5 2017/07/27 15:08:37 bluhm Exp $	*/

/* Public domain. 2005, Otto Moerbeek */

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vis.h>

#define NTESTS	8000
#define NCH	800

char ibuf[NCH];
char obuf[NCH * 4];
char rbuf[NCH * 4];

int flags[] = {
	VIS_ALL,
	VIS_GLOB,
	VIS_TAB,
	VIS_NL,
	VIS_DQ,
	VIS_WHITE,
	VIS_SAFE
};

char *flagname[] = {
	"VIS_ALL",
	"VIS_GLOB",
	"VIS_TAB",
	"VIS_NL",
	"VIS_DQ",
	"VIS_WHITE",
	"VIS_SAFE"
};

int title;

void
dotitle(int i, int j)
{
	if (title == 0)
		printf("%d %s:", i, flagname[j]);
	title = 1;
}

int
main(int argc, char *argv[])
{

	char inp[UCHAR_MAX + 1];
	char out[4 * UCHAR_MAX + 1];
	int i, j, fail = 0;
	ssize_t owant, o, r;

	for (i = 0; i <= UCHAR_MAX; i++) {
		inp[i] = i;
	}
	strvisx(out, inp, UCHAR_MAX + 1, 0);
	printf("%s\n", out);

	for (i = 0; i < NTESTS; i++) {
		arc4random_buf(ibuf, sizeof(ibuf) - 1);
		ibuf[sizeof(ibuf) - 1] = '\0';
		title = 0;

		for (j = 0; j < sizeof(flags)/sizeof(flags[0]); j++) {
			owant = sizeof(ibuf);
			o = strnvis(obuf, ibuf, owant, flags[j]);
			if (o >= owant) {
				owant = o + 1;
				o = strnvis(obuf, ibuf, owant, flags[j]);
				if (o > owant) {
					dotitle(i, j);
					printf("HUGE overflow\n");
				}
				if (o < owant - 1) {
					dotitle(i, j);
					printf("over-estimate of overflow\n");
				}
			} else if (o > strlen(ibuf) * 4) {
				dotitle(i, j);
				printf("wants too much %zd %zu\n",
				    o, strlen(ibuf) * 4);
				continue;
			}

			r = strnunvis(rbuf, obuf, sizeof rbuf);

			if (r == -1) {
				dotitle(i, j);
				printf("cannot decode\n");
				printf("%s\n", obuf);
				fail = 1;
			} else if (r != strlen(ibuf)) {
				dotitle(i, j);
				printf("rlen %zd != inlen %zu\n",
				    r, strlen(ibuf));
				printf("%s\n", obuf);
				printf("%s\n", rbuf);
				fail = 1;
			} else if (bcmp(ibuf, rbuf, r)) {
				dotitle(i, j);
				printf("strings are different\n");
				printf("%s\n", ibuf);
				printf("%s\n", rbuf);
				fail = 1;
			}
		}
	}
	exit(fail);
}
