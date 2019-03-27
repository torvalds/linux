/*
 * bootpgw.c - BOOTP GateWay
 * This program forwards BOOTP Request packets to a BOOTP server.
 */

/************************************************************************
          Copyright 1988, 1991 by Carnegie Mellon University

                          All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted, provided
that the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation, and that the name of Carnegie Mellon University not be used
in advertising or publicity pertaining to distribution of the software
without specific, written prior permission.

CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.
************************************************************************/

/*
 * BOOTPGW is typically used to forward BOOTP client requests from
 * one subnet to a BOOTP server on a different subnet.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>	/* inet_ntoa */

#ifndef	NO_UNISTD
#include <unistd.h>
#endif

#include <err.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <netdb.h>
#include <paths.h>
#include <syslog.h>
#include <assert.h>

#ifdef	NO_SETSID
# include <fcntl.h>		/* for O_RDONLY, etc */
#endif

#ifndef	USE_BFUNCS
# include <memory.h>
/* Yes, memcpy is OK here (no overlapped copies). */
# define bcopy(a,b,c)    memcpy(b,a,c)
# define bzero(p,l)      memset(p,0,l)
# define bcmp(a,b,c)     memcmp(a,b,c)
#endif

#include "bootp.h"
#include "getif.h"
#include "hwaddr.h"
#include "report.h"
#include "patchlevel.h"

/* Local definitions: */
#define MAX_MSG_SIZE			(3*512)	/* Maximum packet size */
#define TRUE 1
#define FALSE 0
#define get_network_errmsg get_errmsg



/*
 * Externals, forward declarations, and global variables
 */

static void usage(void);
static void handle_reply(void);
static void handle_request(void);

/*
 * IP port numbers for client and server obtained from /etc/services
 */

u_short bootps_port, bootpc_port;


/*
 * Internet socket and interface config structures
 */

struct sockaddr_in bind_addr;	/* Listening */
struct sockaddr_in recv_addr;	/* Packet source */
struct sockaddr_in send_addr;	/*  destination */


/*
 * option defaults
 */
int debug = 0;					/* Debugging flag (level) */
struct timeval actualtimeout =
{								/* fifteen minutes */
	15 * 60L,					/* tv_sec */
	0							/* tv_usec */
};
u_char maxhops = 4;				/* Number of hops allowed for requests. */
u_int minwait = 3;				/* Number of seconds client must wait before
						   its bootrequest packets are forwarded. */

/*
 * General
 */

int s;							/* Socket file descriptor */
char *pktbuf;					/* Receive packet buffer */
int pktlen;
char *progname;
char *servername;
int32 server_ipa;				/* Real server IP address, network order. */

struct in_addr my_ip_addr;

struct utsname my_uname;
char *hostname;





/*
 * Initialization such as command-line processing is done and then the
 * main server loop is started.
 */

