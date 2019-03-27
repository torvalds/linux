/* #pragma ident	"@(#)auth_time.c	1.4	92/11/10 SMI" */

/*
 *	auth_time.c
 *
 * This module contains the private function __rpc_get_time_offset()
 * which will return the difference in seconds between the local system's
 * notion of time and a remote server's notion of time. This must be
 * possible without calling any functions that may invoke the name
 * service. (netdir_getbyxxx, getXbyY, etc). The function is used in the
 * synchronize call of the authdes code to synchronize clocks between
 * NIS+ clients and their servers.
 *
 * Note to minimize the amount of duplicate code, portions of the
 * synchronize() function were folded into this code, and the synchronize
 * call becomes simply a wrapper around this function. Further, if this
 * function is called with a timehost it *DOES* recurse to the name
 * server so don't use it in that mode if you are doing name service code.
 *
 *	Copyright (c) 1992 Sun Microsystems Inc.
 *	All rights reserved.
 *
 * Side effects :
 *	When called a client handle to a RPCBIND process is created
 *	and destroyed. Two strings "netid" and "uaddr" are malloc'd
 *	and returned. The SIGALRM processing is modified only if
 *	needed to deal with TCP connections.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/signal.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <rpc/rpc.h>
#include <rpc/rpc_com.h>
#include <rpc/rpcb_prot.h>
#undef NIS
#include <rpcsvc/nis.h>
#include "un-namespace.h"

extern int _rpc_dtablesize( void );

#ifdef TESTING
#define	msg(x)	printf("ERROR: %s\n", x)
/* #define msg(x) syslog(LOG_ERR, "%s", x) */
#else
#define	msg(x)
#endif

static int saw_alarm = 0;

static void
alarm_hndler(int s)
{
	saw_alarm = 1;
	return;
}

/*
 * The internet time server defines the epoch to be Jan 1, 1900
 * whereas UNIX defines it to be Jan 1, 1970. To adjust the result
 * from internet time-service time, into UNIX time we subtract the
 * following offset :
 */
#define	NYEARS	(1970 - 1900)
#define	TOFFSET ((u_long)60*60*24*(365*NYEARS + (NYEARS/4)))


/*
 * Stolen from rpc.nisd:
 * Turn a 'universal address' into a struct sockaddr_in.
 * Bletch.
 */
static int uaddr_to_sockaddr(char *uaddr, struct sockaddr_in *sin)
{
	unsigned char		p_bytes[2];
	int			i;
	unsigned long		a[6];

	i = sscanf(uaddr, "%lu.%lu.%lu.%lu.%lu.%lu", &a[0], &a[1], &a[2],
						&a[3], &a[4], &a[5]);

	if (i < 6)
		return(1);

	for (i = 0; i < 4; i++)
		sin->sin_addr.s_addr |= (a[i] & 0x000000FF) << (8 * i);

	p_bytes[0] = (unsigned char)a[4] & 0x000000FF;
	p_bytes[1] = (unsigned char)a[5] & 0x000000FF;

	sin->sin_family = AF_INET; /* always */
	bcopy((char *)&p_bytes, (char *)&sin->sin_port, 2);

	return (0);
}

/*
 * free_eps()
 *
 * Free the strings that were strduped into the eps structure.
 */
static void
free_eps(endpoint eps[], int num)
{
	int		i;

	for (i = 0; i < num; i++) {
		free(eps[i].uaddr);
		free(eps[i].proto);
		free(eps[i].family);
	}
	return;
}

/*
 * get_server()
 *
 * This function constructs a nis_server structure description for the
 * indicated hostname.
 *
 * NOTE: There is a chance we may end up recursing here due to the
 * fact that gethostbyname() could do an NIS search. Ideally, the
 * NIS+ server will call __rpc_get_time_offset() with the nis_server
 * structure already populated.
 *
 * host  - name of the time host
 * srv   - nis_server struct to use.
 * eps[] - array of endpoints
 * maxep - max array size
 */
