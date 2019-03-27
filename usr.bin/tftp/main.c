/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#if 0
#ifndef lint
static char sccsid[] = "@(#)main.c	8.1 (Berkeley) 6/6/93";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Many bug fixes are from Jim Guyton <guyton@rand-unix> */

/*
 * TFTP User Program -- Command Interface.
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/tftp.h>

#include <ctype.h>
#include <err.h>
#include <histedit.h>
#include <netdb.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tftp-utils.h"
#include "tftp-io.h"
#include "tftp-options.h"
#include "tftp.h"

#define	MAXLINE		(2 * MAXPATHLEN)
#define	TIMEOUT		5		/* secs between rexmt's */

typedef struct	sockaddr_storage peeraddr;
static int	connected;
static char	mode[32];
static jmp_buf	toplevel;
volatile int	txrx_error;
static int	peer;

#define	MAX_MARGV	20
static int	margc;
static char	*margv[MAX_MARGV];

int		verbose;
static char	*port = NULL;

static void	get(int, char **);
static void	help(int, char **);
static void	intr(int);
static void	modecmd(int, char **);
static void	put(int, char **);
static void	quit(int, char **);
static void	setascii(int, char **);
static void	setbinary(int, char **);
static void	setpeer0(char *, const char *);
static void	setpeer(int, char **);
static void	settimeoutpacket(int, char **);
static void	settimeoutnetwork(int, char **);
static void	setdebug(int, char **);
static void	setverbose(int, char **);
static void	showstatus(int, char **);
static void	setblocksize(int, char **);
static void	setblocksize2(int, char **);
static void	setoptions(int, char **);
static void	setrollover(int, char **);
static void	setpacketdrop(int, char **);

static void command(bool, EditLine *, History *, HistEvent *) __dead2;
static const char *command_prompt(void);

static void urihandling(char *URI);
static void getusage(char *);
static void makeargv(char *line);
static void putusage(char *);
static void settftpmode(const char *);

static char	*tail(char *);
static struct	cmd *getcmd(char *);

#define HELPINDENT (sizeof("connect"))

struct cmd {
	const char	*name;
	void	(*handler)(int, char **);
	const char	*help;
};

static struct cmd cmdtab[] = {
	{ "connect",	setpeer,	"connect to remote tftp"	},
	{ "mode",	modecmd,	"set file transfer mode"	},
	{ "put",	put,		"send file"			},
	{ "get",	get,		"receive file"			},
	{ "quit",	quit,		"exit tftp"			},
	{ "verbose",	setverbose,	"toggle verbose mode"		},
	{ "status",	showstatus,	"show current status"		},
	{ "binary",     setbinary,	"set mode to octet"		},
	{ "ascii",      setascii,	"set mode to netascii"		},
	{ "rexmt",	settimeoutpacket,
	  "set per-packet retransmission timeout[-]" },
	{ "timeout",	settimeoutnetwork,
	  "set total retransmission timeout" },
	{ "trace",	setdebug,	"enable 'debug packet'[-]"	},
	{ "debug",	setdebug,	"enable verbose output"		},
	{ "blocksize",	setblocksize,	"set blocksize[*]"		},
	{ "blocksize2",	setblocksize2,	"set blocksize as a power of 2[**]" },
	{ "rollover",	setrollover,	"rollover after 64K packets[**]" },
	{ "options",	setoptions,
	  "enable or disable RFC2347 style options" },
	{ "help",	help,		"print help information"	},
	{ "packetdrop",	setpacketdrop,	"artificial packetloss feature"	},
	{ "?",		help,		"print help information"	},
	{ NULL,		NULL,		NULL				}
};

static struct	modes {
	const char *m_name;
	const char *m_mode;
} modes[] = {
	{ "ascii",	"netascii" },
	{ "netascii",	"netascii" },
	{ "binary",	"octet" },
	{ "image",	"octet" },
	{ "octet",	"octet" },
	{ NULL,		NULL }
};

