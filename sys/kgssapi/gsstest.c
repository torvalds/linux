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

#include <sys/ctype.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/socketvar.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>

#include <kgssapi/gssapi.h>
#include <kgssapi/gssapi_impl.h>
#include <rpc/rpc.h>
#include <rpc/rpc_com.h>
#include <rpc/rpcb_prot.h>
#include <rpc/rpcsec_gss.h>

static void
report_error(gss_OID mech, OM_uint32 maj, OM_uint32 min)
{
	OM_uint32 maj_stat, min_stat;
	OM_uint32 message_context;
	gss_buffer_desc buf;

	uprintf("major_stat=%d, minor_stat=%d\n", maj, min);
	message_context = 0;
	do {
		maj_stat = gss_display_status(&min_stat, maj,
		    GSS_C_GSS_CODE, GSS_C_NO_OID, &message_context, &buf);
		if (GSS_ERROR(maj_stat))
			break;
		uprintf("%.*s\n", (int)buf.length, (char *) buf.value);
		gss_release_buffer(&min_stat, &buf);
	} while (message_context);
	if (mech && min) {
		message_context = 0;
		do {
			maj_stat = gss_display_status(&min_stat, min,
			    GSS_C_MECH_CODE, mech, &message_context, &buf);
			if (GSS_ERROR(maj_stat))
				break;
			uprintf("%.*s\n", (int)buf.length, (char *) buf.value);
			gss_release_buffer(&min_stat, &buf);
		} while (message_context);
	}
}

#if 0
static void
send_token_to_peer(const gss_buffer_t token)
{
	const uint8_t *p;
	size_t i;

	printf("send token:\n");
	printf("%d ", (int) token->length);
	p = (const uint8_t *) token->value;
	for (i = 0; i < token->length; i++)
		printf("%02x", *p++);
	printf("\n");
}

static void
receive_token_from_peer(gss_buffer_t token)
{
	char line[8192];
	char *p;
	uint8_t *q;
	int len, val;

	printf("receive token:\n");
	fgets(line, sizeof(line), stdin);
	if (line[strlen(line) - 1] != '\n') {
		printf("token truncated\n");
		exit(1);
	}
	p = line;
	if (sscanf(line, "%d ", &len) != 1) {
		printf("bad token\n");
		exit(1);
	}
	p = strchr(p, ' ') + 1;
	token->length = len;
	token->value = malloc(len);
	q = (uint8_t *) token->value;
	while (len) {
		if (sscanf(p, "%02x", &val) != 1) {
			printf("bad token\n");
			exit(1);
		}
		*q++ = val;
		p += 2;
		len--;
	}
}
#endif

#if 0
void
server(int argc, char** argv)
{
	OM_uint32 maj_stat, min_stat;
	gss_buffer_desc input_token, output_token;
	gss_ctx_id_t context_hdl = GSS_C_NO_CONTEXT;
	gss_name_t client_name;
	gss_OID mech_type;

	if (argc != 1)
		usage();

	do {
		receive_token_from_peer(&input_token);
		maj_stat = gss_accept_sec_context(&min_stat,
		    &context_hdl,
		    GSS_C_NO_CREDENTIAL,
		    &input_token,
		    GSS_C_NO_CHANNEL_BINDINGS,
		    &client_name,
		    &mech_type,
		    &output_token,
		    NULL,
		    NULL,
		    NULL);
		if (GSS_ERROR(maj_stat)) {
			report_error(mech_type, maj_stat, min_stat);
		}
		if (output_token.length != 0) {
			send_token_to_peer(&output_token);
			gss_release_buffer(&min_stat, &output_token);
		}
		if (GSS_ERROR(maj_stat)) {
			if (context_hdl != GSS_C_NO_CONTEXT)
				gss_delete_sec_context(&min_stat,
				    &context_hdl,
				    GSS_C_NO_BUFFER);
			break;
		}
	} while (maj_stat & GSS_S_CONTINUE_NEEDED);

	if (client_name) {
		gss_buffer_desc name_desc;
		char buf[512];

		gss_display_name(&min_stat, client_name, &name_desc, NULL);
		memcpy(buf, name_desc.value, name_desc.length);
		buf[name_desc.length] = 0;
		gss_release_buffer(&min_stat, &name_desc);
		printf("client name is %s\n", buf);
	}

	receive_token_from_peer(&input_token);
	gss_unwrap(&min_stat, context_hdl, &input_token, &output_token,
	    NULL, NULL);
	printf("%.*s\n", (int)output_token.length, (char *) output_token.value);
	gss_release_buffer(&min_stat, &output_token);
}
#endif

