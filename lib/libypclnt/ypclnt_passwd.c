/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by ThinkSec AS and
 * NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <netconfig.h>
#include <netdb.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rpcsvc/ypclnt.h>
#include <rpcsvc/yppasswd.h>

#include "ypclnt.h"
#include "yppasswd_private.h"

static int yppasswd_remote(ypclnt_t *, const struct passwd *, const char *);
static int yppasswd_local(ypclnt_t *, const struct passwd *);

/*
 * Determines the availability of rpc.yppasswdd.  Returns -1 for not
 * available (or unable to determine), 0 for available, 1 for available in
 * master mode.
 */
int
ypclnt_havepasswdd(ypclnt_t *ypclnt)
{
	struct netconfig *nc = NULL;
	void *localhandle = NULL;
	CLIENT *clnt = NULL;
	int ret;

	/* check if rpc.yppasswdd is running */
	if (getrpcport(ypclnt->server, YPPASSWDPROG,
		YPPASSWDPROC_UPDATE, IPPROTO_UDP) == 0) {
		ypclnt_error(ypclnt, __func__, "no rpc.yppasswdd on server");
		return (-1);
	}

	/* if we're not root, use remote method */
	if (getuid() != 0)
		return (0);

	/* try to connect to rpc.yppasswdd */
	localhandle = setnetconfig();
	while ((nc = getnetconfig(localhandle)) != NULL) {
		if (nc->nc_protofmly != NULL &&
			strcmp(nc->nc_protofmly, NC_LOOPBACK) == 0)
				break;
	}
	if (nc == NULL) {
		ypclnt_error(ypclnt, __func__,
		    "getnetconfig: %s", nc_sperror());
		ret = 0;
		goto done;
	}
	if ((clnt = clnt_tp_create(NULL, MASTER_YPPASSWDPROG,
	    MASTER_YPPASSWDVERS, nc)) == NULL) {
		ypclnt_error(ypclnt, __func__,
		    "failed to connect to rpc.yppasswdd: %s",
		    clnt_spcreateerror(ypclnt->server));
		ret = 0;
		goto done;
	} else 
		ret = 1;

done:
	if (clnt != NULL) {
		clnt_destroy(clnt);
	}
	endnetconfig(localhandle);
	return (ret);
}

/*
 * Updates the NIS user information for the specified user.
 */
int
ypclnt_passwd(ypclnt_t *ypclnt, const struct passwd *pwd, const char *passwd)
{
	switch (ypclnt_havepasswdd(ypclnt)) {
	case 0:
		return (yppasswd_remote(ypclnt, pwd, passwd));
	case 1:
		return (yppasswd_local(ypclnt, pwd));
	default:
		return (-1);
	}
}

/*
 * yppasswd_remote and yppasswd_local are quite similar but still
 * sufficiently different that merging them into one makes the code
 * significantly less readable, IMHO, so we keep them separate.
 */

static int
yppasswd_local(ypclnt_t *ypclnt, const struct passwd *pwd)
{
	struct master_yppasswd yppwd;
	struct rpc_err rpcerr;
	struct netconfig *nc = NULL;
	void *localhandle = NULL;
	CLIENT *clnt = NULL;
	int ret, *result;

	/* fill the master_yppasswd structure */
	memset(&yppwd, 0, sizeof yppwd);
	yppwd.newpw.pw_uid = pwd->pw_uid;
	yppwd.newpw.pw_gid = pwd->pw_gid;
	yppwd.newpw.pw_change = pwd->pw_change;
	yppwd.newpw.pw_expire = pwd->pw_expire;
	yppwd.newpw.pw_fields = pwd->pw_fields;
	yppwd.oldpass = strdup("");
	yppwd.domain = strdup(ypclnt->domain);
	if ((yppwd.newpw.pw_name = strdup(pwd->pw_name)) == NULL ||
	    (yppwd.newpw.pw_passwd = strdup(pwd->pw_passwd)) == NULL ||
	    (yppwd.newpw.pw_class = strdup(pwd->pw_class)) == NULL ||
	    (yppwd.newpw.pw_gecos = strdup(pwd->pw_gecos)) == NULL ||
	    (yppwd.newpw.pw_dir = strdup(pwd->pw_dir)) == NULL ||
	    (yppwd.newpw.pw_shell = strdup(pwd->pw_shell)) == NULL) {
		ypclnt_error(ypclnt, __func__, strerror(errno));
		ret = -1;
		goto done;
	}

	/* connect to rpc.yppasswdd */
	localhandle = setnetconfig();
	while ((nc = getnetconfig(localhandle)) != NULL) {
		if (nc->nc_protofmly != NULL &&
		    strcmp(nc->nc_protofmly, NC_LOOPBACK) == 0)
			break;
	}
	if (nc == NULL) {
		ypclnt_error(ypclnt, __func__,
		    "getnetconfig: %s", nc_sperror());
		ret = -1;
		goto done;
	}
	if ((clnt = clnt_tp_create(NULL, MASTER_YPPASSWDPROG,
	    MASTER_YPPASSWDVERS, nc)) == NULL) {
		ypclnt_error(ypclnt, __func__,
		    "failed to connect to rpc.yppasswdd: %s",
		    clnt_spcreateerror(ypclnt->server));
		ret = -1;
		goto done;
	}
	clnt->cl_auth = authunix_create_default();

	/* request the update */
	result = yppasswdproc_update_master_1(&yppwd, clnt);

	/* check for RPC errors */
	clnt_geterr(clnt, &rpcerr);
	if (rpcerr.re_status != RPC_SUCCESS) {
		ypclnt_error(ypclnt, __func__,
		    "NIS password update failed: %s",
		    clnt_sperror(clnt, ypclnt->server));
		ret = -1;
		goto done;
	}

	/* check the result of the update */
	if (result == NULL || *result != 0) {
		ypclnt_error(ypclnt, __func__,
		    "NIS password update failed");
		/* XXX how do we get more details? */
		ret = -1;
		goto done;
	}

	ypclnt_error(ypclnt, NULL, NULL);
	ret = 0;

 done:
	if (clnt != NULL) {
		auth_destroy(clnt->cl_auth);
		clnt_destroy(clnt);
	}
	endnetconfig(localhandle);
	free(yppwd.newpw.pw_name);
	if (yppwd.newpw.pw_passwd != NULL) {
		memset(yppwd.newpw.pw_passwd, 0, strlen(yppwd.newpw.pw_passwd));
		free(yppwd.newpw.pw_passwd);
	}
	free(yppwd.newpw.pw_class);
	free(yppwd.newpw.pw_gecos);
	free(yppwd.newpw.pw_dir);
	free(yppwd.newpw.pw_shell);
	if (yppwd.oldpass != NULL) {
		memset(yppwd.oldpass, 0, strlen(yppwd.oldpass));
		free(yppwd.oldpass);
	}
	return (ret);
}

