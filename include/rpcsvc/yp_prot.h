/*	$OpenBSD: yp_prot.h,v 1.12 2022/12/27 07:44:56 jmc Exp $	*/
/*	$NetBSD: yp_prot.h,v 1.6 1995/07/14 21:10:58 christos Exp $	*/

/*
 * Copyright (c) 1992, 1993 Theo de Raadt <deraadt@openbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _RPCSVC_YP_PROT_H_
#define _RPCSVC_YP_PROT_H_

/*
 * YPSERV PROTOCOL:
 *
 * ypserv supports the following procedures:
 *
 * YPPROC_NULL		takes (void), returns (void).
 *			called to check if server is alive.
 * YPPROC_DOMAIN	takes (char *), returns (bool_t).
 *			true if ypserv serves the named domain.
 * YPPROC_DOMAIN_NOACK	takes (char *), returns (bool_t).
 *			true if ypserv serves the named domain.
 *			used for broadcasts, does not ack if ypserv
 *			doesn't handle named domain.
 * YPPROC_MATCH		takes (struct ypreq_key), returns (struct ypresp_val)
 *			does a lookup.
 * YPPROC_FIRST		takes (struct ypreq_nokey) returns (ypresp_key_val).
 *			gets the first key/datum from the map.
 * YPPROC_NEXT		takes (struct ypreq_key) returns (ypresp_key_val).
 *			gets the next key/datum from the map.
 * YPPROC_XFR		takes (struct ypreq_xfr), returns (void).
 *			tells ypserv to check if there is a new version of
 *			the map.
 * YPPROC_CLEAR		takes (void), returns (void).
 *			tells ypserv to flush its file cache, so that
 *			newly transferred files will get read.
 * YPPROC_ALL		takes (struct ypreq_nokey), returns (bool_t and
 *			struct ypresp_key_val).
 *			returns an array of data, with the bool_t being
 *			false on the last datum. read the source, it's
 *			convoluted.
 * YPPROC_MASTER	takes (struct ypreq_nokey), returns (ypresp_master).
 * YPPROC_ORDER		takes (struct ypreq_nokey), returns (ypresp_order).
 * YPPROC_MAPLIST	takes (char *), returns (struct ypmaplist *).
 */

#ifndef BOOL_DEFINED
typedef unsigned int bool;
#define BOOL_DEFINED
#endif


/* Program and version symbols, magic numbers */
#define YPPROG		((unsigned long)100004)
#define YPVERS		((unsigned long)2)
#define YPVERS_ORIG	((unsigned long)1)
#define YPMAXRECORD	((unsigned long)1024)
#define YPMAXDOMAIN	((unsigned long)64)
#define YPMAXMAP	((unsigned long)64)
#define YPMAXPEER	((unsigned long)256)

/*
 * I don't know if anything of sun's depends on this, or if they
 * simply defined it so that their own code wouldn't try to send
 * packets over the ethernet MTU. This YP code doesn't use it.
 */
#define YPMSGSZ		1600

#ifndef DATUM
typedef struct {
	const char	*dptr;
	int		 dsize;
} datum;
#define DATUM
#endif

struct ypmap_parms {
	const char *domain;
	const char *map;
	unsigned long ordernum;
	char *owner;
};

struct ypreq_key {
	const char *domain;
	const char *map;
	datum keydat;
};

struct ypreq_nokey {
	const char *domain;
	const char *map;
};

struct ypreq_xfr {
	struct ypmap_parms map_parms;
	unsigned long transid;
	unsigned long proto;
	unsigned short port;
};
#define ypxfr_domain	map_parms.domain
#define ypxfr_map	map_parms.map
#define ypxfr_ordernum	map_parms.ordernum
#define ypxfr_owner	map_parms.owner

struct ypresp_val {
	unsigned long status;
	datum valdat;
};

struct ypresp_key_val {
	unsigned long status;
	datum keydat;
	datum valdat;
};

struct ypresp_master {
	unsigned long status;
	char *master;
};

struct ypresp_order {
	unsigned long status;
	unsigned long ordernum;
};

