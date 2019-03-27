/*
  SPDX-License-Identifier: BSD-3-Clause

  rpcsec_gss_prot.c
  
  Copyright (c) 2000 The Regents of the University of Michigan.
  All rights reserved.
  
  Copyright (c) 2000 Dug Song <dugsong@UMICH.EDU>.
  All rights reserved, all wrongs reversed.
  
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
  3. Neither the name of the University nor the names of its
     contributors may be used to endorse or promote products derived
     from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  $Id: authgss_prot.c,v 1.18 2000/09/01 04:14:03 dugsong Exp $
*/
/* $FreeBSD$ */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <rpc/rpc.h>
#include <rpc/rpcsec_gss.h>
#include "rpcsec_gss_int.h"

#define MAX_GSS_SIZE	10240	/* XXX */

bool_t
xdr_gss_buffer_desc(XDR *xdrs, gss_buffer_desc *p)
{
	char *val;
	u_int len;
	bool_t ret;

	val = p->value;
	len = p->length;
	ret = xdr_bytes(xdrs, &val, &len, MAX_GSS_SIZE);
	p->value = val;
	p->length = len;

	return (ret);
}

bool_t
xdr_rpc_gss_cred(XDR *xdrs, struct rpc_gss_cred *p)
{
	enum_t proc, svc;
	bool_t ret;

	proc = p->gc_proc;
	svc = p->gc_svc;
	ret = (xdr_u_int(xdrs, &p->gc_version) &&
	    xdr_enum(xdrs, &proc) &&
	    xdr_u_int(xdrs, &p->gc_seq) &&
	    xdr_enum(xdrs, &svc) &&
	    xdr_gss_buffer_desc(xdrs, &p->gc_handle));
	p->gc_proc = proc;
	p->gc_svc = svc;

	return (ret);
}

bool_t
xdr_rpc_gss_init_res(XDR *xdrs, struct rpc_gss_init_res *p)
{

	return (xdr_gss_buffer_desc(xdrs, &p->gr_handle) &&
	    xdr_u_int(xdrs, &p->gr_major) &&
	    xdr_u_int(xdrs, &p->gr_minor) &&
	    xdr_u_int(xdrs, &p->gr_win) &&
	    xdr_gss_buffer_desc(xdrs, &p->gr_token));
}

bool_t
xdr_rpc_gss_wrap_data(XDR *xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr,
		      gss_ctx_id_t ctx, gss_qop_t qop,
		      rpc_gss_service_t svc, u_int seq)
{
	gss_buffer_desc	databuf, wrapbuf;
	OM_uint32	maj_stat, min_stat;
	int		start, end, conf_state;
	u_int		len;
	bool_t		xdr_stat;

	/* Skip databody length. */
	start = XDR_GETPOS(xdrs);
	XDR_SETPOS(xdrs, start + 4);
	
	/* Marshal rpc_gss_data_t (sequence number + arguments). */
	if (!xdr_u_int(xdrs, &seq) || !xdr_func(xdrs, xdr_ptr))
		return (FALSE);
	end = XDR_GETPOS(xdrs);

	/* Set databuf to marshalled rpc_gss_data_t. */
	databuf.length = end - start - 4;
	XDR_SETPOS(xdrs, start + 4);
	databuf.value = XDR_INLINE(xdrs, databuf.length);

	xdr_stat = FALSE;
	
	if (svc == rpc_gss_svc_integrity) {
		/* Marshal databody_integ length. */
		XDR_SETPOS(xdrs, start);
		len = databuf.length;
		if (!xdr_u_int(xdrs, &len))
			return (FALSE);
		
		/* Checksum rpc_gss_data_t. */
		maj_stat = gss_get_mic(&min_stat, ctx, qop,
				       &databuf, &wrapbuf);
		if (maj_stat != GSS_S_COMPLETE) {
			log_debug("gss_get_mic failed");
			return (FALSE);
		}
		/* Marshal checksum. */
		XDR_SETPOS(xdrs, end);
		xdr_stat = xdr_gss_buffer_desc(xdrs, &wrapbuf);
		gss_release_buffer(&min_stat, &wrapbuf);
	}		
	else if (svc == rpc_gss_svc_privacy) {
		/* Encrypt rpc_gss_data_t. */
		maj_stat = gss_wrap(&min_stat, ctx, TRUE, qop, &databuf,
				    &conf_state, &wrapbuf);
		if (maj_stat != GSS_S_COMPLETE) {
			log_status("gss_wrap", NULL, maj_stat, min_stat);
			return (FALSE);
		}
		/* Marshal databody_priv. */
		XDR_SETPOS(xdrs, start);
		xdr_stat = xdr_gss_buffer_desc(xdrs, &wrapbuf);
		gss_release_buffer(&min_stat, &wrapbuf);
	}
	return (xdr_stat);
}

