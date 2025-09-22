/*
 * Copyright (c) 2017 Gilles Chehade <gilles@poolp.org>
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

#include <sys/socket.h>
#include <sys/un.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

enum phase {
	PHASE_BANNER,
	PHASE_HELO,
	PHASE_MAILFROM,
	PHASE_RCPTTO,
	PHASE_DATA,
	PHASE_EOM,
	PHASE_QUIT
};

struct session {
	const char	*lhlo;
	const char	*mailfrom;
	char		*rcptto;

	char		**rcpts;
	int		n_rcpts;
};

static int lmtp_connect(const char *);
static void lmtp_engine(int, struct session *);
static void stream_file(FILE *);
	
int
main(int argc, char *argv[])
{
	int ch;
	int conn;
	const char *destination = "localhost";
	struct session	session;

	if (! geteuid())
		errx(EX_TEMPFAIL, "mail.lmtp: may not be executed as root");

	session.lhlo = "localhost";
	session.mailfrom = getenv("SENDER");
	session.rcptto = NULL;

	while ((ch = getopt(argc, argv, "d:l:f:ru")) != -1) {
		switch (ch) {
		case 'd':
			destination = optarg;
			break;
		case 'l':
			session.lhlo = optarg;
			break;
		case 'f':
			session.mailfrom = optarg;
			break;

		case 'r':
			session.rcptto = getenv("RECIPIENT");
			break;

		case 'u':
			session.rcptto = getenv("USER");
			break;

		default:
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (session.mailfrom == NULL)
		errx(EX_TEMPFAIL, "sender must be specified with -f");

	if (argc == 0 && session.rcptto == NULL)
		errx(EX_TEMPFAIL, "no recipient was specified");

	if (session.rcptto) {
		session.rcpts = &session.rcptto;
		session.n_rcpts = 1;
	}
	else {
		session.rcpts = argv;
		session.n_rcpts = argc;
	}

	conn = lmtp_connect(destination);
	lmtp_engine(conn, &session);
	
	return (0);
}

static int
lmtp_connect_inet(const char *destination)
{
	struct addrinfo hints, *res, *res0;
	char *destcopy = NULL;
	const char *hostname = NULL;
	const char *servname = NULL;
	const char *cause = NULL;
	char *p;
	int n, s = -1, save_errno;

	if ((destcopy = strdup(destination)) == NULL)
		err(EX_TEMPFAIL, NULL);

	servname = "25";
	hostname = destcopy;
	p = destcopy;
	if (*p == '[') {
		if ((p = strchr(destcopy, ']')) == NULL)
			errx(EX_TEMPFAIL, "inet: invalid address syntax");

		/* remove [ and ] */
		*p = '\0';
		hostname++;
		if (strncasecmp(hostname, "IPv6:", 5) == 0)
			hostname += 5;

		/* extract port if any */
		switch (*(p+1)) {
		case ':':
			servname = p+2;
			break;
		case '\0':
			break;
		default:
			errx(EX_TEMPFAIL, "inet: invalid address syntax");
		}
	}
	else if ((p = strchr(destcopy, ':')) != NULL) {
		*p++ = '\0';
		servname = p;
	}
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;
	n = getaddrinfo(hostname, servname, &hints, &res0);
	if (n)
		errx(EX_TEMPFAIL, "inet: %s", gai_strerror(n));

	for (res = res0; res; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (s == -1) {
			cause = "socket";
			continue;
		}

		if (connect(s, res->ai_addr, res->ai_addrlen) == -1) {
			cause = "connect";
			save_errno = errno;
			close(s);
			errno = save_errno;
			s = -1;
			continue;
		}
		break;
	}

	freeaddrinfo(res0);
	if (s == -1)
		errx(EX_TEMPFAIL, "%s", cause);

	free(destcopy);
	return s;
}