int
main(argc, argv)
	int argc;
	char **argv;
{
	struct timeval *timeout;
	struct bootp *bp;
	struct servent *servp;
	struct hostent *hep;
	char *stmp;
	int n, ba_len, ra_len;
	int nfound, readfds;
	int standalone;

	progname = strrchr(argv[0], '/');
	if (progname) progname++;
	else progname = argv[0];

	/*
	 * Initialize logging.
	 */
	report_init(0);				/* uses progname */

	/*
	 * Log startup
	 */
	report(LOG_INFO, "version %s.%d", VERSION, PATCHLEVEL);

	/* Debugging for compilers with struct padding. */
	assert(sizeof(struct bootp) == BP_MINPKTSZ);

	/* Get space for receiving packets and composing replies. */
	pktbuf = malloc(MAX_MSG_SIZE);
	if (!pktbuf) {
		report(LOG_ERR, "malloc failed");
		exit(1);
	}
	bp = (struct bootp *) pktbuf;

	/*
	 * Check to see if a socket was passed to us from inetd.
	 *
	 * Use getsockname() to determine if descriptor 0 is indeed a socket
	 * (and thus we are probably a child of inetd) or if it is instead
	 * something else and we are running standalone.
	 */
	s = 0;
	ba_len = sizeof(bind_addr);
	bzero((char *) &bind_addr, ba_len);
	errno = 0;
	standalone = TRUE;
	if (getsockname(s, (struct sockaddr *) &bind_addr, &ba_len) == 0) {
		/*
		 * Descriptor 0 is a socket.  Assume we are a child of inetd.
		 */
		if (bind_addr.sin_family == AF_INET) {
			standalone = FALSE;
			bootps_port = ntohs(bind_addr.sin_port);
		} else {
			/* Some other type of socket? */
			report(LOG_INFO, "getsockname: not an INET socket");
		}
	}
	/*
	 * Set defaults that might be changed by option switches.
	 */
	stmp = NULL;
	timeout = &actualtimeout;

	if (uname(&my_uname) < 0)
		errx(1, "can't get hostname");
	hostname = my_uname.nodename;

	hep = gethostbyname(hostname);
	if (!hep) {
		printf("Can not get my IP address\n");
		exit(1);
	}
	bcopy(hep->h_addr, (char *)&my_ip_addr, sizeof(my_ip_addr));

	/*
	 * Read switches.
	 */
	for (argc--, argv++; argc > 0; argc--, argv++) {
		if (argv[0][0] != '-')
			break;
		switch (argv[0][1]) {

		case 'd':				/* debug level */
			if (argv[0][2]) {
				stmp = &(argv[0][2]);
			} else if (argv[1] && argv[1][0] == '-') {
				/*
				 * Backwards-compatible behavior:
				 * no parameter, so just increment the debug flag.
				 */
				debug++;
				break;
			} else {
				argc--;
				argv++;
				stmp = argv[0];
			}
			if (!stmp || (sscanf(stmp, "%d", &n) != 1) || (n < 0)) {
				warnx("invalid debug level");
				break;
			}
			debug = n;
			break;

		case 'h':				/* hop count limit */
			if (argv[0][2]) {
				stmp = &(argv[0][2]);
			} else {
				argc--;
				argv++;
				stmp = argv[0];
			}
			if (!stmp || (sscanf(stmp, "%d", &n) != 1) ||
				(n < 0) || (n > 16))
			{
				warnx("invalid hop count limit");
				break;
			}
			maxhops = (u_char)n;
			break;

		case 'i':				/* inetd mode */
			standalone = FALSE;
			break;

		case 's':				/* standalone mode */
			standalone = TRUE;
			break;

		case 't':				/* timeout */
			if (argv[0][2]) {
				stmp = &(argv[0][2]);
			} else {
				argc--;
				argv++;
				stmp = argv[0];
			}
			if (!stmp || (sscanf(stmp, "%d", &n) != 1) || (n < 0)) {
				warnx("invalid timeout specification");
				break;
			}
			actualtimeout.tv_sec = (int32) (60 * n);
			/*
			 * If the actual timeout is zero, pass a NULL pointer
			 * to select so it blocks indefinitely, otherwise,
			 * point to the actual timeout value.
			 */
			timeout = (n > 0) ? &actualtimeout : NULL;
			break;

		case 'w':				/* wait time */
			if (argv[0][2]) {
				stmp = &(argv[0][2]);
			} else {
				argc--;
				argv++;
				stmp = argv[0];
			}
			if (!stmp || (sscanf(stmp, "%d", &n) != 1) ||
				(n < 0) || (n > 60))
			{
				warnx("invalid wait time");
				break;
			}
			minwait = (u_int)n;
			break;

		default:
			warnx("unknown switch: -%c", argv[0][1]);
			usage();
			break;

		} /* switch */
	} /* for args */

	/* Make sure server name argument is suplied. */
	servername = argv[0];
	if (!servername) {
		warnx("missing server name");
		usage();
	}
	/*
	 * Get address of real bootp server.
	 */
	if (isdigit(servername[0]))
		server_ipa = inet_addr(servername);
	else {
		hep = gethostbyname(servername);
		if (!hep)
			errx(1, "can't get addr for %s", servername);
		bcopy(hep->h_addr, (char *)&server_ipa, sizeof(server_ipa));
	}

	if (standalone) {
		/*
		 * Go into background and disassociate from controlling terminal.
		 * XXX - This is not the POSIX way (Should use setsid). -gwr
		 */
		if (debug < 3) {
			if (fork())
				exit(0);
#ifdef	NO_SETSID
			setpgrp(0,0);
#ifdef TIOCNOTTY
			n = open(_PATH_TTY, O_RDWR);
			if (n >= 0) {
				ioctl(n, TIOCNOTTY, (char *) 0);
				(void) close(n);
			}
#endif	/* TIOCNOTTY */
#else	/* SETSID */
			if (setsid() < 0)
				perror("setsid");
#endif	/* SETSID */
		} /* if debug < 3 */
		/*
		 * Nuke any timeout value
		 */
		timeout = NULL;

		/*
		 * Here, bootpd would do:
		 *	chdir
		 *	tzone_init
		 *	rdtab_init
		 *	readtab
		 */

		/*
		 * Create a socket.
		 */
		if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
			report(LOG_ERR, "socket: %s", get_network_errmsg());
			exit(1);
		}
		/*
		 * Get server's listening port number
		 */
		servp = getservbyname("bootps", "udp");
		if (servp) {
			bootps_port = ntohs((u_short) servp->s_port);
		} else {
			bootps_port = (u_short) IPPORT_BOOTPS;
			report(LOG_ERR,
			   "bootps/udp: unknown service -- using port %d",
				   bootps_port);
		}

		/*
		 * Bind socket to BOOTPS port.
		 */
		bind_addr.sin_family = AF_INET;
		bind_addr.sin_port = htons(bootps_port);
		bind_addr.sin_addr.s_addr = INADDR_ANY;
		if (bind(s, (struct sockaddr *) &bind_addr,
				 sizeof(bind_addr)) < 0)
		{
			report(LOG_ERR, "bind: %s", get_network_errmsg());
			exit(1);
		}
	} /* if standalone */
	/*
	 * Get destination port number so we can reply to client
	 */
	servp = getservbyname("bootpc", "udp");
	if (servp) {
		bootpc_port = ntohs(servp->s_port);
	} else {
		report(LOG_ERR,
			   "bootpc/udp: unknown service -- using port %d",
			   IPPORT_BOOTPC);
		bootpc_port = (u_short) IPPORT_BOOTPC;
	}

	/* no signal catchers */

	/*
	 * Process incoming requests.
	 */
	for (;;) {
		struct timeval tv;

		readfds = 1 << s;
		if (timeout)
			tv = *timeout;

		nfound = select(s + 1, (fd_set *)&readfds, NULL, NULL,
						(timeout) ? &tv : NULL);
		if (nfound < 0) {
			if (errno != EINTR) {
				report(LOG_ERR, "select: %s", get_errmsg());
			}
			continue;
		}
		if (!(readfds & (1 << s))) {
			report(LOG_INFO, "exiting after %ld minutes of inactivity",
				   (long)(actualtimeout.tv_sec / 60));
			exit(0);
		}
		ra_len = sizeof(recv_addr);
		n = recvfrom(s, pktbuf, MAX_MSG_SIZE, 0,
					 (struct sockaddr *) &recv_addr, &ra_len);
		if (n <= 0) {
			continue;
		}
		if (debug > 3) {
			report(LOG_INFO, "recvd pkt from IP addr %s",
				   inet_ntoa(recv_addr.sin_addr));
		}
		if (n < sizeof(struct bootp)) {
			if (debug) {
				report(LOG_INFO, "received short packet");
			}
			continue;
		}
		pktlen = n;

		switch (bp->bp_op) {
		case BOOTREQUEST:
			handle_request();
			break;
		case BOOTREPLY:
			handle_reply();
			break;
		}
	}
	return 0;
}




