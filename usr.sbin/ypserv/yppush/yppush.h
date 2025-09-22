/*	$OpenBSD: yppush.h,v 1.10 2024/05/21 05:00:48 jsg Exp $ */

/*
 * Copyright (c) 1996 Mats O Jansson <moj@stacken.kth.se>
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

#ifndef _YPPUSH_H_RPCGEN
#define _YPPUSH_H_RPCGEN

#include <rpc/rpc.h>

#define YPPUSH_XFRRESPPROG	((u_long)0x40000000)
#define YPPUSH_XFRRESPVERS	((u_long)1)
#define YPPUSHPROC_NULL		((u_long)0)
#define YPPUSHPROC_XFRRESP	((u_long)1)

__BEGIN_DECLS
bool_t xdr_yppush_status(XDR *, yppush_status *);
bool_t xdr_yppushresp_xfr(XDR *, yppushresp_xfr *);
void * yppushproc_null_1_svc(void *, struct svc_req *);
char * yppush_err_string(enum yppush_status y);
void yppush_xfrrespprog_1(struct svc_req *, SVCXPRT *);
__END_DECLS

#endif /* !_YPPUSH_H_RPCGEN */