static int
lmtp_connect_unix(const char *destination)
{
	struct sockaddr_un addr;
	int s;

	if (*destination != '/')
		errx(EX_TEMPFAIL, "unix: path must be absolute");
	
	if ((s = socket(PF_LOCAL, SOCK_STREAM, 0)) == -1)
		err(EX_TEMPFAIL, NULL);

	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	if (strlcpy(addr.sun_path, destination, sizeof addr.sun_path)
	    >= sizeof addr.sun_path)
		errx(EX_TEMPFAIL, "unix: socket path is too long");

	if (connect(s, (struct sockaddr *)&addr, sizeof addr) == -1)
		err(EX_TEMPFAIL, "connect");

	return s;
}

static int
lmtp_connect(const char *destination)
{
	if (destination[0] == '/')
		return lmtp_connect_unix(destination);
	return lmtp_connect_inet(destination);
}

static void
lmtp_engine(int fd_read, struct session *session)
{
	int fd_write = 0;
	FILE *file_read = 0;
	FILE *file_write = 0;
	char *line = NULL;
	size_t linesize = 0;
	ssize_t linelen;
	enum phase phase = PHASE_BANNER;

	if ((fd_write = dup(fd_read)) == -1)
		err(EX_TEMPFAIL, "dup");

	if ((file_read = fdopen(fd_read, "r")) == NULL)
		err(EX_TEMPFAIL, "fdopen");

	if ((file_write = fdopen(fd_write, "w")) == NULL)
		err(EX_TEMPFAIL, "fdopen");

	do {
		fflush(file_write);

		if ((linelen = getline(&line, &linesize, file_read)) == -1) {
			if (ferror(file_read))
				err(EX_TEMPFAIL, "getline");
			else
				errx(EX_TEMPFAIL, "unexpected EOF from LMTP server");
		}
		line[strcspn(line, "\n")] = '\0';
		line[strcspn(line, "\r")] = '\0';

		if (linelen < 4 ||
		    !isdigit((unsigned char)line[0]) ||
		    !isdigit((unsigned char)line[1]) ||
		    !isdigit((unsigned char)line[2]) ||
		    (line[3] != ' ' && line[3] != '-'))
			errx(EX_TEMPFAIL, "LMTP server sent an invalid line");

		if (line[0] != (phase == PHASE_DATA ? '3' : '2')) {
			int code = EX_TEMPFAIL;
			if (line[0] != '4')
				code = EX_UNAVAILABLE;
			errx(code, "LMTP server error: %s", line);
		}
		
		if (line[3] == '-')
			continue;

		switch (phase) {

		case PHASE_BANNER:
			fprintf(file_write, "LHLO %s\r\n", session->lhlo);
			phase++;
			break;

		case PHASE_HELO:
			fprintf(file_write, "MAIL FROM:<%s>\r\n", session->mailfrom);
			phase++;
			break;

		case PHASE_MAILFROM:
			fprintf(file_write, "RCPT TO:<%s>\r\n", session->rcpts[session->n_rcpts - 1]);
			if (session->n_rcpts - 1 == 0) {
				phase++;
				break;
			}
			session->n_rcpts--;
			break;

		case PHASE_RCPTTO:
			fprintf(file_write, "DATA\r\n");
			phase++;
			break;

		case PHASE_DATA:
			stream_file(file_write);
			fprintf(file_write, ".\r\n");
			phase++;
			break;

		case PHASE_EOM:
			fprintf(file_write, "QUIT\r\n");
			phase++;
			break;						

		case PHASE_QUIT:
			exit(0);
		}
	} while (1);
}

static void
stream_file(FILE *conn)
{
	char *line = NULL;
	size_t linesize = 0;

	while (getline(&line, &linesize, stdin) != -1) {
		line[strcspn(line, "\n")] = '\0';
		if (line[0] == '.')
			fprintf(conn, ".");
		fprintf(conn, "%s\r\n", line);
	}
	free(line);
	if (ferror(stdin))
		err(EX_TEMPFAIL, "getline");
}
