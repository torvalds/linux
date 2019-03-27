/*
 * copy diverted (or tee'd) packets to a file in 'tcpdump' format
 * (ie. this uses the '-lpcap' routines).
 *
 * example usage:
 *	# ipfwpcap -r 8091 divt.log &
 *	# ipfw add 2864 divert 8091 ip from 128.432.53.82 to any
 *	# ipfw add 2864 divert 8091 ip from any to 128.432.53.82
 *
 *   the resulting dump file can be read with ...
 *	# tcpdump -nX -r divt.log
 */
/*
 * Written by P Kern { pkern [AT] cns.utoronto.ca }
 *
 * Copyright (c) 2004 University of Toronto. All rights reserved.
 * Anyone may use or copy this software except that this copyright
 * notice remain intact and that credit is given where it is due.
 * The University of Toronto and the author make no warranty and
 * accept no liability for this software.
 *
 * From: Header: /local/src/local.lib/SRC/ipfwpcap/RCS/ipfwpcap.c,v 1.4 2004/01/15 16:19:07 pkern Exp
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <errno.h>
#include <paths.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>		/* for MAXPATHLEN */
#include <sys/socket.h>
#include <netinet/in.h>

#include <netinet/in_systm.h>	/* for IP_MAXPACKET */
#include <netinet/ip.h>		/* for IP_MAXPACKET */

#include <net/bpf.h>

/* XXX normally defined in config.h */
#define HAVE_STRLCPY 1
#define HAVE_SNPRINTF 1
#define HAVE_VSNPRINTF 1
#include <pcap-int.h>	/* see pcap(3) and /usr/src/contrib/libpcap/. */

#ifdef IP_MAXPACKET
#define BUFMAX	IP_MAXPACKET
#else
#define BUFMAX	65535
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN	1024
#endif

static int debug = 0;
static int reflect = 0;		/* 1 == write packet back to socket. */

static ssize_t totbytes = 0, maxbytes = 0;
static ssize_t totpkts = 0, maxpkts = 0;

static char *prog = NULL;
static char pidfile[MAXPATHLEN];

/*
 * tidy up.
 */
static void
quit(int sig)
{
	(void) unlink(pidfile);
	exit(sig);
}

/*
 * do the "paper work"
 *	- save my own pid in /var/run/$0.{port#}.pid
 */
static void
okay(int pn)
{
	int fd;
	char *p, numbuf[80];

	if (pidfile[0] == '\0') {
		p = strrchr(prog, '/');
		p = (p == NULL) ? prog : p + 1;

		snprintf(pidfile, sizeof pidfile,
			"%s%s.%d.pid", _PATH_VARRUN, p, pn);
	}

	fd = open(pidfile, O_WRONLY|O_CREAT|O_EXCL, 0644);
	if (fd < 0) {
		perror(pidfile);
		exit(21);
	}

	siginterrupt(SIGTERM, 1);
	siginterrupt(SIGHUP, 1);
	signal(SIGTERM, quit);
	signal(SIGHUP, quit);
	signal(SIGINT, quit);

	snprintf(numbuf, sizeof numbuf, "%d\n", getpid());
	if (write(fd, numbuf, strlen(numbuf)) < 0) {
		perror(pidfile);
		quit(23);
	}
	(void) close(fd);
}

static void
usage(void)
{
	fprintf(stderr,
"\n"
"usage:\n"
"    %s [-dr] [-b maxbytes] [-p maxpkts] [-P pidfile] portnum dumpfile\n"
"\n"
"where:\n"
"	'-d'  = enable debugging messages.\n"
"	'-r'  = reflect. write packets back to the divert socket.\n"
"		(ie. simulate the original intent of \"ipfw tee\").\n"
"	'-rr' = indicate that it is okay to quit if packet-count or\n"
"		byte-count limits are reached (see the NOTE below\n"
"		about what this implies).\n"
"	'-b bytcnt'   = stop dumping after {bytcnt} bytes.\n"
"	'-p pktcnt'   = stop dumping after {pktcnt} packets.\n"
"	'-P pidfile'  = alternate file to store the PID\n"
"			(default: /var/run/%s.{portnum}.pid).\n"
"\n"
"	portnum  = divert(4) socket port number.\n"
"	dumpfile = file to write captured packets (tcpdump format).\n"
"		   (specify '-' to write packets to stdout).\n"
"\n"
"The '-r' option should not be necessary, but because \"ipfw tee\" is broken\n"
"(see BUGS in ipfw(8) for details) this feature can be used along with\n"
"an \"ipfw divert\" rule to simulate the original intent of \"ipfw tee\".\n"
"\n"
"NOTE: With an \"ipfw divert\" rule, diverted packets will silently\n"
"      disappear if there is nothing listening to the divert socket.\n"
"\n", prog, prog);
	exit(1);
}

