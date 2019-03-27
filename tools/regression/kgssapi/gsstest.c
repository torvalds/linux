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

#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/module.h>

#include <stdio.h>
#include <string.h>
#include <err.h>
#include <unistd.h>
#include <stdlib.h>

#include <krb5.h>
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>

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
}

int
main(int argc, char **argv)
{
	struct module_stat stat;
	int mod;
	int syscall_num;

	stat.version = sizeof(stat);
	mod = modfind("gsstest_syscall");
	if (mod < 0) {
		fprintf(stderr, "%s: kernel support not present\n", argv[0]);
		exit(1);
	}
	modstat(mod, &stat);
	syscall_num = stat.data.intval;

	switch (atoi(argv[1])) {
	case 1:
		syscall(syscall_num, 1, NULL, NULL);
		break;

	case 2: {
		struct gsstest_2_args args;
		struct gsstest_2_res res;
		char hostname[512];
		char token_buffer[8192];
		OM_uint32 maj_stat, min_stat;
		gss_ctx_id_t client_context = GSS_C_NO_CONTEXT;
		gss_cred_id_t client_cred;
		gss_OID mech_type = GSS_C_NO_OID;
		gss_buffer_desc name_buf, message_buf;
		gss_name_t name;
		int32_t enctypes[] = {
			ETYPE_DES_CBC_CRC,
			ETYPE_ARCFOUR_HMAC_MD5,
			ETYPE_ARCFOUR_HMAC_MD5_56,
			ETYPE_AES256_CTS_HMAC_SHA1_96,
			ETYPE_AES128_CTS_HMAC_SHA1_96,
			ETYPE_DES3_CBC_SHA1,
		};
		int num_enctypes = sizeof(enctypes) / sizeof(enctypes[0]);
		int established;
		int i;

		for (i = 0; i < num_enctypes; i++) {
			printf("testing etype %d\n", enctypes[i]);
			args.output_token.length = sizeof(token_buffer);
			args.output_token.value = token_buffer;

			gethostname(hostname, sizeof(hostname));
			snprintf(token_buffer, sizeof(token_buffer),
			    "nfs@%s", hostname);
			name_buf.length = strlen(token_buffer);
			name_buf.value = token_buffer;
			maj_stat = gss_import_name(&min_stat, &name_buf,
			    GSS_C_NT_HOSTBASED_SERVICE, &name);
			if (GSS_ERROR(maj_stat)) {
				printf("gss_import_name failed\n");
				report_error(mech_type, maj_stat, min_stat);
				goto out;
			}

			maj_stat = gss_acquire_cred(&min_stat, GSS_C_NO_NAME,
			    0, GSS_C_NO_OID_SET, GSS_C_INITIATE, &client_cred,
			    NULL, NULL);
			if (GSS_ERROR(maj_stat)) {
				printf("gss_acquire_cred (client) failed\n");
				report_error(mech_type, maj_stat, min_stat);
				goto out;
			}

			maj_stat = gss_krb5_set_allowable_enctypes(&min_stat,
			    client_cred, 1, &enctypes[i]);
			if (GSS_ERROR(maj_stat)) {
				printf("gss_krb5_set_allowable_enctypes failed\n");
				report_error(mech_type, maj_stat, min_stat);
				goto out;
			}

			res.output_token.length = 0;
			res.output_token.value = 0;
			established = 0;
			while (!established) {
				maj_stat = gss_init_sec_context(&min_stat,
				    client_cred,
				    &client_context,
				    name,
				    GSS_C_NO_OID,
				    (GSS_C_MUTUAL_FLAG
					|GSS_C_CONF_FLAG
					|GSS_C_INTEG_FLAG
					|GSS_C_SEQUENCE_FLAG
					|GSS_C_REPLAY_FLAG),
				    0,
				    GSS_C_NO_CHANNEL_BINDINGS,
				    &res.output_token,
				    &mech_type,
				    &args.input_token,
				    NULL,
				    NULL);
				if (GSS_ERROR(maj_stat)) {
					printf("gss_init_sec_context failed\n");
					report_error(mech_type, maj_stat, min_stat);
					goto out;
				}
				if (args.input_token.length) {
					args.step = 1;
					syscall(syscall_num, 2, &args, &res);
					gss_release_buffer(&min_stat,
					    &args.input_token);
					if (res.maj_stat != GSS_S_COMPLETE
					    && res.maj_stat != GSS_S_CONTINUE_NEEDED) {
						printf("gss_accept_sec_context (kernel) failed\n");
						report_error(mech_type, res.maj_stat,
						    res.min_stat);
						goto out;
					}
				}
				if (maj_stat == GSS_S_COMPLETE)
					established = 1;
			}

			message_buf.value = "Hello world";
			message_buf.length = strlen((char *) message_buf.value);

			maj_stat = gss_get_mic(&min_stat, client_context,
			    GSS_C_QOP_DEFAULT, &message_buf, &args.input_token);
			if (GSS_ERROR(maj_stat)) {
				printf("gss_get_mic failed\n");
				report_error(mech_type, maj_stat, min_stat);
				goto out;
			}
		
			args.step = 2;
			syscall(syscall_num, 2, &args, &res);
			gss_release_buffer(&min_stat, &args.input_token);
			if (GSS_ERROR(res.maj_stat)) {
				printf("kernel gss_verify_mic failed\n");
				report_error(mech_type, res.maj_stat, res.min_stat);
				goto out;
			}

			maj_stat = gss_verify_mic(&min_stat, client_context,
			    &message_buf, &res.output_token, NULL);
			if (GSS_ERROR(maj_stat)) {
				printf("gss_verify_mic failed\n");
				report_error(mech_type, maj_stat, min_stat);
				goto out;
			}

			maj_stat = gss_wrap(&min_stat, client_context,
			    TRUE, GSS_C_QOP_DEFAULT, &message_buf, NULL,
			    &args.input_token);
			if (GSS_ERROR(maj_stat)) {
				printf("gss_wrap failed\n");
				report_error(mech_type, maj_stat, min_stat);
				goto out;
			}

			args.step = 3;
			syscall(syscall_num, 2, &args, &res);
			gss_release_buffer(&min_stat, &args.input_token);
			if (GSS_ERROR(res.maj_stat)) {
				printf("kernel gss_unwrap failed\n");
				report_error(mech_type, res.maj_stat, res.min_stat);
				goto out;
			}

			maj_stat = gss_unwrap(&min_stat, client_context,
			    &res.output_token, &message_buf, NULL, NULL);
			if (GSS_ERROR(maj_stat)) {
				printf("gss_unwrap failed\n");
				report_error(mech_type, maj_stat, min_stat);
				goto out;
			}
			gss_release_buffer(&min_stat, &message_buf);

			maj_stat = gss_wrap(&min_stat, client_context,
			    FALSE, GSS_C_QOP_DEFAULT, &message_buf, NULL,
			    &args.input_token);
			if (GSS_ERROR(maj_stat)) {
				printf("gss_wrap failed\n");
				report_error(mech_type, maj_stat, min_stat);
				goto out;
			}

			args.step = 4;
			syscall(syscall_num, 2, &args, &res);
			gss_release_buffer(&min_stat, &args.input_token);
			if (GSS_ERROR(res.maj_stat)) {
				printf("kernel gss_unwrap failed\n");
				report_error(mech_type, res.maj_stat, res.min_stat);
				goto out;
			}

			maj_stat = gss_unwrap(&min_stat, client_context,
			    &res.output_token, &message_buf, NULL, NULL);
			if (GSS_ERROR(maj_stat)) {
				printf("gss_unwrap failed\n");
				report_error(mech_type, maj_stat, min_stat);
				goto out;
			}
			gss_release_buffer(&min_stat, &message_buf);

			args.step = 5;
			syscall(syscall_num, 2, &args, &res);

			gss_release_name(&min_stat, &name);
			gss_release_cred(&min_stat, &client_cred);
			gss_delete_sec_context(&min_stat, &client_context,
			    GSS_C_NO_BUFFER);
		}

		break;
	}
	case 3:
		syscall(syscall_num, 3, NULL, NULL);
		break;
	case 4:
		syscall(syscall_num, 4, NULL, NULL);
		break;
	}
	return (0);

out:
	return (1);
}