struct ypresp_all {
	bool_t more;
	union {
		struct ypresp_key_val val;
	} ypresp_all_u;
};

struct ypmaplist {
	char ypml_name[YPMAXMAP + 1];
	struct ypmaplist *ypml_next;
};

struct ypresp_maplist {
	unsigned long status;
	struct ypmaplist *list;
};

/* ypserv procedure numbers */
#define YPPROC_NULL		((unsigned long)0)
#define YPPROC_DOMAIN		((unsigned long)1)
#define YPPROC_DOMAIN_NONACK	((unsigned long)2)
#define YPPROC_MATCH		((unsigned long)3)
#define YPPROC_FIRST		((unsigned long)4)
#define YPPROC_NEXT		((unsigned long)5)
#define YPPROC_XFR		((unsigned long)6)
#define YPPROC_CLEAR		((unsigned long)7)
#define YPPROC_ALL		((unsigned long)8)
#define YPPROC_MASTER		((unsigned long)9)
#define YPPROC_ORDER		((unsigned long)10)
#define YPPROC_MAPLIST		((unsigned long)11)

/* ypserv procedure return status values */
#define YP_TRUE		((unsigned long)1)	/* general purpose success code */
#define YP_NOMORE	((unsigned long)2)	/* no more entries in map */
#define YP_FALSE	((unsigned long)0)	/* general purpose failure code */
#define YP_NOMAP	((unsigned long)-1)	/* no such map in domain */
#define YP_NODOM	((unsigned long)-2)	/* domain not supported */
#define YP_NOKEY	((unsigned long)-3)	/* no such key in map */
#define YP_BADOP	((unsigned long)-4)	/* invalid operation */
#define YP_BADDB	((unsigned long)-5)	/* server data base is bad */
#define YP_YPERR	((unsigned long)-6)	/* YP server error */
#define YP_BADARGS	((unsigned long)-7)	/* request arguments bad */
#define YP_VERS		((unsigned long)-8)	/* YP server version mismatch */

/*
 * YPBIND PROTOCOL:
 *
 * ypbind supports the following procedures:
 *
 * YPBINDPROC_NULL	takes (void), returns (void).
 *			to check if ypbind is running.
 * YPBINDPROC_DOMAIN	takes (char *), returns (struct ypbind_resp).
 *			requests that ypbind start to serve the
 *			named domain (if it doesn't already)
 * YPBINDPROC_SETDOM	takes (struct ypbind_setdom), returns (void).
 *			used by ypset.
 */

#define YPBINDPROG		((unsigned long)100007)
#define YPBINDVERS		((unsigned long)2)
#define YPBINDVERS_ORIG		((unsigned long)1)

/* ypbind procedure numbers */
#define YPBINDPROC_NULL		((unsigned long)0)
#define YPBINDPROC_DOMAIN	((unsigned long)1)
#define YPBINDPROC_SETDOM	((unsigned long)2)

/* error code in ypbind_resp.ypbind_status */
enum ypbind_resptype {
	YPBIND_SUCC_VAL = 1,
	YPBIND_FAIL_VAL = 2
};

/* network order, of course */
struct ypbind_binding {
	struct in_addr	ypbind_binding_addr;
	unsigned short	ypbind_binding_port;
};

struct ypbind_resp {
	enum ypbind_resptype	ypbind_status;
	union {
		unsigned long		ypbind_error;
		struct ypbind_binding	ypbind_bindinfo;
	} ypbind_respbody;
};

/* error code in ypbind_resp.ypbind_respbody.ypbind_error */
#define YPBIND_ERR_ERR		1	/* internal error */
#define YPBIND_ERR_NOSERV	2	/* no bound server for passed domain */
#define YPBIND_ERR_RESC		3	/* system resource allocation failure */

/*
 * Request data structure for ypbind "Set domain" procedure.
 */
struct ypbind_setdom {
	char ypsetdom_domain[YPMAXDOMAIN + 1];
	struct ypbind_binding ypsetdom_binding;
	unsigned short ypsetdom_vers;
};
#define ypsetdom_addr ypsetdom_binding.ypbind_binding_addr
#define ypsetdom_port ypsetdom_binding.ypbind_binding_port

