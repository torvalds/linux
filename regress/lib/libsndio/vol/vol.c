#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sndio.h>
#include "tools.h"

#define BUFSZ 0x100

void usage(void);
void onvol(void *, unsigned);

unsigned char buf[BUFSZ];
struct sio_par par;
unsigned vol = 0xdeadbeef;

void
usage(void) {
	fprintf(stderr, "usage: vol [-r rate] [-c nchan] [-e enc]\n");
}

void
onvol(void *addr, unsigned val)
{
	vol = val;
	fprintf(stderr, "volume set to %u\n", vol);
}
 
int
main(int argc, char **argv) {
	int ch;
	int tty;
	struct sio_hdl *hdl;
	struct termios tio;
	struct pollfd pfd[2];
	char cmd;
	ssize_t n, len;
	
	/*
	 * defaults parameters
	 */
	sio_initpar(&par);
	par.sig = 1;
	par.bits = 16;
	par.pchan = 2;
	par.rate = 44100;

	if (isatty(STDIN_FILENO)) {
		fprintf(stderr, "stdin can't be a tty\n");
		exit(1);
	}
	tty = open("/dev/tty", O_RDWR);
	if (tty < 0) {
		perror("/dev/tty");
		exit(1);
	}
	if (tcgetattr(tty, &tio) < 0) {
		perror("tcsetattr");
		exit(1);
	}
	tio.c_lflag &= ~ICANON;
	tio.c_lflag &= ~ECHO;
	tio.c_cc[VMIN] = 1;
	tio.c_cc[VTIME] = 0;
	if (tcsetattr(tty, TCSAFLUSH, &tio) < 0) {
		perror("tcsetattr");
		exit(1);
	}

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
	if (!sio_setpar(hdl, &par)) {
		fprintf(stderr, "sio_setpar() failed\n");
		exit(1);
	}
	if (!sio_onvol(hdl, onvol, NULL))
		fprintf(stderr, "warning: no volume knob on this device\n");
	fprintf(stderr, "use ``+'' and ``-'' to adjust the volume\n");
	if (!sio_start(hdl)) {
		fprintf(stderr, "sio_start() failed\n");
		exit(1);
	}
	for (;;) {
		pfd[0].fd = tty;
		pfd[0].events = POLLIN;
		sio_pollfd(hdl, &pfd[1], POLLOUT);
		if (poll(pfd, 2, -1) < 0) {
			perror("poll");
			exit(1);
		}
		if (pfd[0].revents & POLLIN) {
			if (read(tty, &cmd, 1) < 0) {
				perror("read(tty)");
				exit(1);
			}
			switch (cmd) {
			case '+':
				if (vol < SIO_MAXVOL) {
					vol++;
					sio_setvol(hdl, vol);
				}
				break;
			case '-':
				if (vol > 0) {
					vol--;
					sio_setvol(hdl, vol);
				}
				break;
			}
		}
		if (sio_revents(hdl, &pfd[1]) & POLLOUT) {
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
		}
	}
	sio_close(hdl);
	return 0;
}
