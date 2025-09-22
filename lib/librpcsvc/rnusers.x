/*	$OpenBSD: rnusers.x,v 1.15 2022/12/27 17:10:07 jmc Exp $	*/

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Find out about remote users
 */

#ifndef RPC_HDR
#endif


#ifdef RPC_HDR
%/*
% * The following structures are used by version 2 of the rusersd protocol.
% * They were not developed with rpcgen, so they do not appear as RPCL.
% */
%
%#define	RUSERSVERS_ORIG 1	/* original version */
%#define	RUSERSVERS_IDLE 2
%#define	MAXUSERS 100
%
%/*
% * This is the structure used in version 2 of the rusersd RPC service.
% * It corresponds to the utmp structure for BSD systems.
% */
%
%#define RNUSERS_MAXUSERLEN 8
%#define RNUSERS_MAXLINELEN 8
%#define RNUSERS_MAXHOSTLEN 16
%
%struct ru_utmp {
%	char	*ut_line;		/* tty name */
%	char	*ut_name;		/* user id */
%	char	*ut_host;		/* host name, if remote */
%	int	ut_time;		/* time on */
%};
%typedef struct ru_utmp rutmp;
%
%struct utmparr {
%	struct ru_utmp **uta_arr;
%	int uta_cnt;
%};
%typedef struct utmparr utmparr;
%int	xdr_utmparr(XDR *, struct utmparr *);
%
%struct utmpidle {
%	struct ru_utmp ui_utmp;
%	unsigned ui_idle;
%};
%
%struct utmpidlearr {
%	struct utmpidle **uia_arr;
%	int uia_cnt;
%};
%typedef struct utmpidlearr utmpidlearr;
%int xdr_utmpidlearr(XDR *, struct utmpidlearr *);
%
%#define RUSERSVERS_1 ((u_long)1)
%#define RUSERSVERS_2 ((u_long)2)
%#ifndef RUSERSPROG
%#define RUSERSPROG ((u_long)100002)
%#endif
%#ifndef RUSERSPROC_NUM
%#define RUSERSPROC_NUM ((u_long)1)
%#endif
%#ifndef RUSERSPROC_NAMES
%#define RUSERSPROC_NAMES ((u_long)2)
%#endif
%#ifndef RUSERSPROC_ALLNAMES
%#define RUSERSPROC_ALLNAMES ((u_long)3)
%#endif
%
#endif	/* RPC_HDR */

#ifdef	RPC_XDR
%bool_t	xdr_utmp(XDR *, struct ru_utmp *);
%bool_t	xdr_utmpptr(XDR *, struct ru_utmp **);
%bool_t	xdr_utmparr(XDR *, struct utmparr *);
%bool_t	xdr_utmpidle(XDR *, struct utmpidle *);
%bool_t	xdr_utmpidleptr(XDR *, struct utmpidle **);
%
%bool_t
%xdr_utmp(XDR *xdrs, struct ru_utmp *objp)
%{
%	int size;
%
%	size = RNUSERS_MAXLINELEN;
%	if (!xdr_bytes(xdrs, &objp->ut_line, &size, RNUSERS_MAXLINELEN))
%		return (FALSE);
%	size = RNUSERS_MAXUSERLEN;
%	if (!xdr_bytes(xdrs, &objp->ut_name, &size, RNUSERS_MAXUSERLEN))
%		return (FALSE);
%	size = RNUSERS_MAXHOSTLEN;
%	if (!xdr_bytes(xdrs, &objp->ut_host, &size, RNUSERS_MAXHOSTLEN))
%		return (FALSE);
%	if (!xdr_int(xdrs, &objp->ut_time))
%		return (FALSE);
%	return (TRUE);
%}
%
%bool_t
%xdr_utmpptr(XDR *xdrs, struct ru_utmp **objpp)
%{
%	if (!xdr_reference(xdrs, (char **) objpp, sizeof (struct ru_utmp),
%	    xdr_utmp))
%		return (FALSE);
%	return (TRUE);
%}
%
%bool_t
%xdr_utmparr(XDR *xdrs, struct utmparr *objp)
%{
%	if (!xdr_array(xdrs, (char **)&objp->uta_arr, (u_int *)&objp->uta_cnt,
%	    MAXUSERS, sizeof(struct ru_utmp *), xdr_utmpptr))
%		return (FALSE);
%	return (TRUE);
%}
%
%bool_t
%xdr_utmpidle(XDR *xdrs, struct utmpidle *objp)
%{
%	if (!xdr_utmp(xdrs, &objp->ui_utmp))
%		return (FALSE);
%	if (!xdr_u_int(xdrs, &objp->ui_idle))
%		return (FALSE);
%	return (TRUE);
%}
%
%bool_t
%xdr_utmpidleptr(XDR *xdrs, struct utmpidle **objpp)
%{
%	if (!xdr_reference(xdrs, (char **) objpp, sizeof (struct utmpidle),
%	    xdr_utmpidle))
%		return (FALSE);
%	return (TRUE);
%}
%
%bool_t
%xdr_utmpidlearr(XDR *xdrs, struct utmpidlearr *objp)
%{
%	if (!xdr_array(xdrs, (char **)&objp->uia_arr, (u_int *)&objp->uia_cnt,
%	    MAXUSERS, sizeof(struct utmpidle *), xdr_utmpidleptr))
%		return (FALSE);
%	return (TRUE);
%}
#endif