/* 1.2.752.43.13.14 */
static gss_OID_desc gss_krb5_set_allowable_enctypes_x_desc =
{6, (void *) "\x2a\x85\x70\x2b\x0d\x0e"};

gss_OID GSS_KRB5_SET_ALLOWABLE_ENCTYPES_X = &gss_krb5_set_allowable_enctypes_x_desc;
#define ETYPE_DES_CBC_CRC	1

/*
 * Create an initiator context and acceptor context in the kernel and
 * use them to exchange signed and sealed messages.
 */
static int
gsstest_1(struct thread *td)
{
	OM_uint32 maj_stat, min_stat;
	OM_uint32 smaj_stat, smin_stat;
	int context_established = 0;
	gss_ctx_id_t client_context = GSS_C_NO_CONTEXT;
	gss_ctx_id_t server_context = GSS_C_NO_CONTEXT;
	gss_cred_id_t client_cred = GSS_C_NO_CREDENTIAL;
	gss_cred_id_t server_cred = GSS_C_NO_CREDENTIAL;
	gss_name_t name = GSS_C_NO_NAME;
	gss_name_t received_name = GSS_C_NO_NAME;
	gss_buffer_desc name_desc;
	gss_buffer_desc client_token, server_token, message_buf;
	gss_OID mech, actual_mech, mech_type;
	static gss_OID_desc krb5_desc =
		{9, (void *)"\x2a\x86\x48\x86\xf7\x12\x01\x02\x02"};
#if 0
	static gss_OID_desc spnego_desc =
		{6, (void *)"\x2b\x06\x01\x05\x05\x02"};
	static gss_OID_desc ntlm_desc =
		{10, (void *)"\x2b\x06\x01\x04\x01\x82\x37\x02\x02\x0a"};
#endif
	char enctype[sizeof(uint32_t)];

	mech = GSS_C_NO_OID;

	{
		static char sbuf[512];
		memcpy(sbuf, "nfs@", 4);
		getcredhostname(td->td_ucred, sbuf + 4, sizeof(sbuf) - 4);
		name_desc.value = sbuf;
	}

	name_desc.length = strlen((const char *) name_desc.value);
	maj_stat = gss_import_name(&min_stat, &name_desc,
	    GSS_C_NT_HOSTBASED_SERVICE, &name);
	if (GSS_ERROR(maj_stat)) {
		printf("gss_import_name failed\n");
		report_error(mech, maj_stat, min_stat);
		goto out;
	}

	maj_stat = gss_acquire_cred(&min_stat, GSS_C_NO_NAME,
	    0, GSS_C_NO_OID_SET, GSS_C_INITIATE, &client_cred,
	    NULL, NULL);
	if (GSS_ERROR(maj_stat)) {
		printf("gss_acquire_cred (client) failed\n");
		report_error(mech, maj_stat, min_stat);
		goto out;
	}

	enctype[0] = (ETYPE_DES_CBC_CRC >> 24) & 0xff;
	enctype[1] = (ETYPE_DES_CBC_CRC >> 16) & 0xff;
	enctype[2] = (ETYPE_DES_CBC_CRC >> 8) & 0xff;
	enctype[3] = ETYPE_DES_CBC_CRC & 0xff;
	message_buf.length = sizeof(enctype);
	message_buf.value = enctype;
	maj_stat = gss_set_cred_option(&min_stat, &client_cred,
	    GSS_KRB5_SET_ALLOWABLE_ENCTYPES_X, &message_buf);
	if (GSS_ERROR(maj_stat)) {
		printf("gss_set_cred_option failed\n");
		report_error(mech, maj_stat, min_stat);
		goto out;
	}

	server_token.length = 0;
	server_token.value = NULL;
	while (!context_established) {
		client_token.length = 0;
		client_token.value = NULL;
		maj_stat = gss_init_sec_context(&min_stat,
		    client_cred,
		    &client_context,
		    name,
		    mech,
		    GSS_C_MUTUAL_FLAG|GSS_C_CONF_FLAG|GSS_C_INTEG_FLAG,
		    0,
		    GSS_C_NO_CHANNEL_BINDINGS,
		    &server_token,
		    &actual_mech,
		    &client_token,
		    NULL,
		    NULL);
		if (server_token.length)
			gss_release_buffer(&smin_stat, &server_token);
		if (GSS_ERROR(maj_stat)) {
			printf("gss_init_sec_context failed\n");
			report_error(mech, maj_stat, min_stat);
			goto out;
		}

		if (client_token.length != 0) {
			if (!server_cred) {
				gss_OID_set_desc oid_set;
				oid_set.count = 1;
				oid_set.elements = &krb5_desc;
				smaj_stat = gss_acquire_cred(&smin_stat,
				    name, 0, &oid_set, GSS_C_ACCEPT, &server_cred,
				    NULL, NULL);
				if (GSS_ERROR(smaj_stat)) {
					printf("gss_acquire_cred (server) failed\n");
					report_error(mech_type, smaj_stat, smin_stat);
					goto out;
				}
			}
			smaj_stat = gss_accept_sec_context(&smin_stat,
			    &server_context,
			    server_cred,
			    &client_token,
			    GSS_C_NO_CHANNEL_BINDINGS,
			    &received_name,
			    &mech_type,
			    &server_token,
			    NULL,
			    NULL,
			    NULL);
			if (GSS_ERROR(smaj_stat)) {
				printf("gss_accept_sec_context failed\n");
				report_error(mech_type, smaj_stat, smin_stat);
				goto out;
			}
			gss_release_buffer(&min_stat, &client_token);
		}
		if (GSS_ERROR(maj_stat)) {
			if (client_context != GSS_C_NO_CONTEXT)
				gss_delete_sec_context(&min_stat,
				    &client_context,
				    GSS_C_NO_BUFFER);
			break;
		}

		if (maj_stat == GSS_S_COMPLETE) {
			context_established = 1;
		}
	}

	message_buf.length = strlen("Hello world");
	message_buf.value = (void *) "Hello world";

	maj_stat = gss_get_mic(&min_stat, client_context,
	    GSS_C_QOP_DEFAULT, &message_buf, &client_token);
	if (GSS_ERROR(maj_stat)) {
		printf("gss_get_mic failed\n");
		report_error(mech_type, maj_stat, min_stat);
		goto out;
	}
	maj_stat = gss_verify_mic(&min_stat, server_context,
	    &message_buf, &client_token, NULL);
	if (GSS_ERROR(maj_stat)) {
		printf("gss_verify_mic failed\n");
		report_error(mech_type, maj_stat, min_stat);
		goto out;
	}
	gss_release_buffer(&min_stat, &client_token);

	maj_stat = gss_wrap(&min_stat, client_context,
	    TRUE, GSS_C_QOP_DEFAULT, &message_buf, NULL, &client_token);
	if (GSS_ERROR(maj_stat)) {
		printf("gss_wrap failed\n");
		report_error(mech_type, maj_stat, min_stat);
		goto out;
	}
	maj_stat = gss_unwrap(&min_stat, server_context,
	    &client_token, &server_token, NULL, NULL);
	if (GSS_ERROR(maj_stat)) {
		printf("gss_unwrap failed\n");
		report_error(mech_type, maj_stat, min_stat);
		goto out;
	}

 	if (message_buf.length != server_token.length
	    || memcmp(message_buf.value, server_token.value,
		message_buf.length))
		printf("unwrap result corrupt\n");
	
	gss_release_buffer(&min_stat, &client_token);
	gss_release_buffer(&min_stat, &server_token);

out:
	if (client_context)
		gss_delete_sec_context(&min_stat, &client_context,
		    GSS_C_NO_BUFFER);
	if (server_context)
		gss_delete_sec_context(&min_stat, &server_context,
		    GSS_C_NO_BUFFER);
	if (client_cred)
		gss_release_cred(&min_stat, &client_cred);
	if (server_cred)
		gss_release_cred(&min_stat, &server_cred);
	if (name)
		gss_release_name(&min_stat, &name);
	if (received_name)
		gss_release_name(&min_stat, &received_name);

	return (0);
}

