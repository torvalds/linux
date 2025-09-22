/* $OpenBSD: syslogc.c,v 1.19 2021/11/15 15:14:24 millert Exp $ */

/*
 * Copyright (c) 2004 Damien Miller
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_CTLSOCK		"/var/run/syslogd.sock"

#define MAX_MEMBUF_NAME	64	/* Max length of membuf log name */

/*
 * Client protocol NB. all numeric fields in network byte order
 */
#define CTL_VERSION		2

/* Request */
struct ctl_cmd {
	u_int32_t	version;
#define CMD_READ	1	/* Read out log */
#define CMD_READ_CLEAR	2	/* Read and clear log */
#define CMD_CLEAR	3	/* Clear log */
#define CMD_LIST	4	/* List available logs */
#define CMD_FLAGS	5	/* Query flags only */
#define CMD_READ_CONT	6	/* Read out log continuously */
	u_int32_t	cmd;
	u_int32_t	lines;
	char		logname[MAX_MEMBUF_NAME];
};

/* Reply */
struct ctl_reply_hdr {
	u_int32_t	version;
#define CTL_HDR_FLAG_OVERFLOW	0x01
	u_int32_t	flags;
	/* Reply text follows, up to MAX_MEMBUF long */
};

static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
	    "usage: %s [-Ccfo] [-n lines] [-s reporting_socket] logname\n"
	    "       %s -q\n", __progname, __progname);
	exit(1);
}

int
main(int argc, char **argv)
{
	const char *ctlsock_path;
	char buf[8192];
	struct sockaddr_un ctl;
	int ctlsock, ch, oflag, rval;
	FILE *ctlf;
	struct ctl_cmd cc;
	struct ctl_reply_hdr rr;
	const char *errstr;

	memset(&cc, '\0', sizeof(cc));

	ctlsock_path = DEFAULT_CTLSOCK;
	rval = oflag = 0;
	while ((ch = getopt(argc, argv, "Ccfhon:qs:")) != -1) {
		switch (ch) {
		case 'C':
			cc.cmd = CMD_CLEAR;
			break;
		case 'c':
			cc.cmd = CMD_READ_CLEAR;
			break;
		case 'h':
			usage();
			break;
		case 'f':
			cc.cmd = CMD_READ_CONT;
			break;
		case 'n':
			cc.lines = strtonum(optarg, 1, UINT32_MAX, &errstr);
			if (errstr)
				errx(1, "number of lines is %s: %s",
				    errstr, optarg);
			break;
		case 'o':
			cc.cmd = CMD_FLAGS;
			oflag = 1;
			break;
		case 'q':
			cc.cmd = CMD_LIST;
			break;
		case 's':
			ctlsock_path = optarg;
			break;
		default:
			usage();
			break;
		}
	}

	if (cc.cmd == 0)
		cc.cmd = CMD_READ;

	if ((cc.cmd != CMD_LIST && optind != argc - 1) ||
	    (cc.cmd == CMD_LIST && optind != argc))
		usage();

	if (cc.cmd != CMD_LIST) {
		if (strlcpy(cc.logname, argv[optind], sizeof(cc.logname)) >=
		    sizeof(cc.logname))
			errx(1, "Specified log name is too long");
	}

	memset(&ctl, '\0', sizeof(ctl));
	strlcpy(ctl.sun_path, ctlsock_path, sizeof(ctl.sun_path));
	ctl.sun_family = AF_UNIX;

	if ((ctlsock = socket(PF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");
	if (connect(ctlsock, (struct sockaddr *)&ctl, sizeof(ctl)) == -1)
		err(1, "connect: %s", ctl.sun_path);
	if ((ctlf = fdopen(ctlsock, "r+")) == NULL)
		err(1, "fdopen");

	if (pledge("stdio", NULL) == -1)
		err(1, "stdio");

	cc.version = htonl(CTL_VERSION);
	cc.cmd = htonl(cc.cmd);
	/* Send command */
	if (fwrite(&cc, sizeof(cc), 1, ctlf) != 1)
		err(1, "fwrite");

	fflush(ctlf);
	setvbuf(ctlf, NULL, _IOLBF, 0);
	setvbuf(stdout, NULL, _IOLBF, 0);

	/* Fetch header */
	if (fread(&rr, sizeof(rr), 1, ctlf) != 1)
		err(1, "fread header");

	if (ntohl(rr.version) != CTL_VERSION)
		errx(1, "unsupported syslogd version");

	/* Write out reply */
	while ((fgets(buf, sizeof(buf), ctlf)) != NULL) {
		if (!strcmp(buf, "<ENOBUFS>\n"))
			fprintf(stderr, "syslogc [%s]: Lines were dropped!\n",
			    cc.logname);
		else
			fputs(buf, stdout);
	}

	if (oflag && (ntohl(rr.flags) & CTL_HDR_FLAG_OVERFLOW)) {
		printf("%s has overflowed\n", cc.logname);
		rval = 1;
	}

	fclose(ctlf);
	close(ctlsock);

	exit(rval);
}