int
main(int argc, char *argv[])
{
	HistEvent he;
	static EditLine *el;
	static History *hist;
	bool interactive;

	acting_as_client = 1;
	peer = -1;
	strcpy(mode, "octet");
	signal(SIGINT, intr);

	interactive = isatty(STDIN_FILENO);
	if (interactive) {
		el = el_init("tftp", stdin, stdout, stderr);
		hist = history_init();
		history(hist, &he, H_SETSIZE, 100);
		el_set(el, EL_HIST, history, hist);
		el_set(el, EL_EDITOR, "emacs");
		el_set(el, EL_PROMPT, command_prompt);
		el_set(el, EL_SIGNAL, 1);
		el_source(el, NULL);
	}

	if (argc > 1) {
		if (setjmp(toplevel) != 0)
			exit(txrx_error);

		if (strncmp(argv[1], "tftp://", 7) == 0) {
			urihandling(argv[1]);
			exit(txrx_error);
		}

		setpeer(argc, argv);
	}

	if (setjmp(toplevel) != 0) {
		if (interactive)
			el_reset(el);
		(void)putchar('\n');
	}

	init_options();
	command(interactive, el, hist, &he);
}

/*
 * RFC3617 handling of TFTP URIs:
 *
 *    tftpURI         = "tftp://" host "/" file [ mode ]
 *    mode            = ";"  "mode=" ( "netascii" / "octet" )
 *    file            = *( unreserved / escaped )
 *    host            = <as specified by RFC 2732>
 *    unreserved      = <as specified in RFC 2396>
 *    escaped         = <as specified in RFC 2396>
 *
 * We are cheating a little bit by allowing any mode as specified in the
 * modes table defined earlier on in this file and mapping it on the real
 * mode.
 */
static void
urihandling(char *URI)
{
	char	uri[ARG_MAX];
	char	*host = NULL;
	char	*path = NULL;
	char	*opts = NULL;
	const char *tmode = "octet";
	char	*s;
	char	line[MAXLINE];
	int	i;

	strlcpy(uri, URI, ARG_MAX);
	host = uri + 7;

	if ((s = strchr(host, '/')) == NULL) {
		fprintf(stderr,
		    "Invalid URI: Couldn't find / after hostname\n");
		exit(1);
	}
	*s = '\0';
	path = s + 1;

	if ((s = strchr(path, ';')) != NULL) {
		*s = '\0';
		opts = s + 1;

		if (strncmp(opts, "mode=", 5) == 0) {
			tmode = opts;
			tmode += 5;

			for (i = 0; modes[i].m_name != NULL; i++) {
				if (strcmp(modes[i].m_name, tmode) == 0)
					break;
			}
			if (modes[i].m_name == NULL) {
				fprintf(stderr, "Invalid mode: '%s'\n", mode);
				exit(1);
			}
			settftpmode(modes[i].m_mode);
		}
	} else {
		settftpmode("octet");
	}

	setpeer0(host, NULL);

	sprintf(line, "get %s", path);
	makeargv(line);
	get(margc, margv);
}

static char    hostname[MAXHOSTNAMELEN];

static void
setpeer0(char *host, const char *lport)
{
	struct addrinfo hints, *res0, *res;
	int error;
	const char *cause = "unknown";

	if (connected) {
		close(peer);
		peer = -1;
	}
	connected = 0;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags = AI_CANONNAME;
	if (!lport)
		lport = "tftp";
	error = getaddrinfo(host, lport, &hints, &res0);
	if (error) {
		warnx("%s", gai_strerror(error));
		return;
	}

	for (res = res0; res; res = res->ai_next) {
		if (res->ai_addrlen > sizeof(peeraddr))
			continue;
		peer = socket(res->ai_family, res->ai_socktype,
			res->ai_protocol);
		if (peer < 0) {
			cause = "socket";
			continue;
		}

		memset(&peer_sock, 0, sizeof(peer_sock));
		peer_sock.ss_family = res->ai_family;
		peer_sock.ss_len = res->ai_addrlen;
		if (bind(peer, (struct sockaddr *)&peer_sock, peer_sock.ss_len) < 0) {
			cause = "bind";
			close(peer);
			peer = -1;
			continue;
		}

		break;
	}

	if (peer < 0)
		warn("%s", cause);
	else {
		/* res->ai_addr <= sizeof(peeraddr) is guaranteed */
		memcpy(&peer_sock, res->ai_addr, res->ai_addrlen);
		if (res->ai_canonname) {
			(void) strlcpy(hostname, res->ai_canonname,
				sizeof(hostname));
		} else
			(void) strlcpy(hostname, host, sizeof(hostname));
		connected = 1;
	}

	freeaddrinfo(res0);
}