static int
yppasswd_remote(ypclnt_t *ypclnt, const struct passwd *pwd, const char *passwd)
{
	struct yppasswd yppwd;
	struct rpc_err rpcerr;
	CLIENT *clnt = NULL;
	int ret, *result;

	/* fill the yppasswd structure */
	memset(&yppwd, 0, sizeof yppwd);
	yppwd.newpw.pw_uid = pwd->pw_uid;
	yppwd.newpw.pw_gid = pwd->pw_gid;
	if ((yppwd.newpw.pw_name = strdup(pwd->pw_name)) == NULL ||
	    (yppwd.newpw.pw_passwd = strdup(pwd->pw_passwd)) == NULL ||
	    (yppwd.newpw.pw_gecos = strdup(pwd->pw_gecos)) == NULL ||
	    (yppwd.newpw.pw_dir = strdup(pwd->pw_dir)) == NULL ||
	    (yppwd.newpw.pw_shell = strdup(pwd->pw_shell)) == NULL ||
	    (yppwd.oldpass = strdup(passwd ? passwd : "")) == NULL) {
		ypclnt_error(ypclnt, __func__, strerror(errno));
		ret = -1;
		goto done;
	}

	/* connect to rpc.yppasswdd */
	clnt = clnt_create(ypclnt->server, YPPASSWDPROG, YPPASSWDVERS, "udp");
	if (clnt == NULL) {
		ypclnt_error(ypclnt, __func__,
		    "failed to connect to rpc.yppasswdd: %s",
		    clnt_spcreateerror(ypclnt->server));
		ret = -1;
		goto done;
	}
	clnt->cl_auth = authunix_create_default();

	/* request the update */
	result = yppasswdproc_update_1(&yppwd, clnt);

	/* check for RPC errors */
	clnt_geterr(clnt, &rpcerr);
	if (rpcerr.re_status != RPC_SUCCESS) {
		ypclnt_error(ypclnt, __func__,
		    "NIS password update failed: %s",
		    clnt_sperror(clnt, ypclnt->server));
		ret = -1;
		goto done;
	}

	/* check the result of the update */
	if (result == NULL || *result != 0) {
		ypclnt_error(ypclnt, __func__,
		    "NIS password update failed");
		/* XXX how do we get more details? */
		ret = -1;
		goto done;
	}

	ypclnt_error(ypclnt, NULL, NULL);
	ret = 0;

 done:
	if (clnt != NULL) {
		auth_destroy(clnt->cl_auth);
		clnt_destroy(clnt);
	}
	free(yppwd.newpw.pw_name);
	if (yppwd.newpw.pw_passwd != NULL) {
		memset(yppwd.newpw.pw_passwd, 0, strlen(yppwd.newpw.pw_passwd));
		free(yppwd.newpw.pw_passwd);
	}
	free(yppwd.newpw.pw_gecos);
	free(yppwd.newpw.pw_dir);
	free(yppwd.newpw.pw_shell);
	if (yppwd.oldpass != NULL) {
		memset(yppwd.oldpass, 0, strlen(yppwd.oldpass));
		free(yppwd.oldpass);
	}
	return (ret);
}