/*
 * Print "usage" message and exit
 */

static void
usage()
{
	fprintf(stderr,
			"usage:  bootpgw [-d level] [-i] [-s] [-t timeout] server\n");
	fprintf(stderr, "\t -d n\tset debug level\n");
	fprintf(stderr, "\t -h n\tset max hop count\n");
	fprintf(stderr, "\t -i\tforce inetd mode (run as child of inetd)\n");
	fprintf(stderr, "\t -s\tforce standalone mode (run without inetd)\n");
	fprintf(stderr, "\t -t n\tset inetd exit timeout to n minutes\n");
	fprintf(stderr, "\t -w n\tset min wait time (secs)\n");
	exit(1);
}



/*
 * Process BOOTREQUEST packet.
 *
 * Note, this just forwards the request to a real server.
 */
static void
handle_request()
{
	struct bootp *bp = (struct bootp *) pktbuf;
	u_short secs;
        u_char hops;

	/* XXX - SLIP init: Set bp_ciaddr = recv_addr here? */

	if (debug) {
		report(LOG_INFO, "request from %s",
			   inet_ntoa(recv_addr.sin_addr));
	}
	/* Has the client been waiting long enough? */
	secs = ntohs(bp->bp_secs);
	if (secs < minwait)
		return;

	/* Has this packet hopped too many times? */
	hops = bp->bp_hops;
	if (++hops > maxhops) {
		report(LOG_NOTICE, "request from %s reached hop limit",
			   inet_ntoa(recv_addr.sin_addr));
		return;
	}
	bp->bp_hops = hops;

	/*
	 * Here one might discard a request from the same subnet as the
	 * real server, but we can assume that the real server will send
	 * a reply to the client before it waits for minwait seconds.
	 */

	/* If gateway address is not set, put in local interface addr. */
	if (bp->bp_giaddr.s_addr == 0) {
#if 0	/* BUG */
		struct sockaddr_in *sip;
		struct ifreq *ifr;
		/*
		 * XXX - This picks the wrong interface when the receive addr
		 * is the broadcast address.  There is no  portable way to
		 * find out which interface a broadcast was received on. -gwr
		 * (Thanks to <walker@zk3.dec.com> for finding this bug!)
		 */
		ifr = getif(s, &recv_addr.sin_addr);
		if (!ifr) {
			report(LOG_NOTICE, "no interface for request from %s",
				   inet_ntoa(recv_addr.sin_addr));
			return;
		}
		sip = (struct sockaddr_in *) &(ifr->ifr_addr);
		bp->bp_giaddr = sip->sin_addr;
#else	/* BUG */
		/*
		 * XXX - Just set "giaddr" to our "official" IP address.
		 * RFC 1532 says giaddr MUST be set to the address of the
		 * interface on which the request was received.  Setting
		 * it to our "default" IP address is not strictly correct,
		 * but is good enough to allow the real BOOTP server to
		 * get the reply back here.  Then, before we forward the
		 * reply to the client, the giaddr field is corrected.
		 * (In case the client uses giaddr, which it should not.)
		 * See handle_reply()
		 */
		bp->bp_giaddr = my_ip_addr;
#endif	/* BUG */

		/*
		 * XXX - DHCP says to insert a subnet mask option into the
		 * options area of the request (if vendor magic == std).
		 */
	}
	/* Set up socket address for send. */
	send_addr.sin_family = AF_INET;
	send_addr.sin_port = htons(bootps_port);
	send_addr.sin_addr.s_addr = server_ipa;

	/* Send reply with same size packet as request used. */
	if (sendto(s, pktbuf, pktlen, 0,
			   (struct sockaddr *) &send_addr,
			   sizeof(send_addr)) < 0)
	{
		report(LOG_ERR, "sendto: %s", get_network_errmsg());
	}
}