int
main(int ac, char *av[])
{
	int r, sd, portnum, l;
        struct sockaddr_in sin;
	int errflg = 0;

	int nfd;
	fd_set rds;

	ssize_t nr;

	char *dumpf, buf[BUFMAX];

	pcap_t *p;
	pcap_dumper_t *dp;
	struct pcap_pkthdr phd;

	prog = av[0];

	while ((r = getopt(ac, av, "drb:p:P:")) != -1) {
		switch (r) {
		case 'd':
			debug++;
			break;
		case 'r':
			reflect++;
			break;
		case 'b':
			maxbytes = (ssize_t) atol(optarg);
			break;
		case 'p':
			maxpkts = (ssize_t) atoi(optarg);
			break;
		case 'P':
			strcpy(pidfile, optarg);
			break;
		case '?':
		default:
			errflg++;
			break;
		}
	}

	if ((ac - optind) != 2 || errflg)
		usage();

	portnum = atoi(av[optind++]);
	dumpf = av[optind];

if (debug) fprintf(stderr, "bind to %d.\ndump to '%s'.\n", portnum, dumpf);

	if ((r = socket(PF_INET, SOCK_RAW, IPPROTO_DIVERT)) == -1) {
		perror("socket(DIVERT)");
		exit(2);
	}
	sd = r;

	sin.sin_port = htons(portnum);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;

	if (bind(sd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		perror("bind(divert)");
		exit(3);
	}

	p = pcap_open_dead(DLT_RAW, BUFMAX);
	dp = pcap_dump_open(p, dumpf);
	if (dp == NULL) {
		pcap_perror(p, dumpf);
		exit(4);
	}

	okay(portnum);

	nfd = sd + 1;
	for (;;) {
		FD_ZERO(&rds);
		FD_SET(sd, &rds);

		r = select(nfd, &rds, NULL, NULL, NULL);
		if (r == -1) {
			if (errno == EINTR) continue;
			perror("select");
			quit(11);
		}

		if (!FD_ISSET(sd, &rds))
			/* hmm. no work. */
			continue;

		/*
		 * use recvfrom(3 and sendto(3) as in natd(8).
		 * see /usr/src/sbin/natd/natd.c
		 * see ipfw(8) about using 'divert' and 'tee'.
		 */

		/*
		 * read packet.
		 */
		l = sizeof(sin);
		nr = recvfrom(sd, buf, sizeof(buf), 0, (struct sockaddr *)&sin, &l);
if (debug) fprintf(stderr, "recvfrom(%d) = %zd (%d)\n", sd, nr, l);
		if (nr < 0 && errno != EINTR) {
			perror("recvfrom(sd)");
			quit(12);
		}
		if (nr <= 0) continue;

		if (reflect) {
			/*
			 * write packet back so it can continue
			 * being processed by any further IPFW rules.
			 */
			l = sizeof(sin);
			r = sendto(sd, buf, nr, 0, (struct sockaddr *)&sin, l);
if (debug) fprintf(stderr, "  sendto(%d) = %d\n", sd, r);
			if (r < 0) { perror("sendto(sd)"); quit(13); }
		}

		/*
		 * check maximums, if any.
		 * but don't quit if must continue reflecting packets.
		 */
		if (maxpkts) {
			totpkts++;
			if (totpkts > maxpkts) {
				if (reflect == 1) continue;
				quit(0);
			}
		}
		if (maxbytes) {
			totbytes += nr;
			if (totbytes > maxbytes) {
				if (reflect == 1) continue;
				quit(0);
			}
		}

		/*
		 * save packet in tcpdump(1) format. see pcap(3).
		 * divert packets are fully assembled. see ipfw(8).
		 */
		(void) gettimeofday(&(phd.ts), NULL);
		phd.caplen = phd.len = nr;
		pcap_dump((u_char *)dp, &phd, buf);
		if (ferror((FILE *)dp)) { perror(dumpf); quit(14); }
		(void) fflush((FILE *)dp);
	}

	quit(0);
}
