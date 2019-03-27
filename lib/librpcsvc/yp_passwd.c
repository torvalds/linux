/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995, 1996
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdlib.h>
#include <rpc/rpc.h>
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#include <rpcsvc/yppasswd.h>
#include <netinet/in.h>

/*
 * XXX <rpcsvc/yppasswd.h> does a typedef that makes 'yppasswd'
 * a type of struct yppasswd. This leads to a namespace collision:
 * gcc will not let you have a type called yppasswd and a function
 * called yppasswd(). In order to get around this, we call the
 * actual function _yppasswd() and put a macro called yppasswd()
 * in yppasswd.h which calls the underlying function, thereby
 * fooling gcc.
 */

int
_yppasswd(char *oldpass, struct x_passwd *newpw)
{
	char *server;
	char *domain;
	int rval, result;
	struct yppasswd yppasswd;

	yppasswd.newpw = *newpw;
	yppasswd.oldpass = oldpass;

	if (yp_get_default_domain(&domain))
		return (-1);

	if (yp_master(domain, "passwd.byname", &server))
		return(-1);

	rval = getrpcport(server, YPPASSWDPROG,
				YPPASSWDPROC_UPDATE, IPPROTO_UDP);

	if (rval == 0 || rval >= IPPORT_RESERVED) {
		free(server);
		return(-1);
	}

	rval = callrpc(server, YPPASSWDPROG, YPPASSWDVERS, YPPASSWDPROC_UPDATE,
		       (xdrproc_t)xdr_yppasswd, (char *)&yppasswd,
		       (xdrproc_t)xdr_int, (char *)&result);

	free(server);
	if (rval || result)
		return(-1);
	else
		return(0);
}
