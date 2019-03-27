/*
 * natd - Network Address Translation Daemon for FreeBSD.
 *
 * This software is provided free of charge, with no 
 * warranty of any kind, either expressed or implied.
 * Use at your own risk.
 * 
 * You may copy, modify and distribute this software (natd.c) freely.
 *
 * Ari Suutari <suutari@iki.fi>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define SYSLOG_NAMES

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/queue.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <machine/in_cksum.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <arpa/inet.h>

#include <alias.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "natd.h"

struct instance {
	const char		*name;
	struct libalias		*la;
	LIST_ENTRY(instance)	list;

	int			ifIndex;
	int			assignAliasAddr;
	char*			ifName;
	int			logDropped;
	u_short			inPort;
	u_short			outPort;
	u_short			inOutPort;
	struct in_addr		aliasAddr;
	int			ifMTU;
	int			aliasOverhead;
	int			dropIgnoredIncoming;
	int			divertIn;
	int			divertOut;
	int			divertInOut;
};

static LIST_HEAD(, instance) root = LIST_HEAD_INITIALIZER(root);

struct libalias *mla;
static struct instance *mip;
static int ninstance = 1;

/* 
 * Default values for input and output
 * divert socket ports.
 */

#define	DEFAULT_SERVICE	"natd"

/*
 * Definition of a port range, and macros to deal with values.
 * FORMAT:  HI 16-bits == first port in range, 0 == all ports.
 *          LO 16-bits == number of ports in range
 * NOTES:   - Port values are not stored in network byte order.
 */

typedef u_long port_range;

#define GETLOPORT(x)     ((x) >> 0x10)
#define GETNUMPORTS(x)   ((x) & 0x0000ffff)
#define GETHIPORT(x)     (GETLOPORT((x)) + GETNUMPORTS((x)))

/* Set y to be the low-port value in port_range variable x. */
#define SETLOPORT(x,y)   ((x) = ((x) & 0x0000ffff) | ((y) << 0x10))

/* Set y to be the number of ports in port_range variable x. */
#define SETNUMPORTS(x,y) ((x) = ((x) & 0xffff0000) | (y))

/*
 * Function prototypes.
 */

static void	DoAliasing (int fd, int direction);
static void	DaemonMode (void);
static void	HandleRoutingInfo (int fd);
static void	Usage (void);
static char*	FormatPacket (struct ip*);
static void	PrintPacket (struct ip*);
static void	SyslogPacket (struct ip*, int priority, const char *label);
static int	SetAliasAddressFromIfName (const char *ifName);
static void	InitiateShutdown (int);
static void	Shutdown (int);
static void	RefreshAddr (int);
static void	ParseOption (const char* option, const char* parms);
static void	ReadConfigFile (const char* fileName);
static void	SetupPortRedirect (const char* parms);
static void	SetupProtoRedirect(const char* parms);
static void	SetupAddressRedirect (const char* parms);
static void	StrToAddr (const char* str, struct in_addr* addr);
static u_short  StrToPort (const char* str, const char* proto);
static int      StrToPortRange (const char* str, const char* proto, port_range *portRange);
static int 	StrToProto (const char* str);
static int      StrToAddrAndPortRange (char* str, struct in_addr* addr, char* proto, port_range *portRange);
static void	ParseArgs (int argc, char** argv);
static void	SetupPunchFW(const char *strValue);
static void	SetupSkinnyPort(const char *strValue);
static void	NewInstance(const char *name);
static void	DoGlobal (int fd);
static int	CheckIpfwRulenum(unsigned int rnum);

/*
 * Globals.
 */

static	int			verbose;
static 	int			background;
static	int			running;
static	int			logFacility;

static 	int			dynamicMode;
static 	int			icmpSock;
static	int			logIpfwDenied;
static	const char*		pidName;
static	int			routeSock;
static	int			globalPort;
static	int			divertGlobal;
static	int			exitDelay;


