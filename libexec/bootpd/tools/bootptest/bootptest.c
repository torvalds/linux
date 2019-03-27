/*
 * bootptest.c - Test out a bootp server.
 *
 * This simple program was put together from pieces taken from
 * various places, including the CMU BOOTP client and server.
 * The packet printing routine is from the Berkeley "tcpdump"
 * program with some enhancements I added.  The print-bootp.c
 * file was shared with my copy of "tcpdump" and therefore uses
 * some unusual utility routines that would normally be provided
 * by various parts of the tcpdump program.  Gordon W. Ross
 *
 * Boilerplate:
 *
 * This program includes software developed by the University of
 * California, Lawrence Berkeley Laboratory and its contributors.
 * (See the copyright notice in print-bootp.c)
 *
 * The remainder of this program is public domain.  You may do
 * whatever you like with it except claim that you wrote it.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * HISTORY:
 *
 * 12/02/93 Released version 1.4 (with bootp-2.3.2)
 * 11/05/93 Released version 1.3
 * 10/14/93 Released version 1.2
 * 10/11/93 Released version 1.1
 * 09/28/93 Released version 1.0
 * 09/93 Original developed by Gordon W. Ross <gwr@mc.com>
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

char *usage = "bootptest [-h] server-name [vendor-data-template-file]";

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>			/* inet_ntoa */

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
#include <assert.h>

#include "bootp.h"
#include "bootptest.h"
#include "getif.h"
#include "getether.h"

#include "patchlevel.h"

static void send_request(int s);

#define LOG_ERR 1
#define BUFLEN 1024
#define WAITSECS 1
#define MAXWAIT  10

int vflag = 1;
int tflag = 0;
int thiszone;
char *progname;
unsigned char *packetp;
unsigned char *snapend;
int snaplen;


/*
 * IP port numbers for client and server obtained from /etc/services
 */

u_short bootps_port, bootpc_port;


/*
 * Internet socket and interface config structures
 */

struct sockaddr_in sin_server;	/* where to send requests */
struct sockaddr_in sin_client;	/* for bind and listen */
struct sockaddr_in sin_from;	/* Packet source */
u_char eaddr[16];				/* Ethernet address */

/*
 * General
 */

int debug = 1;					/* Debugging flag (level) */
char *sndbuf;					/* Send packet buffer */
char *rcvbuf;					/* Receive packet buffer */

struct utsname my_uname;
char *hostname;

/*
 * Vendor magic cookies for CMU and RFC1048
 */

unsigned char vm_cmu[4] = VM_CMU;
unsigned char vm_rfc1048[4] = VM_RFC1048;
short secs;						/* How long client has waited */

/*
 * Initialization such as command-line processing is done, then
 * the receiver loop is started.  Die when interrupted.
 */

