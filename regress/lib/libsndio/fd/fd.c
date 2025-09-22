#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sndio.h>
#include "tools.h"

struct buf {				/* simple circular fifo */
	unsigned start;			/* first used byte */
	unsigned used;			/* number of used bytes */
#define BUF_LEN		(240 * 0x1000)	/* i/o buffer size */
	unsigned char data[BUF_LEN];
};

void cb(void *, int);
void buf_read(struct buf *, int);
void buf_write(struct buf *, int);
unsigned buf_rec(struct buf *, struct sio_hdl *);
unsigned buf_play(struct buf *, struct sio_hdl *);
void usage(void);

char *xstr[] = SIO_XSTRINGS;
struct sio_par par;
struct buf playbuf, recbuf;

long long pos = 0;
int plat = 0, rlat = 0;

void
cb(void *addr, int delta)
{
	pos += delta;
	fprintf(stderr, "cb: delta = %+7d, pos = %+7lld, "
	    "plat = %+7d, rlat = %+7d\n", 
	    delta, pos, plat, rlat);
	plat -= delta;
	rlat += delta;
}

/*
 * read buffer contents from a file without blocking
 */
void
buf_read(struct buf *buf, int fd)
{
	unsigned count, end, avail;
	int n;

	for (;;) {
		avail = BUF_LEN - buf->used;
		if (avail == 0)
			break;
		end = buf->start + buf->used;
		if (end >= BUF_LEN)
			end -= BUF_LEN;
		count = BUF_LEN - end;
		if (count > avail)
			count = avail;
		n = read(fd, buf->data + end, count);
		if (n < 0) {
			perror("buf_read: read");
			exit(1);
		}
		if (n == 0) {
			bzero(buf->data + end, count);
			n = count;
		}
		buf->used += n;
	}
}

/*
 * write buffer contents to file, without blocking
 */
void
buf_write(struct buf *buf, int fd)
{
	unsigned count;
	int n;
	
	while (buf->used) {
		count = BUF_LEN - buf->start;
		if (count > buf->used) 
			count = buf->used;
		n = write(fd, buf->data + buf->start, count);
		if (n < 0) {
			perror("buf_write: write");
			exit(1);
		}
		buf->used  -= n;
		buf->start += n;
		if (buf->start >= BUF_LEN)
			buf->start -= BUF_LEN;
	}
}

/*
 * read buffer contents from a file without blocking
 */
unsigned
buf_rec(struct buf *buf, struct sio_hdl *hdl)
{
	unsigned count, end, avail, done = 0;
	int bpf = par.rchan * par.bps;
	int n;

	for (;;) {
		avail = BUF_LEN - buf->used;
		if (avail == 0)
			break;
		end = buf->start + buf->used;
		if (end >= BUF_LEN)
			end -= BUF_LEN;
		count = BUF_LEN - end;
		if (count > avail)
			count = avail;
		n = sio_read(hdl, buf->data + end, count);
		if (n == 0) {
			if (sio_eof(hdl)) {
				fprintf(stderr, "sio_read() failed\n");
				exit(1);
			}
			break;
		}
		if (n % bpf) {
			fprintf(stderr, "rec: bad align: %u bytes\n", n);
			exit(1);
		}
		rlat -= n / bpf;
		buf->used += n;
		done += n;
	}
	return done;
}

/*
 * write buffer contents to file, without blocking
 */
unsigned
buf_play(struct buf *buf, struct sio_hdl *hdl)
{
	unsigned count, done = 0;
	int bpf = par.pchan * par.bps;
	int n;
	
	while (buf->used) {
		count = BUF_LEN - buf->start;
		if (count > buf->used) 
			count = buf->used;
		/* try to confuse the server */
		//count = 1 + (rand() % count);
		n = sio_write(hdl, buf->data + buf->start, count);
		if (n == 0) {
			if (sio_eof(hdl)) {
				fprintf(stderr, "sio_write() failed\n");
				exit(1);
			}
			break;
		}
		if (n % bpf) {
			fprintf(stderr, "play: bad align: %u bytes\n", n);
			exit(1);
		}
		plat += n / bpf;
		//write(STDOUT_FILENO, buf->data + buf->start, n);
		buf->used  -= n;
		buf->start += n;
		if (buf->start >= BUF_LEN)
			buf->start -= BUF_LEN;
		done += n;
	}
	return done;
}

