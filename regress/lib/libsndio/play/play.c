#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sndio.h>
#include "tools.h"

#define BUFSZ 0x100

void cb(void *, int);
void usage(void);

unsigned char buf[BUFSZ];
struct sio_par par;
char *xstr[] = SIO_XSTRINGS;

long long realpos = 0, playpos = 0;

void
cb(void *addr, int delta)
{
	int bytes = delta * (int)(par.bps * par.pchan);

	realpos += bytes;

	fprintf(stderr,
	    "cb: bytes = %+7d, latency = %+7lld, "
	    "realpos = %+7lld, bufused = %+7lld\n",
	    bytes, playpos - realpos,
	    realpos, (realpos < 0) ? playpos : playpos - realpos);
}

void
usage(void) {
	fprintf(stderr, "usage: play [-r rate] [-c nchan] [-e enc]\n");
}
 
int
main(int argc, char **argv) {
	int ch;
	struct sio_hdl *hdl;
	ssize_t n, len;
	
	/*
	 * defaults parameters
	 */
	sio_initpar(&par);
	par.sig = 1;
	par.bits = 16;
	par.pchan = 2;
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
			if (sscanf(optarg, "%u", &par.pchan) != 1) {
				fprintf(stderr, "%s: bad channels\n", optarg);
				exit(1);
			}
			break;
		case 'e':
			if (!sio_strtoenc(&par, optarg)) {
				fprintf(stderr, "%s: bad encoding\n", optarg);
				exit(1);
			}
			break;
		case 'b':
			if (sscanf(optarg, "%u", &par.appbufsz) != 1) {
				fprintf(stderr, "%s: bad buf size\n", optarg);
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

	hdl = sio_open(SIO_DEVANY, SIO_PLAY, 0);
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
	fprintf(stderr, "using %u bytes per buffer, rounding to %u\n",
	    par.bufsz * par.bps * par.pchan,
	    par.round * par.bps * par.pchan);
	for (;;) {
		len = read(STDIN_FILENO, buf, BUFSZ);
		if (len < 0) {
			perror("stdin");
			exit(1);
		}
		if (len == 0)
			break;
		n = sio_write(hdl, buf, len);
		if (n == 0) {
			fprintf(stderr, "sio_write: failed\n");
			exit(1);
		}
		playpos += n;
	}
	sio_close(hdl);
	return 0;
}
