/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef _KERNEL
#include <sys/malloc.h>
#else
#include <stdlib.h>
#include <string.h>
#endif

#include <rpc/rpc.h>
#include <rpc/rpc_com.h>

#include "gssd.h"

bool_t
xdr_gss_buffer_desc(XDR *xdrs, gss_buffer_desc *buf)
{
	char *val;
	u_int len;

	len = buf->length;
	val = buf->value;
	if (!xdr_bytes(xdrs, &val, &len, ~0))
		return (FALSE);
	buf->length = len;
	buf->value = val;

	return (TRUE);
}

bool_t
xdr_gss_OID_desc(XDR *xdrs, gss_OID_desc *oid)
{
	char *val;
	u_int len;

	len = oid->length;
	val = oid->elements;
	if (!xdr_bytes(xdrs, &val, &len, ~0))
		return (FALSE);
	oid->length = len;
	oid->elements = val;

	return (TRUE);
}

bool_t
xdr_gss_OID(XDR *xdrs, gss_OID *oidp)
{
	gss_OID oid;
	bool_t is_null;

	switch (xdrs->x_op) {
	case XDR_ENCODE:
		oid = *oidp;
		if (oid) {
			is_null = FALSE;
			if (!xdr_bool(xdrs, &is_null)
			    || !xdr_gss_OID_desc(xdrs, oid))
				return (FALSE);
		} else {
			is_null = TRUE;
			if (!xdr_bool(xdrs, &is_null))
				return (FALSE);
		}
		break;

	case XDR_DECODE:
		if (!xdr_bool(xdrs, &is_null))
			return (FALSE);
		if (is_null) {
			*oidp = GSS_C_NO_OID;
		} else {
			oid = mem_alloc(sizeof(gss_OID_desc));
			memset(oid, 0, sizeof(*oid));
			if (!xdr_gss_OID_desc(xdrs, oid)) {
				mem_free(oid, sizeof(gss_OID_desc));
				return (FALSE);
			}
			*oidp = oid;
		}
		break;

	case XDR_FREE:
		oid = *oidp;
		if (oid) {
			xdr_gss_OID_desc(xdrs, oid);
			mem_free(oid, sizeof(gss_OID_desc));
		}
	}

	return (TRUE);
}

bool_t
xdr_gss_OID_set_desc(XDR *xdrs, gss_OID_set_desc *set)
{
	caddr_t addr;
	u_int len;

	len = set->count;
	addr = (caddr_t) set->elements;
	if (!xdr_array(xdrs, &addr, &len, ~0, sizeof(gss_OID_desc),
		(xdrproc_t) xdr_gss_OID_desc))
		return (FALSE);
	set->count = len;
	set->elements = (gss_OID) addr;

	return (TRUE);
}

bool_t
xdr_gss_OID_set(XDR *xdrs, gss_OID_set *setp)
{
	gss_OID_set set;
	bool_t is_null;

	switch (xdrs->x_op) {
	case XDR_ENCODE:
		set = *setp;
		if (set) {
			is_null = FALSE;
			if (!xdr_bool(xdrs, &is_null)
			    || !xdr_gss_OID_set_desc(xdrs, set))
				return (FALSE);
		} else {
			is_null = TRUE;
			if (!xdr_bool(xdrs, &is_null))
				return (FALSE);
		}
		break;

	case XDR_DECODE:
		if (!xdr_bool(xdrs, &is_null))
			return (FALSE);
		if (is_null) {
			*setp = GSS_C_NO_OID_SET;
		} else {
			set = mem_alloc(sizeof(gss_OID_set_desc));
			memset(set, 0, sizeof(*set));
			if (!xdr_gss_OID_set_desc(xdrs, set)) {
				mem_free(set, sizeof(gss_OID_set_desc));
				return (FALSE);
			}
			*setp = set;
		}
		break;

	case XDR_FREE:
		set = *setp;
		if (set) {
			xdr_gss_OID_set_desc(xdrs, set);
			mem_free(set, sizeof(gss_OID_set_desc));
		}
	}

	return (TRUE);
}

bool_t
xdr_gss_channel_bindings_t(XDR *xdrs, gss_channel_bindings_t *chp)
{
	gss_channel_bindings_t ch;
	bool_t is_null;

	switch (xdrs->x_op) {
	case XDR_ENCODE:
		ch = *chp;
		if (ch) {
			is_null = FALSE;
			if (!xdr_bool(xdrs, &is_null)
			    || !xdr_uint32_t(xdrs, &ch->initiator_addrtype)
			    || !xdr_gss_buffer_desc(xdrs,
				&ch->initiator_address)
			    || !xdr_uint32_t(xdrs, &ch->acceptor_addrtype)
			    || !xdr_gss_buffer_desc(xdrs,
				&ch->acceptor_address)
			    || !xdr_gss_buffer_desc(xdrs,
				&ch->application_data))
				return (FALSE);
		} else {
			is_null = TRUE;
			if (!xdr_bool(xdrs, &is_null))
				return (FALSE);
		}
		break;

	case XDR_DECODE:
		if (!xdr_bool(xdrs, &is_null))
			return (FALSE);
		if (is_null) {
			*chp = GSS_C_NO_CHANNEL_BINDINGS;
		} else {
			ch = mem_alloc(sizeof(*ch));
			memset(ch, 0, sizeof(*ch));
			if (!xdr_uint32_t(xdrs, &ch->initiator_addrtype)
			    || !xdr_gss_buffer_desc(xdrs,
				&ch->initiator_address)
			    || !xdr_uint32_t(xdrs, &ch->acceptor_addrtype)
			    || !xdr_gss_buffer_desc(xdrs,
				&ch->acceptor_address)
			    || !xdr_gss_buffer_desc(xdrs,
				&ch->application_data)) {
				mem_free(ch, sizeof(*ch));
				return (FALSE);
			}
			*chp = ch;
		}
		break;

	case XDR_FREE:
		ch = *chp;
		if (ch) {
			xdr_gss_buffer_desc(xdrs, &ch->initiator_address);
			xdr_gss_buffer_desc(xdrs, &ch->acceptor_address);
			xdr_gss_buffer_desc(xdrs, &ch->application_data);
			mem_free(ch, sizeof(*ch));
		}
	}

	return (TRUE);
}