/*
 * Interoperability with userland. This takes several steps:
 *
 * 1. Accept an initiator token from userland, return acceptor
 * token. Repeat this step until both userland and kernel return
 * GSS_S_COMPLETE.
 *
 * 2. Receive a signed message from userland and verify the
 * signature. Return a signed reply to userland for it to verify.
 *
 * 3. Receive a wrapped message from userland and unwrap it. Return a
 * wrapped reply to userland.
 */
static int
gsstest_2(struct thread *td, int step, const gss_buffer_t input_token,
    OM_uint32 *maj_stat_res, OM_uint32 *min_stat_res, gss_buffer_t output_token)
{
	OM_uint32 maj_stat, min_stat;
	static int context_established = 0;
	static gss_ctx_id_t server_context = GSS_C_NO_CONTEXT;
	static gss_cred_id_t server_cred = GSS_C_NO_CREDENTIAL;
	static gss_name_t name = GSS_C_NO_NAME;
	gss_buffer_desc name_desc;
	gss_buffer_desc message_buf;
	gss_OID mech_type = GSS_C_NO_OID;
	char enctype[sizeof(uint32_t)];
	int error = EINVAL;

	maj_stat = GSS_S_FAILURE;
	min_stat = 0;
	switch (step) {

	case 1:
		if (server_context == GSS_C_NO_CONTEXT) {
			static char sbuf[512];
			memcpy(sbuf, "nfs@", 4);
			getcredhostname(td->td_ucred, sbuf + 4,
			    sizeof(sbuf) - 4);
			name_desc.value = sbuf;
			name_desc.length = strlen((const char *)
			    name_desc.value);
			maj_stat = gss_import_name(&min_stat, &name_desc,
			    GSS_C_NT_HOSTBASED_SERVICE, &name);
			if (GSS_ERROR(maj_stat)) {
				printf("gss_import_name failed\n");
				report_error(mech_type, maj_stat, min_stat);
				goto out;
			}

			maj_stat = gss_acquire_cred(&min_stat,
			    name, 0, GSS_C_NO_OID_SET, GSS_C_ACCEPT,
			    &server_cred, NULL, NULL);
			if (GSS_ERROR(maj_stat)) {
				printf("gss_acquire_cred (server) failed\n");
				report_error(mech_type, maj_stat, min_stat);
				goto out;
			}

			enctype[0] = (ETYPE_DES_CBC_CRC >> 24) & 0xff;
			enctype[1] = (ETYPE_DES_CBC_CRC >> 16) & 0xff;
			enctype[2] = (ETYPE_DES_CBC_CRC >> 8) & 0xff;
			enctype[3] = ETYPE_DES_CBC_CRC & 0xff;
			message_buf.length = sizeof(enctype);
			message_buf.value = enctype;
			maj_stat = gss_set_cred_option(&min_stat, &server_cred,
			    GSS_KRB5_SET_ALLOWABLE_ENCTYPES_X, &message_buf);
			if (GSS_ERROR(maj_stat)) {
				printf("gss_set_cred_option failed\n");
				report_error(mech_type, maj_stat, min_stat);
				goto out;
			}
		}

		maj_stat = gss_accept_sec_context(&min_stat,
		    &server_context,
		    server_cred,
		    input_token,
		    GSS_C_NO_CHANNEL_BINDINGS,
		    NULL,
		    &mech_type,
		    output_token,
		    NULL,
		    NULL,
		    NULL);
		if (GSS_ERROR(maj_stat)) {
			printf("gss_accept_sec_context failed\n");
			report_error(mech_type, maj_stat, min_stat);
			goto out;
		}

		if (maj_stat == GSS_S_COMPLETE) {
			context_established = 1;
		}
		*maj_stat_res = maj_stat;
		*min_stat_res = min_stat;
		break;

	case 2:
		message_buf.length = strlen("Hello world");
		message_buf.value = (void *) "Hello world";

		maj_stat = gss_verify_mic(&min_stat, server_context,
		    &message_buf, input_token, NULL);
		if (GSS_ERROR(maj_stat)) {
			printf("gss_verify_mic failed\n");
			report_error(mech_type, maj_stat, min_stat);
			goto out;
		}

		maj_stat = gss_get_mic(&min_stat, server_context,
		    GSS_C_QOP_DEFAULT, &message_buf, output_token);
		if (GSS_ERROR(maj_stat)) {
			printf("gss_get_mic failed\n");
			report_error(mech_type, maj_stat, min_stat);
			goto out;
		}
		break;

	case 3:
		maj_stat = gss_unwrap(&min_stat, server_context,
		    input_token, &message_buf, NULL, NULL);
		if (GSS_ERROR(maj_stat)) {
			printf("gss_unwrap failed\n");
			report_error(mech_type, maj_stat, min_stat);
			goto out;
		}
		gss_release_buffer(&min_stat, &message_buf);

		message_buf.length = strlen("Hello world");
		message_buf.value = (void *) "Hello world";
		maj_stat = gss_wrap(&min_stat, server_context,
		    TRUE, GSS_C_QOP_DEFAULT, &message_buf, NULL, output_token);
		if (GSS_ERROR(maj_stat)) {
			printf("gss_wrap failed\n");
			report_error(mech_type, maj_stat, min_stat);
			goto out;
		}
		break;

	case 4:
		maj_stat = gss_unwrap(&min_stat, server_context,
		    input_token, &message_buf, NULL, NULL);
		if (GSS_ERROR(maj_stat)) {
			printf("gss_unwrap failed\n");
			report_error(mech_type, maj_stat, min_stat);
			goto out;
		}
		gss_release_buffer(&min_stat, &message_buf);

		message_buf.length = strlen("Hello world");
		message_buf.value = (void *) "Hello world";
		maj_stat = gss_wrap(&min_stat, server_context,
		    FALSE, GSS_C_QOP_DEFAULT, &message_buf, NULL, output_token);
		if (GSS_ERROR(maj_stat)) {
			printf("gss_wrap failed\n");
			report_error(mech_type, maj_stat, min_stat);
			goto out;
		}
		break;

	case 5:
		error = 0;
		goto out;
	}
	*maj_stat_res = maj_stat;
	*min_stat_res = min_stat;
	return (0);
	
out:
	*maj_stat_res = maj_stat;
	*min_stat_res = min_stat;
	if (server_context)
		gss_delete_sec_context(&min_stat, &server_context,
		    GSS_C_NO_BUFFER);
	if (server_cred)
		gss_release_cred(&min_stat, &server_cred);
	if (name)
		gss_release_name(&min_stat, &name);

	return (error);
}