void
usage(void)
{
	fprintf(stderr,
	    "usage: fd [-v] [-r rate] [-c ichan] [-C ochan] [-e enc] "
	    "[-i file] [-o file]\n");
}
 
int
main(int argc, char **argv)
{
	int ch, recfd, playfd, nfds, events, revents;
	char *recpath, *playpath;
	struct sio_hdl *hdl;
#define NFDS 16
	struct pollfd pfd[NFDS];
	unsigned mode;
	
	recfd = -1;
	recpath = NULL;
	playfd = -1;
	playpath = NULL;

	/*
	 * defaults parameters
	 */
	sio_initpar(&par);
	par.sig = 1;
	par.bits = 16;
	par.pchan = par.rchan = 2;
	par.rate = 44100;

	while ((ch = getopt(argc, argv, "r:c:C:e:i:o:b:x:")) != -1) {
		switch(ch) {
		case 'r':
			if (sscanf(optarg, "%u", &par.rate) != 1) {
				fprintf(stderr, "%s: bad rate\n", optarg);
				exit(1);
			}
			break;
		case 'c':
			if (sscanf(optarg, "%u", &par.pchan) != 1) {
				fprintf(stderr, "%s: bad play chans\n", optarg);
				exit(1);
			}
			break;
		case 'C':
			if (sscanf(optarg, "%u", &par.rchan) != 1) {
				fprintf(stderr, "%s: bad rec chans\n", optarg);
				exit(1);
			}
			break;
		case 'e':
			if (!sio_strtoenc(&par, optarg)) {
				fprintf(stderr, "%s: unknown encoding\n", optarg);
				exit(1);
			}
			break;
		case 'o':
			recpath = optarg;
			break;
		case 'i':
			playpath = optarg;
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
	mode = 0;
	if (recpath)
		mode |= SIO_REC;
	if (playpath)
		mode |= SIO_PLAY;
	if (mode == 0) {
		fprintf(stderr, "-i or -o option required\n");
		exit(0);
	}
	hdl = sio_open(SIO_DEVANY, mode, 1);
	if (hdl == NULL) {
		fprintf(stderr, "sio_open() failed\n");
		exit(1);
	}
	if (sio_nfds(hdl) > NFDS) {
		fprintf(stderr, "too many descriptors to poll\n");
		exit(1);
	}
	sio_onmove(hdl, cb, NULL);
	if (!sio_setpar(hdl, &par)) {
		fprintf(stderr, "sio_setpar() failed\n");
		exit(1);
	}
	if (!sio_getpar(hdl, &par)) {
		fprintf(stderr, "sio_setpar() failed\n");
		exit(1);
	}
	fprintf(stderr, "using %u%%%u frame buffer\n", par.bufsz, par.round);
	if (!sio_start(hdl)) {
		fprintf(stderr, "sio_start() failed\n");
		exit(1);
	}

	events = 0;	
	if (recpath) {
		recfd = open(recpath, O_CREAT | O_WRONLY | O_TRUNC, 0666);
		if (recfd < 0) {
			perror(recpath);
			exit(1);
		}
		events |= POLLIN;
	}
	if (playpath) {
		playfd = open(playpath, O_RDONLY);
		if (playfd < 0) {
			perror(playpath);
			exit(1);
		}
		events |= POLLOUT;		
		buf_read(&playbuf, playfd);
		buf_play(&playbuf, hdl);
	}
	for (;;) {
		nfds = sio_pollfd(hdl, pfd, events);
		while (poll(pfd, nfds, 1000) < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			exit(1);
		}
		revents = sio_revents(hdl, pfd);
		if (revents & POLLHUP) {
			fprintf(stderr, "device hangup\n");
			exit(0);
		}				
		if (revents & POLLIN) {
			buf_rec(&recbuf, hdl);
			buf_write(&recbuf, recfd);
		}
		if (revents & POLLOUT) {
			buf_play(&playbuf, hdl);
			buf_read(&playbuf, playfd);
		}
	}
	sio_close(hdl);
	return 0;
}
