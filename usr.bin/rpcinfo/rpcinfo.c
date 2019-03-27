/*	$NetBSD: rpcinfo.c,v 1.15 2000/10/04 20:09:05 mjl Exp $	*/

/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 *
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */

/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

/* #ident	"@(#)rpcinfo.c	1.18	93/07/05 SMI" */

#if 0
#ifndef lint
static char sccsid[] = "@(#)rpcinfo.c 1.16 89/04/05 Copyr 1986 Sun Micro";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * rpcinfo: ping a particular rpc program
 * 	or dump the registered programs on the remote machine.
 */

/*
 * We are for now defining PORTMAP here.  It doesn't even compile
 * unless it is defined.
 */
#ifndef	PORTMAP
#define	PORTMAP
#endif

/*
 * If PORTMAP is defined, rpcinfo will talk to both portmapper and
 * rpcbind programs; else it talks only to rpcbind. In the latter case
 * all the portmapper specific options such as -u, -t, -p become void.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <rpc/rpc.h>
#include <stdio.h>
#include <rpc/rpcb_prot.h>
#include <rpc/rpcent.h>
#include <rpc/nettype.h>
#include <rpc/rpc_com.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <ctype.h>

#ifdef PORTMAP		/* Support for version 2 portmapper */
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#endif

#define MAXHOSTLEN 256
#define	MIN_VERS	((u_long) 0)
#define	MAX_VERS	((u_long) 4294967295UL)
#define	UNKNOWN		"unknown"

/*
 * Functions to be performed.
 */
#define	NONE		0	/* no function */
#define	PMAPDUMP	1	/* dump portmapper registrations */
#define	TCPPING		2	/* ping TCP service */
#define	UDPPING		3	/* ping UDP service */
#define	BROADCAST	4	/* ping broadcast service */
#define	DELETES		5	/* delete registration for the service */
#define	ADDRPING	6	/* pings at the given address */
#define	PROGPING	7	/* pings a program on a given host */
#define	RPCBDUMP	8	/* dump rpcbind registrations */
#define	RPCBDUMP_SHORT	9	/* dump rpcbind registrations - short version */
#define	RPCBADDRLIST	10	/* dump addr list about one prog */
#define	RPCBGETSTAT	11	/* Get statistics */

struct netidlist {
	char *netid;
	struct netidlist *next;
};

struct verslist {
	int vers;
	struct verslist *next;
};

struct rpcbdump_short {
	u_long prog;
	struct verslist *vlist;
	struct netidlist *nlist;
	struct rpcbdump_short *next;
	char *owner;
};



#ifdef PORTMAP
static void	ip_ping(u_short, const char *, int, char **);
static CLIENT	*clnt_com_create(struct sockaddr_in *, u_long, u_long, int *,
				 const char *);
static void	pmapdump(int, char **);
static void	get_inet_address(struct sockaddr_in *, char *);
#endif

static bool_t	reply_proc(void *, struct netbuf *, struct netconfig *);
static void	brdcst(int, char **);
static void	addrping(char *, char *, int, char **);
static void	progping(char *, int, char **);
static CLIENT	*clnt_addr_create(char *, struct netconfig *, u_long, u_long);
static CLIENT   *clnt_rpcbind_create(char *, int, struct netbuf **);
static CLIENT   *getclnthandle(char *, struct netconfig *, u_long,
			       struct netbuf **);
static CLIENT	*local_rpcb(u_long, u_long);
static int	pstatus(CLIENT *, u_long, u_long);
static void	rpcbdump(int, char *, int, char **);
static void	rpcbgetstat(int, char **);
static void	rpcbaddrlist(char *, int, char **);
static void	deletereg(char *, int, char **);
static void	print_rmtcallstat(int, rpcb_stat *);
static void	print_getaddrstat(int, rpcb_stat *);
static void	usage(void);
static u_long	getprognum(char *);
static u_long	getvers(char *);
static char	*spaces(int);
static bool_t	add_version(struct rpcbdump_short *, u_long);
static bool_t	add_netid(struct rpcbdump_short *, char *);