int
main(argc, argv)
	int argc;
	char **argv;
{
	struct bootp *bp;
	struct servent *sep;
	struct hostent *hep;

	char *servername = NULL;
	char *vendor_file = NULL;
	char *bp_file = NULL;
	int32 server_addr;			/* inet addr, network order */
	int s;						/* Socket file descriptor */
	int n, fromlen, recvcnt;
	int use_hwa = 0;
	int32 vend_magic;
	int32 xid;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];
	argc--;
	argv++;

	if (debug)
		printf("%s: version %s.%d\n", progname, VERSION, PATCHLEVEL);

	/*
	 * Verify that "struct bootp" has the correct official size.
	 * (Catch evil compilers that do struct padding.)
	 */
	assert(sizeof(struct bootp) == BP_MINPKTSZ);

	if (uname(&my_uname) < 0)
		errx(1, "can't get hostname");
	hostname = my_uname.nodename;

	sndbuf = malloc(BUFLEN);
	rcvbuf = malloc(BUFLEN);
	if (!sndbuf || !rcvbuf) {
		printf("malloc failed\n");
		exit(1);
	}

	/* default magic number */
	bcopy(vm_rfc1048, (char*)&vend_magic, 4);

	/* Handle option switches. */
	while (argc > 0) {
		if (argv[0][0] != '-')
			break;
		switch (argv[0][1]) {

		case 'f':				/* File name to request. */
			if (argc < 2)
				goto error;
			argc--; argv++;
			bp_file = *argv;
			break;

		case 'h':				/* Use hardware address. */
			use_hwa = 1;
			break;

		case 'm':				/* Magic number value. */
			if (argc < 2)
				goto error;
			argc--; argv++;
			vend_magic = inet_addr(*argv);
			break;

		error:
		default:
			puts(usage);
			exit(1);

		}
		argc--;
		argv++;
	}

	/* Get server name (or address) for query. */
	if (argc > 0) {
		servername = *argv;
		argc--;
		argv++;
	}
	/* Get optional vendor-data-template-file. */
	if (argc > 0) {
		vendor_file = *argv;
		argc--;
		argv++;
	}
	if (!servername) {
		printf("missing server name.\n");
		puts(usage);
		exit(1);
	}
	/*
	 * Create a socket.
	 */
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}
	/*
	 * Get server's listening port number
	 */
	sep = getservbyname("bootps", "udp");
	if (sep) {
		bootps_port = ntohs((u_short) sep->s_port);
	} else {
		warnx("bootps/udp: unknown service -- using port %d",
				IPPORT_BOOTPS);
		bootps_port = (u_short) IPPORT_BOOTPS;
	}

	/*
	 * Set up server socket address (for send)
	 */
	if (servername) {
		if (isdigit(servername[0]))
			server_addr = inet_addr(servername);
		else {
			hep = gethostbyname(servername);
			if (!hep)
				errx(1, "%s: unknown host", servername);
			bcopy(hep->h_addr, &server_addr, sizeof(server_addr));
		}
	} else {
		/* Get broadcast address */
		/* XXX - not yet */
		server_addr = INADDR_ANY;
	}
	sin_server.sin_family = AF_INET;
	sin_server.sin_port = htons(bootps_port);
	sin_server.sin_addr.s_addr = server_addr;

	/*
	 * Get client's listening port number
	 */
	sep = getservbyname("bootpc", "udp");
	if (sep) {
		bootpc_port = ntohs(sep->s_port);
	} else {
		warnx("bootpc/udp: unknown service -- using port %d",
				IPPORT_BOOTPC);
		bootpc_port = (u_short) IPPORT_BOOTPC;
	}

	/*
	 * Set up client socket address (for listen)
	 */
	sin_client.sin_family = AF_INET;
	sin_client.sin_port = htons(bootpc_port);
	sin_client.sin_addr.s_addr = INADDR_ANY;

	/*
	 * Bind client socket to BOOTPC port.
	 */
	if (bind(s, (struct sockaddr *) &sin_client, sizeof(sin_client)) < 0) {
		if (errno == EACCES) {
			warn("bind BOOTPC port");
			errx(1, "you need to run this as root");
		}
		else
			err(1, "bind BOOTPC port");
	}
	/*
	 * Build a request.
	 */
	bp = (struct bootp *) sndbuf;
	bzero(bp, sizeof(*bp));
	bp->bp_op = BOOTREQUEST;
	xid = (int32) getpid();
	bp->bp_xid = (u_int32) htonl(xid);
	if (bp_file)
		strncpy(bp->bp_file, bp_file, BP_FILE_LEN);

	/*
	 * Fill in the hardware address (or client IP address)
	 */
	if (use_hwa) {
		struct ifreq *ifr;

		ifr = getif(s, &sin_server.sin_addr);
		if (!ifr) {
			printf("No interface for %s\n", servername);
			exit(1);
		}
		if (getether(ifr->ifr_name, (char*)eaddr)) {
			printf("Can not get ether addr for %s\n", ifr->ifr_name);
			exit(1);
		}
		/* Copy Ethernet address into request packet. */
		bp->bp_htype = 1;
		bp->bp_hlen = 6;
		bcopy(eaddr, bp->bp_chaddr, bp->bp_hlen);
	} else {
		/* Fill in the client IP address. */
		hep = gethostbyname(hostname);
		if (!hep) {
			printf("Can not get my IP address\n");
			exit(1);
		}
		bcopy(hep->h_addr, &bp->bp_ciaddr, hep->h_length);
	}

	/*
	 * Copy in the default vendor data.
	 */
	bcopy((char*)&vend_magic, bp->bp_vend, 4);
	if (vend_magic)
		bp->bp_vend[4] = TAG_END;

	/*
	 * Read in the "options" part of the request.
	 * This also determines the size of the packet.
	 */
	snaplen = sizeof(*bp);
	if (vendor_file) {
		int fd = open(vendor_file, 0);
		if (fd < 0) {
			perror(vendor_file);
			exit(1);
		}
		/* Compute actual space for options. */
		n = BUFLEN - sizeof(*bp) + BP_VEND_LEN;
		n = read(fd, bp->bp_vend, n);
		close(fd);
		if (n < 0) {
			perror(vendor_file);
			exit(1);
		}
		printf("read %d bytes of vendor template\n", n);
		if (n > BP_VEND_LEN) {
			printf("warning: extended options in use (len > %d)\n",
				   BP_VEND_LEN);
			snaplen += (n - BP_VEND_LEN);
		}
	}
	/*
	 * Set globals needed by print_bootp
	 * (called by send_request)
	 */
	packetp = (unsigned char *) eaddr;
	snapend = (unsigned char *) sndbuf + snaplen;

	/* Send a request once per second while waiting for replies. */
	recvcnt = 0;
	bp->bp_secs = secs = 0;
	send_request(s);
	while (1) {
		struct timeval tv;
		int readfds;

		tv.tv_sec = WAITSECS;
		tv.tv_usec = 0L;
		readfds = (1 << s);
		n = select(s + 1, (fd_set *) & readfds, NULL, NULL, &tv);
		if (n < 0) {
			perror("select");
			break;
		}
		if (n == 0) {
			/*
			 * We have not received a response in the last second.
			 * If we have ever received any responses, exit now.
			 * Otherwise, bump the "wait time" field and re-send.
			 */
			if (recvcnt > 0)
				exit(0);
			secs += WAITSECS;
			if (secs > MAXWAIT)
				break;
			bp->bp_secs = htons(secs);
			send_request(s);
			continue;
		}
		fromlen = sizeof(sin_from);
		n = recvfrom(s, rcvbuf, BUFLEN, 0,
					 (struct sockaddr *) &sin_from, &fromlen);
		if (n <= 0) {
			continue;
		}
		if (n < sizeof(struct bootp)) {
			printf("received short packet\n");
			continue;
		}
		recvcnt++;

		/* Print the received packet. */
		printf("Recvd from %s", inet_ntoa(sin_from.sin_addr));
		/* set globals needed by bootp_print() */
		snaplen = n;
		snapend = (unsigned char *) rcvbuf + snaplen;
		bootp_print((struct bootp *)rcvbuf, n, sin_from.sin_port, 0);
		putchar('\n');
		/*
		 * This no longer exits immediately after receiving
		 * one response because it is useful to know if the
		 * client might get multiple responses.  This code
		 * will now listen for one second after a response.
		 */
	}
	errx(1, "no response from %s", servername);
}