int main (int argc, char** argv)
{
	struct sockaddr_in	addr;
	fd_set			readMask;
	int			fdMax;
	int			rval;
/* 
 * Initialize packet aliasing software.
 * Done already here to be able to alter option bits
 * during command line and configuration file processing.
 */
	NewInstance("default");

/*
 * Parse options.
 */
	verbose 		= 0;
	background		= 0;
	running			= 1;
	dynamicMode		= 0;
 	logFacility		= LOG_DAEMON;
	logIpfwDenied		= -1;
	pidName			= PIDFILE;
	routeSock 		= -1;
	icmpSock 		= -1;
	fdMax	 		= -1;
	divertGlobal		= -1;
	exitDelay		= EXIT_DELAY;

	ParseArgs (argc, argv);
/*
 * Log ipfw(8) denied packets by default in verbose mode.
 */
	if (logIpfwDenied == -1)
		logIpfwDenied = verbose;
/*
 * Open syslog channel.
 */
	openlog ("natd", LOG_CONS | LOG_PID | (verbose ? LOG_PERROR : 0),
		 logFacility);

	LIST_FOREACH(mip, &root, list) {
		mla = mip->la;
/*
 * If not doing the transparent proxying only,
 * check that valid aliasing address has been given.
 */
		if (mip->aliasAddr.s_addr == INADDR_NONE && mip->ifName == NULL &&
		    !(LibAliasSetMode(mla, 0,0) & PKT_ALIAS_PROXY_ONLY))
			errx (1, "instance %s: aliasing address not given", mip->name);

		if (mip->aliasAddr.s_addr != INADDR_NONE && mip->ifName != NULL)
			errx (1, "both alias address and interface "
				 "name are not allowed");
/*
 * Check that valid port number is known.
 */
		if (mip->inPort != 0 || mip->outPort != 0)
			if (mip->inPort == 0 || mip->outPort == 0)
				errx (1, "both input and output ports are required");

		if (mip->inPort == 0 && mip->outPort == 0 && mip->inOutPort == 0)
			ParseOption ("port", DEFAULT_SERVICE);

/*
 * Check if ignored packets should be dropped.
 */
		mip->dropIgnoredIncoming = LibAliasSetMode (mla, 0, 0);
		mip->dropIgnoredIncoming &= PKT_ALIAS_DENY_INCOMING;
/*
 * Create divert sockets. Use only one socket if -p was specified
 * on command line. Otherwise, create separate sockets for
 * outgoing and incoming connections.
 */
		if (mip->inOutPort) {

			mip->divertInOut = socket (PF_INET, SOCK_RAW, IPPROTO_DIVERT);
			if (mip->divertInOut == -1)
				Quit ("Unable to create divert socket.");
			if (mip->divertInOut > fdMax)
				fdMax = mip->divertInOut;

			mip->divertIn  = -1;
			mip->divertOut = -1;
/*
 * Bind socket.
 */

			addr.sin_family		= AF_INET;
			addr.sin_addr.s_addr	= INADDR_ANY;
			addr.sin_port		= mip->inOutPort;

			if (bind (mip->divertInOut,
				  (struct sockaddr*) &addr,
				  sizeof addr) == -1)
				Quit ("Unable to bind divert socket.");
		}
		else {

			mip->divertIn = socket (PF_INET, SOCK_RAW, IPPROTO_DIVERT);
			if (mip->divertIn == -1)
				Quit ("Unable to create incoming divert socket.");
			if (mip->divertIn > fdMax)
				fdMax = mip->divertIn;


			mip->divertOut = socket (PF_INET, SOCK_RAW, IPPROTO_DIVERT);
			if (mip->divertOut == -1)
				Quit ("Unable to create outgoing divert socket.");
			if (mip->divertOut > fdMax)
				fdMax = mip->divertOut;

			mip->divertInOut = -1;

/*
 * Bind divert sockets.
 */

			addr.sin_family		= AF_INET;
			addr.sin_addr.s_addr	= INADDR_ANY;
			addr.sin_port		= mip->inPort;

			if (bind (mip->divertIn,
				  (struct sockaddr*) &addr,
				  sizeof addr) == -1)
				Quit ("Unable to bind incoming divert socket.");

			addr.sin_family		= AF_INET;
			addr.sin_addr.s_addr	= INADDR_ANY;
			addr.sin_port		= mip->outPort;

			if (bind (mip->divertOut,
				  (struct sockaddr*) &addr,
				  sizeof addr) == -1)
				Quit ("Unable to bind outgoing divert socket.");
		}
/*
 * Create routing socket if interface name specified and in dynamic mode.
 */
		if (mip->ifName) {
			if (dynamicMode) {

				if (routeSock == -1)
					routeSock = socket (PF_ROUTE, SOCK_RAW, 0);
				if (routeSock == -1)
					Quit ("Unable to create routing info socket.");
				if (routeSock > fdMax)
					fdMax = routeSock;

				mip->assignAliasAddr = 1;
			}
			else {
				do {
					rval = SetAliasAddressFromIfName (mip->ifName);
					if (background == 0 || dynamicMode == 0)
						break;
					if (rval == EAGAIN)
						sleep(1);
				} while (rval == EAGAIN);
				if (rval != 0)
					exit(1);
			}
		}

	}
	if (globalPort) {

		divertGlobal = socket (PF_INET, SOCK_RAW, IPPROTO_DIVERT);
		if (divertGlobal == -1)
			Quit ("Unable to create divert socket.");
		if (divertGlobal > fdMax)
			fdMax = divertGlobal;

/*
* Bind socket.
*/

		addr.sin_family		= AF_INET;
		addr.sin_addr.s_addr	= INADDR_ANY;
		addr.sin_port		= globalPort;

		if (bind (divertGlobal,
			  (struct sockaddr*) &addr,
			  sizeof addr) == -1)
			Quit ("Unable to bind global divert socket.");
	}
/*
 * Create socket for sending ICMP messages.
 */
	icmpSock = socket (AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (icmpSock == -1)
		Quit ("Unable to create ICMP socket.");

/*
 * And disable reads for the socket, otherwise it slowly fills
 * up with received icmps which we do not use.
 */
	shutdown(icmpSock, SHUT_RD);

/*
 * Become a daemon unless verbose mode was requested.
 */
	if (!verbose)
		DaemonMode ();
/*
 * Catch signals to manage shutdown and
 * refresh of interface address.
 */
	siginterrupt(SIGTERM, 1);
	siginterrupt(SIGHUP, 1);
	if (exitDelay)
		signal(SIGTERM, InitiateShutdown);
	else
		signal(SIGTERM, Shutdown);
	signal (SIGHUP, RefreshAddr);
/*
 * Set alias address if it has been given.
 */
	mip = LIST_FIRST(&root);	/* XXX: simon */
	LIST_FOREACH(mip, &root, list) {
		mla = mip->la;
		if (mip->aliasAddr.s_addr != INADDR_NONE)
			LibAliasSetAddress (mla, mip->aliasAddr);
	}

	while (running) {
		mip = LIST_FIRST(&root);	/* XXX: simon */

		if (mip->divertInOut != -1 && !mip->ifName && ninstance == 1) {
/*
 * When using only one socket, just call 
 * DoAliasing repeatedly to process packets.
 */
			DoAliasing (mip->divertInOut, DONT_KNOW);
			continue;
		}
/* 
 * Build read mask from socket descriptors to select.
 */
		FD_ZERO (&readMask);
/*
 * Check if new packets are available.
 */
		LIST_FOREACH(mip, &root, list) {
			if (mip->divertIn != -1)
				FD_SET (mip->divertIn, &readMask);

			if (mip->divertOut != -1)
				FD_SET (mip->divertOut, &readMask);

			if (mip->divertInOut != -1)
				FD_SET (mip->divertInOut, &readMask);
		}
/*
 * Routing info is processed always.
 */
		if (routeSock != -1)
			FD_SET (routeSock, &readMask);

		if (divertGlobal != -1)
			FD_SET (divertGlobal, &readMask);

		if (select (fdMax + 1,
			    &readMask,
			    NULL,
			    NULL,
			    NULL) == -1) {

			if (errno == EINTR)
				continue;

			Quit ("Select failed.");
		}

		if (divertGlobal != -1)
			if (FD_ISSET (divertGlobal, &readMask))
				DoGlobal (divertGlobal);
		LIST_FOREACH(mip, &root, list) {
			mla = mip->la;
			if (mip->divertIn != -1)
				if (FD_ISSET (mip->divertIn, &readMask))
					DoAliasing (mip->divertIn, INPUT);

			if (mip->divertOut != -1)
				if (FD_ISSET (mip->divertOut, &readMask))
					DoAliasing (mip->divertOut, OUTPUT);

			if (mip->divertInOut != -1) 
				if (FD_ISSET (mip->divertInOut, &readMask))
					DoAliasing (mip->divertInOut, DONT_KNOW);

		}
		if (routeSock != -1)
			if (FD_ISSET (routeSock, &readMask))
				HandleRoutingInfo (routeSock);
	}

	if (background)
		unlink (pidName);

	return 0;
}

static void DaemonMode(void)
{
	FILE*	pidFile;

	daemon (0, 0);
	background = 1;

	pidFile = fopen (pidName, "w");
	if (pidFile) {

		fprintf (pidFile, "%d\n", getpid ());
		fclose (pidFile);
	}
}

static void ParseArgs (int argc, char** argv)
{
	int		arg;
	char*		opt;
	char		parmBuf[256];
	int		len; /* bounds checking */

	for (arg = 1; arg < argc; arg++) {

		opt  = argv[arg];
		if (*opt != '-') {

			warnx ("invalid option %s", opt);
			Usage ();
		}

		parmBuf[0] = '\0';
		len = 0;

		while (arg < argc - 1) {

			if (argv[arg + 1][0] == '-')
				break;

			if (len) {
				strncat (parmBuf, " ", sizeof(parmBuf) - (len + 1));
				len += strlen(parmBuf + len);
			}

			++arg;
			strncat (parmBuf, argv[arg], sizeof(parmBuf) - (len + 1));
			len += strlen(parmBuf + len);

		}

		ParseOption (opt + 1, (len ? parmBuf : NULL));

	}
}

static void DoGlobal (int fd)
{
	int			bytes;
	int			origBytes;
	char			buf[IP_MAXPACKET];
	struct sockaddr_in	addr;
	int			wrote;
	socklen_t		addrSize;
	struct ip*		ip;
	char			msgBuf[80];

/*
 * Get packet from socket.
 */
	addrSize  = sizeof addr;
	origBytes = recvfrom (fd,
			      buf,
			      sizeof buf,
			      0,
			      (struct sockaddr*) &addr,
			      &addrSize);

	if (origBytes == -1) {

		if (errno != EINTR)
			Warn ("read from divert socket failed");

		return;
	}

#if 0
	if (mip->assignAliasAddr) {
		if (SetAliasAddressFromIfName (mip->ifName) != 0)
			exit(1);
		mip->assignAliasAddr = 0;
	}
#endif
/*
 * This is an IP packet.
 */
	ip = (struct ip*) buf;

	if (verbose) {
/*
 * Print packet direction and protocol type.
 */
		printf ("Glb ");

		switch (ip->ip_p) {
		case IPPROTO_TCP:
			printf ("[TCP]  ");
			break;

		case IPPROTO_UDP:
			printf ("[UDP]  ");
			break;

		case IPPROTO_ICMP:
			printf ("[ICMP] ");
			break;

		default:
			printf ("[%d]    ", ip->ip_p);
			break;
		}
/*
 * Print addresses.
 */
		PrintPacket (ip);
	}

	LIST_FOREACH(mip, &root, list) {
		mla = mip->la;
		if (LibAliasOutTry (mla, buf, IP_MAXPACKET, 0) != PKT_ALIAS_IGNORED)
			break;
	}
/*
 * Length might have changed during aliasing.
 */
	bytes = ntohs (ip->ip_len);
/*
 * Update alias overhead size for outgoing packets.
 */
	if (mip != NULL && bytes - origBytes > mip->aliasOverhead)
		mip->aliasOverhead = bytes - origBytes;

	if (verbose) {
		
/*
 * Print addresses after aliasing.
 */
		printf (" aliased to\n");
		printf ("           ");
		PrintPacket (ip);
		printf ("\n");
	}

/*
 * Put packet back for processing.
 */
	wrote = sendto (fd, 
		        buf,
	    		bytes,
	    		0,
	    		(struct sockaddr*) &addr,
	    		sizeof addr);
	
	if (wrote != bytes) {

		if (errno == EMSGSIZE && mip != NULL) {

			if (mip->ifMTU != -1)
				SendNeedFragIcmp (icmpSock,
						  (struct ip*) buf,
						  mip->ifMTU - mip->aliasOverhead);
		}
		else if (errno == EACCES && logIpfwDenied) {

			sprintf (msgBuf, "failed to write packet back");
			Warn (msgBuf);
		}
	}
}


static void DoAliasing (int fd, int direction)
{
	int			bytes;
	int			origBytes;
	char			buf[IP_MAXPACKET];
	struct sockaddr_in	addr;
	int			wrote;
	int			status;
	socklen_t		addrSize;
	struct ip*		ip;
	char			msgBuf[80];
	int			rval;

	if (mip->assignAliasAddr) {
		do {
			rval = SetAliasAddressFromIfName (mip->ifName);
			if (background == 0 || dynamicMode == 0)
				break;
			if (rval == EAGAIN)
				sleep(1);
		} while (rval == EAGAIN);
		if (rval != 0)
			exit(1);
		mip->assignAliasAddr = 0;
	}
/*
 * Get packet from socket.
 */
	addrSize  = sizeof addr;
	origBytes = recvfrom (fd,
			      buf,
			      sizeof buf,
			      0,
			      (struct sockaddr*) &addr,
			      &addrSize);

	if (origBytes == -1) {

		if (errno != EINTR)
			Warn ("read from divert socket failed");

		return;
	}
/*
 * This is an IP packet.
 */
	ip = (struct ip*) buf;
	if (direction == DONT_KNOW) {
		if (addr.sin_addr.s_addr == INADDR_ANY)
			direction = OUTPUT;
		else
			direction = INPUT;
	}

	if (verbose) {
/*
 * Print packet direction and protocol type.
 */
		printf (direction == OUTPUT ? "Out " : "In  ");
		if (ninstance > 1)
			printf ("{%s}", mip->name);

		switch (ip->ip_p) {
		case IPPROTO_TCP:
			printf ("[TCP]  ");
			break;

		case IPPROTO_UDP:
			printf ("[UDP]  ");
			break;

		case IPPROTO_ICMP:
			printf ("[ICMP] ");
			break;

		default:
			printf ("[%d]    ", ip->ip_p);
			break;
		}
/*
 * Print addresses.
 */
		PrintPacket (ip);
	}

	if (direction == OUTPUT) {
/*
 * Outgoing packets. Do aliasing.
 */
		LibAliasOut (mla, buf, IP_MAXPACKET);
	}
	else {

/*
 * Do aliasing.
 */	
		status = LibAliasIn (mla, buf, IP_MAXPACKET);
		if (status == PKT_ALIAS_IGNORED &&
		    mip->dropIgnoredIncoming) {

			if (verbose)
				printf (" dropped.\n");

			if (mip->logDropped)
				SyslogPacket (ip, LOG_WARNING, "denied");

			return;
		}
	}
/*
 * Length might have changed during aliasing.
 */
	bytes = ntohs (ip->ip_len);
/*
 * Update alias overhead size for outgoing packets.
 */
	if (direction == OUTPUT &&
	    bytes - origBytes > mip->aliasOverhead)
		mip->aliasOverhead = bytes - origBytes;

	if (verbose) {
		
/*
 * Print addresses after aliasing.
 */
		printf (" aliased to\n");
		printf ("           ");
		PrintPacket (ip);
		printf ("\n");
	}

/*
 * Put packet back for processing.
 */
	wrote = sendto (fd, 
		        buf,
	    		bytes,
	    		0,
	    		(struct sockaddr*) &addr,
	    		sizeof addr);
	
	if (wrote != bytes) {

		if (errno == EMSGSIZE) {

			if (direction == OUTPUT &&
			    mip->ifMTU != -1)
				SendNeedFragIcmp (icmpSock,
						  (struct ip*) buf,
						  mip->ifMTU - mip->aliasOverhead);
		}
		else if (errno == EACCES && logIpfwDenied) {

			sprintf (msgBuf, "failed to write packet back");
			Warn (msgBuf);
		}
	}
}

static void HandleRoutingInfo (int fd)
{
	int			bytes;
	struct if_msghdr	ifMsg;
/*
 * Get packet from socket.
 */
	bytes = read (fd, &ifMsg, sizeof ifMsg);
	if (bytes == -1) {

		Warn ("read from routing socket failed");
		return;
	}

	if (ifMsg.ifm_version != RTM_VERSION) {

		Warn ("unexpected packet read from routing socket");
		return;
	}

	if (verbose)
		printf ("Routing message %#x received.\n", ifMsg.ifm_type);

	if ((ifMsg.ifm_type == RTM_NEWADDR || ifMsg.ifm_type == RTM_IFINFO)) {
		LIST_FOREACH(mip, &root, list) {
			mla = mip->la;
			if (ifMsg.ifm_index == mip->ifIndex) {
				if (verbose)
					printf("Interface address/MTU has probably changed.\n");
				mip->assignAliasAddr = 1;
			}
		}
	}
}

static void PrintPacket (struct ip* ip)
{
	printf ("%s", FormatPacket (ip));
}

static void SyslogPacket (struct ip* ip, int priority, const char *label)
{
	syslog (priority, "%s %s", label, FormatPacket (ip));
}

static char* FormatPacket (struct ip* ip)
{
	static char	buf[256];
	struct tcphdr*	tcphdr;
	struct udphdr*	udphdr;
	struct icmp*	icmphdr;
	char		src[20];
	char		dst[20];

	strcpy (src, inet_ntoa (ip->ip_src));
	strcpy (dst, inet_ntoa (ip->ip_dst));

	switch (ip->ip_p) {
	case IPPROTO_TCP:
		tcphdr = (struct tcphdr*) ((char*) ip + (ip->ip_hl << 2));
		sprintf (buf, "[TCP] %s:%d -> %s:%d",
			      src,
			      ntohs (tcphdr->th_sport),
			      dst,
			      ntohs (tcphdr->th_dport));
		break;

	case IPPROTO_UDP:
		udphdr = (struct udphdr*) ((char*) ip + (ip->ip_hl << 2));
		sprintf (buf, "[UDP] %s:%d -> %s:%d",
			      src,
			      ntohs (udphdr->uh_sport),
			      dst,
			      ntohs (udphdr->uh_dport));
		break;

	case IPPROTO_ICMP:
		icmphdr = (struct icmp*) ((char*) ip + (ip->ip_hl << 2));
		sprintf (buf, "[ICMP] %s -> %s %u(%u)",
			      src,
			      dst,
			      icmphdr->icmp_type,
			      icmphdr->icmp_code);
		break;

	default:
		sprintf (buf, "[%d] %s -> %s ", ip->ip_p, src, dst);
		break;
	}

	return buf;
}

static int
SetAliasAddressFromIfName(const char *ifn)
{
	size_t needed;
	int mib[6];
	char *buf, *lim, *next;
	struct if_msghdr *ifm;
	struct ifa_msghdr *ifam;
	struct sockaddr_dl *sdl;
	struct sockaddr_in *sin;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET;	/* Only IP addresses please */
	mib[4] = NET_RT_IFLIST;
	mib[5] = 0;		/* ifIndex??? */
/*
 * Get interface data.
 */
	if (sysctl(mib, 6, NULL, &needed, NULL, 0) == -1)
		err(1, "iflist-sysctl-estimate");
	if ((buf = malloc(needed)) == NULL)
		errx(1, "malloc failed");
	if (sysctl(mib, 6, buf, &needed, NULL, 0) == -1 && errno != ENOMEM)
		err(1, "iflist-sysctl-get");
	lim = buf + needed;
/*
 * Loop through interfaces until one with
 * given name is found. This is done to
 * find correct interface index for routing
 * message processing.
 */
	mip->ifIndex	= 0;
	next = buf;
	while (next < lim) {
		ifm = (struct if_msghdr *)next;
		next += ifm->ifm_msglen;
		if (ifm->ifm_version != RTM_VERSION) {
			if (verbose)
				warnx("routing message version %d "
				      "not understood", ifm->ifm_version);
			continue;
		}
		if (ifm->ifm_type == RTM_IFINFO) {
			sdl = (struct sockaddr_dl *)(ifm + 1);
			if (strlen(ifn) == sdl->sdl_nlen &&
			    strncmp(ifn, sdl->sdl_data, sdl->sdl_nlen) == 0) {
				mip->ifIndex = ifm->ifm_index;
				mip->ifMTU = ifm->ifm_data.ifi_mtu;
				break;
			}
		}
	}
	if (!mip->ifIndex)
		errx(1, "unknown interface name %s", ifn);
/*
 * Get interface address.
 */
	sin = NULL;
	while (next < lim) {
		ifam = (struct ifa_msghdr *)next;
		next += ifam->ifam_msglen;
		if (ifam->ifam_version != RTM_VERSION) {
			if (verbose)
				warnx("routing message version %d "
				      "not understood", ifam->ifam_version);
			continue;
		}
		if (ifam->ifam_type != RTM_NEWADDR)
			break;
		if (ifam->ifam_addrs & RTA_IFA) {
			int i;
			char *cp = (char *)(ifam + 1);

			for (i = 1; i < RTA_IFA; i <<= 1)
				if (ifam->ifam_addrs & i)
					cp += SA_SIZE((struct sockaddr *)cp);
			if (((struct sockaddr *)cp)->sa_family == AF_INET) {
				sin = (struct sockaddr_in *)cp;
				break;
			}
		}
	}
	if (sin == NULL) {
		warnx("%s: cannot get interface address", ifn);
		free(buf);
		return EAGAIN;
	}

	LibAliasSetAddress(mla, sin->sin_addr);
	syslog(LOG_INFO, "Aliasing to %s, mtu %d bytes",
	       inet_ntoa(sin->sin_addr), mip->ifMTU);

	free(buf);

	return 0;
}

void Quit (const char* msg)
{
	Warn (msg);
	exit (1);
}

void Warn (const char* msg)
{
	if (background)
		syslog (LOG_ALERT, "%s (%m)", msg);
	else
		warn ("%s", msg);
}

static void RefreshAddr (int sig __unused)
{
	LibAliasRefreshModules();
	if (mip != NULL && mip->ifName != NULL)
		mip->assignAliasAddr = 1;
}

static void InitiateShutdown (int sig __unused)
{
/*
 * Start timer to allow kernel gracefully
 * shutdown existing connections when system
 * is shut down.
 */
	siginterrupt(SIGALRM, 1);
	signal (SIGALRM, Shutdown);
	ualarm(exitDelay*1000, 1000);
}

static void Shutdown (int sig __unused)
{
	running = 0;
}

/* 
 * Different options recognized by this program.
 */

enum Option {

	LibAliasOption,
	Instance,
	Verbose,
	InPort,
	OutPort,
	Port,
	GlobalPort,
	AliasAddress,
	TargetAddress,
	InterfaceName,
	RedirectPort,
	RedirectProto,
	RedirectAddress,
	ConfigFile,
	DynamicMode,
	ProxyRule,
 	LogDenied,
 	LogFacility,
	PunchFW,
	SkinnyPort,
	LogIpfwDenied,
	PidFile,
	ExitDelay
};

enum Param {
	
	YesNo,
	Numeric,
	String,
	None,
	Address,
	Service
};

/*
 * Option information structure (used by ParseOption).
 */

struct OptionInfo {
	
	enum Option		type;
	int			packetAliasOpt;
	enum Param		parm;
	const char*		parmDescription;
	const char*		description;
	const char*		name; 
	const char*		shortName;
};

/*
 * Table of known options.
 */

static struct OptionInfo optionTable[] = {

	{ LibAliasOption,
		PKT_ALIAS_UNREGISTERED_ONLY,
		YesNo,
		"[yes|no]",
		"alias only unregistered addresses",
		"unregistered_only",
		"u" },

	{ LibAliasOption,
		PKT_ALIAS_LOG,
		YesNo,
		"[yes|no]",
		"enable logging",
		"log",
		"l" },

	{ LibAliasOption,
		PKT_ALIAS_PROXY_ONLY,
		YesNo,
		"[yes|no]",
		"proxy only",
		"proxy_only",
		NULL },

	{ LibAliasOption,
		PKT_ALIAS_REVERSE,
		YesNo,
		"[yes|no]",
		"operate in reverse mode",
		"reverse",
		NULL },

	{ LibAliasOption,
		PKT_ALIAS_DENY_INCOMING,
		YesNo,
		"[yes|no]",
		"allow incoming connections",
		"deny_incoming",
		"d" },

	{ LibAliasOption,
		PKT_ALIAS_USE_SOCKETS,
		YesNo,
		"[yes|no]",
		"use sockets to inhibit port conflict",
		"use_sockets",
		"s" },

	{ LibAliasOption,
		PKT_ALIAS_SAME_PORTS,
		YesNo,
		"[yes|no]",
		"try to keep original port numbers for connections",
		"same_ports",
		"m" },

	{ Verbose,
		0,
		YesNo,
		"[yes|no]",
		"verbose mode, dump packet information",
		"verbose",
		"v" },
	
	{ DynamicMode,
		0,
		YesNo,
		"[yes|no]",
		"dynamic mode, automatically detect interface address changes",
		"dynamic",
		NULL },
	
	{ InPort,
		0,
		Service,
		"number|service_name",
		"set port for incoming packets",
		"in_port",
		"i" },
	
	{ OutPort,
		0,
		Service,
		"number|service_name",
		"set port for outgoing packets",
		"out_port",
		"o" },
	
	{ Port,
		0,
		Service,
		"number|service_name",
		"set port (defaults to natd/divert)",
		"port",
		"p" },
	
	{ GlobalPort,
		0,
		Service,
		"number|service_name",
		"set globalport",
		"globalport",
		NULL },
	
	{ AliasAddress,
		0,
		Address,
		"x.x.x.x",
		"address to use for aliasing",
		"alias_address",
		"a" },
	
	{ TargetAddress,
		0,
		Address,
		"x.x.x.x",
		"address to use for incoming sessions",
		"target_address",
		"t" },
	
	{ InterfaceName,
		0,
		String,
	        "network_if_name",
		"take aliasing address from interface",
		"interface",
		"n" },

	{ ProxyRule,
		0,
		String,
	        "[type encode_ip_hdr|encode_tcp_stream] port xxxx server "
		"a.b.c.d:yyyy",
		"add transparent proxying / destination NAT",
		"proxy_rule",
		NULL },

	{ RedirectPort,
		0,
		String,
	        "tcp|udp local_addr:local_port_range[,...] [public_addr:]public_port_range"
	 	" [remote_addr[:remote_port_range]]",
		"redirect a port (or ports) for incoming traffic",
		"redirect_port",
		NULL },

	{ RedirectProto,
		0,
		String,
	        "proto local_addr [public_addr] [remote_addr]",
		"redirect packets of a given proto",
		"redirect_proto",
		NULL },

	{ RedirectAddress,
		0,
		String,
	        "local_addr[,...] public_addr",
		"define mapping between local and public addresses",
		"redirect_address",
		NULL },

	{ ConfigFile,
		0,
		String,
		"file_name",
		"read options from configuration file",
		"config",
		"f" },

	{ LogDenied,
		0,
		YesNo,
	        "[yes|no]",
		"enable logging of denied incoming packets",
		"log_denied",
		NULL },

	{ LogFacility,
		0,
		String,
	        "facility",
		"name of syslog facility to use for logging",
		"log_facility",
		NULL },

	{ PunchFW,
		0,
		String,
	        "basenumber:count",
		"punch holes in the firewall for incoming FTP/IRC DCC connections",
		"punch_fw",
		NULL },

	{ SkinnyPort,
		0,
		String,
		"port",
		"set the TCP port for use with the Skinny Station protocol",
		"skinny_port",
		NULL },

	{ LogIpfwDenied,
		0,
		YesNo,
	        "[yes|no]",
		"log packets converted by natd, but denied by ipfw",
		"log_ipfw_denied",
		NULL },

	{ PidFile,
		0,
		String,
		"file_name",
		"store PID in an alternate file",
		"pid_file",
		"P" },
	{ Instance,
		0,
		String,
		"instance name",
		"name of aliasing engine instance",
		"instance",
		NULL },
	{ ExitDelay,
		0,
		Numeric,
		"ms",
		"delay in ms before daemon exit after signal",
		"exit_delay",
		NULL },
};
	
static void ParseOption (const char* option, const char* parms)
{
	int			i;
	struct OptionInfo*	info;
	int			yesNoValue;
	int			aliasValue;
	int			numValue;
	u_short			uNumValue;
	const char*		strValue;
	struct in_addr		addrValue;
	int			max;
	char*			end;
	const CODE* 		fac_record = NULL;
/*
 * Find option from table.
 */
	max = sizeof (optionTable) / sizeof (struct OptionInfo);
	for (i = 0, info = optionTable; i < max; i++, info++) {

		if (!strcmp (info->name, option))
			break;

		if (info->shortName)
			if (!strcmp (info->shortName, option))
				break;
	}

	if (i >= max) {

		warnx ("unknown option %s", option);
		Usage ();
	}

	uNumValue	= 0;
	yesNoValue	= 0;
	numValue	= 0;
	strValue	= NULL;
/*
 * Check parameters.
 */
	switch (info->parm) {
	case YesNo:
		if (!parms)
			parms = "yes";

		if (!strcmp (parms, "yes"))
			yesNoValue = 1;
		else
			if (!strcmp (parms, "no"))
				yesNoValue = 0;
			else
				errx (1, "%s needs yes/no parameter", option);
		break;

	case Service:
		if (!parms)
			errx (1, "%s needs service name or "
				 "port number parameter",
				 option);

		uNumValue = StrToPort (parms, "divert");
		break;

	case Numeric:
		if (parms)
			numValue = strtol (parms, &end, 10);
		else
			end = NULL;

		if (end == parms)
			errx (1, "%s needs numeric parameter", option);
		break;

	case String:
		strValue = parms;
		if (!strValue)
			errx (1, "%s needs parameter", option);
		break;

	case None:
		if (parms)
			errx (1, "%s does not take parameters", option);
		break;

	case Address:
		if (!parms)
			errx (1, "%s needs address/host parameter", option);

		StrToAddr (parms, &addrValue);
		break;
	}

	switch (info->type) {
	case LibAliasOption:
	
		aliasValue = yesNoValue ? info->packetAliasOpt : 0;
		LibAliasSetMode (mla, aliasValue, info->packetAliasOpt);
		break;

	case Verbose:
		verbose = yesNoValue;
		break;

	case DynamicMode:
		dynamicMode = yesNoValue;
		break;

	case InPort:
		mip->inPort = uNumValue;
		break;

	case OutPort:
		mip->outPort = uNumValue;
		break;

	case Port:
		mip->inOutPort = uNumValue;
		break;

	case GlobalPort:
		globalPort = uNumValue;
		break;

	case AliasAddress:
		memcpy (&mip->aliasAddr, &addrValue, sizeof (struct in_addr));
		break;

	case TargetAddress:
		LibAliasSetTarget(mla, addrValue);
		break;

	case RedirectPort:
		SetupPortRedirect (strValue);
		break;

	case RedirectProto:
		SetupProtoRedirect(strValue);
		break;

	case RedirectAddress:
		SetupAddressRedirect (strValue);
		break;

	case ProxyRule:
		LibAliasProxyRule (mla, strValue);
		break;

	case InterfaceName:
		if (mip->ifName)
			free (mip->ifName);

		mip->ifName = strdup (strValue);
		break;

	case ConfigFile:
		ReadConfigFile (strValue);
		break;

	case LogDenied:
		mip->logDropped = yesNoValue;
		break;

	case LogFacility:

		fac_record = facilitynames;
		while (fac_record->c_name != NULL) {

			if (!strcmp (fac_record->c_name, strValue)) {

				logFacility = fac_record->c_val;
				break;

			}
			else
				fac_record++;
		}

		if(fac_record->c_name == NULL)
			errx(1, "Unknown log facility name: %s", strValue);	

		break;

	case PunchFW:
		SetupPunchFW(strValue);
		break;

	case SkinnyPort:
		SetupSkinnyPort(strValue);
		break;

	case LogIpfwDenied:
		logIpfwDenied = yesNoValue;
		break;

	case PidFile:
		pidName = strdup (strValue);
		break;
	case Instance:
		NewInstance(strValue);
		break;
	case ExitDelay:
		if (numValue < 0 || numValue > MAX_EXIT_DELAY)
			errx(1, "Incorrect exit delay: %d", numValue);	
		exitDelay = numValue;
		break;
	}
}

void ReadConfigFile (const char* fileName)
{
	FILE*	file;
	char	*buf;
	size_t	len;
	char	*ptr, *p;
	char*	option;

	file = fopen (fileName, "r");
	if (!file)
		err(1, "cannot open config file %s", fileName);

	while ((buf = fgetln(file, &len)) != NULL) {
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		else
			errx(1, "config file format error: "
				"last line should end with newline");

/*
 * Check for comments, strip off trailing spaces.
 */
		if ((ptr = strchr(buf, '#')))
			*ptr = '\0';
		for (ptr = buf; isspace(*ptr); ++ptr)
			continue;
		if (*ptr == '\0')
			continue;
		for (p = strchr(buf, '\0'); isspace(*--p);)
			continue;
		*++p = '\0';

/*
 * Extract option name.
 */
		option = ptr;
		while (*ptr && !isspace (*ptr))
			++ptr;

		if (*ptr != '\0') {

			*ptr = '\0';
			++ptr;
		}
/*
 * Skip white space between name and parms.
 */
		while (*ptr && isspace (*ptr))
			++ptr;

		ParseOption (option, *ptr ? ptr : NULL);
	}

	fclose (file);
}

static void Usage(void)
{
	int			i;
	int			max;
	struct OptionInfo*	info;

	fprintf (stderr, "Recognized options:\n\n");

	max = sizeof (optionTable) / sizeof (struct OptionInfo);
	for (i = 0, info = optionTable; i < max; i++, info++) {

		fprintf (stderr, "-%-20s %s\n", info->name,
						info->parmDescription);

		if (info->shortName)
			fprintf (stderr, "-%-20s %s\n", info->shortName,
							info->parmDescription);

		fprintf (stderr, "      %s\n\n", info->description);
	}

	exit (1);
}

void SetupPortRedirect (const char* parms)
{
	char		*buf;
	char*		ptr;
	char*		serverPool;
	struct in_addr	localAddr;
	struct in_addr	publicAddr;
	struct in_addr	remoteAddr;
	port_range      portRange;
	u_short         localPort      = 0;
	u_short         publicPort     = 0;
	u_short         remotePort     = 0;
	u_short         numLocalPorts  = 0;
	u_short         numPublicPorts = 0;
	u_short         numRemotePorts = 0;
	int		proto;
	char*		protoName;
	char*		separator;
	int             i;
	struct alias_link *aliaslink = NULL;

	buf = strdup (parms);
	if (!buf)
		errx (1, "redirect_port: strdup() failed");
/*
 * Extract protocol.
 */
	protoName = strtok (buf, " \t");
	if (!protoName)
		errx (1, "redirect_port: missing protocol");

	proto = StrToProto (protoName);
/*
 * Extract local address.
 */
	ptr = strtok (NULL, " \t");
	if (!ptr)
		errx (1, "redirect_port: missing local address");

	separator = strchr(ptr, ',');
	if (separator) {		/* LSNAT redirection syntax. */
		localAddr.s_addr = INADDR_NONE;
		localPort = ~0;
		numLocalPorts = 1;
		serverPool = ptr;
	} else {
		if ( StrToAddrAndPortRange (ptr, &localAddr, protoName, &portRange) != 0 )
			errx (1, "redirect_port: invalid local port range");

		localPort     = GETLOPORT(portRange);
		numLocalPorts = GETNUMPORTS(portRange);
		serverPool = NULL;
	}

/*
 * Extract public port and optionally address.
 */
	ptr = strtok (NULL, " \t");
	if (!ptr)
		errx (1, "redirect_port: missing public port");

	separator = strchr (ptr, ':');
	if (separator) {
	        if (StrToAddrAndPortRange (ptr, &publicAddr, protoName, &portRange) != 0 )
		        errx (1, "redirect_port: invalid public port range");
	}
	else {
		publicAddr.s_addr = INADDR_ANY;
		if (StrToPortRange (ptr, protoName, &portRange) != 0)
		        errx (1, "redirect_port: invalid public port range");
	}

	publicPort     = GETLOPORT(portRange);
	numPublicPorts = GETNUMPORTS(portRange);

/*
 * Extract remote address and optionally port.
 */
	ptr = strtok (NULL, " \t");
	if (ptr) {
		separator = strchr (ptr, ':');
		if (separator) {
		        if (StrToAddrAndPortRange (ptr, &remoteAddr, protoName, &portRange) != 0)
			        errx (1, "redirect_port: invalid remote port range");
		} else {
		        SETLOPORT(portRange, 0);
			SETNUMPORTS(portRange, 1);
			StrToAddr (ptr, &remoteAddr);
		}
	}
	else {
	        SETLOPORT(portRange, 0);
		SETNUMPORTS(portRange, 1);
		remoteAddr.s_addr = INADDR_ANY;
	}

	remotePort     = GETLOPORT(portRange);
	numRemotePorts = GETNUMPORTS(portRange);

/*
 * Make sure port ranges match up, then add the redirect ports.
 */
	if (numLocalPorts != numPublicPorts)
	        errx (1, "redirect_port: port ranges must be equal in size");

	/* Remote port range is allowed to be '0' which means all ports. */
	if (numRemotePorts != numLocalPorts && (numRemotePorts != 1 || remotePort != 0))
	        errx (1, "redirect_port: remote port must be 0 or equal to local port range in size");

	for (i = 0 ; i < numPublicPorts ; ++i) {
	        /* If remotePort is all ports, set it to 0. */
	        u_short remotePortCopy = remotePort + i;
	        if (numRemotePorts == 1 && remotePort == 0)
		        remotePortCopy = 0;

		aliaslink = LibAliasRedirectPort (mla, localAddr,
						htons(localPort + i),
						remoteAddr,
						htons(remotePortCopy),
						publicAddr,
						htons(publicPort + i),
						proto);
	}

/*
 * Setup LSNAT server pool.
 */
	if (serverPool != NULL && aliaslink != NULL) {
		ptr = strtok(serverPool, ",");
		while (ptr != NULL) {
			if (StrToAddrAndPortRange(ptr, &localAddr, protoName, &portRange) != 0)
				errx(1, "redirect_port: invalid local port range");

			localPort = GETLOPORT(portRange);
			if (GETNUMPORTS(portRange) != 1)
				errx(1, "redirect_port: local port must be single in this context");
			LibAliasAddServer(mla, aliaslink, localAddr, htons(localPort));
			ptr = strtok(NULL, ",");
		}
	}
	
	free (buf);
}

void
SetupProtoRedirect(const char* parms)
{
	char		*buf;
	char*		ptr;
	struct in_addr	localAddr;
	struct in_addr	publicAddr;
	struct in_addr	remoteAddr;
	int		proto;
	char*		protoName;
	struct protoent *protoent;

	buf = strdup (parms);
	if (!buf)
		errx (1, "redirect_port: strdup() failed");
/*
 * Extract protocol.
 */
	protoName = strtok(buf, " \t");
	if (!protoName)
		errx(1, "redirect_proto: missing protocol");

	protoent = getprotobyname(protoName);
	if (protoent == NULL)
		errx(1, "redirect_proto: unknown protocol %s", protoName);
	else
		proto = protoent->p_proto;
/*
 * Extract local address.
 */
	ptr = strtok(NULL, " \t");
	if (!ptr)
		errx(1, "redirect_proto: missing local address");
	else
		StrToAddr(ptr, &localAddr);
/*
 * Extract optional public address.
 */
	ptr = strtok(NULL, " \t");
	if (ptr)
		StrToAddr(ptr, &publicAddr);
	else
		publicAddr.s_addr = INADDR_ANY;
/*
 * Extract optional remote address.
 */
	ptr = strtok(NULL, " \t");
	if (ptr)
		StrToAddr(ptr, &remoteAddr);
	else
		remoteAddr.s_addr = INADDR_ANY;
/*
 * Create aliasing link.
 */
	(void)LibAliasRedirectProto(mla, localAddr, remoteAddr, publicAddr,
				       proto);

	free (buf);
}

void SetupAddressRedirect (const char* parms)
{
	char		*buf;
	char*		ptr;
	char*		separator;
	struct in_addr	localAddr;
	struct in_addr	publicAddr;
	char*		serverPool;
	struct alias_link *aliaslink;

	buf = strdup (parms);
	if (!buf)
		errx (1, "redirect_port: strdup() failed");
/*
 * Extract local address.
 */
	ptr = strtok (buf, " \t");
	if (!ptr)
		errx (1, "redirect_address: missing local address");

	separator = strchr(ptr, ',');
	if (separator) {		/* LSNAT redirection syntax. */
		localAddr.s_addr = INADDR_NONE;
		serverPool = ptr;
	} else {
		StrToAddr (ptr, &localAddr);
		serverPool = NULL;
	}
/*
 * Extract public address.
 */
	ptr = strtok (NULL, " \t");
	if (!ptr)
		errx (1, "redirect_address: missing public address");

	StrToAddr (ptr, &publicAddr);
	aliaslink = LibAliasRedirectAddr(mla, localAddr, publicAddr);

/*
 * Setup LSNAT server pool.
 */
	if (serverPool != NULL && aliaslink != NULL) {
		ptr = strtok(serverPool, ",");
		while (ptr != NULL) {
			StrToAddr(ptr, &localAddr);
			LibAliasAddServer(mla, aliaslink, localAddr, htons(~0));
			ptr = strtok(NULL, ",");
		}
	}

	free (buf);
}

void StrToAddr (const char* str, struct in_addr* addr)
{
	struct hostent* hp;

	if (inet_aton (str, addr))
		return;

	hp = gethostbyname (str);
	if (!hp)
		errx (1, "unknown host %s", str);

	memcpy (addr, hp->h_addr, sizeof (struct in_addr));
}

u_short StrToPort (const char* str, const char* proto)
{
	u_short		port;
	struct servent*	sp;
	char*		end;

	port = strtol (str, &end, 10);
	if (end != str)
		return htons (port);

	sp = getservbyname (str, proto);
	if (!sp)
		errx (1, "%s/%s: unknown service", str, proto);

	return sp->s_port;
}

int StrToPortRange (const char* str, const char* proto, port_range *portRange)
{
	const char*	sep;
	struct servent*	sp;
	char*		end;
	u_short         loPort;
	u_short         hiPort;
	
	/* First see if this is a service, return corresponding port if so. */
	sp = getservbyname (str,proto);
	if (sp) {
	        SETLOPORT(*portRange, ntohs(sp->s_port));
		SETNUMPORTS(*portRange, 1);
		return 0;
	}
	        
	/* Not a service, see if it's a single port or port range. */
	sep = strchr (str, '-');
	if (sep == NULL) {
	        SETLOPORT(*portRange, strtol(str, &end, 10));
		if (end != str) {
		        /* Single port. */
		        SETNUMPORTS(*portRange, 1);
			return 0;
		}

		/* Error in port range field. */
		errx (1, "%s/%s: unknown service", str, proto);
	}

	/* Port range, get the values and sanity check. */
	sscanf (str, "%hu-%hu", &loPort, &hiPort);
	SETLOPORT(*portRange, loPort);
	SETNUMPORTS(*portRange, 0);	/* Error by default */
	if (loPort <= hiPort)
	        SETNUMPORTS(*portRange, hiPort - loPort + 1);

	if (GETNUMPORTS(*portRange) == 0)
	        errx (1, "invalid port range %s", str);

	return 0;
}


static int
StrToProto (const char* str)
{
	if (!strcmp (str, "tcp"))
		return IPPROTO_TCP;

	if (!strcmp (str, "udp"))
		return IPPROTO_UDP;

	errx (1, "unknown protocol %s. Expected tcp or udp", str);
}

static int
StrToAddrAndPortRange (char* str, struct in_addr* addr, char* proto, port_range *portRange)
{
	char*	ptr;

	ptr = strchr (str, ':');
	if (!ptr)
		errx (1, "%s is missing port number", str);

	*ptr = '\0';
	++ptr;

	StrToAddr (str, addr);
	return StrToPortRange (ptr, proto, portRange);
}

static void
SetupPunchFW(const char *strValue)
{
	unsigned int base, num;

	if (sscanf(strValue, "%u:%u", &base, &num) != 2)
		errx(1, "punch_fw: basenumber:count parameter required");

	if (CheckIpfwRulenum(base + num - 1) == -1)
		errx(1, "punch_fw: basenumber:count parameter should fit "
			"the maximum allowed rule numbers");

	LibAliasSetFWBase(mla, base, num);
	(void)LibAliasSetMode(mla, PKT_ALIAS_PUNCH_FW, PKT_ALIAS_PUNCH_FW);
}

static void
SetupSkinnyPort(const char *strValue)
{
	unsigned int port;

	if (sscanf(strValue, "%u", &port) != 1)
		errx(1, "skinny_port: port parameter required");

	LibAliasSetSkinnyPort(mla, port);
}

static void
NewInstance(const char *name)
{
	struct instance *ip;

	LIST_FOREACH(ip, &root, list) {
		if (!strcmp(ip->name, name)) {
			mla = ip->la;
			mip = ip;
			return;
		}
	}
	ninstance++;
	ip = calloc(1, sizeof(*ip));
	ip->name = strdup(name);
	ip->la = LibAliasInit (ip->la);
	ip->assignAliasAddr	= 0;
	ip->ifName		= NULL;
 	ip->logDropped		= 0;
	ip->inPort		= 0;
	ip->outPort		= 0;
	ip->inOutPort		= 0;
	ip->aliasAddr.s_addr	= INADDR_NONE;
	ip->ifMTU		= -1;
	ip->aliasOverhead	= 12;
	LIST_INSERT_HEAD(&root, ip, list);
	mla = ip->la;
	mip = ip;
}

static int
CheckIpfwRulenum(unsigned int rnum)
{
	unsigned int default_rule;
	size_t len = sizeof(default_rule);

	if (sysctlbyname("net.inet.ip.fw.default_rule", &default_rule, &len,
		NULL, 0) == -1) {
		warn("Failed to get the default ipfw rule number, using "
		     "default historical value 65535.  The reason was");
		default_rule = 65535;
	}
	if (rnum >= default_rule) {
		return -1;
	}

	return 0;
}
