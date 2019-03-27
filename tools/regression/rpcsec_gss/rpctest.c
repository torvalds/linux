/*-
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
 *
 * $FreeBSD$
 */

#ifdef __FreeBSD__
#include <sys/cdefs.h>
#else
#define __unused
#endif

#include <ctype.h>
#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rpc/rpc.h>
#include <rpc/rpcsec_gss.h>

static rpc_gss_principal_t server_acl = NULL;

static void
usage(void)
{
	printf("rpctest client | server\n");
	exit(1);
}

static void
print_principal(rpc_gss_principal_t principal)
{
	int i, len, n;
	uint8_t *p;

	len = principal->len;
	p = (uint8_t *) principal->name;
	while (len > 0) {
		n = len;
		if (n > 16)
			n = 16;
		for (i = 0; i < n; i++)
			printf("%02x ", p[i]);
		for (; i < 16; i++)
			printf("   ");
		printf("|");
		for (i = 0; i < n; i++)
			printf("%c", isprint(p[i]) ? p[i] : '.');
		printf("|\n");
		len -= n;
		p += n;
	}
}

static void
test_client(int argc, const char **argv)
{
	rpcproc_t prog = 123456;
	rpcvers_t vers = 1;
	const char *netid = "tcp";
	char hostname[128], service[128+5];
	CLIENT *client;
	AUTH *auth;
	const char **mechs;
	rpc_gss_options_req_t options_req;
	rpc_gss_options_ret_t options_ret;
	rpc_gss_service_t svc;
	struct timeval tv;
	enum clnt_stat stat;

	if (argc == 2)
		strlcpy(hostname, argv[1], sizeof(hostname));
	else
		gethostname(hostname, sizeof(hostname));

	client = clnt_create(hostname, prog, vers, netid);
	if (!client) {
		printf("rpc_createerr.cf_stat = %d\n",
		    rpc_createerr.cf_stat);
		printf("rpc_createerr.cf_error.re_errno = %d\n",
		    rpc_createerr.cf_error.re_errno);
		return;
	}
	
	strcpy(service, "host");
	strcat(service, "@");
	strcat(service, hostname);

	mechs = rpc_gss_get_mechanisms();
	auth = NULL;
	while (*mechs) {
		options_req.req_flags = GSS_C_MUTUAL_FLAG;
		options_req.time_req = 600;
		options_req.my_cred = GSS_C_NO_CREDENTIAL;
		options_req.input_channel_bindings = NULL;
		auth = rpc_gss_seccreate(client, service,
		    *mechs,
		    rpc_gss_svc_none,
		    NULL,
		    &options_req,
		    &options_ret);
		if (auth)
			break;
		mechs++;
	}
	if (!auth) {
		clnt_pcreateerror("rpc_gss_seccreate");
		printf("Can't authenticate with server %s.\n",
		    hostname);
		exit(1);
	}
	client->cl_auth = auth;

	for (svc = rpc_gss_svc_none; svc <= rpc_gss_svc_privacy; svc++) {
		const char *svc_names[] = {
			"rpc_gss_svc_default",
			"rpc_gss_svc_none",
			"rpc_gss_svc_integrity",
			"rpc_gss_svc_privacy"
		};
		int num;

		rpc_gss_set_defaults(auth, svc, NULL);
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		num = 42;
		stat = CLNT_CALL(client, 1,
		    (xdrproc_t) xdr_int, (char *) &num,
		    (xdrproc_t) xdr_int, (char *) &num, tv);
		if (stat == RPC_SUCCESS) {
			printf("succeeded with %s\n", svc_names[svc]);
			if (num != 142)
				printf("unexpected reply %d\n", num);
		} else {
			clnt_perror(client, "call failed");
		}
	}
	AUTH_DESTROY(auth);
	CLNT_DESTROY(client);
}

static void
server_program_1(struct svc_req *rqstp, register SVCXPRT *transp)
{
	rpc_gss_rawcred_t *rcred;
	rpc_gss_ucred_t *ucred;
	int		i, num;

	if (rqstp->rq_cred.oa_flavor != RPCSEC_GSS) {
		svcerr_weakauth(transp);
		return;
	}		
		
	if (!rpc_gss_getcred(rqstp, &rcred, &ucred, NULL)) {
		svcerr_systemerr(transp);
		return;
	}

	printf("svc=%d, mech=%s, uid=%d, gid=%d, gids={",
	    rcred->service, rcred->mechanism, ucred->uid, ucred->gid);
	for (i = 0; i < ucred->gidlen; i++) {
		if (i > 0) printf(",");
		printf("%d", ucred->gidlist[i]);
	}
	printf("}\n");

	switch (rqstp->rq_proc) {
	case 0:
		if (!svc_getargs(transp, (xdrproc_t) xdr_void, 0)) {
			svcerr_decode(transp);
			goto out;
		}
		if (!svc_sendreply(transp, (xdrproc_t) xdr_void, 0)) {
			svcerr_systemerr(transp);
		}
		goto out;

	case 1:
		if (!svc_getargs(transp, (xdrproc_t) xdr_int,
			(char *) &num)) {
			svcerr_decode(transp);
			goto out;
		}
		num += 100;
		if (!svc_sendreply(transp, (xdrproc_t) xdr_int,
			(char *) &num)) {
			svcerr_systemerr(transp);
		}
		goto out;

	default:
		svcerr_noproc(transp);
		goto out;
	}

out:
	return;
}