/*
 * Create an RPC client handle for the given (address,prog,vers)
 * triple using UDP.
 */
static CLIENT *
gsstest_get_rpc(struct sockaddr *sa, rpcprog_t prog, rpcvers_t vers)
{
	struct thread *td = curthread;
	const char* protofmly;
	struct sockaddr_storage ss;
	struct socket *so;
	CLIENT *rpcb;
	struct timeval timo;
	RPCB parms;
	char *uaddr;
	enum clnt_stat stat = RPC_SUCCESS;
	int rpcvers = RPCBVERS4;
	bool_t do_tcp = FALSE;
	struct portmap mapping;
	u_short port = 0;

	/*
	 * First we need to contact the remote RPCBIND service to find
	 * the right port.
	 */
	memcpy(&ss, sa, sa->sa_len);
	switch (ss.ss_family) {
	case AF_INET:
		((struct sockaddr_in *)&ss)->sin_port = htons(111);
		protofmly = "inet";
		socreate(AF_INET, &so, SOCK_DGRAM, 0, td->td_ucred, td);
		break;
		
#ifdef INET6
	case AF_INET6:
		((struct sockaddr_in6 *)&ss)->sin6_port = htons(111);
		protofmly = "inet6";
		socreate(AF_INET6, &so, SOCK_DGRAM, 0, td->td_ucred, td);
		break;
#endif

	default:
		/*
		 * Unsupported address family - fail.
		 */
		return (NULL);
	}

	rpcb = clnt_dg_create(so, (struct sockaddr *)&ss,
	    RPCBPROG, rpcvers, 0, 0);
	if (!rpcb)
		return (NULL);

try_tcp:
	parms.r_prog = prog;
	parms.r_vers = vers;
	if (do_tcp)
		parms.r_netid = "tcp";
	else
		parms.r_netid = "udp";
	parms.r_addr = "";
	parms.r_owner = "";

	/*
	 * Use the default timeout.
	 */
	timo.tv_sec = 25;
	timo.tv_usec = 0;
again:
	switch (rpcvers) {
	case RPCBVERS4:
	case RPCBVERS:
		/*
		 * Try RPCBIND 4 then 3.
		 */
		uaddr = NULL;
		stat = CLNT_CALL(rpcb, (rpcprog_t) RPCBPROC_GETADDR,
		    (xdrproc_t) xdr_rpcb, &parms,
		    (xdrproc_t) xdr_wrapstring, &uaddr, timo);
		if (stat == RPC_PROGVERSMISMATCH) {
			if (rpcvers == RPCBVERS4)
				rpcvers = RPCBVERS;
			else if (rpcvers == RPCBVERS)
				rpcvers = PMAPVERS;
			CLNT_CONTROL(rpcb, CLSET_VERS, &rpcvers);
			goto again;
		} else if (stat == RPC_SUCCESS) {
			/*
			 * We have a reply from the remote RPCBIND - turn it
			 * into an appropriate address and make a new client
			 * that can talk to the remote service.
			 *
			 * XXX fixup IPv6 scope ID.
			 */
			struct netbuf *a;
			a = __rpc_uaddr2taddr_af(ss.ss_family, uaddr);
			xdr_free((xdrproc_t) xdr_wrapstring, &uaddr);
			if (!a) {
				CLNT_DESTROY(rpcb);
				return (NULL);
			}
			memcpy(&ss, a->buf, a->len);
			free(a->buf, M_RPC);
			free(a, M_RPC);
		}
		break;
	case PMAPVERS:
		/*
		 * Try portmap.
		 */
		mapping.pm_prog = parms.r_prog;
		mapping.pm_vers = parms.r_vers;
		mapping.pm_prot = do_tcp ? IPPROTO_TCP : IPPROTO_UDP;
		mapping.pm_port = 0;

		stat = CLNT_CALL(rpcb, (rpcprog_t) PMAPPROC_GETPORT,
		    (xdrproc_t) xdr_portmap, &mapping,
		    (xdrproc_t) xdr_u_short, &port, timo);

		if (stat == RPC_SUCCESS) {
			switch (ss.ss_family) {
			case AF_INET:
				((struct sockaddr_in *)&ss)->sin_port =
					htons(port);
				break;
		
#ifdef INET6
			case AF_INET6:
				((struct sockaddr_in6 *)&ss)->sin6_port =
					htons(port);
				break;
#endif
			}
		}
		break;
	default:
		panic("invalid rpcvers %d", rpcvers);
	}
	/*
	 * We may have a positive response from the portmapper, but
	 * the requested service was not found. Make sure we received
	 * a valid port.
	 */
	switch (ss.ss_family) {
	case AF_INET:
		port = ((struct sockaddr_in *)&ss)->sin_port;
		break;
#ifdef INET6
	case AF_INET6:
		port = ((struct sockaddr_in6 *)&ss)->sin6_port;
		break;
#endif
	}
	if (stat != RPC_SUCCESS || !port) {
		/*
		 * If we were able to talk to rpcbind or portmap, but the udp
		 * variant wasn't available, ask about tcp.
		 *
		 * XXX - We could also check for a TCP portmapper, but
		 * if the host is running a portmapper at all, we should be able
		 * to hail it over UDP.
		 */
		if (stat == RPC_SUCCESS && !do_tcp) {
			do_tcp = TRUE;
			goto try_tcp;
		}

		/* Otherwise, bad news. */
		printf("gsstest_get_rpc: failed to contact remote rpcbind, "
		    "stat = %d, port = %d\n",
		    (int) stat, port);
		CLNT_DESTROY(rpcb);
		return (NULL);
	}

	if (do_tcp) {
		/*
		 * Destroy the UDP client we used to speak to rpcbind and
		 * recreate as a TCP client.
		 */
		struct netconfig *nconf = NULL;

		CLNT_DESTROY(rpcb);

		switch (ss.ss_family) {
		case AF_INET:
			nconf = getnetconfigent("tcp");
			break;
#ifdef INET6
		case AF_INET6:
			nconf = getnetconfigent("tcp6");
			break;
#endif
		}

		rpcb = clnt_reconnect_create(nconf, (struct sockaddr *)&ss,
		    prog, vers, 0, 0);
	} else {
		/*
		 * Re-use the client we used to speak to rpcbind.
		 */
		CLNT_CONTROL(rpcb, CLSET_SVC_ADDR, &ss);
		CLNT_CONTROL(rpcb, CLSET_PROG, &prog);
		CLNT_CONTROL(rpcb, CLSET_VERS, &vers);
	}

	return (rpcb);
}