static void
send_request(s)
	int s;
{
	/* Print the request packet. */
	printf("Sending to %s", inet_ntoa(sin_server.sin_addr));
	bootp_print((struct bootp *)sndbuf, snaplen, sin_from.sin_port, 0);
	putchar('\n');

	/* Send the request packet. */
	if (sendto(s, sndbuf, snaplen, 0,
			   (struct sockaddr *) &sin_server,
			   sizeof(sin_server)) < 0)
	{
		perror("sendto server");
		exit(1);
	}
}

/*
 * Print out a filename (or other ascii string).
 * Return true if truncated.
 */
int
printfn(s, ep)
	u_char *s, *ep;
{
	u_char c;

	putchar('"');
	while ((c = *s++) != '\0') {
		if (s > ep) {
			putchar('"');
			return (1);
		}
		if (!isascii(c)) {
			c = toascii(c);
			putchar('M');
			putchar('-');
		}
		if (!isprint(c)) {
			c ^= 0x40;			/* DEL to ?, others to alpha */
			putchar('^');
		}
		putchar(c);
	}
	putchar('"');
	return (0);
}

/*
 * Convert an IP addr to a string.
 * (like inet_ntoa, but ina is a pointer)
 */
char *
ipaddr_string(ina)
	struct in_addr *ina;
{
	static char b[24];
	u_char *p;

	p = (u_char *) ina;
	snprintf(b, sizeof(b), "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
	return (b);
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