static void
setpeer(int argc, char *argv[])
{
	char	line[MAXLINE];

	if (argc < 2) {
		strcpy(line, "Connect ");
		printf("(to) ");
		fgets(&line[strlen(line)], sizeof line - strlen(line), stdin);
		makeargv(line);
		argc = margc;
		argv = margv;
	}
	if ((argc < 2) || (argc > 3)) {
		printf("usage: %s [host [port]]\n", argv[0]);
		return;
	}
	if (argc == 3) {
		port = argv[2];
		setpeer0(argv[1], argv[2]);
	} else
		setpeer0(argv[1], NULL);
}

static void
modecmd(int argc, char *argv[])
{
	struct modes *p;
	const char *sep;

	if (argc < 2) {
		printf("Using %s mode to transfer files.\n", mode);
		return;
	}
	if (argc == 2) {
		for (p = modes; p->m_name; p++)
			if (strcmp(argv[1], p->m_name) == 0)
				break;
		if (p->m_name) {
			settftpmode(p->m_mode);
			return;
		}
		printf("%s: unknown mode\n", argv[1]);
		/* drop through and print usage message */
	}

	printf("usage: %s [", argv[0]);
	sep = " ";
	for (p = modes; p->m_name != NULL; p++) {
		printf("%s%s", sep, p->m_name);
		if (*sep == ' ')
			sep = " | ";
	}
	printf(" ]\n");
	return;
}

static void
setbinary(int argc __unused, char *argv[] __unused)
{

	settftpmode("octet");
}

static void
setascii(int argc __unused, char *argv[] __unused)
{

	settftpmode("netascii");
}

static void
settftpmode(const char *newmode)
{

	strlcpy(mode, newmode, sizeof(mode));
	if (verbose)
		printf("mode set to %s\n", mode);
}


/*
 * Send file(s).
 */
static void
put(int argc, char *argv[])
{
	int	fd;
	int	n;
	char	*cp, *targ;
	char	line[MAXLINE];
	struct stat sb;

	if (argc < 2) {
		strcpy(line, "send ");
		printf("(file) ");
		fgets(&line[strlen(line)], sizeof line - strlen(line), stdin);
		makeargv(line);
		argc = margc;
		argv = margv;
	}
	if (argc < 2) {
		putusage(argv[0]);
		return;
	}
	targ = argv[argc - 1];
	if (strrchr(argv[argc - 1], ':')) {
		char *lcp;

		for (n = 1; n < argc - 1; n++)
			if (strchr(argv[n], ':')) {
				putusage(argv[0]);
				return;
			}
		lcp = argv[argc - 1];
		targ = strrchr(lcp, ':');
		*targ++ = 0;
		if (lcp[0] == '[' && lcp[strlen(lcp) - 1] == ']') {
			lcp[strlen(lcp) - 1] = '\0';
			lcp++;
		}
		setpeer0(lcp, NULL);
	}
	if (!connected) {
		printf("No target machine specified.\n");
		return;
	}
	if (argc < 4) {
		cp = argc == 2 ? tail(targ) : argv[1];
		fd = open(cp, O_RDONLY);
		if (fd < 0) {
			warn("%s", cp);
			return;
		}

		if (fstat(fd, &sb) < 0) {
			warn("%s", cp);
			return;
		}
		asprintf(&options[OPT_TSIZE].o_request, "%ju", sb.st_size);

		if (verbose)
			printf("putting %s to %s:%s [%s]\n",
			    cp, hostname, targ, mode);
		xmitfile(peer, port, fd, targ, mode);
		close(fd);
		return;
	}
				/* this assumes the target is a directory */
				/* on a remote unix system.  hmmmm.  */
	cp = strchr(targ, '\0');
	*cp++ = '/';
	for (n = 1; n < argc - 1; n++) {
		strcpy(cp, tail(argv[n]));
		fd = open(argv[n], O_RDONLY);
		if (fd < 0) {
			warn("%s", argv[n]);
			continue;
		}

		if (fstat(fd, &sb) < 0) {
			warn("%s", argv[n]);
			continue;
		}
		asprintf(&options[OPT_TSIZE].o_request, "%ju", sb.st_size);

		if (verbose)
			printf("putting %s to %s:%s [%s]\n",
			    argv[n], hostname, targ, mode);
		xmitfile(peer, port, fd, targ, mode);
	}
}

