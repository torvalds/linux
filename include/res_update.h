/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1999 by Internet Software Consortium, Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 *	$Id: res_update.h,v 1.3 2005/04/27 04:56:15 sra Exp $
 * $FreeBSD$
 */

#ifndef __RES_UPDATE_H
#define __RES_UPDATE_H

/*! \file */

#include <sys/types.h>
#include <arpa/nameser.h>
#include <resolv.h>

/*%
 * This RR-like structure is particular to UPDATE.
 */
struct ns_updrec {
	struct {
		struct ns_updrec *prev;
		struct ns_updrec *next;
	} r_link, r_glink;
	ns_sect		r_section;	/*%< ZONE/PREREQUISITE/UPDATE */
	char *		r_dname;	/*%< owner of the RR */
	ns_class	r_class;	/*%< class number */
	ns_type		r_type;		/*%< type number */
	u_int32_t	r_ttl;		/*%< time to live */
	u_char *	r_data;		/*%< rdata fields as text string */
	u_int		r_size;		/*%< size of r_data field */
	int		r_opcode;	/*%< type of operation */
	/* following fields for private use by the resolver/server routines */
	struct databuf *r_dp;		/*%< databuf to process */
	struct databuf *r_deldp;	/*%< databuf's deleted/overwritten */
	u_int		r_zone;		/*%< zone number on server */
};
typedef struct ns_updrec ns_updrec;
typedef struct {
	ns_updrec *head;
	ns_updrec *tail;
} ns_updque;

#define res_mkupdate		__res_mkupdate
#define res_update		__res_update
#define res_mkupdrec		__res_mkupdrec
#define res_freeupdrec		__res_freeupdrec
#define res_nmkupdate		__res_nmkupdate
#define res_nupdate		__res_nupdate

int		res_mkupdate(ns_updrec *, u_char *, int);
int		res_update(ns_updrec *);
ns_updrec *	res_mkupdrec(int, const char *, u_int, u_int, u_long);
void		res_freeupdrec(ns_updrec *);
int		res_nmkupdate(res_state, ns_updrec *, u_char *, int);
int		res_nupdate(res_state, ns_updrec *, ns_tsig_key *);

#endif /*__RES_UPDATE_H*/

/*! \file */
