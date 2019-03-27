/*	$NetBSD: security.c,v 1.5 2000/06/08 09:01:05 fvdl Exp $	*/
/*	$FreeBSD$ */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <rpc/rpc.h>
#include <rpc/rpcb_prot.h>
#include <rpc/pmap_prot.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <netdb.h>

/*
 * XXX for special case checks in check_callit.
 */
#include <rpcsvc/mount.h>
#include <rpcsvc/rquota.h>
#include <rpcsvc/nfs_prot.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>
#include <rpcsvc/yppasswd.h>

#include "rpcbind.h"

#ifdef LIBWRAP
# include <tcpd.h>
#ifndef LIBWRAP_ALLOW_FACILITY
# define LIBWRAP_ALLOW_FACILITY LOG_AUTH
#endif
#ifndef LIBWRAP_ALLOW_SEVERITY
# define LIBWRAP_ALLOW_SEVERITY LOG_INFO
#endif
#ifndef LIBWRAP_DENY_FACILITY
# define LIBWRAP_DENY_FACILITY LOG_AUTH
#endif
#ifndef LIBWRAP_DENY_SEVERITY
# define LIBWRAP_DENY_SEVERITY LOG_WARNING
#endif
int allow_severity = LIBWRAP_ALLOW_FACILITY|LIBWRAP_ALLOW_SEVERITY;
int deny_severity = LIBWRAP_DENY_FACILITY|LIBWRAP_DENY_SEVERITY;
#endif

#ifndef PORTMAP_LOG_FACILITY
# define PORTMAP_LOG_FACILITY LOG_AUTH
#endif
#ifndef PORTMAP_LOG_SEVERITY
# define PORTMAP_LOG_SEVERITY LOG_INFO
#endif
int log_severity = PORTMAP_LOG_FACILITY|PORTMAP_LOG_SEVERITY;

extern int verboselog;

int 
check_access(SVCXPRT *xprt, rpcproc_t proc, void *args, unsigned int rpcbvers)
{
	struct netbuf *caller = svc_getrpccaller(xprt);
	struct sockaddr *addr = (struct sockaddr *)caller->buf;
#ifdef LIBWRAP
	struct request_info req;
#endif
	rpcprog_t prog = 0;
	rpcb *rpcbp;
	struct pmap *pmap;

	/*
	 * The older PMAP_* equivalents have the same numbers, so
	 * they are accounted for here as well.
	 */
	switch (proc) {
	case RPCBPROC_GETADDR:
	case RPCBPROC_SET:
	case RPCBPROC_UNSET:
		if (rpcbvers > PMAPVERS) {
			rpcbp = (rpcb *)args;
			prog = rpcbp->r_prog;
		} else {
			pmap = (struct pmap *)args;
			prog = pmap->pm_prog;
		}
		if (proc == RPCBPROC_GETADDR)
			break;
		if (!insecure && !is_loopback(caller)) {
			if (verboselog)
				logit(log_severity, addr, proc, prog,
				    " declined (non-loopback sender)");
			return 0;
		}
		break;
	case RPCBPROC_CALLIT:
	case RPCBPROC_INDIRECT:
	case RPCBPROC_DUMP:
	case RPCBPROC_GETTIME:
	case RPCBPROC_UADDR2TADDR:
	case RPCBPROC_TADDR2UADDR:
	case RPCBPROC_GETVERSADDR:
	case RPCBPROC_GETADDRLIST:
	case RPCBPROC_GETSTAT:
	default:
		break;
	}

#ifdef LIBWRAP
	if (libwrap && addr->sa_family != AF_LOCAL) {
		request_init(&req, RQ_DAEMON, "rpcbind", RQ_CLIENT_SIN, addr,
		    0);
		sock_methods(&req);
		if(!hosts_access(&req)) {
			logit(deny_severity, addr, proc, prog,
			    ": request from unauthorized host");
			return 0;
		}
	}
#endif
	if (verboselog)
		logit(log_severity, addr, proc, prog, "");
    	return 1;
}

int
is_loopback(struct netbuf *nbuf)
{
	struct sockaddr *addr = (struct sockaddr *)nbuf->buf;
	struct sockaddr_in *sin;
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif

	switch (addr->sa_family) {
	case AF_INET:
		if (!oldstyle_local)
			return 0;
		sin = (struct sockaddr_in *)addr;
        	return ((sin->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) &&
		    (ntohs(sin->sin_port) < IPPORT_RESERVED));
#ifdef INET6
	case AF_INET6:
		if (!oldstyle_local)
			return 0;
		sin6 = (struct sockaddr_in6 *)addr;
		return (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr) &&
		    (ntohs(sin6->sin6_port) < IPV6PORT_RESERVED));
#endif
	case AF_LOCAL:
		return 1;
	default:
		break;
	}
	
	return 0;
}


