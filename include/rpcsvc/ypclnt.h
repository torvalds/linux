/*	$OpenBSD: ypclnt.h,v 1.10 2016/05/30 02:53:29 guenther Exp $	*/
/*	$NetBSD: ypclnt.h,v 1.7 1995/07/14 21:11:10 christos Exp $	*/

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

#ifndef _RPCSVC_YPCLNT_H_
#define _RPCSVC_YPCLNT_H_

#define YPERR_BADARGS	1		/* args to function are bad */
#define YPERR_RPC	2		/* RPC failure */
#define YPERR_DOMAIN	3		/* can't bind to a server for domain */
#define YPERR_MAP	4		/* no such map in server's domain */
#define YPERR_KEY	5		/* no such key in map */
#define YPERR_YPERR	6		/* some internal YP server or client error */
#define YPERR_RESRC	7		/* local resource allocation failure */
#define YPERR_NOMORE	8		/* no more records in map database */
#define YPERR_PMAP	9		/* can't communicate with portmapper */
#define YPERR_YPBIND	10		/* can't communicate with ypbind */
#define YPERR_YPSERV	11		/* can't communicate with ypserv */
#define YPERR_NODOM	12		/* local domain name not set */
#define YPERR_BADDB	13		/* YP data base is bad */
#define YPERR_VERS	14		/* YP version mismatch */
#define YPERR_ACCESS	15		/* access violation */
#define YPERR_BUSY	16		/* database is busy */

/*
 * Types of update operations
 */
#define YPOP_CHANGE	1		/* change, do not add */
#define YPOP_INSERT	2		/* add, do not change */
#define YPOP_DELETE	3		/* delete this entry */
#define YPOP_STORE	4		/* add, or change */

struct ypall_callback {
	/* return non-0 to stop getting called */
	int (*foreach)(unsigned long, char *, int, char *, int, void *);
	char *data;		/* opaque pointer for use of callback fn */
};

__BEGIN_DECLS
int	yp_bind(const char *);
void	yp_unbind(const char *);
int	yp_get_default_domain(char **);
int	yp_match(const char *, const char *, const char *, int , char **,
	    int *);
int	yp_first(const char *, const char *, char **, int *, char **, int *);
int	yp_next(const char *, const char *, const char *, int, char **, int *,
	    char **, int *);
int	yp_master(const char *, const char *, char **);
int	yp_order(const char *, const char *, int *);
int	yp_all(const char *, const char *, struct ypall_callback *);
char *	yperr_string(int);
int	ypprot_err(unsigned int);
struct ypmaplist;
int	yp_maplist(const char *, struct ypmaplist **);
__END_DECLS

#endif /* _RPCSVC_YPCLNT_H_ */
