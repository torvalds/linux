#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sndio.h>
#include "tools.h"

#define BUFSZ 0x1000

void cb(void *, int);
void usage(void);

unsigned char buf[BUFSZ];
struct sio_par par;
char *xstr[] = SIO_XSTRINGS;

long long pos = 0;
int rlat = 0;

void
cb(void *addr, int delta)
{
	pos += delta;
	rlat += delta;
	fprintf(stderr,
	    "cb: delta = %+7d, rlat = %+7d, pos = %+7lld\n",
	    delta, rlat, pos);
}

void
usage(void) {
	fprintf(stderr, "usage: rec [-r rate] [-c nchan] [-e enc]\n");
}
 
int
main(int argc, char **argv) {
	int ch;
	struct sio_hdl *hdl;
	ssize_t n;
	
	/*
	 * defaults parameters
	 */
	sio_initpar(&par);
	par.sig = 1;
	par.bits = 16;
	par.rchan = 2;
	par.rate = 44100;

	while ((ch = getopt(argc, argv, "r:c:e:b:x:")) != -1) {
		switch(ch) {
		case 'r':
			if (sscanf(optarg, "%u", &par.rate) != 1) {
				fprintf(stderr, "%s: bad rate\n", optarg);
				exit(1);
			}
			break;
		case 'c':
			if (sscanf(optarg, "%u", &par.rchan) != 1) {
				fprintf(stderr, "%s: channels number\n", optarg);
				exit(1);
			}
			break;
		case 'e':
			if (!sio_strtoenc(&par, optarg)) {
				fprintf(stderr, "%s: unknown encoding\n", optarg);
				exit(1);
			}
			break;
		case 'x':
			for (par.xrun = 0;; par.xrun++) {
				if (par.xrun == sizeof(xstr) / sizeof(char *)) {
					fprintf(stderr, 
					    "%s: bad xrun mode\n", optarg);
					exit(1);
				}
				if (strcmp(xstr[par.xrun], optarg) == 0)
					break;
			}
			break;			
		default:
			usage();
			exit(1);
			break;
		}
	}

	hdl = sio_open(SIO_DEVANY, SIO_REC, 0);
	if (hdl == NULL) {
		fprintf(stderr, "sio_open() failed\n");
		exit(1);
	}
	sio_onmove(hdl, cb, NULL);
	if (!sio_setpar(hdl, &par)) {
		fprintf(stderr, "sio_setpar() failed\n");
		exit(1);
	}
	if (!sio_getpar(hdl, &par)) {
		fprintf(stderr, "sio_getpar() failed\n");
		exit(1);
	}
	if (!sio_start(hdl)) {
		fprintf(stderr, "sio_start() failed\n");
		exit(1);
	}
	for (;;) {
		n = sio_read(hdl, buf, BUFSZ);
		if (n == 0) {
			fprintf(stderr, "sio_write: failed\n");
			exit(1);
		}
		rlat -= n / (int)(par.bps * par.rchan);
		if (write(STDOUT_FILENO, buf, n) < 0) {
			perror("stdout");
			exit(1);
		}
	}
	sio_close(hdl);
	return 0;
}
