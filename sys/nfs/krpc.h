/*	$OpenBSD: krpc.h,v 1.7 2012/12/05 23:20:23 deraadt Exp $	*/
/*	$NetBSD: krpc.h,v 1.4 1995/12/19 23:07:11 cgd Exp $	*/

int krpc_call(struct sockaddr_in *, u_int, u_int, u_int, struct mbuf **,
    struct mbuf **, int);
int krpc_portmap(struct sockaddr_in *, u_int, u_int, u_int16_t *);

struct mbuf *xdr_string_encode(char *, int);
struct mbuf *xdr_string_decode(struct mbuf *, char *, int *);
struct mbuf *xdr_inaddr_encode(struct in_addr *);
struct mbuf *xdr_inaddr_decode(struct mbuf *, struct in_addr *);

/* RPC definitions for the portmapper. */
#define	PMAPPORT		111
#define	PMAPPROG		100000
#define	PMAPVERS		2
#define	PMAPPROC_NULL		0
#define	PMAPPROC_SET		1
#define	PMAPPROC_UNSET		2
#define	PMAPPROC_GETPORT	3
#define	PMAPPROC_DUMP		4
#define	PMAPPROC_CALLIT		5

/* RPC definitions for bootparamd. */
#define	BOOTPARAM_PROG		100026
#define	BOOTPARAM_VERS		1
#define BOOTPARAM_WHOAMI	1
#define BOOTPARAM_GETFILE	2