static void
putusage(char *s)
{

	printf("usage: %s file [remotename]\n", s);
	printf("       %s file host:remotename\n", s);
	printf("       %s file1 file2 ... fileN [[host:]remote-directory]\n", s);
}

/*
 * Receive file(s).
 */
static void
get(int argc, char *argv[])
{
	int fd;
	int n;
	char *cp;
	char *src;
	char	line[MAXLINE];

	if (argc < 2) {
		strcpy(line, "get ");
		printf("(files) ");
		fgets(&line[strlen(line)], sizeof line - strlen(line), stdin);
		makeargv(line);
		argc = margc;
		argv = margv;
	}
	if (argc < 2) {
		getusage(argv[0]);
		return;
	}
	if (!connected) {
		for (n = 1; n < argc ; n++)
			if (strrchr(argv[n], ':') == 0) {
				printf("No remote host specified and "
				    "no host given for file '%s'\n", argv[n]);
				getusage(argv[0]);
				return;
			}
	}
	for (n = 1; n < argc ; n++) {
		src = strrchr(argv[n], ':');
		if (src == NULL)
			src = argv[n];
		else {
			char *lcp;

			*src++ = 0;
			lcp = argv[n];
			if (lcp[0] == '[' && lcp[strlen(lcp) - 1] == ']') {
				lcp[strlen(lcp) - 1] = '\0';
				lcp++;
			}
			setpeer0(lcp, NULL);
			if (!connected)
				continue;
		}
		if (argc < 4) {
			cp = argc == 3 ? argv[2] : tail(src);
			fd = creat(cp, 0644);
			if (fd < 0) {
				warn("%s", cp);
				return;
			}
			if (verbose)
				printf("getting from %s:%s to %s [%s]\n",
				    hostname, src, cp, mode);
			recvfile(peer, port, fd, src, mode);
			break;
		}
		cp = tail(src);         /* new .. jdg */
		fd = creat(cp, 0644);
		if (fd < 0) {
			warn("%s", cp);
			continue;
		}
		if (verbose)
			printf("getting from %s:%s to %s [%s]\n",
			    hostname, src, cp, mode);
		recvfile(peer, port, fd, src, mode);
	}
}

static void
getusage(char *s)
{

	printf("usage: %s file [localname]\n", s);
	printf("       %s [host:]file [localname]\n", s);
	printf("       %s [host1:]file1 [host2:]file2 ... [hostN:]fileN\n", s);
}

static void
settimeoutpacket(int argc, char *argv[])
{
	int t;
	char	line[MAXLINE];

	if (argc < 2) {
		strcpy(line, "Packet timeout ");
		printf("(value) ");
		fgets(&line[strlen(line)], sizeof line - strlen(line), stdin);
		makeargv(line);
		argc = margc;
		argv = margv;
	}
	if (argc != 2) {
		printf("usage: %s value\n", argv[0]);
		return;
	}
	t = atoi(argv[1]);
	if (t < 0) {
		printf("%s: bad value\n", argv[1]);
		return;
	}

	settimeouts(t, timeoutnetwork, maxtimeouts);
}

