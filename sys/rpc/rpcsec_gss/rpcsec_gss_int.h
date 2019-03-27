/*
  rpcsec_gss.h
  
  SPDX-License-Identifier: BSD-3-Clause

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

  $Id: auth_gss.h,v 1.12 2001/04/30 19:44:47 andros Exp $
*/
/* $FreeBSD$ */

#ifndef _RPCSEC_GSS_INT_H
#define _RPCSEC_GSS_INT_H

#include <kgssapi/gssapi_impl.h>

/* RPCSEC_GSS control procedures. */
typedef enum {
	RPCSEC_GSS_DATA = 0,
	RPCSEC_GSS_INIT = 1,
	RPCSEC_GSS_CONTINUE_INIT = 2,
	RPCSEC_GSS_DESTROY = 3
} rpc_gss_proc_t;

#define RPCSEC_GSS_VERSION	1

/* Credentials. */
struct rpc_gss_cred {
	u_int		gc_version;	/* version */
	rpc_gss_proc_t	gc_proc;	/* control procedure */
	u_int		gc_seq;		/* sequence number */
	rpc_gss_service_t gc_svc;	/* service */
	gss_buffer_desc	gc_handle;	/* handle to server-side context */
};

/* Context creation response. */
struct rpc_gss_init_res {
	gss_buffer_desc	gr_handle;	/* handle to server-side context */
	u_int		gr_major;	/* major status */
	u_int		gr_minor;	/* minor status */
	u_int		gr_win;		/* sequence window */
	gss_buffer_desc	gr_token;	/* token */
};

/* Maximum sequence number value. */
#define MAXSEQ		0x80000000

/* Prototypes. */
__BEGIN_DECLS

bool_t	xdr_rpc_gss_cred(XDR *xdrs, struct rpc_gss_cred *p);
bool_t	xdr_rpc_gss_init_res(XDR *xdrs, struct rpc_gss_init_res *p);
bool_t xdr_rpc_gss_wrap_data(struct mbuf **argsp,
    gss_ctx_id_t ctx, gss_qop_t qop, rpc_gss_service_t svc,
    u_int seq);
bool_t xdr_rpc_gss_unwrap_data(struct mbuf **resultsp,
    gss_ctx_id_t ctx, gss_qop_t qop, rpc_gss_service_t svc, u_int seq);
const char *_rpc_gss_num_to_qop(const char *mech, u_int num);
void	_rpc_gss_set_error(int rpc_gss_error, int system_error);

void	rpc_gss_log_debug(const char *fmt, ...);
void	rpc_gss_log_status(const char *m, gss_OID mech, OM_uint32 major,
    OM_uint32 minor);

__END_DECLS

#endif /* !_RPCSEC_GSS_INT_H */
