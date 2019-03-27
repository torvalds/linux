/*	$NetBSD: krpc.h,v 1.4 1995/12/19 23:07:11 cgd Exp $	*/
/* $FreeBSD$	*/

#include <sys/cdefs.h>

struct mbuf;
struct thread;
struct sockaddr;
struct sockaddr_in;

int krpc_call(struct sockaddr_in *_sin,
	u_int prog, u_int vers, u_int func,
	struct mbuf **data, struct sockaddr **from, struct thread *td);

int krpc_portmap(struct sockaddr_in *_sin,
	u_int prog, u_int vers, u_int16_t *portp, struct thread *td);

struct mbuf *xdr_string_encode(char *str, int len);

/*
 * RPC definitions for the portmapper
 */
#define	PMAPPORT		111
#define	PMAPPROG		100000
#define	PMAPVERS		2
#define	PMAPPROC_NULL		0
#define	PMAPPROC_SET		1
#define	PMAPPROC_UNSET		2
#define	PMAPPROC_GETPORT	3
#define	PMAPPROC_DUMP		4
#define	PMAPPROC_CALLIT		5