#if 0
static void
report_error(gss_OID mech, OM_uint32 maj, OM_uint32 min)
{
	OM_uint32 maj_stat, min_stat;
	OM_uint32 message_context;
	gss_buffer_desc buf;

	printf("major_stat=%d, minor_stat=%d\n", maj, min);

	message_context = 0;
	do {
		maj_stat = gss_display_status(&min_stat, maj,
		    GSS_C_GSS_CODE, GSS_C_NO_OID, &message_context, &buf);
		printf("%.*s\n", (int)buf.length, (char *) buf.value);
		gss_release_buffer(&min_stat, &buf);
	} while (message_context);
	if (mech) {
		message_context = 0;
		do {
			maj_stat = gss_display_status(&min_stat, min,
			    GSS_C_MECH_CODE, mech, &message_context, &buf);
			printf("%.*s\n", (int)buf.length, (char *) buf.value);
			gss_release_buffer(&min_stat, &buf);
		} while (message_context);
	}
	exit(1);
}
#endif

static bool_t
server_new_context(__unused struct svc_req *req,
    __unused gss_cred_id_t deleg,
    __unused gss_ctx_id_t gss_context,
    rpc_gss_lock_t *lock,
    __unused void **cookie)
{
	rpc_gss_rawcred_t *rcred = lock->raw_cred;

	printf("new security context version=%d, mech=%s, qop=%s:\n",
	    rcred->version, rcred->mechanism, rcred->qop);
	print_principal(rcred->client_principal);

	if (!server_acl)
		return (TRUE);

	if (rcred->client_principal->len != server_acl->len
	    || memcmp(rcred->client_principal->name, server_acl->name,
		server_acl->len)) {
		return (FALSE);
	}

	return (TRUE);
}

static void
test_server(__unused int argc, __unused const char **argv)
{
	char hostname[128];
	char principal[128 + 5];
	const char **mechs;
	static rpc_gss_callback_t cb;

	if (argc == 3) {
		if (!rpc_gss_get_principal_name(&server_acl, argv[1],
			argv[2], NULL, NULL)) {
			printf("Can't create %s ACL entry for %s\n",
			    argv[1], argv[2]);
			return;
		}
	}

	gethostname(hostname, sizeof(hostname));;
	snprintf(principal, sizeof(principal), "host@%s", hostname);

	mechs = rpc_gss_get_mechanisms();
	while (*mechs) {
		if (!rpc_gss_set_svc_name(principal, *mechs, GSS_C_INDEFINITE,
			123456, 1)) {
			rpc_gss_error_t e;

			rpc_gss_get_error(&e);
			printf("setting name for %s for %s failed: %d, %d\n",
			    principal, *mechs,
			     e.rpc_gss_error, e.system_error);

#if 0
			gss_OID mech_oid;
			gss_OID_set_desc oid_set;
			gss_name_t name;
			OM_uint32 maj_stat, min_stat;
			gss_buffer_desc namebuf;
			gss_cred_id_t cred;

			rpc_gss_mech_to_oid(*mechs, &mech_oid);
			oid_set.count = 1;
			oid_set.elements = mech_oid;

			namebuf.value = principal;
			namebuf.length = strlen(principal);
			maj_stat = gss_import_name(&min_stat, &namebuf,
			    GSS_C_NT_HOSTBASED_SERVICE, &name);
			if (maj_stat) {
				printf("gss_import_name failed\n");
				report_error(mech_oid, maj_stat, min_stat);
			}
			maj_stat = gss_acquire_cred(&min_stat, name,
			    0, &oid_set, GSS_C_ACCEPT, &cred, NULL, NULL);
			if (maj_stat) {
				printf("gss_acquire_cred failed\n");
				report_error(mech_oid, maj_stat, min_stat);
			}
#endif
		}
		mechs++;
	}

	cb.program = 123456;
	cb.version = 1;
	cb.callback = server_new_context;
	rpc_gss_set_callback(&cb);

	svc_create(server_program_1, 123456, 1, 0);
	svc_run();
}

static void
test_get_principal_name(int argc, const char **argv)
{
	const char *mechname, *name, *node, *domain;
	rpc_gss_principal_t principal;

	if (argc < 3 || argc > 5) {
		printf("usage: rpctest principal <mechname> <name> "
		    "[<node> [<domain>] ]\n");
		exit(1);
	}

	mechname = argv[1];
	name = argv[2];
	node = NULL;
	domain = NULL;
	if (argc > 3) {
		node = argv[3];
		if (argc > 4)
			domain = argv[4];
	}

	if (rpc_gss_get_principal_name(&principal, mechname, name,
		node, domain)) {
		printf("succeeded:\n");
		print_principal(principal);
		free(principal);
	} else {
		printf("failed\n");
	}
}

int
main(int argc, const char **argv)
{

	if (argc < 2)
		usage();
	if (!strcmp(argv[1], "client"))
		test_client(argc - 1, argv + 1);
	else if (!strcmp(argv[1], "server"))
		test_server(argc - 1, argv + 1);
	else if (!strcmp(argv[1], "principal"))
		test_get_principal_name(argc - 1, argv + 1);
	else
		usage();

	return (0);
}