static nis_server *
get_server(struct sockaddr_in *sin, char *host, nis_server *srv,
    endpoint eps[], int maxep)
{
	char			hname[256];
	int			num_ep = 0, i;
	struct hostent		*he;
	struct hostent		dummy;
	char			*ptr[2];
	endpoint		*ep;

	if (host == NULL && sin == NULL)
		return (NULL);

	if (sin == NULL) {
		he = gethostbyname(host);
		if (he == NULL)
			return(NULL);
	} else {
		he = &dummy;
		ptr[0] = (char *)&sin->sin_addr.s_addr;
		ptr[1] = NULL;
		dummy.h_addr_list = ptr;
	}

	/*
	 * This is lame. We go around once for TCP, then again
	 * for UDP.
	 */
	for (i = 0, ep = eps; (he->h_addr_list[i] != NULL) && (num_ep < maxep);
	    i++, ep++, num_ep++) {
		struct in_addr *a;

		a = (struct in_addr *)he->h_addr_list[i];
		snprintf(hname, sizeof(hname), "%s.0.111", inet_ntoa(*a));
		ep->uaddr = strdup(hname);
		ep->family = strdup("inet");
		ep->proto =  strdup("tcp");
		if (ep->uaddr == NULL || ep->family == NULL || ep->proto == NULL) {
			free_eps(eps, num_ep + 1);
			return (NULL);
		}
	}

	for (i = 0; (he->h_addr_list[i] != NULL) && (num_ep < maxep);
	    i++, ep++, num_ep++) {
		struct in_addr *a;

		a = (struct in_addr *)he->h_addr_list[i];
		snprintf(hname, sizeof(hname), "%s.0.111", inet_ntoa(*a));
		ep->uaddr = strdup(hname);
		ep->family = strdup("inet");
		ep->proto =  strdup("udp");
		if (ep->uaddr == NULL || ep->family == NULL || ep->proto == NULL) {
			free_eps(eps, num_ep + 1);
			return (NULL);
		}
	}

	srv->name = (nis_name) host;
	srv->ep.ep_len = num_ep;
	srv->ep.ep_val = eps;
	srv->key_type = NIS_PK_NONE;
	srv->pkey.n_bytes = NULL;
	srv->pkey.n_len = 0;
	return (srv);
}

/*
 * __rpc_get_time_offset()
 *
 * This function uses a nis_server structure to contact the a remote
 * machine (as named in that structure) and returns the offset in time
 * between that machine and this one. This offset is returned in seconds
 * and may be positive or negative.
 *
 * The first time through, a lot of fiddling is done with the netconfig
 * stuff to find a suitable transport. The function is very aggressive
 * about choosing UDP or at worst TCP if it can. This is because
 * those transports support both the RCPBIND call and the internet
 * time service.
 *
 * Once through, *uaddr is set to the universal address of
 * the machine and *netid is set to the local netid for the transport
 * that uaddr goes with. On the second call, the netconfig stuff
 * is skipped and the uaddr/netid pair are used to fetch the netconfig
 * structure and to then contact the machine for the time.
 *
 * td = "server" - "client"
 *
 * td    - Time difference
 * srv   - NIS Server description
 * thost - if no server, this is the timehost
 * uaddr - known universal address
 * netid - known network identifier
 */
