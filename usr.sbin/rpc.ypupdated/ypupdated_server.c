/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995, 1996
 *      Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * ypupdate server implementation
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Center for Telecommunications Research
 * Columbia University, New York City
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <rpc/rpc.h>
#include <rpc/key_prot.h>
#include <sys/param.h>
#include <rpcsvc/yp.h>
#include "ypupdate_prot.h"
#include "ypupdated_extern.h"
#include "yp_extern.h"
#include "ypxfr_extern.h"

int children = 0;
int forked = 0;

/*
 * Try to avoid spoofing: if a client chooses to use a very large
 * window and then tries a bunch of randomly chosen encrypted timestamps,
 * there's a chance he might stumble onto a valid combination.
 * We therefore reject any RPCs with a window size larger than a preset
 * value.
 */
#ifndef WINDOW
#define WINDOW (60*60)
#endif

static enum auth_stat
yp_checkauth(struct svc_req *svcreq)
{
	struct authdes_cred *des_cred;

	switch (svcreq->rq_cred.oa_flavor) {
	case AUTH_DES:
		des_cred = (struct authdes_cred *) svcreq->rq_clntcred;
		if (des_cred->adc_fullname.window > WINDOW) {
			yp_error("warning: client-specified window size \
was too large -- possible spoof attempt");
			return(AUTH_BADCRED);
		}
		return(AUTH_OK);
		break;
	case AUTH_UNIX:
	case AUTH_NONE:
		yp_error("warning: client didn't use DES authentication");
		return(AUTH_TOOWEAK);
		break;
	default:
		yp_error("client used unknown auth flavor");
		return(AUTH_REJECTEDCRED);
		break;
	}
}

unsigned int *
ypu_change_1_svc(struct ypupdate_args *args, struct svc_req *svcreq)
{
	struct authdes_cred *des_cred;
	static int res;
	char *netname;
	enum auth_stat astat;

	res = 0;

	astat = yp_checkauth(svcreq);

	if (astat != AUTH_OK) {
		svcerr_auth(svcreq->rq_xprt, astat);
		return(&res);
	}

	des_cred = (struct authdes_cred *) svcreq->rq_clntcred;
	netname = des_cred->adc_fullname.name;

	res = localupdate(netname, "/etc/publickey", YPOP_CHANGE,
		args->key.yp_buf_len, args->key.yp_buf_val,
		args->datum.yp_buf_len, args->datum.yp_buf_val);

	if (res)
		return (&res);

	res = ypmap_update(netname, args->mapname, YPOP_CHANGE,
		args->key.yp_buf_len, args->key.yp_buf_val,
		args->datum.yp_buf_len, args->datum.yp_buf_val);

	return (&res);
}

unsigned int *
ypu_insert_1_svc(struct ypupdate_args *args, struct svc_req *svcreq)
{
	struct authdes_cred *des_cred;
	static int res;
	char *netname;
	enum auth_stat astat;

	res = 0;

	astat = yp_checkauth(svcreq);

	if (astat != AUTH_OK) {
		svcerr_auth(svcreq->rq_xprt, astat);
		return(&res);
	}

	des_cred = (struct authdes_cred *) svcreq->rq_clntcred;
	netname = des_cred->adc_fullname.name;

	res = localupdate(netname, "/etc/publickey", YPOP_INSERT,
		args->key.yp_buf_len, args->key.yp_buf_val,
		args->datum.yp_buf_len, args->datum.yp_buf_val);

	if (res)
		return (&res);

	res = ypmap_update(netname, args->mapname, YPOP_INSERT,
		args->key.yp_buf_len, args->key.yp_buf_val,
		args->datum.yp_buf_len, args->datum.yp_buf_val);

	return (&res);
}

unsigned int *
ypu_delete_1_svc(struct ypdelete_args *args, struct svc_req *svcreq)
{
	struct authdes_cred *des_cred;
	static int res;
	char *netname;
	enum auth_stat astat;

	res = 0;

	astat = yp_checkauth(svcreq);

	if (astat != AUTH_OK) {
		svcerr_auth(svcreq->rq_xprt, astat);
		return(&res);
	}

	des_cred = (struct authdes_cred *) svcreq->rq_clntcred;
	netname = des_cred->adc_fullname.name;

	res = localupdate(netname, "/etc/publickey", YPOP_DELETE,
		args->key.yp_buf_len, args->key.yp_buf_val,
		0,			NULL);

	if (res)
		return (&res);

	res = ypmap_update(netname, args->mapname, YPOP_DELETE,
		args->key.yp_buf_len, args->key.yp_buf_val,
		0,			NULL);

	return (&res);
}

unsigned int *
ypu_store_1_svc(struct ypupdate_args *args, struct svc_req *svcreq)
{
	struct authdes_cred *des_cred;
	static int res;
	char *netname;
	enum auth_stat astat;

	res = 0;

	astat = yp_checkauth(svcreq);

	if (astat != AUTH_OK) {
		svcerr_auth(svcreq->rq_xprt, astat);
		return(&res);
	}

	des_cred = (struct authdes_cred *) svcreq->rq_clntcred;
	netname = des_cred->adc_fullname.name;

	res = localupdate(netname, "/etc/publickey", YPOP_STORE,
		args->key.yp_buf_len, args->key.yp_buf_val,
		args->datum.yp_buf_len, args->datum.yp_buf_val);

	if (res)
		return (&res);

	res = ypmap_update(netname, args->mapname, YPOP_STORE,
		args->key.yp_buf_len, args->key.yp_buf_val,
		args->datum.yp_buf_len, args->datum.yp_buf_val);

	return (&res);
}
