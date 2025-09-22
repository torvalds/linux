/*	$OpenBSD: yppush_proc.c,v 1.9 2009/10/27 23:59:58 deraadt Exp $ */

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

#include <sys/types.h>
#include <rpcsvc/yp.h>
#include <stdio.h>
#include "yppush.h"

extern int Verbose;

void *
yppushproc_null_1_svc(void *argp, struct svc_req *rqstp)
{
	static char *result;

	/*
	 * insert server code here
	 */
	return((void *) &result);
}

yppushresp_xfr *
yppushproc_xfrresp_1_svc(void *v, struct svc_req *rqstp)
{
	yppushresp_xfr *argp = (yppushresp_xfr *)v;
	static char *result;

	/*
	 * insert server code here
	 */
	if ((argp->status < YPPUSH_SUCC) || Verbose)
		fprintf(stderr, "yppush: %s\n",
		    yppush_err_string(argp->status));
	return((yppushresp_xfr *) &result);
}