static void
settimeoutnetwork(int argc, char *argv[])
{
	int t;
	char	line[MAXLINE];

	if (argc < 2) {
		strcpy(line, "Network timeout ");
		printf("(value) ");
		fgets(&line[strlen(line)], sizeof line - strlen(line), stdin);
		makeargv(line);
		argc = margc;
		argv = margv;
	}
	if (argc != 2) {
		printf("usage: %s value\n", argv[0]);
		return;
	}
	t = atoi(argv[1]);
	if (t < 0) {
		printf("%s: bad value\n", argv[1]);
		return;
	}

	settimeouts(timeoutpacket, t, maxtimeouts);
}

static void
showstatus(int argc __unused, char *argv[] __unused)
{

	printf("Remote host: %s\n",
	    connected ? hostname : "none specified yet");
	printf("RFC2347 Options support: %s\n",
	    options_rfc_enabled ? "enabled" : "disabled");
	printf("Non-RFC defined options support: %s\n",
	    options_extra_enabled ? "enabled" : "disabled");
	printf("Mode: %s\n", mode);
	printf("Verbose: %s\n", verbose ? "on" : "off");
	printf("Debug: %s\n", debug_show(debug));
	printf("Artificial packetloss: %d in 100 packets\n",
	    packetdroppercentage);
	printf("Segment size: %d bytes\n", segsize);
	printf("Network timeout: %d seconds\n", timeoutpacket);
	printf("Maximum network timeout: %d seconds\n", timeoutnetwork);
	printf("Maximum timeouts: %d \n", maxtimeouts);
}

static void
intr(int dummy __unused)
{

	signal(SIGALRM, SIG_IGN);
	alarm(0);
	longjmp(toplevel, -1);
}

static char *
tail(char *filename)
{
	char *s;

	while (*filename) {
		s = strrchr(filename, '/');
		if (s == NULL)
			break;
		if (s[1])
			return (s + 1);
		*s = '\0';
	}
	return (filename);
}

static const char *
command_prompt(void)
{

	return ("tftp> ");
}

/*
 * Command parser.
 */
static void
command(bool interactive, EditLine *el, History *hist, HistEvent *hep)
{
	struct cmd *c;
	const char *bp;
	char *cp;
	int len, num;
	char	line[MAXLINE];

	for (;;) {
		if (interactive) {
			if ((bp = el_gets(el, &num)) == NULL || num == 0)
				exit(0);
			len = MIN(MAXLINE, num);
			memcpy(line, bp, len);
			line[len] = '\0';
			history(hist, hep, H_ENTER, bp);
		} else {
			line[0] = 0;
			if (fgets(line, sizeof line , stdin) == NULL) {
				if (feof(stdin)) {
					exit(txrx_error);
				} else {
					continue;
				}
			}
		}
		if ((cp = strchr(line, '\n')))
			*cp = '\0';
		if (line[0] == 0)
			continue;
		makeargv(line);
		if (margc == 0)
			continue;
		c = getcmd(margv[0]);
		if (c == (struct cmd *)-1) {
			printf("?Ambiguous command\n");
			continue;
		}
		if (c == NULL) {
			printf("?Invalid command\n");
			continue;
		}
		(*c->handler)(margc, margv);
	}
}

static struct cmd *
getcmd(char *name)
{
	const char *p, *q;
	struct cmd *c, *found;
	int nmatches, longest;

	longest = 0;
	nmatches = 0;
	found = 0;
	for (c = cmdtab; (p = c->name) != NULL; c++) {
		for (q = name; *q == *p++; q++)
			if (*q == 0)		/* exact match? */
				return (c);
		if (!*q) {			/* the name was a prefix */
			if (q - name > longest) {
				longest = q - name;
				nmatches = 1;
				found = c;
			} else if (q - name == longest)
				nmatches++;
		}
	}
	if (nmatches > 1)
		return ((struct cmd *)-1);
	return (found);
}

/*
 * Slice a string up into argc/argv.
 */