/*
 * RPCSEC_GSS client
 */
static int
gsstest_3(struct thread *td)
{
	struct sockaddr_in sin;
	char service[128];
	CLIENT *client;
	AUTH *auth;
	rpc_gss_options_ret_t options_ret;
	enum clnt_stat stat;
	struct timeval tv;
	rpc_gss_service_t svc;
	int i;

	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin.sin_port = 0;

	client = gsstest_get_rpc((struct sockaddr *) &sin, 123456, 1);
	if (!client) {
		uprintf("Can't connect to service\n");
		return(1);
	}

	memcpy(service, "host@", 5);
	getcredhostname(td->td_ucred, service + 5, sizeof(service) - 5);

	auth = rpc_gss_seccreate(client, curthread->td_ucred,
	    service, "kerberosv5", rpc_gss_svc_privacy,
	    NULL, NULL, &options_ret);
	if (!auth) {
		gss_OID oid;
		uprintf("Can't authorize to service (mech=%s)\n",
			options_ret.actual_mechanism);
		oid = GSS_C_NO_OID;
		rpc_gss_mech_to_oid(options_ret.actual_mechanism, &oid);
		report_error(oid, options_ret.major_status,
		    options_ret.minor_status);
		CLNT_DESTROY(client);
		return (1);
	}

	for (svc = rpc_gss_svc_none; svc <= rpc_gss_svc_privacy; svc++) {
		const char *svc_names[] = {
			"rpc_gss_svc_default",
			"rpc_gss_svc_none",
			"rpc_gss_svc_integrity",
			"rpc_gss_svc_privacy"
		};
		int num;

		rpc_gss_set_defaults(auth, svc, NULL);

		client->cl_auth = auth;
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		for (i = 42; i < 142; i++) {
			num = i;
			stat = CLNT_CALL(client, 1,
			    (xdrproc_t) xdr_int, (char *) &num,
			    (xdrproc_t) xdr_int, (char *) &num, tv);
			if (stat == RPC_SUCCESS) {
				if (num != i + 100)
					uprintf("unexpected reply %d\n", num);
			} else {
				uprintf("call failed, stat=%d\n", (int) stat);
				break;
			}
		}
		if (i == 142)
			uprintf("call succeeded with %s\n", svc_names[svc]);
	}

	AUTH_DESTROY(auth);
	CLNT_RELEASE(client);

	return (0);
}

