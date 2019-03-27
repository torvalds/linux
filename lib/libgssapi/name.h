/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Doug Rabson
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

#include <sys/queue.h>

struct _gss_mechanism_name {
	SLIST_ENTRY(_gss_mechanism_name) gmn_link;
	struct _gss_mech_switch *gmn_mech;	/* mechanism ops for MN */
	gss_OID			gmn_mech_oid;	/* mechanism oid for MN */
	gss_name_t		gmn_name;	/* underlying MN */
};
SLIST_HEAD(_gss_mechanism_name_list, _gss_mechanism_name);

struct _gss_name {
	gss_OID_desc		gn_type;	/* type of name */
	gss_buffer_desc		gn_value;	/* value (as imported) */
	struct _gss_mechanism_name_list gn_mn;	/* list of MNs */
};

extern OM_uint32
	_gss_find_mn(OM_uint32 *, struct _gss_name *, gss_OID, 
	      struct _gss_mechanism_name **);
extern struct _gss_name *
	_gss_make_name(struct _gss_mech_switch *m, gss_name_t new_mn);