/* logit - report events of interest via the syslog daemon */
void
logit(int severity, struct sockaddr *addr, rpcproc_t procnum, rpcprog_t prognum,
      const char *text)
{
	const char *procname;
	char	procbuf[32];
	char   *progname;
	char	progbuf[32];
	char fromname[NI_MAXHOST];
	struct rpcent *rpc;
	static const char *procmap[] = {
	/* RPCBPROC_NULL */		"null",
	/* RPCBPROC_SET */		"set",
	/* RPCBPROC_UNSET */		"unset",
	/* RPCBPROC_GETADDR */		"getport/addr",
	/* RPCBPROC_DUMP */		"dump",
	/* RPCBPROC_CALLIT */		"callit",
	/* RPCBPROC_GETTIME */		"gettime",
	/* RPCBPROC_UADDR2TADDR */	"uaddr2taddr",
	/* RPCBPROC_TADDR2UADDR */	"taddr2uaddr",
	/* RPCBPROC_GETVERSADDR */	"getversaddr",
	/* RPCBPROC_INDIRECT */		"indirect",
	/* RPCBPROC_GETADDRLIST */	"getaddrlist",
	/* RPCBPROC_GETSTAT */		"getstat"
	};
   
	/*
	 * Fork off a process or the portmap daemon might hang while
	 * getrpcbynumber() or syslog() does its thing.
	 */

	if (fork() == 0) {
		setproctitle("logit");

		/* Try to map program number to name. */

		if (prognum == 0) {
			progname = "";
		} else if ((rpc = getrpcbynumber((int) prognum))) {
			progname = rpc->r_name;
		} else {
			snprintf(progname = progbuf, sizeof(progbuf), "%u",
			    (unsigned)prognum);
		}

		/* Try to map procedure number to name. */

		if (procnum >= (sizeof procmap / sizeof (char *))) {
			snprintf(procbuf, sizeof procbuf, "%u",
			    (unsigned)procnum);
			procname = procbuf;
		} else
			procname = procmap[procnum];

		/* Write syslog record. */

		if (addr->sa_family == AF_LOCAL)
			strcpy(fromname, "local");
		else
			getnameinfo(addr, addr->sa_len, fromname,
			    sizeof fromname, NULL, 0, NI_NUMERICHOST);

		syslog(severity, "connect from %s to %s(%s)%s",
			fromname, procname, progname, text);
		_exit(0);
	}
}

int
check_callit(SVCXPRT *xprt, struct r_rmtcall_args *args, int versnum __unused)
{
	struct sockaddr *sa = (struct sockaddr *)svc_getrpccaller(xprt)->buf;

	/*
	 * Always allow calling NULLPROC
	 */
	if (args->rmt_proc == 0)
		return 1;

	/*
	 * XXX - this special casing sucks.
	 */
	switch (args->rmt_prog) {
	case RPCBPROG:
		/*
		 * Allow indirect calls to ourselves in insecure mode.
		 * The is_loopback checks aren't useful then anyway.
		 */
		if (!insecure)
			goto deny;
		break;
	case MOUNTPROG:
		if (args->rmt_proc != MOUNTPROC_MNT &&
		    args->rmt_proc != MOUNTPROC_UMNT)
			break;
		goto deny;
	case YPBINDPROG:
		if (args->rmt_proc != YPBINDPROC_SETDOM)
			break;
		/* FALLTHROUGH */
	case YPPASSWDPROG:
	case NFS_PROGRAM:
	case RQUOTAPROG:
		goto deny;
	case YPPROG:
		switch (args->rmt_proc) {
		case YPPROC_ALL:
		case YPPROC_MATCH:
		case YPPROC_FIRST:
		case YPPROC_NEXT:
			goto deny;
		default:
			break;
		}
	default:
		break;
	}

	return 1;
deny:
#ifdef LIBWRAP
	logit(deny_severity, sa, args->rmt_proc, args->rmt_prog,
	    ": indirect call not allowed");
#else
	logit(0, sa, args->rmt_proc, args->rmt_prog,
	    ": indirect call not allowed");
#endif
	return 0;
}