static void
makeargv(char *line)
{
	char *cp;
	char **argp = margv;

	margc = 0;
	if ((cp = strchr(line, '\n')) != NULL)
		*cp = '\0';
	for (cp = line; margc < MAX_MARGV - 1 && *cp != '\0';) {
		while (isspace(*cp))
			cp++;
		if (*cp == '\0')
			break;
		*argp++ = cp;
		margc += 1;
		while (*cp != '\0' && !isspace(*cp))
			cp++;
		if (*cp == '\0')
			break;
		*cp++ = '\0';
	}
	*argp++ = 0;
}

static void
quit(int argc __unused, char *argv[] __unused)
{

	exit(txrx_error);
}

/*
 * Help command.
 */
static void
help(int argc, char *argv[])
{
	struct cmd *c;

	if (argc == 1) {
		printf("Commands may be abbreviated.  Commands are:\n\n");
		for (c = cmdtab; c->name; c++)
			printf("%-*s\t%s\n", (int)HELPINDENT, c->name, c->help);

		printf("\n[-] : You shouldn't use these ones anymore.\n");
		printf("[*] : RFC2347 options support required.\n");
		printf("[**] : Non-standard RFC2347 option.\n");
		return;
	}
	while (--argc > 0) {
		char *arg;
		arg = *++argv;
		c = getcmd(arg);
		if (c == (struct cmd *)-1)
			printf("?Ambiguous help command: %s\n", arg);
		else if (c == (struct cmd *)0)
			printf("?Invalid help command: %s\n", arg);
		else
			printf("%s\n", c->help);
	}
}

static void
setverbose(int argc __unused, char *argv[] __unused)
{

	verbose = !verbose;
	printf("Verbose mode %s.\n", verbose ? "on" : "off");
}

static void
setoptions(int argc, char *argv[])
{

	if (argc == 2) {
		if (strcasecmp(argv[1], "enable") == 0 ||
		    strcasecmp(argv[1], "on") == 0) {
			options_extra_enabled = 1;
			options_rfc_enabled = 1;
		}
		if (strcasecmp(argv[1], "disable") == 0 ||
		    strcasecmp(argv[1], "off") == 0) {
			options_extra_enabled = 0;
			options_rfc_enabled = 0;
		}
		if (strcasecmp(argv[1], "extra") == 0)
			options_extra_enabled = !options_extra_enabled;
	}
	printf("Support for RFC2347 style options are now %s.\n",
	    options_rfc_enabled ? "enabled" : "disabled");
	printf("Support for non-RFC defined options are now %s.\n",
	    options_extra_enabled ? "enabled" : "disabled");

	printf("\nThe following options are available:\n"
	    "\toptions on	: enable support for RFC2347 style options\n"
	    "\toptions off	: disable support for RFC2347 style options\n"
	    "\toptions extra	: toggle support for non-RFC defined options\n"
	);
}

static void
setrollover(int argc, char *argv[])
{

	if (argc == 2) {
		if (strcasecmp(argv[1], "never") == 0 ||
		    strcasecmp(argv[1], "none") == 0) {
			free(options[OPT_ROLLOVER].o_request);
			options[OPT_ROLLOVER].o_request = NULL;
		}
		if (strcasecmp(argv[1], "1") == 0) {
			free(options[OPT_ROLLOVER].o_request);
			options[OPT_ROLLOVER].o_request = strdup("1");
		}
		if (strcasecmp(argv[1], "0") == 0) {
			free(options[OPT_ROLLOVER].o_request);
			options[OPT_ROLLOVER].o_request = strdup("0");
		}
	}
	printf("Support for the rollover options is %s.\n",
	    options[OPT_ROLLOVER].o_request != NULL ? "enabled" : "disabled");
	if (options[OPT_ROLLOVER].o_request != NULL)
		printf("Block rollover will be to block %s.\n",
		    options[OPT_ROLLOVER].o_request);


	printf("\nThe following rollover options are available:\n"
	    "\trollover 0	: rollover to block zero (default)\n"
	    "\trollover 1	: rollover to block one\n"
	    "\trollover never	: do not support the rollover option\n"
	    "\trollover none	: do not support the rollover option\n"
	);
}