int
__rpc_get_time_offset(struct timeval *td, nis_server *srv, char *thost,
    char **uaddr, struct sockaddr_in *netid)
{
	CLIENT			*clnt; 		/* Client handle 	*/
	endpoint		*ep,		/* useful endpoints	*/
				*useep = NULL;	/* endpoint of xp	*/
	char			*useua = NULL;	/* uaddr of selected xp	*/
	int			epl, i;		/* counters		*/
	enum clnt_stat		status;		/* result of clnt_call	*/
	u_long			thetime, delta;
	int			needfree = 0;
	struct timeval		tv;
	int			time_valid;
	int			udp_ep = -1, tcp_ep = -1;
	int			a1, a2, a3, a4;
	char			ut[64], ipuaddr[64];
	endpoint		teps[32];
	nis_server		tsrv;
	void			(*oldsig)(int) = NULL; /* old alarm handler */
	struct sockaddr_in	sin;
	socklen_t		len;
	int			s = RPC_ANYSOCK;
	int			type = 0;

	td->tv_sec = 0;
	td->tv_usec = 0;

	/*
	 * First check to see if we need to find and address for this
	 * server.
	 */
	if (*uaddr == NULL) {
		if ((srv != NULL) && (thost != NULL)) {
			msg("both timehost and srv pointer used!");
			return (0);
		}
		if (! srv) {
			srv = get_server(netid, thost, &tsrv, teps, 32);
			if (srv == NULL) {
				msg("unable to contruct server data.");
				return (0);
			}
			needfree = 1;	/* need to free data in endpoints */
		}

		ep = srv->ep.ep_val;
		epl = srv->ep.ep_len;

		/* Identify the TCP and UDP endpoints */
		for (i = 0;
			(i < epl) && ((udp_ep == -1) || (tcp_ep == -1)); i++) {
			if (strcasecmp(ep[i].proto, "udp") == 0)
				udp_ep = i;
			if (strcasecmp(ep[i].proto, "tcp") == 0)
				tcp_ep = i;
		}

		/* Check to see if it is UDP or TCP */
		if (tcp_ep > -1) {
			useep = &ep[tcp_ep];
			useua = ep[tcp_ep].uaddr;
			type = SOCK_STREAM;
		} else if (udp_ep > -1) {
			useep = &ep[udp_ep];
			useua = ep[udp_ep].uaddr;
			type = SOCK_DGRAM;
		}

		if (useep == NULL) {
			msg("no acceptable transport endpoints.");
			if (needfree)
				free_eps(teps, tsrv.ep.ep_len);
			return (0);
		}
	}

	/*
	 * Create a sockaddr from the uaddr.
	 */
	if (*uaddr != NULL)
		useua = *uaddr;

	/* Fixup test for NIS+ */
	sscanf(useua, "%d.%d.%d.%d.", &a1, &a2, &a3, &a4);
	sprintf(ipuaddr, "%d.%d.%d.%d.0.111", a1, a2, a3, a4);
	useua = &ipuaddr[0];

	bzero((char *)&sin, sizeof(sin));
	if (uaddr_to_sockaddr(useua, &sin)) {
		msg("unable to translate uaddr to sockaddr.");
		if (needfree)
			free_eps(teps, tsrv.ep.ep_len);
		return (0);
	}

	/*
	 * Create the client handle to rpcbind. Note we always try
	 * version 3 since that is the earliest version that supports
	 * the RPCB_GETTIME call. Also it is the version that comes
	 * standard with SVR4. Since most everyone supports TCP/IP
	 * we could consider trying the rtime call first.
	 */
	clnt = clnttcp_create(&sin, RPCBPROG, RPCBVERS, &s, 0, 0);
	if (clnt == NULL) {
		msg("unable to create client handle to rpcbind.");
		if (needfree)
			free_eps(teps, tsrv.ep.ep_len);
		return (0);
	}

	tv.tv_sec = 5;
	tv.tv_usec = 0;
	time_valid = 0;
	status = clnt_call(clnt, RPCBPROC_GETTIME, (xdrproc_t)xdr_void, NULL,
					(xdrproc_t)xdr_u_long, &thetime, tv);
	/*
	 * The only error we check for is anything but success. In
	 * fact we could have seen PROGMISMATCH if talking to a 4.1
	 * machine (pmap v2) or TIMEDOUT if the net was busy.
	 */
	if (status == RPC_SUCCESS)
		time_valid = 1;
	else {
		int save;

		/* Blow away possible stale CLNT handle. */
		if (clnt != NULL) {
			clnt_destroy(clnt);
			clnt = NULL;
		}

		/*
		 * Convert PMAP address into timeservice address
		 * We take advantage of the fact that we "know" what
		 * the universal address looks like for inet transports.
		 *
		 * We also know that the internet timeservice is always
		 * listening on port 37.
		 */
		sscanf(useua, "%d.%d.%d.%d.", &a1, &a2, &a3, &a4);
		sprintf(ut, "%d.%d.%d.%d.0.37", a1, a2, a3, a4);

		if (uaddr_to_sockaddr(ut, &sin)) {
			msg("cannot convert timeservice uaddr to sockaddr.");
			goto error;
		}

		s = _socket(AF_INET, type, 0);
		if (s == -1) {
			msg("unable to open fd to network.");
			goto error;
		}

		/*
		 * Now depending on whether or not we're talking to
		 * UDP we set a timeout or not.
		 */
		if (type == SOCK_DGRAM) {
			struct timeval timeout = { 20, 0 };
			struct sockaddr_in from;
			fd_set readfds;
			int res;

			if (_sendto(s, &thetime, sizeof(thetime), 0,
				(struct sockaddr *)&sin, sizeof(sin)) == -1) {
				msg("udp : sendto failed.");
				goto error;
			}
			do {
				FD_ZERO(&readfds);
				FD_SET(s, &readfds);
				res = _select(_rpc_dtablesize(), &readfds,
				     (fd_set *)NULL, (fd_set *)NULL, &timeout);
			} while (res < 0 && errno == EINTR);
			if (res <= 0)
				goto error;
			len = sizeof(from);
			res = _recvfrom(s, (char *)&thetime, sizeof(thetime), 0,
				       (struct sockaddr *)&from, &len);
			if (res == -1) {
				msg("recvfrom failed on udp transport.");
				goto error;
			}
			time_valid = 1;
		} else {
			int res;

			oldsig = (void (*)(int))signal(SIGALRM, alarm_hndler);
			saw_alarm = 0; /* global tracking the alarm */
			alarm(20); /* only wait 20 seconds */
			res = _connect(s, (struct sockaddr *)&sin, sizeof(sin));
			if (res == -1) {
				msg("failed to connect to tcp endpoint.");
				goto error;
			}
			if (saw_alarm) {
				msg("alarm caught it, must be unreachable.");
				goto error;
			}
			res = _read(s, (char *)&thetime, sizeof(thetime));
			if (res != sizeof(thetime)) {
				if (saw_alarm)
					msg("timed out TCP call.");
				else
					msg("wrong size of results returned");

				goto error;
			}
			time_valid = 1;
		}
		save = errno;
		(void)_close(s);
		errno = save;
		s = RPC_ANYSOCK;

		if (time_valid) {
			thetime = ntohl(thetime);
			thetime = thetime - TOFFSET; /* adjust to UNIX time */
		} else
			thetime = 0;
	}

	gettimeofday(&tv, 0);

error:
	/*
	 * clean up our allocated data structures.
	 */

	if (s != RPC_ANYSOCK)
		(void)_close(s);

	if (clnt != NULL)
		clnt_destroy(clnt);

	alarm(0);	/* reset that alarm if its outstanding */
	if (oldsig) {
		signal(SIGALRM, oldsig);
	}

	/*
	 * note, don't free uaddr strings until after we've made a
	 * copy of them.
	 */
	if (time_valid) {
		if (*uaddr == NULL)
			*uaddr = strdup(useua);

		/* Round to the nearest second */
		tv.tv_sec += (tv.tv_sec > 500000) ? 1 : 0;
		delta = (thetime > tv.tv_sec) ? thetime - tv.tv_sec :
						tv.tv_sec - thetime;
		td->tv_sec = (thetime < tv.tv_sec) ? - delta : delta;
		td->tv_usec = 0;
	} else {
		msg("unable to get the server's time.");
	}

	if (needfree)
		free_eps(teps, tsrv.ep.ep_len);

	return (time_valid);
}