/*
 * YPPUSH PROTOCOL:
 *
 * Sun says:
 * "Protocol between clients (ypxfr, only) and yppush
 *  yppush speaks a protocol in the transient range, which
 *  is supplied to ypxfr as a command-line parameter when it
 *  is activated by ypserv."
 *
 * This protocol is not implemented, naturally, because this YP
 * implementation only does the client side.
 */
#define YPPUSHVERS		((unsigned long)1)
#define YPPUSHVERS_ORIG		((unsigned long)1)

/* yppush procedure numbers */
#define YPPUSHPROC_NULL		((unsigned long)0)
#define YPPUSHPROC_XFRRESP	((unsigned long)1)

struct yppushresp_xfr {
	unsigned long	transid;
	unsigned long	status;
};

/* yppush status value in yppushresp_xfr.status */
#define YPPUSH_SUCC	((unsigned long)1)	/* Success */
#define YPPUSH_AGE	((unsigned long)2)	/* Master's version not newer */
#define YPPUSH_NOMAP	((unsigned long)-1)	/* Can't find server for map */
#define YPPUSH_NODOM	((unsigned long)-2)	/* Domain not supported */
#define YPPUSH_RSRC	((unsigned long)-3)	/* Local resource alloc failure */
#define YPPUSH_RPC	((unsigned long)-4)	/* RPC failure talking to server */
#define YPPUSH_MADDR	((unsigned long)-5)	/* Can't get master address */
#define YPPUSH_YPERR	((unsigned long)-6)	/* YP server/map db error */
#define YPPUSH_BADARGS	((unsigned long)-7)	/* Request arguments bad */
#define YPPUSH_DBM	((unsigned long)-8)	/* Local dbm operation failed */
#define YPPUSH_FILE	((unsigned long)-9)	/* Local file I/O operation failed */
#define YPPUSH_SKEW	((unsigned long)-10)	/* Map version skew during transfer */
#define YPPUSH_CLEAR	((unsigned long)-11)	/* Can't send "Clear" req to local ypserv */
#define YPPUSH_FORCE	((unsigned long)-12)	/* No local order number in map - use -f */
#define YPPUSH_XFRERR	((unsigned long)-13)	/* ypxfr error */
#define YPPUSH_REFUSED	((unsigned long)-14)	/* Transfer request refused by ypserv */

__BEGIN_DECLS
bool_t xdr_domainname(XDR *, char *);
bool_t xdr_peername(XDR *, char *);
bool_t xdr_datum(XDR *, datum *);
bool_t xdr_mapname(XDR *, char *);
bool_t xdr_ypreq_key(XDR *, struct ypreq_key *);
bool_t xdr_ypreq_nokey(XDR *, struct ypreq_nokey *);
bool_t xdr_yp_inaddr(XDR *, struct in_addr *);
bool_t xdr_ypbind_binding(XDR *, struct ypbind_binding *);
bool_t xdr_ypbind_resptype(XDR *, enum ypbind_resptype *);
bool_t xdr_ypstat(XDR *, enum ypbind_resptype *);
bool_t xdr_ypbind_resp(XDR *, struct ypbind_resp *);
bool_t xdr_ypresp_val(XDR *, struct ypresp_val *);
bool_t xdr_ypbind_setdom(XDR *, struct ypbind_setdom *);
bool_t xdr_ypresp_key_val(XDR *, struct ypresp_key_val *);
bool_t xdr_ypresp_all(XDR *, struct ypresp_all *);
bool_t xdr_ypresp_master(XDR *, struct ypresp_master *);
bool_t xdr_ypmaplist_str(XDR *, char *);
bool_t xdr_ypmaplist(XDR *, struct ypmaplist *);
bool_t xdr_ypresp_maplist(XDR *, struct ypresp_maplist *);
bool_t xdr_ypresp_order(XDR *, struct ypresp_order *);
__END_DECLS

#endif /* _RPCSVC_YP_PROT_H_ */