/*
 * RPCSEC_GSS server
 */
static rpc_gss_principal_t server_acl = NULL;
static bool_t server_new_context(struct svc_req *req, gss_cred_id_t deleg,
    gss_ctx_id_t gss_context, rpc_gss_lock_t *lock, void **cookie);
static void server_program_1(struct svc_req *rqstp, register SVCXPRT *transp);

static int
gsstest_4(struct thread *td)
{
	SVCPOOL *pool;
	char principal[128 + 5];
	const char **mechs;
	static rpc_gss_callback_t cb;

	memcpy(principal, "host@", 5);
	getcredhostname(td->td_ucred, principal + 5, sizeof(principal) - 5);

	mechs = rpc_gss_get_mechanisms();
	while (*mechs) {
		if (!rpc_gss_set_svc_name(principal, *mechs, GSS_C_INDEFINITE,
			123456, 1)) {
			rpc_gss_error_t e;

			rpc_gss_get_error(&e);
			printf("setting name for %s for %s failed: %d, %d\n",
			    principal, *mechs,
			    e.rpc_gss_error, e.system_error);
		}
		mechs++;
	}

	cb.program = 123456;
	cb.version = 1;
	cb.callback = server_new_context;
	rpc_gss_set_callback(&cb);

	pool = svcpool_create("gsstest", NULL);

	svc_create(pool, server_program_1, 123456, 1, NULL);
	svc_run(pool);

	rpc_gss_clear_svc_name(123456, 1);
	rpc_gss_clear_callback(&cb);

	svcpool_destroy(pool);

	return (0);
}