bool_t
xdr_rpc_gss_unwrap_data(XDR *xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr,
			gss_ctx_id_t ctx, gss_qop_t qop,
			rpc_gss_service_t svc, u_int seq)
{
	XDR		tmpxdrs;
	gss_buffer_desc	databuf, wrapbuf;
	OM_uint32	maj_stat, min_stat;
	u_int		seq_num, conf_state, qop_state;
	bool_t		xdr_stat;

	if (xdr_func == (xdrproc_t) xdr_void || xdr_ptr == NULL)
		return (TRUE);
	
	memset(&databuf, 0, sizeof(databuf));
	memset(&wrapbuf, 0, sizeof(wrapbuf));
	
	if (svc == rpc_gss_svc_integrity) {
		/* Decode databody_integ. */
		if (!xdr_gss_buffer_desc(xdrs, &databuf)) {
			log_debug("xdr decode databody_integ failed");
			return (FALSE);
		}
		/* Decode checksum. */
		if (!xdr_gss_buffer_desc(xdrs, &wrapbuf)) {
			mem_free(databuf.value, databuf.length);
			log_debug("xdr decode checksum failed");
			return (FALSE);
		}
		/* Verify checksum and QOP. */
		maj_stat = gss_verify_mic(&min_stat, ctx, &databuf,
					  &wrapbuf, &qop_state);
		mem_free(wrapbuf.value, wrapbuf.length);
		
		if (maj_stat != GSS_S_COMPLETE || qop_state != qop) {
			mem_free(databuf.value, databuf.length);
			log_status("gss_verify_mic", NULL, maj_stat, min_stat);
			return (FALSE);
		}
	} else if (svc == rpc_gss_svc_privacy) {
		/* Decode databody_priv. */
		if (!xdr_gss_buffer_desc(xdrs, &wrapbuf)) {
			log_debug("xdr decode databody_priv failed");
			return (FALSE);
		}
		/* Decrypt databody. */
		maj_stat = gss_unwrap(&min_stat, ctx, &wrapbuf, &databuf,
				      &conf_state, &qop_state);
		
		mem_free(wrapbuf.value, wrapbuf.length);
		
		/* Verify encryption and QOP. */
		if (maj_stat != GSS_S_COMPLETE || qop_state != qop ||
			conf_state != TRUE) {
			gss_release_buffer(&min_stat, &databuf);
			log_status("gss_unwrap", NULL, maj_stat, min_stat);
			return (FALSE);
		}
	}
	/* Decode rpc_gss_data_t (sequence number + arguments). */
	xdrmem_create(&tmpxdrs, databuf.value, databuf.length, XDR_DECODE);
	xdr_stat = (xdr_u_int(&tmpxdrs, &seq_num) &&
	    xdr_func(&tmpxdrs, xdr_ptr));
	XDR_DESTROY(&tmpxdrs);

	/*
	 * Integrity service allocates databuf via XDR so free it the
	 * same way.
	 */
	if (svc == rpc_gss_svc_integrity) {
		xdr_free((xdrproc_t) xdr_gss_buffer_desc, (char *) &databuf);
	} else {
		gss_release_buffer(&min_stat, &databuf);
	}
	
	/* Verify sequence number. */
	if (xdr_stat == TRUE && seq_num != seq) {
		log_debug("wrong sequence number in databody");
		return (FALSE);
	}
	return (xdr_stat);
}

#ifdef DEBUG
#include <ctype.h>

void
log_debug(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "rpcsec_gss: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

void
log_status(const char *m, gss_OID mech, OM_uint32 maj_stat, OM_uint32 min_stat)
{
	OM_uint32 min;
	gss_buffer_desc msg;
	int msg_ctx = 0;

	fprintf(stderr, "rpcsec_gss: %s: ", m);
	
	gss_display_status(&min, maj_stat, GSS_C_GSS_CODE, GSS_C_NULL_OID,
			   &msg_ctx, &msg);
	fprintf(stderr, "%s - ", (char *)msg.value);
	gss_release_buffer(&min, &msg);

	gss_display_status(&min, min_stat, GSS_C_MECH_CODE, mech,
			   &msg_ctx, &msg);
	fprintf(stderr, "%s\n", (char *)msg.value);
	gss_release_buffer(&min, &msg);
}

#else

void
log_debug(__unused const char *fmt, ...)
{
}

void
log_status(__unused const char *m, __unused gss_OID mech,
    __unused OM_uint32 maj_stat, __unused OM_uint32 min_stat)
{
}

#endif