static void
setdebug(int argc, char *argv[])
{
	int i;

	if (argc != 1) {
		i = 1;
		while (i < argc)
			debug ^= debug_find(argv[i++]);
	}
	printf("The following debugging is enabled: %s\n", debug_show(debug));

	printf("\nThe following debugs are available:\n");
	i = 0;
	while (debugs[i].name != NULL) {
		printf("\t%s\t%s\n", debugs[i].name, debugs[i].desc);
		i++;
	}
}

static void
setblocksize(int argc, char *argv[])
{

	if (!options_rfc_enabled)
		printf("RFC2347 style options are not enabled "
		    "(but proceeding anyway)\n");

	if (argc != 1) {
		int size = atoi(argv[1]);
		size_t max;
		u_long maxdgram;

		max = sizeof(maxdgram);
		if (sysctlbyname("net.inet.udp.maxdgram",
			&maxdgram, &max, NULL, 0) < 0) {
			perror("sysctl: net.inet.udp.maxdgram");
			return;
		}

		if (size < BLKSIZE_MIN || size > BLKSIZE_MAX) {
			printf("Blocksize should be between %d and %d bytes.\n",
				BLKSIZE_MIN, BLKSIZE_MAX);
			return;
		} else if (size > (int)maxdgram - 4) {
			printf("Blocksize can't be bigger than %ld bytes due "
			    "to the net.inet.udp.maxdgram sysctl limitation.\n",
			    maxdgram - 4);
			asprintf(&options[OPT_BLKSIZE].o_request,
			    "%ld", maxdgram - 4);
		} else {
			asprintf(&options[OPT_BLKSIZE].o_request, "%d", size);
		}
	}
	printf("Blocksize is now %s bytes.\n", options[OPT_BLKSIZE].o_request);
}

static void
setblocksize2(int argc, char *argv[])
{

	if (!options_rfc_enabled || !options_extra_enabled)
		printf(
		    "RFC2347 style or non-RFC defined options are not enabled "
		    "(but proceeding anyway)\n");

	if (argc != 1) {
		int size = atoi(argv[1]);
		int i;
		size_t max;
		u_long maxdgram;

		int sizes[] = {
			8, 16, 32, 64, 128, 256, 512, 1024,
			2048, 4096, 8192, 16384, 32768, 0
		};

		max = sizeof(maxdgram);
		if (sysctlbyname("net.inet.udp.maxdgram",
			&maxdgram, &max, NULL, 0) < 0) {
			perror("sysctl: net.inet.udp.maxdgram");
			return;
		}

		for (i = 0; sizes[i] != 0; i++) {
			if (sizes[i] == size) break;
		}
		if (sizes[i] == 0) {
			printf("Blocksize2 should be a power of two between "
			    "8 and 32768.\n");
			return;
		}

		if (size < BLKSIZE_MIN || size > BLKSIZE_MAX) {
			printf("Blocksize2 should be between "
			    "%d and %d bytes.\n", BLKSIZE_MIN, BLKSIZE_MAX);
			return;
		} else if (size > (int)maxdgram - 4) {
			printf("Blocksize2 can't be bigger than %ld bytes due "
			    "to the net.inet.udp.maxdgram sysctl limitation.\n",
			    maxdgram - 4);
			for (i = 0; sizes[i+1] != 0; i++) {
				if ((int)maxdgram < sizes[i+1]) break;
			}
			asprintf(&options[OPT_BLKSIZE2].o_request,
			    "%d", sizes[i]);
		} else {
			asprintf(&options[OPT_BLKSIZE2].o_request, "%d", size);
		}
	}
	printf("Blocksize2 is now %s bytes.\n",
	    options[OPT_BLKSIZE2].o_request);
}

static void
setpacketdrop(int argc, char *argv[])
{

	if (argc != 1)
		packetdroppercentage = atoi(argv[1]);

	printf("Randomly %d in 100 packets will be dropped\n",
	    packetdroppercentage);
}