int
main(int argc, char **argv)
{
	register int c;
	int errflg;
	int function;
	char *netid = NULL;
	char *address = NULL;
#ifdef PORTMAP
	char *strptr;
	u_short portnum = 0;
#endif

	function = NONE;
	errflg = 0;
#ifdef PORTMAP
	while ((c = getopt(argc, argv, "a:bdlmn:pstT:u")) != -1) {
#else
	while ((c = getopt(argc, argv, "a:bdlmn:sT:")) != -1) {
#endif
		switch (c) {
#ifdef PORTMAP
		case 'p':
			if (function != NONE)
				errflg = 1;
			else
				function = PMAPDUMP;
			break;

		case 't':
			if (function != NONE)
				errflg = 1;
			else
				function = TCPPING;
			break;

		case 'u':
			if (function != NONE)
				errflg = 1;
			else
				function = UDPPING;
			break;

		case 'n':
			portnum = (u_short) strtol(optarg, &strptr, 10);
			if (strptr == optarg || *strptr != '\0')
				errx(1, "%s is illegal port number", optarg);
			break;
#endif
		case 'a':
			address = optarg;
			if (function != NONE)
				errflg = 1;
			else
				function = ADDRPING;
			break;
		case 'b':
			if (function != NONE)
				errflg = 1;
			else
				function = BROADCAST;
			break;

		case 'd':
			if (function != NONE)
				errflg = 1;
			else
				function = DELETES;
			break;

		case 'l':
			if (function != NONE)
				errflg = 1;
			else
				function = RPCBADDRLIST;
			break;

		case 'm':
			if (function != NONE)
				errflg = 1;
			else
				function = RPCBGETSTAT;
			break;

		case 's':
			if (function != NONE)
				errflg = 1;
			else
				function = RPCBDUMP_SHORT;
			break;

		case 'T':
			netid = optarg;
			break;
		case '?':
			errflg = 1;
			break;
		}
	}

	if (errflg || ((function == ADDRPING) && !netid))
		usage();

	if (function == NONE) {
		if (argc - optind > 1)
			function = PROGPING;
		else
			function = RPCBDUMP;
	}

	switch (function) {
#ifdef PORTMAP
	case PMAPDUMP:
		if (portnum != 0)
			usage();
		pmapdump(argc - optind, argv + optind);
		break;

	case UDPPING:
		ip_ping(portnum, "udp", argc - optind, argv + optind);
		break;

	case TCPPING:
		ip_ping(portnum, "tcp", argc - optind, argv + optind);
		break;
#endif
	case BROADCAST:
		brdcst(argc - optind, argv + optind);
		break;
	case DELETES:
		deletereg(netid, argc - optind, argv + optind);
		break;
	case ADDRPING:
		addrping(address, netid, argc - optind, argv + optind);
		break;
	case PROGPING:
		progping(netid, argc - optind, argv + optind);
		break;
	case RPCBDUMP:
	case RPCBDUMP_SHORT:
		rpcbdump(function, netid, argc - optind, argv + optind);
		break;
	case RPCBGETSTAT:
		rpcbgetstat(argc - optind, argv + optind);
		break;
	case RPCBADDRLIST:
		rpcbaddrlist(netid, argc - optind, argv + optind);
		break;
	}
	return (0);
}

static CLIENT *
local_rpcb(u_long prog, u_long vers)
{
	void *localhandle;
	struct netconfig *nconf;
	CLIENT *clnt;

	localhandle = setnetconfig();
	while ((nconf = getnetconfig(localhandle)) != NULL) {
		if (nconf->nc_protofmly != NULL &&
		    strcmp(nconf->nc_protofmly, NC_LOOPBACK) == 0)
			break;
	}
	if (nconf == NULL) {
		warnx("getnetconfig: %s", nc_sperror());
		return (NULL);
	}

	clnt = clnt_tp_create(NULL, prog, vers, nconf);
	endnetconfig(localhandle);
	return clnt;
}

#ifdef PORTMAP
static CLIENT *
clnt_com_create(struct sockaddr_in *addr, u_long prog, u_long vers,
    int *fdp, const char *trans)
{
	CLIENT *clnt;

	if (strcmp(trans, "tcp") == 0) {
		clnt = clnttcp_create(addr, prog, vers, fdp, 0, 0);
	} else {
		struct timeval to;

		to.tv_sec = 5;
		to.tv_usec = 0;
		clnt = clntudp_create(addr, prog, vers, to, fdp);
	}
	if (clnt == (CLIENT *)NULL) {
		clnt_pcreateerror("rpcinfo");
		if (vers == MIN_VERS)
			printf("program %lu is not available\n", prog);
		else
			printf("program %lu version %lu is not available\n",
							prog, vers);
		exit(1);
	}
	return (clnt);
}

/*
 * If portnum is 0, then go and get the address from portmapper, which happens
 * transparently through clnt*_create(); If version number is not given, it
 * tries to find out the version number by making a call to version 0 and if
 * that fails, it obtains the high order and the low order version number. If
 * version 0 calls succeeds, it tries for MAXVERS call and repeats the same.
 */
static void
ip_ping(u_short portnum, const char *trans, int argc, char **argv)
{
	CLIENT *client;
	int fd = RPC_ANYFD;
	struct timeval to;
	struct sockaddr_in addr;
	enum clnt_stat rpc_stat;
	u_long prognum, vers, minvers, maxvers;
	struct rpc_err rpcerr;
	int failure = 0;

	if (argc < 2 || argc > 3)
		usage();
	to.tv_sec = 10;
	to.tv_usec = 0;
	prognum = getprognum(argv[1]);
	get_inet_address(&addr, argv[0]);
	if (argc == 2) {	/* Version number not known */
		/*
		 * A call to version 0 should fail with a program/version
		 * mismatch, and give us the range of versions supported.
		 */
		vers = MIN_VERS;
	} else {
		vers = getvers(argv[2]);
	}
	addr.sin_port = htons(portnum);
	client = clnt_com_create(&addr, prognum, vers, &fd, trans);
	rpc_stat = CLNT_CALL(client, NULLPROC, (xdrproc_t) xdr_void,
			(char *)NULL, (xdrproc_t) xdr_void, (char *)NULL,
			to);
	if (argc != 2) {
		/* Version number was known */
		if (pstatus(client, prognum, vers) < 0)
			exit(1);
		(void) CLNT_DESTROY(client);
		return;
	}
	/* Version number not known */
	(void) CLNT_CONTROL(client, CLSET_FD_NCLOSE, (char *)NULL);
	if (rpc_stat == RPC_PROGVERSMISMATCH) {
		clnt_geterr(client, &rpcerr);
		minvers = rpcerr.re_vers.low;
		maxvers = rpcerr.re_vers.high;
	} else if (rpc_stat == RPC_SUCCESS) {
		/*
		 * Oh dear, it DOES support version 0.
		 * Let's try version MAX_VERS.
		 */
		(void) CLNT_DESTROY(client);
		addr.sin_port = htons(portnum);
		client = clnt_com_create(&addr, prognum, MAX_VERS, &fd, trans);
		rpc_stat = CLNT_CALL(client, NULLPROC, (xdrproc_t) xdr_void,
				(char *)NULL, (xdrproc_t) xdr_void,
				(char *)NULL, to);
		if (rpc_stat == RPC_PROGVERSMISMATCH) {
			clnt_geterr(client, &rpcerr);
			minvers = rpcerr.re_vers.low;
			maxvers = rpcerr.re_vers.high;
		} else if (rpc_stat == RPC_SUCCESS) {
			/*
			 * It also supports version MAX_VERS.
			 * Looks like we have a wise guy.
			 * OK, we give them information on all
			 * 4 billion versions they support...
			 */
			minvers = 0;
			maxvers = MAX_VERS;
		} else {
			(void) pstatus(client, prognum, MAX_VERS);
			exit(1);
		}
	} else {
		(void) pstatus(client, prognum, (u_long)0);
		exit(1);
	}
	(void) CLNT_DESTROY(client);
	for (vers = minvers; vers <= maxvers; vers++) {
		addr.sin_port = htons(portnum);
		client = clnt_com_create(&addr, prognum, vers, &fd, trans);
		rpc_stat = CLNT_CALL(client, NULLPROC, (xdrproc_t) xdr_void,
				(char *)NULL, (xdrproc_t) xdr_void,
				(char *)NULL, to);
		if (pstatus(client, prognum, vers) < 0)
				failure = 1;
		(void) CLNT_DESTROY(client);
	}
	if (failure)
		exit(1);
	(void) close(fd);
	return;
}

/*
 * Dump all the portmapper registerations
 */
static void
pmapdump(int argc, char **argv)
{
	struct sockaddr_in server_addr;
	struct pmaplist *head = NULL;
	int socket = RPC_ANYSOCK;
	struct timeval minutetimeout;
	register CLIENT *client;
	struct rpcent *rpc;
	enum clnt_stat clnt_st;
	struct rpc_err err;
	char *host = NULL;

	if (argc > 1)
		usage();
	if (argc == 1) {
		host = argv[0];
		get_inet_address(&server_addr, host);
		server_addr.sin_port = htons(PMAPPORT);
		client = clnttcp_create(&server_addr, PMAPPROG, PMAPVERS,
		    &socket, 50, 500);
	} else
		client = local_rpcb(PMAPPROG, PMAPVERS);

	if (client == NULL) {
		if (rpc_createerr.cf_stat == RPC_TLIERROR) {
			/*
			 * "Misc. TLI error" is not too helpful. Most likely
			 * the connection to the remote server timed out, so
			 * this error is at least less perplexing.
			 */
			rpc_createerr.cf_stat = RPC_PMAPFAILURE;
			rpc_createerr.cf_error.re_status = RPC_FAILED;
		}
		clnt_pcreateerror("rpcinfo: can't contact portmapper");
		exit(1);
	}

	minutetimeout.tv_sec = 60;
	minutetimeout.tv_usec = 0;

	clnt_st = CLNT_CALL(client, PMAPPROC_DUMP, (xdrproc_t) xdr_void,
		NULL, (xdrproc_t) xdr_pmaplist_ptr, (char *)&head,
		minutetimeout);
	if (clnt_st != RPC_SUCCESS) {
		if ((clnt_st == RPC_PROGVERSMISMATCH) ||
		    (clnt_st == RPC_PROGUNAVAIL)) {
			CLNT_GETERR(client, &err);
			if (err.re_vers.low > PMAPVERS) {
				if (host)
					warnx("%s does not support portmapper."
					    "Try rpcinfo %s instead", host,
					    host);
				else
					warnx("local host does not support "
					    "portmapper.  Try 'rpcinfo' "
					    "instead");
			}
			exit(1);
		}
		clnt_perror(client, "rpcinfo: can't contact portmapper");
		exit(1);
	}
	if (head == NULL) {
		printf("No remote programs registered.\n");
	} else {
		printf("   program vers proto   port  service\n");
		for (; head != NULL; head = head->pml_next) {
			printf("%10ld%5ld",
				head->pml_map.pm_prog,
				head->pml_map.pm_vers);
			if (head->pml_map.pm_prot == IPPROTO_UDP)
				printf("%6s", "udp");
			else if (head->pml_map.pm_prot == IPPROTO_TCP)
				printf("%6s", "tcp");
			else if (head->pml_map.pm_prot == IPPROTO_ST)
				printf("%6s", "local");
			else
				printf("%6ld", head->pml_map.pm_prot);
			printf("%7ld", head->pml_map.pm_port);
			rpc = getrpcbynumber(head->pml_map.pm_prog);
			if (rpc)
				printf("  %s\n", rpc->r_name);
			else
				printf("\n");
		}
	}
}

static void
get_inet_address(struct sockaddr_in *addr, char *host)
{
	struct netconfig *nconf;
	struct addrinfo hints, *res;
	int error;

	(void) memset((char *)addr, 0, sizeof (*addr));
	addr->sin_addr.s_addr = inet_addr(host);
	if (addr->sin_addr.s_addr == INADDR_NONE ||
	    addr->sin_addr.s_addr == INADDR_ANY) {
		if ((nconf = __rpc_getconfip("udp")) == NULL &&
		    (nconf = __rpc_getconfip("tcp")) == NULL)
			errx(1, "couldn't find a suitable transport");
		else {
			memset(&hints, 0, sizeof hints);
			hints.ai_family = AF_INET;
			if ((error = getaddrinfo(host, "rpcbind", &hints, &res))
			    != 0)
				errx(1, "%s: %s", host, gai_strerror(error));
			else {
				memcpy(addr, res->ai_addr, res->ai_addrlen);
				freeaddrinfo(res);
			}
			(void) freenetconfigent(nconf);
		}
	} else {
		addr->sin_family = AF_INET;
	}
}
#endif /* PORTMAP */

/*
 * reply_proc collects replies from the broadcast.
 * to get a unique list of responses the output of rpcinfo should
 * be piped through sort(1) and then uniq(1).
 */

/*ARGSUSED*/
static bool_t
reply_proc(void *res, struct netbuf *who, struct netconfig *nconf)
	/* void *res;			Nothing comes back */
	/* struct netbuf *who;		Who sent us the reply */
	/* struct netconfig *nconf; 	On which transport the reply came */
{
	char *uaddr;
	char hostbuf[NI_MAXHOST];
	const char *hostname;
	struct sockaddr *sa = (struct sockaddr *)who->buf;

	if (getnameinfo(sa, sa->sa_len, hostbuf, NI_MAXHOST, NULL, 0, 0)) {
		hostname = UNKNOWN;
	} else {
		hostname = hostbuf;
	}
	uaddr = taddr2uaddr(nconf, who);
	if (uaddr == NULL) {
		printf("%s\t%s\n", UNKNOWN, hostname);
	} else {
		printf("%s\t%s\n", uaddr, hostname);
		free((char *)uaddr);
	}
	return (FALSE);
}

static void
brdcst(int argc, char **argv)
{
	enum clnt_stat rpc_stat;
	u_long prognum, vers;

	if (argc != 2)
		usage();
	prognum = getprognum(argv[0]);
	vers = getvers(argv[1]);
	rpc_stat = rpc_broadcast(prognum, vers, NULLPROC,
		(xdrproc_t) xdr_void, (char *)NULL, (xdrproc_t) xdr_void,
		(char *)NULL, (resultproc_t) reply_proc, NULL);
	if ((rpc_stat != RPC_SUCCESS) && (rpc_stat != RPC_TIMEDOUT))
		errx(1, "broadcast failed: %s", clnt_sperrno(rpc_stat));
	exit(0);
}

static bool_t
add_version(struct rpcbdump_short *rs, u_long vers)
{
	struct verslist *vl;

	for (vl = rs->vlist; vl; vl = vl->next)
		if (vl->vers == vers)
			break;
	if (vl)
		return (TRUE);
	vl = (struct verslist *)malloc(sizeof (struct verslist));
	if (vl == NULL)
		return (FALSE);
	vl->vers = vers;
	vl->next = rs->vlist;
	rs->vlist = vl;
	return (TRUE);
}

static bool_t
add_netid(struct rpcbdump_short *rs, char *netid)
{
	struct netidlist *nl;

	for (nl = rs->nlist; nl; nl = nl->next)
		if (strcmp(nl->netid, netid) == 0)
			break;
	if (nl)
		return (TRUE);
	nl = (struct netidlist *)malloc(sizeof (struct netidlist));
	if (nl == NULL)
		return (FALSE);
	nl->netid = netid;
	nl->next = rs->nlist;
	rs->nlist = nl;
	return (TRUE);
}

static void
rpcbdump(int dumptype, char *netid, int argc, char **argv)
{
	rpcblist_ptr head = NULL;
	struct timeval minutetimeout;
	register CLIENT *client;
	struct rpcent *rpc;
	char *host;
	struct netidlist *nl;
	struct verslist *vl;
	struct rpcbdump_short *rs, *rs_tail;
	char buf[256];
	enum clnt_stat clnt_st;
	struct rpc_err err;
	struct rpcbdump_short *rs_head = NULL;

	if (argc > 1)
		usage();
	if (argc == 1) {
		host = argv[0];
		if (netid == NULL) {
			client = clnt_rpcbind_create(host, RPCBVERS, NULL);
		} else {
			struct netconfig *nconf;
	
			nconf = getnetconfigent(netid);
			if (nconf == NULL) {
				nc_perror("rpcinfo: invalid transport");
				exit(1);
			}
			client = getclnthandle(host, nconf, RPCBVERS, NULL);
			if (nconf)
				(void) freenetconfigent(nconf);
		}
	} else
		client = local_rpcb(PMAPPROG, RPCBVERS);

	if (client == (CLIENT *)NULL) {
		clnt_pcreateerror("rpcinfo: can't contact rpcbind");
		exit(1);
	}

	minutetimeout.tv_sec = 60;
	minutetimeout.tv_usec = 0;
	clnt_st = CLNT_CALL(client, RPCBPROC_DUMP, (xdrproc_t) xdr_void,
		NULL, (xdrproc_t) xdr_rpcblist_ptr, (char *) &head,
		minutetimeout);
	if (clnt_st != RPC_SUCCESS) {
	    if ((clnt_st == RPC_PROGVERSMISMATCH) ||
		(clnt_st == RPC_PROGUNAVAIL)) {
		int vers;

		CLNT_GETERR(client, &err);
		if (err.re_vers.low == RPCBVERS4) {
		    vers = RPCBVERS4;
		    clnt_control(client, CLSET_VERS, (char *)&vers);
		    clnt_st = CLNT_CALL(client, RPCBPROC_DUMP,
			(xdrproc_t) xdr_void, NULL,
			(xdrproc_t) xdr_rpcblist_ptr, (char *) &head,
			minutetimeout);
		    if (clnt_st != RPC_SUCCESS)
			goto failed;
		} else {
		    if (err.re_vers.high == PMAPVERS) {
			int high, low;
			struct pmaplist *pmaphead = NULL;
			rpcblist_ptr list, prev;

			vers = PMAPVERS;
			clnt_control(client, CLSET_VERS, (char *)&vers);
			clnt_st = CLNT_CALL(client, PMAPPROC_DUMP,
				(xdrproc_t) xdr_void, NULL,
				(xdrproc_t) xdr_pmaplist_ptr,
				(char *)&pmaphead, minutetimeout);
			if (clnt_st != RPC_SUCCESS)
				goto failed;
			/*
			 * convert to rpcblist_ptr format
			 */
			for (head = NULL; pmaphead != NULL;
				pmaphead = pmaphead->pml_next) {
			    list = (rpcblist *)malloc(sizeof (rpcblist));
			    if (list == NULL)
				goto error;
			    if (head == NULL)
				head = list;
			    else
				prev->rpcb_next = (rpcblist_ptr) list;

			    list->rpcb_next = NULL;
			    list->rpcb_map.r_prog = pmaphead->pml_map.pm_prog;
			    list->rpcb_map.r_vers = pmaphead->pml_map.pm_vers;
			    if (pmaphead->pml_map.pm_prot == IPPROTO_UDP)
				list->rpcb_map.r_netid = "udp";
			    else if (pmaphead->pml_map.pm_prot == IPPROTO_TCP)
				list->rpcb_map.r_netid = "tcp";
			    else {
#define	MAXLONG_AS_STRING	"2147483648"
				list->rpcb_map.r_netid =
					malloc(strlen(MAXLONG_AS_STRING) + 1);
				if (list->rpcb_map.r_netid == NULL)
					goto error;
				sprintf(list->rpcb_map.r_netid, "%6ld",
					pmaphead->pml_map.pm_prot);
			    }
			    list->rpcb_map.r_owner = UNKNOWN;
			    low = pmaphead->pml_map.pm_port & 0xff;
			    high = (pmaphead->pml_map.pm_port >> 8) & 0xff;
			    list->rpcb_map.r_addr = strdup("0.0.0.0.XXX.XXX");
			    sprintf(&list->rpcb_map.r_addr[8], "%d.%d",
				high, low);
			    prev = list;
			}
		    }
		}
	    } else {	/* any other error */
failed:
		    clnt_perror(client, "rpcinfo: can't contact rpcbind: ");
		    exit(1);
	    }
	}
	if (head == NULL) {
		printf("No remote programs registered.\n");
	} else if (dumptype == RPCBDUMP) {
		printf(
"   program version netid     address                service    owner\n");
		for (; head != NULL; head = head->rpcb_next) {
			printf("%10u%5u    ",
				head->rpcb_map.r_prog, head->rpcb_map.r_vers);
			printf("%-9s ", head->rpcb_map.r_netid);
			printf("%-22s", head->rpcb_map.r_addr);
			rpc = getrpcbynumber(head->rpcb_map.r_prog);
			if (rpc)
				printf(" %-10s", rpc->r_name);
			else
				printf(" %-10s", "-");
			printf(" %s\n", head->rpcb_map.r_owner);
		}
	} else if (dumptype == RPCBDUMP_SHORT) {
		for (; head != NULL; head = head->rpcb_next) {
			for (rs = rs_head; rs; rs = rs->next)
				if (head->rpcb_map.r_prog == rs->prog)
					break;
			if (rs == NULL) {
				rs = (struct rpcbdump_short *)
					malloc(sizeof (struct rpcbdump_short));
				if (rs == NULL)
					goto error;
				rs->next = NULL;
				if (rs_head == NULL) {
					rs_head = rs;
					rs_tail = rs;
				} else {
					rs_tail->next = rs;
					rs_tail = rs;
				}
				rs->prog = head->rpcb_map.r_prog;
				rs->owner = head->rpcb_map.r_owner;
				rs->nlist = NULL;
				rs->vlist = NULL;
			}
			if (add_version(rs, head->rpcb_map.r_vers) == FALSE)
				goto error;
			if (add_netid(rs, head->rpcb_map.r_netid) == FALSE)
				goto error;
		}
		printf(
"   program version(s) netid(s)                         service     owner\n");
		for (rs = rs_head; rs; rs = rs->next) {
			char *p = buf;

			printf("%10ld  ", rs->prog);
			for (vl = rs->vlist; vl; vl = vl->next) {
				sprintf(p, "%d", vl->vers);
				p = p + strlen(p);
				if (vl->next)
					sprintf(p++, ",");
			}
			printf("%-10s", buf);
			buf[0] = '\0';
			for (nl = rs->nlist; nl; nl = nl->next) {
				strlcat(buf, nl->netid, sizeof(buf));
				if (nl->next)
					strlcat(buf, ",", sizeof(buf));
			}
			printf("%-32s", buf);
			rpc = getrpcbynumber(rs->prog);
			if (rpc)
				printf(" %-11s", rpc->r_name);
			else
				printf(" %-11s", "-");
			printf(" %s\n", rs->owner);
		}
	}
	clnt_destroy(client);
	return;
error:	warnx("no memory");
	return;
}

static char nullstring[] = "\000";

static void
rpcbaddrlist(char *netid, int argc, char **argv)
{
	rpcb_entry_list_ptr head = NULL;
	struct timeval minutetimeout;
	register CLIENT *client;
	struct rpcent *rpc;
	char *host;
	RPCB parms;
	struct netbuf *targaddr;

	if (argc != 3)
		usage();
	host = argv[0];
	if (netid == NULL) {
		client = clnt_rpcbind_create(host, RPCBVERS4, &targaddr);
	} else {
		struct netconfig *nconf;

		nconf = getnetconfigent(netid);
		if (nconf == NULL) {
			nc_perror("rpcinfo: invalid transport");
			exit(1);
		}
		client = getclnthandle(host, nconf, RPCBVERS4, &targaddr);
		if (nconf)
			(void) freenetconfigent(nconf);
	}
	if (client == (CLIENT *)NULL) {
		clnt_pcreateerror("rpcinfo: can't contact rpcbind");
		exit(1);
	}
	minutetimeout.tv_sec = 60;
	minutetimeout.tv_usec = 0;

	parms.r_prog = 	getprognum(argv[1]);
	parms.r_vers = 	getvers(argv[2]);
	parms.r_netid = client->cl_netid;
	if (targaddr == NULL) {
		parms.r_addr = nullstring;	/* for XDRing */
	} else {
		/*
		 * We also send the remote system the address we
		 * used to contact it in case it can help it
		 * connect back with us
		 */
		struct netconfig *nconf;

		nconf = getnetconfigent(client->cl_netid);
		if (nconf != NULL) {
			parms.r_addr = taddr2uaddr(nconf, targaddr);
			if (parms.r_addr == NULL)
				parms.r_addr = nullstring;
			freenetconfigent(nconf);
		} else {
			parms.r_addr = nullstring;	/* for XDRing */
		}
		free(targaddr->buf);
		free(targaddr);
	}
	parms.r_owner = nullstring;

	if (CLNT_CALL(client, RPCBPROC_GETADDRLIST, (xdrproc_t) xdr_rpcb,
		(char *) &parms, (xdrproc_t) xdr_rpcb_entry_list_ptr,
		(char *) &head, minutetimeout) != RPC_SUCCESS) {
		clnt_perror(client, "rpcinfo: can't contact rpcbind: ");
		exit(1);
	}
	if (head == NULL) {
		printf("No remote programs registered.\n");
	} else {
		printf(
	"   program vers  tp_family/name/class    address\t\t  service\n");
		for (; head != NULL; head = head->rpcb_entry_next) {
			rpcb_entry *re;
			char buf[128];

			re = &head->rpcb_entry_map;
			printf("%10u%3u    ",
				parms.r_prog, parms.r_vers);
			sprintf(buf, "%s/%s/%s ",
				re->r_nc_protofmly, re->r_nc_proto,
				re->r_nc_semantics == NC_TPI_CLTS ? "clts" :
				re->r_nc_semantics == NC_TPI_COTS ? "cots" :
						"cots_ord");
			printf("%-24s", buf);
			printf("%-24s", re->r_maddr);
			rpc = getrpcbynumber(parms.r_prog);
			if (rpc)
				printf(" %-13s", rpc->r_name);
			else
				printf(" %-13s", "-");
			printf("\n");
		}
	}
	clnt_destroy(client);
	return;
}

/*
 * monitor rpcbind
 */
static void
rpcbgetstat(int argc, char **argv)
{
	rpcb_stat_byvers inf;
	struct timeval minutetimeout;
	register CLIENT *client;
	char *host;
	int i, j;
	rpcbs_addrlist *pa;
	rpcbs_rmtcalllist *pr;
	int cnt, flen;
#define	MAXFIELD	64
	char fieldbuf[MAXFIELD];
#define	MAXLINE		256
	char linebuf[MAXLINE];
	char *cp, *lp;
	const char *pmaphdr[] = {
		"NULL", "SET", "UNSET", "GETPORT",
		"DUMP", "CALLIT"
	};
	const char *rpcb3hdr[] = {
		"NULL", "SET", "UNSET", "GETADDR", "DUMP", "CALLIT", "TIME",
		"U2T", "T2U"
	};
	const char *rpcb4hdr[] = {
		"NULL", "SET", "UNSET", "GETADDR", "DUMP", "CALLIT", "TIME",
		"U2T",  "T2U", "VERADDR", "INDRECT", "GETLIST", "GETSTAT"
	};

#define	TABSTOP	8

	if (argc >= 1) {
		host = argv[0];
		client = clnt_rpcbind_create(host, RPCBVERS4, NULL);
	} else
		client = local_rpcb(PMAPPROG, RPCBVERS4);
	if (client == (CLIENT *)NULL) {
		clnt_pcreateerror("rpcinfo: can't contact rpcbind");
		exit(1);
	}
	minutetimeout.tv_sec = 60;
	minutetimeout.tv_usec = 0;
	memset((char *)&inf, 0, sizeof (rpcb_stat_byvers));
	if (CLNT_CALL(client, RPCBPROC_GETSTAT, (xdrproc_t) xdr_void, NULL,
		(xdrproc_t) xdr_rpcb_stat_byvers, (char *)&inf, minutetimeout)
			!= RPC_SUCCESS) {
		clnt_perror(client, "rpcinfo: can't contact rpcbind: ");
		exit(1);
	}
	printf("PORTMAP (version 2) statistics\n");
	lp = linebuf;
	for (i = 0; i <= rpcb_highproc_2; i++) {
		fieldbuf[0] = '\0';
		switch (i) {
		case PMAPPROC_SET:
			sprintf(fieldbuf, "%d/", inf[RPCBVERS_2_STAT].setinfo);
			break;
		case PMAPPROC_UNSET:
			sprintf(fieldbuf, "%d/",
				inf[RPCBVERS_2_STAT].unsetinfo);
			break;
		case PMAPPROC_GETPORT:
			cnt = 0;
			for (pa = inf[RPCBVERS_2_STAT].addrinfo; pa;
				pa = pa->next)
				cnt += pa->success;
			sprintf(fieldbuf, "%d/", cnt);
			break;
		case PMAPPROC_CALLIT:
			cnt = 0;
			for (pr = inf[RPCBVERS_2_STAT].rmtinfo; pr;
				pr = pr->next)
				cnt += pr->success;
			sprintf(fieldbuf, "%d/", cnt);
			break;
		default: break;  /* For the remaining ones */
		}
		cp = &fieldbuf[0] + strlen(fieldbuf);
		sprintf(cp, "%d", inf[RPCBVERS_2_STAT].info[i]);
		flen = strlen(fieldbuf);
		printf("%s%s", pmaphdr[i],
			spaces((TABSTOP * (1 + flen / TABSTOP))
			- strlen(pmaphdr[i])));
		sprintf(lp, "%s%s", fieldbuf,
			spaces(cnt = ((TABSTOP * (1 + flen / TABSTOP))
			- flen)));
		lp += (flen + cnt);
	}
	printf("\n%s\n\n", linebuf);

	if (inf[RPCBVERS_2_STAT].info[PMAPPROC_CALLIT]) {
		printf("PMAP_RMTCALL call statistics\n");
		print_rmtcallstat(RPCBVERS_2_STAT, &inf[RPCBVERS_2_STAT]);
		printf("\n");
	}

	if (inf[RPCBVERS_2_STAT].info[PMAPPROC_GETPORT]) {
		printf("PMAP_GETPORT call statistics\n");
		print_getaddrstat(RPCBVERS_2_STAT, &inf[RPCBVERS_2_STAT]);
		printf("\n");
	}

	printf("RPCBIND (version 3) statistics\n");
	lp = linebuf;
	for (i = 0; i <= rpcb_highproc_3; i++) {
		fieldbuf[0] = '\0';
		switch (i) {
		case RPCBPROC_SET:
			sprintf(fieldbuf, "%d/", inf[RPCBVERS_3_STAT].setinfo);
			break;
		case RPCBPROC_UNSET:
			sprintf(fieldbuf, "%d/",
				inf[RPCBVERS_3_STAT].unsetinfo);
			break;
		case RPCBPROC_GETADDR:
			cnt = 0;
			for (pa = inf[RPCBVERS_3_STAT].addrinfo; pa;
				pa = pa->next)
				cnt += pa->success;
			sprintf(fieldbuf, "%d/", cnt);
			break;
		case RPCBPROC_CALLIT:
			cnt = 0;
			for (pr = inf[RPCBVERS_3_STAT].rmtinfo; pr;
				pr = pr->next)
				cnt += pr->success;
			sprintf(fieldbuf, "%d/", cnt);
			break;
		default: break;  /* For the remaining ones */
		}
		cp = &fieldbuf[0] + strlen(fieldbuf);
		sprintf(cp, "%d", inf[RPCBVERS_3_STAT].info[i]);
		flen = strlen(fieldbuf);
		printf("%s%s", rpcb3hdr[i],
			spaces((TABSTOP * (1 + flen / TABSTOP))
			- strlen(rpcb3hdr[i])));
		sprintf(lp, "%s%s", fieldbuf,
			spaces(cnt = ((TABSTOP * (1 + flen / TABSTOP))
			- flen)));
		lp += (flen + cnt);
	}
	printf("\n%s\n\n", linebuf);

	if (inf[RPCBVERS_3_STAT].info[RPCBPROC_CALLIT]) {
		printf("RPCB_RMTCALL (version 3) call statistics\n");
		print_rmtcallstat(RPCBVERS_3_STAT, &inf[RPCBVERS_3_STAT]);
		printf("\n");
	}

	if (inf[RPCBVERS_3_STAT].info[RPCBPROC_GETADDR]) {
		printf("RPCB_GETADDR (version 3) call statistics\n");
		print_getaddrstat(RPCBVERS_3_STAT, &inf[RPCBVERS_3_STAT]);
		printf("\n");
	}

	printf("RPCBIND (version 4) statistics\n");

	for (j = 0; j <= 9; j += 9) { /* Just two iterations for printing */
		lp = linebuf;
		for (i = j; i <= MAX(8, rpcb_highproc_4 - 9 + j); i++) {
			fieldbuf[0] = '\0';
			switch (i) {
			case RPCBPROC_SET:
				sprintf(fieldbuf, "%d/",
					inf[RPCBVERS_4_STAT].setinfo);
				break;
			case RPCBPROC_UNSET:
				sprintf(fieldbuf, "%d/",
					inf[RPCBVERS_4_STAT].unsetinfo);
				break;
			case RPCBPROC_GETADDR:
				cnt = 0;
				for (pa = inf[RPCBVERS_4_STAT].addrinfo; pa;
					pa = pa->next)
					cnt += pa->success;
				sprintf(fieldbuf, "%d/", cnt);
				break;
			case RPCBPROC_CALLIT:
				cnt = 0;
				for (pr = inf[RPCBVERS_4_STAT].rmtinfo; pr;
					pr = pr->next)
					cnt += pr->success;
				sprintf(fieldbuf, "%d/", cnt);
				break;
			default: break;  /* For the remaining ones */
			}
			cp = &fieldbuf[0] + strlen(fieldbuf);
			/*
			 * XXX: We also add RPCBPROC_GETADDRLIST queries to
			 * RPCB_GETADDR because rpcbind includes the
			 * RPCB_GETADDRLIST successes in RPCB_GETADDR.
			 */
			if (i != RPCBPROC_GETADDR)
			    sprintf(cp, "%d", inf[RPCBVERS_4_STAT].info[i]);
			else
			    sprintf(cp, "%d", inf[RPCBVERS_4_STAT].info[i] +
			    inf[RPCBVERS_4_STAT].info[RPCBPROC_GETADDRLIST]);
			flen = strlen(fieldbuf);
			printf("%s%s", rpcb4hdr[i],
				spaces((TABSTOP * (1 + flen / TABSTOP))
				- strlen(rpcb4hdr[i])));
			sprintf(lp, "%s%s", fieldbuf,
				spaces(cnt = ((TABSTOP * (1 + flen / TABSTOP))
				- flen)));
			lp += (flen + cnt);
		}
		printf("\n%s\n", linebuf);
	}

	if (inf[RPCBVERS_4_STAT].info[RPCBPROC_CALLIT] ||
			    inf[RPCBVERS_4_STAT].info[RPCBPROC_INDIRECT]) {
		printf("\n");
		printf("RPCB_RMTCALL (version 4) call statistics\n");
		print_rmtcallstat(RPCBVERS_4_STAT, &inf[RPCBVERS_4_STAT]);
	}

	if (inf[RPCBVERS_4_STAT].info[RPCBPROC_GETADDR]) {
		printf("\n");
		printf("RPCB_GETADDR (version 4) call statistics\n");
		print_getaddrstat(RPCBVERS_4_STAT, &inf[RPCBVERS_4_STAT]);
	}
	clnt_destroy(client);
}

/*
 * Delete registeration for this (prog, vers, netid)
 */
static void
deletereg(char *netid, int argc, char **argv)
{
	struct netconfig *nconf = NULL;

	if (argc != 2)
		usage();
	if (netid) {
		nconf = getnetconfigent(netid);
		if (nconf == NULL)
			errx(1, "netid %s not supported", netid);
	}
	if ((rpcb_unset(getprognum(argv[0]), getvers(argv[1]), nconf)) == 0)
		errx(1,
	"could not delete registration for prog %s version %s",
			argv[0], argv[1]);
}

/*
 * Create and return a handle for the given nconf.
 * Exit if cannot create handle.
 */
static CLIENT *
clnt_addr_create(char *address, struct netconfig *nconf,
    u_long prog, u_long vers)
{
	CLIENT *client;
	static struct netbuf *nbuf;
	static int fd = RPC_ANYFD;

	if (fd == RPC_ANYFD) {
		if ((fd = __rpc_nconf2fd(nconf)) == -1) {
			rpc_createerr.cf_stat = RPC_TLIERROR;
			clnt_pcreateerror("rpcinfo");
			exit(1);
		}
		/* Convert the uaddr to taddr */
		nbuf = uaddr2taddr(nconf, address);
		if (nbuf == NULL)
			errx(1, "no address for client handle");
	}
	client = clnt_tli_create(fd, nconf, nbuf, prog, vers, 0, 0);
	if (client == (CLIENT *)NULL) {
		clnt_pcreateerror("rpcinfo");
		exit(1);
	}
	return (client);
}

/*
 * If the version number is given, ping that (prog, vers); else try to find
 * the version numbers supported for that prog and ping all the versions.
 * Remote rpcbind is not contacted for this service. The requests are
 * sent directly to the services themselves.
 */
static void
addrping(char *address, char *netid, int argc, char **argv)
{
	CLIENT *client;
	struct timeval to;
	enum clnt_stat rpc_stat;
	u_long prognum, versnum, minvers, maxvers;
	struct rpc_err rpcerr;
	int failure = 0;
	struct netconfig *nconf;
	int fd;

	if (argc < 1 || argc > 2 || (netid == NULL))
		usage();
	nconf = getnetconfigent(netid);
	if (nconf == (struct netconfig *)NULL)
		errx(1, "could not find %s", netid);
	to.tv_sec = 10;
	to.tv_usec = 0;
	prognum = getprognum(argv[0]);
	if (argc == 1) {	/* Version number not known */
		/*
		 * A call to version 0 should fail with a program/version
		 * mismatch, and give us the range of versions supported.
		 */
		versnum = MIN_VERS;
	} else {
		versnum = getvers(argv[1]);
	}
	client = clnt_addr_create(address, nconf, prognum, versnum);
	rpc_stat = CLNT_CALL(client, NULLPROC, (xdrproc_t) xdr_void,
			(char *)NULL, (xdrproc_t) xdr_void,
			(char *)NULL, to);
	if (argc == 2) {
		/* Version number was known */
		if (pstatus(client, prognum, versnum) < 0)
			failure = 1;
		(void) CLNT_DESTROY(client);
		if (failure)
			exit(1);
		return;
	}
	/* Version number not known */
	(void) CLNT_CONTROL(client, CLSET_FD_NCLOSE, (char *)NULL);
	(void) CLNT_CONTROL(client, CLGET_FD, (char *)&fd);
	if (rpc_stat == RPC_PROGVERSMISMATCH) {
		clnt_geterr(client, &rpcerr);
		minvers = rpcerr.re_vers.low;
		maxvers = rpcerr.re_vers.high;
	} else if (rpc_stat == RPC_SUCCESS) {
		/*
		 * Oh dear, it DOES support version 0.
		 * Let's try version MAX_VERS.
		 */
		(void) CLNT_DESTROY(client);
		client = clnt_addr_create(address, nconf, prognum, MAX_VERS);
		rpc_stat = CLNT_CALL(client, NULLPROC, (xdrproc_t) xdr_void,
				(char *)NULL, (xdrproc_t) xdr_void,
				(char *)NULL, to);
		if (rpc_stat == RPC_PROGVERSMISMATCH) {
			clnt_geterr(client, &rpcerr);
			minvers = rpcerr.re_vers.low;
			maxvers = rpcerr.re_vers.high;
		} else if (rpc_stat == RPC_SUCCESS) {
			/*
			 * It also supports version MAX_VERS.
			 * Looks like we have a wise guy.
			 * OK, we give them information on all
			 * 4 billion versions they support...
			 */
			minvers = 0;
			maxvers = MAX_VERS;
		} else {
			(void) pstatus(client, prognum, MAX_VERS);
			exit(1);
		}
	} else {
		(void) pstatus(client, prognum, (u_long)0);
		exit(1);
	}
	(void) CLNT_DESTROY(client);
	for (versnum = minvers; versnum <= maxvers; versnum++) {
		client = clnt_addr_create(address, nconf, prognum, versnum);
		rpc_stat = CLNT_CALL(client, NULLPROC, (xdrproc_t) xdr_void,
				(char *)NULL, (xdrproc_t) xdr_void,
				(char *)NULL, to);
		if (pstatus(client, prognum, versnum) < 0)
				failure = 1;
		(void) CLNT_DESTROY(client);
	}
	(void) close(fd);
	if (failure)
		exit(1);
	return;
}

/*
 * If the version number is given, ping that (prog, vers); else try to find
 * the version numbers supported for that prog and ping all the versions.
 * Remote rpcbind is *contacted* for this service. The requests are
 * then sent directly to the services themselves.
 */
static void
progping(char *netid, int argc, char **argv)
{
	CLIENT *client;
	struct timeval to;
	enum clnt_stat rpc_stat;
	u_long prognum, versnum, minvers, maxvers;
	struct rpc_err rpcerr;
	int failure = 0;
	struct netconfig *nconf;

	if (argc < 2 || argc > 3 || (netid == NULL))
		usage();
	prognum = getprognum(argv[1]);
	if (argc == 2) { /* Version number not known */
		/*
		 * A call to version 0 should fail with a program/version
		 * mismatch, and give us the range of versions supported.
		 */
		versnum = MIN_VERS;
	} else {
		versnum = getvers(argv[2]);
	}
	if (netid) {
		nconf = getnetconfigent(netid);
		if (nconf == (struct netconfig *)NULL)
			errx(1, "could not find %s", netid);
		client = clnt_tp_create(argv[0], prognum, versnum, nconf);
	} else {
		client = clnt_create(argv[0], prognum, versnum, "NETPATH");
	}
	if (client == (CLIENT *)NULL) {
		clnt_pcreateerror("rpcinfo");
		exit(1);
	}
	to.tv_sec = 10;
	to.tv_usec = 0;
	rpc_stat = CLNT_CALL(client, NULLPROC, (xdrproc_t) xdr_void,
			(char *)NULL, (xdrproc_t) xdr_void,
			(char *)NULL, to);
	if (argc == 3) {
		/* Version number was known */
		if (pstatus(client, prognum, versnum) < 0)
			failure = 1;
		(void) CLNT_DESTROY(client);
		if (failure)
			exit(1);
		return;
	}
	/* Version number not known */
	if (rpc_stat == RPC_PROGVERSMISMATCH) {
		clnt_geterr(client, &rpcerr);
		minvers = rpcerr.re_vers.low;
		maxvers = rpcerr.re_vers.high;
	} else if (rpc_stat == RPC_SUCCESS) {
		/*
		 * Oh dear, it DOES support version 0.
		 * Let's try version MAX_VERS.
		 */
		versnum = MAX_VERS;
		(void) CLNT_CONTROL(client, CLSET_VERS, (char *)&versnum);
		rpc_stat = CLNT_CALL(client, NULLPROC,
				(xdrproc_t) xdr_void, (char *)NULL,
				(xdrproc_t)  xdr_void, (char *)NULL, to);
		if (rpc_stat == RPC_PROGVERSMISMATCH) {
			clnt_geterr(client, &rpcerr);
			minvers = rpcerr.re_vers.low;
			maxvers = rpcerr.re_vers.high;
		} else if (rpc_stat == RPC_SUCCESS) {
			/*
			 * It also supports version MAX_VERS.
			 * Looks like we have a wise guy.
			 * OK, we give them information on all
			 * 4 billion versions they support...
			 */
			minvers = 0;
			maxvers = MAX_VERS;
		} else {
			(void) pstatus(client, prognum, MAX_VERS);
			exit(1);
		}
	} else {
		(void) pstatus(client, prognum, (u_long)0);
		exit(1);
	}
	for (versnum = minvers; versnum <= maxvers; versnum++) {
		(void) CLNT_CONTROL(client, CLSET_VERS, (char *)&versnum);
		rpc_stat = CLNT_CALL(client, NULLPROC, (xdrproc_t) xdr_void,
					(char *)NULL, (xdrproc_t) xdr_void,
					(char *)NULL, to);
		if (pstatus(client, prognum, versnum) < 0)
				failure = 1;
	}
	(void) CLNT_DESTROY(client);
	if (failure)
		exit(1);
	return;
}

static void
usage(void)
{
	fprintf(stderr, "usage: rpcinfo [-m | -s] [host]\n");
#ifdef PORTMAP
	fprintf(stderr, "       rpcinfo -p [host]\n");
#endif
	fprintf(stderr, "       rpcinfo -T netid host prognum [versnum]\n");
	fprintf(stderr, "       rpcinfo -l host prognum versnum\n");
#ifdef PORTMAP
	fprintf(stderr,
"       rpcinfo [-n portnum] -u | -t host prognum [versnum]\n");
#endif
	fprintf(stderr,
"       rpcinfo -a serv_address -T netid prognum [version]\n");
	fprintf(stderr, "       rpcinfo -b prognum versnum\n");
	fprintf(stderr, "       rpcinfo -d [-T netid] prognum versnum\n");
	exit(1);
}

static u_long
getprognum (char *arg)
{
	char *strptr;
	register struct rpcent *rpc;
	register u_long prognum;
	char *tptr = arg;

	while (*tptr && isdigit(*tptr++));
	if (*tptr || isalpha(*(tptr - 1))) {
		rpc = getrpcbyname(arg);
		if (rpc == NULL)
			errx(1, "%s is unknown service", arg);
		prognum = rpc->r_number;
	} else {
		prognum = strtol(arg, &strptr, 10);
		if (strptr == arg || *strptr != '\0')
			errx(1, "%s is illegal program number", arg);
	}
	return (prognum);
}

static u_long
getvers(char *arg)
{
	char *strptr;
	register u_long vers;

	vers = (int) strtol(arg, &strptr, 10);
	if (strptr == arg || *strptr != '\0')
		errx(1, "%s is illegal version number", arg);
	return (vers);
}

/*
 * This routine should take a pointer to an "rpc_err" structure, rather than
 * a pointer to a CLIENT structure, but "clnt_perror" takes a pointer to
 * a CLIENT structure rather than a pointer to an "rpc_err" structure.
 * As such, we have to keep the CLIENT structure around in order to print
 * a good error message.
 */
static int
pstatus(register CLIENT *client, u_long prog, u_long vers)
{
	struct rpc_err rpcerr;

	clnt_geterr(client, &rpcerr);
	if (rpcerr.re_status != RPC_SUCCESS) {
		clnt_perror(client, "rpcinfo");
		printf("program %lu version %lu is not available\n",
			prog, vers);
		return (-1);
	} else {
		printf("program %lu version %lu ready and waiting\n",
			prog, vers);
		return (0);
	}
}

static CLIENT *
clnt_rpcbind_create(char *host, int rpcbversnum, struct netbuf **targaddr)
{
	static const char *tlist[3] = {
		"circuit_n", "circuit_v", "datagram_v"
	};
	int i;
	struct netconfig *nconf;
	CLIENT *clnt = NULL;
	void *handle;

	rpc_createerr.cf_stat = RPC_SUCCESS;
	for (i = 0; i < 3; i++) {
		if ((handle = __rpc_setconf(tlist[i])) == NULL)
			continue;
		while (clnt == (CLIENT *)NULL) {
			if ((nconf = __rpc_getconf(handle)) == NULL) {
				if (rpc_createerr.cf_stat == RPC_SUCCESS)
				    rpc_createerr.cf_stat = RPC_UNKNOWNPROTO;
				break;
			}
			clnt = getclnthandle(host, nconf, rpcbversnum,
					targaddr);
		}
		if (clnt)
			break;
		__rpc_endconf(handle);
	}
	return (clnt);
}

static CLIENT*
getclnthandle(char *host, struct netconfig *nconf,
    u_long rpcbversnum, struct netbuf **targaddr)
{
	struct netbuf addr;
	struct addrinfo hints, *res;
	CLIENT *client = NULL;

	/* Get the address of the rpcbind */
	memset(&hints, 0, sizeof hints);
	if (getaddrinfo(host, "rpcbind", &hints, &res) != 0) {
		rpc_createerr.cf_stat = RPC_N2AXLATEFAILURE;
		return (NULL);
	}
	addr.len = addr.maxlen = res->ai_addrlen;
	addr.buf = res->ai_addr;
	client = clnt_tli_create(RPC_ANYFD, nconf, &addr, RPCBPROG,
			rpcbversnum, 0, 0);
	if (client) {
		if (targaddr != NULL) {
			*targaddr =
			    (struct netbuf *)malloc(sizeof (struct netbuf));
			if (*targaddr != NULL) {
				(*targaddr)->maxlen = addr.maxlen;
				(*targaddr)->len = addr.len;
				(*targaddr)->buf = (char *)malloc(addr.len);
				if ((*targaddr)->buf != NULL) {
					memcpy((*targaddr)->buf, addr.buf,
						addr.len);
				}
			}
		}
	} else {
		if (rpc_createerr.cf_stat == RPC_TLIERROR) {
			/*
			 * Assume that the other system is dead; this is a
			 * better error to display to the user.
			 */
			rpc_createerr.cf_stat = RPC_RPCBFAILURE;
			rpc_createerr.cf_error.re_status = RPC_FAILED;
		}
	}
	freeaddrinfo(res);
	return (client);
}

static void
print_rmtcallstat(int rtype, rpcb_stat *infp)
{
	register rpcbs_rmtcalllist_ptr pr;
	struct rpcent *rpc;

	if (rtype == RPCBVERS_4_STAT)
		printf(
		"prog\t\tvers\tproc\tnetid\tindirect success failure\n");
	else
		printf("prog\t\tvers\tproc\tnetid\tsuccess\tfailure\n");
	for (pr = infp->rmtinfo; pr; pr = pr->next) {
		rpc = getrpcbynumber(pr->prog);
		if (rpc)
			printf("%-16s", rpc->r_name);
		else
			printf("%-16d", pr->prog);
		printf("%d\t%d\t%s\t",
			pr->vers, pr->proc, pr->netid);
		if (rtype == RPCBVERS_4_STAT)
			printf("%d\t ", pr->indirect);
		printf("%d\t%d\n", pr->success, pr->failure);
	}
}

static void
print_getaddrstat(int rtype, rpcb_stat *infp)
{
	rpcbs_addrlist_ptr al;
	register struct rpcent *rpc;

	printf("prog\t\tvers\tnetid\t  success\tfailure\n");
	for (al = infp->addrinfo; al; al = al->next) {
		rpc = getrpcbynumber(al->prog);
		if (rpc)
			printf("%-16s", rpc->r_name);
		else
			printf("%-16d", al->prog);
		printf("%d\t%s\t  %-12d\t%d\n",
			al->vers, al->netid,
			al->success, al->failure);
	}
}

static char *
spaces(int howmany)
{
	static char space_array[] =		/* 64 spaces */
	"                                                                ";

	if (howmany <= 0 || howmany > sizeof (space_array)) {
		return ("");
	}
	return (&space_array[sizeof (space_array) - howmany - 1]);
}