/*
 * Process BOOTREPLY packet.
 */
static void
handle_reply()
{
	struct bootp *bp = (struct bootp *) pktbuf;
	struct ifreq *ifr;
	struct sockaddr_in *sip;
	unsigned char *ha;
	int len, haf;

	if (debug) {
		report(LOG_INFO, "   reply for %s",
			   inet_ntoa(bp->bp_yiaddr));
	}
	/* Make sure client is directly accessible. */
	ifr = getif(s, &(bp->bp_yiaddr));
	if (!ifr) {
		report(LOG_NOTICE, "no interface for reply to %s",
			   inet_ntoa(bp->bp_yiaddr));
		return;
	}
#if 1	/* Experimental (see BUG above) */
/* #ifdef CATER_TO_OLD_CLIENTS ? */
	/*
	 * The giaddr field has been set to our "default" IP address
	 * which might not be on the same interface as the client.
	 * In case the client looks at giaddr, (which it should not)
	 * giaddr is now set to the address of the correct interface.
	 */
	sip = (struct sockaddr_in *) &(ifr->ifr_addr);
	bp->bp_giaddr = sip->sin_addr;
#endif

	/* Set up socket address for send to client. */
	send_addr.sin_family = AF_INET;
	send_addr.sin_addr = bp->bp_yiaddr;
	send_addr.sin_port = htons(bootpc_port);

	/* Create an ARP cache entry for the client. */
	ha = bp->bp_chaddr;
	len = bp->bp_hlen;
	if (len > MAXHADDRLEN)
		len = MAXHADDRLEN;
	haf = (int) bp->bp_htype;
	if (haf == 0)
		haf = HTYPE_ETHERNET;

	if (debug > 1)
		report(LOG_INFO, "setarp %s - %s",
			   inet_ntoa(bp->bp_yiaddr), haddrtoa(ha, len));
	setarp(s, &bp->bp_yiaddr, haf, ha, len);

	/* Send reply with same size packet as request used. */
	if (sendto(s, pktbuf, pktlen, 0,
			   (struct sockaddr *) &send_addr,
			   sizeof(send_addr)) < 0)
	{
		report(LOG_ERR, "sendto: %s", get_network_errmsg());
	}
}

/*
 * Local Variables:
 * tab-width: 4
 * c-indent-level: 4
 * c-argdecl-indent: 4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: -4
 * c-label-offset: -4
 * c-brace-offset: 0
 * End:
 */