static void
server_program_1(struct svc_req *rqstp, register SVCXPRT *transp)
{
	rpc_gss_rawcred_t *rcred;
	rpc_gss_ucred_t *ucred;
	int		i, num;

	if (rqstp->rq_cred.oa_flavor != RPCSEC_GSS) {
		svcerr_weakauth(rqstp);
		return;
	}		
		
	if (!rpc_gss_getcred(rqstp, &rcred, &ucred, NULL)) {
		svcerr_systemerr(rqstp);
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
		if (!svc_getargs(rqstp, (xdrproc_t) xdr_void, 0)) {
			svcerr_decode(rqstp);
			goto out;
		}
		if (!svc_sendreply(rqstp, (xdrproc_t) xdr_void, 0)) {
			svcerr_systemerr(rqstp);
		}
		goto out;

	case 1:
		if (!svc_getargs(rqstp, (xdrproc_t) xdr_int,
			(char *) &num)) {
			svcerr_decode(rqstp);
			goto out;
		}
		num += 100;
		if (!svc_sendreply(rqstp, (xdrproc_t) xdr_int,
			(char *) &num)) {
			svcerr_systemerr(rqstp);
		}
		goto out;

	default:
		svcerr_noproc(rqstp);
		goto out;
	}

out:
	svc_freereq(rqstp);
	return;
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

static bool_t
server_new_context(__unused struct svc_req *req,
    gss_cred_id_t deleg,
    __unused gss_ctx_id_t gss_context,
    rpc_gss_lock_t *lock,
    __unused void **cookie)
{
	rpc_gss_rawcred_t *rcred = lock->raw_cred;
	OM_uint32 junk;

	printf("new security context version=%d, mech=%s, qop=%s:\n",
	    rcred->version, rcred->mechanism, rcred->qop);
	print_principal(rcred->client_principal);

	if (server_acl) {
		if (rcred->client_principal->len != server_acl->len
		    || memcmp(rcred->client_principal->name, server_acl->name,
			server_acl->len)) {
			return (FALSE);
		}
	}
	gss_release_cred(&junk, &deleg);

	return (TRUE);
}

/*
 * Hook up a syscall for gssapi testing.
 */

struct gsstest_args {
        int a_op;
	void *a_args;
	void *a_res;
};

struct gsstest_2_args {
	int step;		/* test step number */
	gss_buffer_desc input_token; /* token from userland */
	gss_buffer_desc output_token; /* buffer to receive reply token */
};
struct gsstest_2_res {
	OM_uint32 maj_stat;	/* maj_stat from kernel */
	OM_uint32 min_stat;	/* min_stat from kernel */
	gss_buffer_desc output_token; /* reply token (using space from gsstest_2_args.output) */
};

static int
gsstest(struct thread *td, struct gsstest_args *uap)
{
	int error;

	switch (uap->a_op) {
	case 1:
                return (gsstest_1(td));

	case 2: {
		struct gsstest_2_args args;
		struct gsstest_2_res res;
		gss_buffer_desc input_token, output_token;
		OM_uint32 junk;

		error = copyin(uap->a_args, &args, sizeof(args));
		if (error)
			return (error);
		input_token.length = args.input_token.length;
		input_token.value = malloc(input_token.length, M_GSSAPI,
		    M_WAITOK);
		error = copyin(args.input_token.value, input_token.value,
		    input_token.length);
		if (error) {
			gss_release_buffer(&junk, &input_token);
			return (error);
		}
		output_token.length = 0;
		output_token.value = NULL;
		gsstest_2(td, args.step, &input_token,
		    &res.maj_stat, &res.min_stat, &output_token);
		gss_release_buffer(&junk, &input_token);
		if (output_token.length > args.output_token.length) {
			gss_release_buffer(&junk, &output_token);
			return (EOVERFLOW);
		}
		res.output_token.length = output_token.length;
		res.output_token.value = args.output_token.value;
		error = copyout(output_token.value, res.output_token.value,
		    output_token.length);
		gss_release_buffer(&junk, &output_token);
		if (error)
			return (error);

		return (copyout(&res, uap->a_res, sizeof(res)));
		
		break;
	}
	case 3:
		return (gsstest_3(td));
	case 4:
		return (gsstest_4(td));
	}

        return (EINVAL);
}

/*
 * The `sysent' for the new syscall
 */
static struct sysent gsstest_sysent = {
        3,                      /* sy_narg */
        (sy_call_t *) gsstest	/* sy_call */
};

/*
 * The offset in sysent where the syscall is allocated.
 */
static int gsstest_offset = NO_SYSCALL;

/*
 * The function called at load/unload.
 */


static int
gsstest_load(struct module *module, int cmd, void *arg)
{
        int error = 0;

        switch (cmd) {
        case MOD_LOAD :
                break;
        case MOD_UNLOAD :
                break;
        default :
                error = EOPNOTSUPP;
                break;
        }
        return error;
}

SYSCALL_MODULE(gsstest_syscall, &gsstest_offset, &gsstest_sysent,
    gsstest_load, NULL);
