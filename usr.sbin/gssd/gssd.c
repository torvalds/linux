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

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/syslog.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#ifndef WITHOUT_KERBEROS
#include <krb5.h>
#endif
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gssapi/gssapi.h>
#include <rpc/rpc.h>
#include <rpc/rpc_com.h>

#include "gssd.h"

#ifndef _PATH_GSS_MECH
#define _PATH_GSS_MECH	"/etc/gss/mech"
#endif
#ifndef _PATH_GSSDSOCK
#define _PATH_GSSDSOCK	"/var/run/gssd.sock"
#endif
#define GSSD_CREDENTIAL_CACHE_FILE	"/tmp/krb5cc_gssd"

struct gss_resource {
	LIST_ENTRY(gss_resource) gr_link;
	uint64_t	gr_id;	/* identifier exported to kernel */
	void*		gr_res;	/* GSS-API resource pointer */
};
LIST_HEAD(gss_resource_list, gss_resource) gss_resources;
int gss_resource_count;
uint32_t gss_next_id;
uint32_t gss_start_time;
int debug_level;
static char ccfile_dirlist[PATH_MAX + 1], ccfile_substring[NAME_MAX + 1];
static char pref_realm[1024];
static int verbose;
static int use_old_des;
static int hostbased_initiator_cred;
#ifndef WITHOUT_KERBEROS
/* 1.2.752.43.13.14 */
static gss_OID_desc gss_krb5_set_allowable_enctypes_x_desc =
{6, (void *) "\x2a\x85\x70\x2b\x0d\x0e"};
static gss_OID GSS_KRB5_SET_ALLOWABLE_ENCTYPES_X =
    &gss_krb5_set_allowable_enctypes_x_desc;
static gss_OID_desc gss_krb5_mech_oid_x_desc =
{9, (void *) "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02" };
static gss_OID GSS_KRB5_MECH_OID_X =
    &gss_krb5_mech_oid_x_desc;
#endif

static void gssd_load_mech(void);
static int find_ccache_file(const char *, uid_t, char *);
static int is_a_valid_tgt_cache(const char *, uid_t, int *, time_t *);
static void gssd_verbose_out(const char *, ...);
#ifndef WITHOUT_KERBEROS
static krb5_error_code gssd_get_cc_from_keytab(const char *);
static OM_uint32 gssd_get_user_cred(OM_uint32 *, uid_t, gss_cred_id_t *);
#endif
void gssd_terminate(int);

extern void gssd_1(struct svc_req *rqstp, SVCXPRT *transp);
extern int gssd_syscall(char *path);

int
main(int argc, char **argv)
{
	/*
	 * We provide an RPC service on a local-domain socket. The
	 * kernel's GSS-API code will pass what it can't handle
	 * directly to us.
	 */
	struct sockaddr_un sun;
	int fd, oldmask, ch, debug;
	SVCXPRT *xprt;

	/*
	 * Initialize the credential cache file name substring and the
	 * search directory list.
	 */
	strlcpy(ccfile_substring, "krb5cc_", sizeof(ccfile_substring));
	ccfile_dirlist[0] = '\0';
	pref_realm[0] = '\0';
	debug = 0;
	verbose = 0;
	while ((ch = getopt(argc, argv, "dhovs:c:r:")) != -1) {
		switch (ch) {
		case 'd':
			debug_level++;
			break;
		case 'h':
#ifndef WITHOUT_KERBEROS
			/*
			 * Enable use of a host based initiator credential
			 * in the default keytab file.
			 */
			hostbased_initiator_cred = 1;
#else
			errx(1, "This option not available when built"
			    " without MK_KERBEROS\n");
#endif
			break;
		case 'o':
#ifndef WITHOUT_KERBEROS
			/*
			 * Force use of DES and the old type of GSSAPI token.
			 */
			use_old_des = 1;
#else
			errx(1, "This option not available when built"
			    " without MK_KERBEROS\n");
#endif
			break;
		case 'v':
			verbose = 1;
			break;
		case 's':
#ifndef WITHOUT_KERBEROS
			/*
			 * Set the directory search list. This enables use of
			 * find_ccache_file() to search the directories for a
			 * suitable credentials cache file.
			 */
			strlcpy(ccfile_dirlist, optarg, sizeof(ccfile_dirlist));
#else
			errx(1, "This option not available when built"
			    " without MK_KERBEROS\n");
#endif
			break;
		case 'c':
			/*
			 * Specify a non-default credential cache file
			 * substring.
			 */
			strlcpy(ccfile_substring, optarg,
			    sizeof(ccfile_substring));
			break;
		case 'r':
			/*
			 * Set the preferred realm for the credential cache tgt.
			 */
			strlcpy(pref_realm, optarg, sizeof(pref_realm));
			break;
		default:
			fprintf(stderr,
			    "usage: %s [-d] [-s dir-list] [-c file-substring]"
			    " [-r preferred-realm]\n", argv[0]);
			exit(1);
			break;
		}
	}

	gssd_load_mech();

	if (!debug_level) {
		if (daemon(0, 0) != 0)
			err(1, "Can't daemonize");
		signal(SIGINT, SIG_IGN);
		signal(SIGQUIT, SIG_IGN);
		signal(SIGHUP, SIG_IGN);
	}
	signal(SIGTERM, gssd_terminate);
	signal(SIGPIPE, gssd_terminate);

	memset(&sun, 0, sizeof sun);
	sun.sun_family = AF_LOCAL;
	unlink(_PATH_GSSDSOCK);
	strcpy(sun.sun_path, _PATH_GSSDSOCK);
	sun.sun_len = SUN_LEN(&sun);
	fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (fd < 0) {
		if (debug_level == 0) {
			syslog(LOG_ERR, "Can't create local gssd socket");
			exit(1);
		}
		err(1, "Can't create local gssd socket");
	}
	oldmask = umask(S_IXUSR|S_IRWXG|S_IRWXO);
	if (bind(fd, (struct sockaddr *) &sun, sun.sun_len) < 0) {
		if (debug_level == 0) {
			syslog(LOG_ERR, "Can't bind local gssd socket");
			exit(1);
		}
		err(1, "Can't bind local gssd socket");
	}
	umask(oldmask);
	if (listen(fd, SOMAXCONN) < 0) {
		if (debug_level == 0) {
			syslog(LOG_ERR, "Can't listen on local gssd socket");
			exit(1);
		}
		err(1, "Can't listen on local gssd socket");
	}
	xprt = svc_vc_create(fd, RPC_MAXDATASIZE, RPC_MAXDATASIZE);
	if (!xprt) {
		if (debug_level == 0) {
			syslog(LOG_ERR,
			    "Can't create transport for local gssd socket");
			exit(1);
		}
		err(1, "Can't create transport for local gssd socket");
	}
	if (!svc_reg(xprt, GSSD, GSSDVERS, gssd_1, NULL)) {
		if (debug_level == 0) {
			syslog(LOG_ERR,
			    "Can't register service for local gssd socket");
			exit(1);
		}
		err(1, "Can't register service for local gssd socket");
	}

	LIST_INIT(&gss_resources);
	gss_next_id = 1;
	gss_start_time = time(0);

	gssd_syscall(_PATH_GSSDSOCK);
	svc_run();
	gssd_syscall("");

	return (0);
}

static void
gssd_load_mech(void)
{
	FILE		*fp;
	char		buf[256];
	char		*p;
	char		*name, *oid, *lib, *kobj;

	fp = fopen(_PATH_GSS_MECH, "r");
	if (!fp)
		return;

	while (fgets(buf, sizeof(buf), fp)) {
		if (*buf == '#')
			continue;
		p = buf;
		name = strsep(&p, "\t\n ");
		if (p) while (isspace(*p)) p++;
		oid = strsep(&p, "\t\n ");
		if (p) while (isspace(*p)) p++;
		lib = strsep(&p, "\t\n ");
		if (p) while (isspace(*p)) p++;
		kobj = strsep(&p, "\t\n ");
		if (!name || !oid || !lib || !kobj)
			continue;

		if (strcmp(kobj, "-")) {
			/*
			 * Attempt to load the kernel module if its
			 * not already present.
			 */
			if (modfind(kobj) < 0) {
				if (kldload(kobj) < 0) {
					fprintf(stderr,
			"%s: can't find or load kernel module %s for %s\n",
					    getprogname(), kobj, name);
				}
			}
		}
	}
	fclose(fp);
}

static void *
gssd_find_resource(uint64_t id)
{
	struct gss_resource *gr;

	if (!id)
		return (NULL);

	LIST_FOREACH(gr, &gss_resources, gr_link)
		if (gr->gr_id == id)
			return (gr->gr_res);

	return (NULL);
}

static uint64_t
gssd_make_resource(void *res)
{
	struct gss_resource *gr;

	if (!res)
		return (0);

	gr = malloc(sizeof(struct gss_resource));
	if (!gr)
		return (0);
	gr->gr_id = (gss_next_id++) + ((uint64_t) gss_start_time << 32);
	gr->gr_res = res;
	LIST_INSERT_HEAD(&gss_resources, gr, gr_link);
	gss_resource_count++;
	if (debug_level > 1)
		printf("%d resources allocated\n", gss_resource_count);

	return (gr->gr_id);
}

static void
gssd_delete_resource(uint64_t id)
{
	struct gss_resource *gr;

	LIST_FOREACH(gr, &gss_resources, gr_link) {
		if (gr->gr_id == id) {
			LIST_REMOVE(gr, gr_link);
			free(gr);
			gss_resource_count--;
			if (debug_level > 1)
				printf("%d resources allocated\n",
				    gss_resource_count);
			return;
		}
	}
}

static void
gssd_verbose_out(const char *fmt, ...)
{
	va_list ap;

	if (verbose != 0) {
		va_start(ap, fmt);
		if (debug_level == 0)
			vsyslog(LOG_INFO | LOG_DAEMON, fmt, ap);
		else
			vfprintf(stderr, fmt, ap);
		va_end(ap);
	}
}

bool_t
gssd_null_1_svc(void *argp, void *result, struct svc_req *rqstp)
{

	gssd_verbose_out("gssd_null: done\n");
	return (TRUE);
}

bool_t
gssd_init_sec_context_1_svc(init_sec_context_args *argp, init_sec_context_res *result, struct svc_req *rqstp)
{
	gss_cred_id_t cred = GSS_C_NO_CREDENTIAL;
	gss_ctx_id_t ctx = GSS_C_NO_CONTEXT;
	gss_name_t name = GSS_C_NO_NAME;
	char ccname[PATH_MAX + 5 + 1], *cp, *cp2;
	int gotone, gotcred;
	OM_uint32 min_stat;
#ifndef WITHOUT_KERBEROS
	gss_buffer_desc principal_desc;
	char enctype[sizeof(uint32_t)];
	int key_enctype;
	OM_uint32 maj_stat;
#endif

	memset(result, 0, sizeof(*result));
	if (hostbased_initiator_cred != 0 && argp->cred != 0 &&
	    argp->uid == 0) {
		/*
		 * These credentials are for a host based initiator name
		 * in a keytab file, which should now have credentials
		 * in /tmp/krb5cc_gssd, because gss_acquire_cred() did
		 * the equivalent of "kinit -k".
		 */
		snprintf(ccname, sizeof(ccname), "FILE:%s",
		    GSSD_CREDENTIAL_CACHE_FILE);
	} else if (ccfile_dirlist[0] != '\0' && argp->cred == 0) {
		/*
		 * For the "-s" case and no credentials provided as an
		 * argument, search the directory list for an appropriate
		 * credential cache file. If the search fails, return failure.
		 */
		gotone = 0;
		cp = ccfile_dirlist;
		do {
			cp2 = strchr(cp, ':');
			if (cp2 != NULL)
				*cp2 = '\0';
			gotone = find_ccache_file(cp, argp->uid, ccname);
			if (gotone != 0)
				break;
			if (cp2 != NULL)
				*cp2++ = ':';
			cp = cp2;
		} while (cp != NULL && *cp != '\0');
		if (gotone == 0) {
			result->major_status = GSS_S_CREDENTIALS_EXPIRED;
			gssd_verbose_out("gssd_init_sec_context: -s no"
			    " credential cache file found for uid=%d\n",
			    (int)argp->uid);
			return (TRUE);
		}
	} else {
		/*
		 * If there wasn't a "-s" option or the credentials have
		 * been provided as an argument, do it the old way.
		 * When credentials are provided, the uid should be root.
		 */
		if (argp->cred != 0 && argp->uid != 0) {
			if (debug_level == 0)
				syslog(LOG_ERR, "gss_init_sec_context:"
				    " cred for non-root");
			else
				fprintf(stderr, "gss_init_sec_context:"
				    " cred for non-root\n");
		}
		snprintf(ccname, sizeof(ccname), "FILE:/tmp/krb5cc_%d",
		    (int) argp->uid);
	}
	setenv("KRB5CCNAME", ccname, TRUE);

	if (argp->cred) {
		cred = gssd_find_resource(argp->cred);
		if (!cred) {
			result->major_status = GSS_S_CREDENTIALS_EXPIRED;
			gssd_verbose_out("gssd_init_sec_context: cred"
			    " resource not found\n");
			return (TRUE);
		}
	}
	if (argp->ctx) {
		ctx = gssd_find_resource(argp->ctx);
		if (!ctx) {
			result->major_status = GSS_S_CONTEXT_EXPIRED;
			gssd_verbose_out("gssd_init_sec_context: context"
			    " resource not found\n");
			return (TRUE);
		}
	}
	if (argp->name) {
		name = gssd_find_resource(argp->name);
		if (!name) {
			result->major_status = GSS_S_BAD_NAME;
			gssd_verbose_out("gssd_init_sec_context: name"
			    " resource not found\n");
			return (TRUE);
		}
	}
	gotcred = 0;

#ifndef WITHOUT_KERBEROS
	if (use_old_des != 0) {
		if (cred == GSS_C_NO_CREDENTIAL) {
			/* Acquire a credential for the uid. */
			maj_stat = gssd_get_user_cred(&min_stat, argp->uid,
			    &cred);
			if (maj_stat == GSS_S_COMPLETE)
				gotcred = 1;
			else
				gssd_verbose_out("gssd_init_sec_context: "
				    "get user cred failed uid=%d major=0x%x "
				    "minor=%d\n", (int)argp->uid,
				    (unsigned int)maj_stat, (int)min_stat);
		}
		if (cred != GSS_C_NO_CREDENTIAL) {
			key_enctype = ETYPE_DES_CBC_CRC;
			enctype[0] = (key_enctype >> 24) & 0xff;
			enctype[1] = (key_enctype >> 16) & 0xff;
			enctype[2] = (key_enctype >> 8) & 0xff;
			enctype[3] = key_enctype & 0xff;
			principal_desc.length = sizeof(enctype);
			principal_desc.value = enctype;
			result->major_status = gss_set_cred_option(
			    &result->minor_status, &cred,
			    GSS_KRB5_SET_ALLOWABLE_ENCTYPES_X,
			    &principal_desc);
			gssd_verbose_out("gssd_init_sec_context: set allowable "
			    "enctype major=0x%x minor=%d\n",
			    (unsigned int)result->major_status,
			    (int)result->minor_status);
			if (result->major_status != GSS_S_COMPLETE) {
				if (gotcred != 0)
					gss_release_cred(&min_stat, &cred);
				return (TRUE);
			}
		}
	}
#endif
	result->major_status = gss_init_sec_context(&result->minor_status,
	    cred, &ctx, name, argp->mech_type,
	    argp->req_flags, argp->time_req, argp->input_chan_bindings,
	    &argp->input_token, &result->actual_mech_type,
	    &result->output_token, &result->ret_flags, &result->time_rec);
	gssd_verbose_out("gssd_init_sec_context: done major=0x%x minor=%d"
	    " uid=%d\n", (unsigned int)result->major_status,
	    (int)result->minor_status, (int)argp->uid);
	if (gotcred != 0)
		gss_release_cred(&min_stat, &cred);

	if (result->major_status == GSS_S_COMPLETE
	    || result->major_status == GSS_S_CONTINUE_NEEDED) {
		if (argp->ctx)
			result->ctx = argp->ctx;
		else
			result->ctx = gssd_make_resource(ctx);
	}

	return (TRUE);
}

bool_t
gssd_accept_sec_context_1_svc(accept_sec_context_args *argp, accept_sec_context_res *result, struct svc_req *rqstp)
{
	gss_ctx_id_t ctx = GSS_C_NO_CONTEXT;
	gss_cred_id_t cred = GSS_C_NO_CREDENTIAL;
	gss_name_t src_name;
	gss_cred_id_t delegated_cred_handle;

	memset(result, 0, sizeof(*result));
	if (argp->ctx) {
		ctx = gssd_find_resource(argp->ctx);
		if (!ctx) {
			result->major_status = GSS_S_CONTEXT_EXPIRED;
			gssd_verbose_out("gssd_accept_sec_context: ctx"
			    " resource not found\n");
			return (TRUE);
		}
	}
	if (argp->cred) {
		cred = gssd_find_resource(argp->cred);
		if (!cred) {
			result->major_status = GSS_S_CREDENTIALS_EXPIRED;
			gssd_verbose_out("gssd_accept_sec_context: cred"
			    " resource not found\n");
			return (TRUE);
		}
	}

	memset(result, 0, sizeof(*result));
	result->major_status = gss_accept_sec_context(&result->minor_status,
	    &ctx, cred, &argp->input_token, argp->input_chan_bindings,
	    &src_name, &result->mech_type, &result->output_token,
	    &result->ret_flags, &result->time_rec,
	    &delegated_cred_handle);
	gssd_verbose_out("gssd_accept_sec_context: done major=0x%x minor=%d\n",
	    (unsigned int)result->major_status, (int)result->minor_status);

	if (result->major_status == GSS_S_COMPLETE
	    || result->major_status == GSS_S_CONTINUE_NEEDED) {
		if (argp->ctx)
			result->ctx = argp->ctx;
		else
			result->ctx = gssd_make_resource(ctx);
		result->src_name = gssd_make_resource(src_name);
		result->delegated_cred_handle =
			gssd_make_resource(delegated_cred_handle);
	}

	return (TRUE);
}

bool_t
gssd_delete_sec_context_1_svc(delete_sec_context_args *argp, delete_sec_context_res *result, struct svc_req *rqstp)
{
	gss_ctx_id_t ctx = gssd_find_resource(argp->ctx);

	if (ctx) {
		result->major_status = gss_delete_sec_context(
			&result->minor_status, &ctx, &result->output_token);
		gssd_delete_resource(argp->ctx);
	} else {
		result->major_status = GSS_S_COMPLETE;
		result->minor_status = 0;
	}
	gssd_verbose_out("gssd_delete_sec_context: done major=0x%x minor=%d\n",
	    (unsigned int)result->major_status, (int)result->minor_status);

	return (TRUE);
}

bool_t
gssd_export_sec_context_1_svc(export_sec_context_args *argp, export_sec_context_res *result, struct svc_req *rqstp)
{
	gss_ctx_id_t ctx = gssd_find_resource(argp->ctx);

	if (ctx) {
		result->major_status = gss_export_sec_context(
			&result->minor_status, &ctx,
			&result->interprocess_token);
		result->format = KGSS_HEIMDAL_1_1;
		gssd_delete_resource(argp->ctx);
	} else {
		result->major_status = GSS_S_FAILURE;
		result->minor_status = 0;
		result->interprocess_token.length = 0;
		result->interprocess_token.value = NULL;
	}
	gssd_verbose_out("gssd_export_sec_context: done major=0x%x minor=%d\n",
	    (unsigned int)result->major_status, (int)result->minor_status);

	return (TRUE);
}

bool_t
gssd_import_name_1_svc(import_name_args *argp, import_name_res *result, struct svc_req *rqstp)
{
	gss_name_t name;

	result->major_status = gss_import_name(&result->minor_status,
	    &argp->input_name_buffer, argp->input_name_type, &name);
	gssd_verbose_out("gssd_import_name: done major=0x%x minor=%d\n",
	    (unsigned int)result->major_status, (int)result->minor_status);

	if (result->major_status == GSS_S_COMPLETE)
		result->output_name = gssd_make_resource(name);
	else
		result->output_name = 0;

	return (TRUE);
}

bool_t
gssd_canonicalize_name_1_svc(canonicalize_name_args *argp, canonicalize_name_res *result, struct svc_req *rqstp)
{
	gss_name_t name = gssd_find_resource(argp->input_name);
	gss_name_t output_name;

	memset(result, 0, sizeof(*result));
	if (!name) {
		result->major_status = GSS_S_BAD_NAME;
		return (TRUE);
	}

	result->major_status = gss_canonicalize_name(&result->minor_status,
	    name, argp->mech_type, &output_name);
	gssd_verbose_out("gssd_canonicalize_name: done major=0x%x minor=%d\n",
	    (unsigned int)result->major_status, (int)result->minor_status);

	if (result->major_status == GSS_S_COMPLETE)
		result->output_name = gssd_make_resource(output_name);
	else
		result->output_name = 0;

	return (TRUE);
}

bool_t
gssd_export_name_1_svc(export_name_args *argp, export_name_res *result, struct svc_req *rqstp)
{
	gss_name_t name = gssd_find_resource(argp->input_name);

	memset(result, 0, sizeof(*result));
	if (!name) {
		result->major_status = GSS_S_BAD_NAME;
		gssd_verbose_out("gssd_export_name: name resource not found\n");
		return (TRUE);
	}

	result->major_status = gss_export_name(&result->minor_status,
	    name, &result->exported_name);
	gssd_verbose_out("gssd_export_name: done major=0x%x minor=%d\n",
	    (unsigned int)result->major_status, (int)result->minor_status);

	return (TRUE);
}

bool_t
gssd_release_name_1_svc(release_name_args *argp, release_name_res *result, struct svc_req *rqstp)
{
	gss_name_t name = gssd_find_resource(argp->input_name);

	if (name) {
		result->major_status = gss_release_name(&result->minor_status,
		    &name);
		gssd_delete_resource(argp->input_name);
	} else {
		result->major_status = GSS_S_COMPLETE;
		result->minor_status = 0;
	}
	gssd_verbose_out("gssd_release_name: done major=0x%x minor=%d\n",
	    (unsigned int)result->major_status, (int)result->minor_status);

	return (TRUE);
}

bool_t
gssd_pname_to_uid_1_svc(pname_to_uid_args *argp, pname_to_uid_res *result, struct svc_req *rqstp)
{
	gss_name_t name = gssd_find_resource(argp->pname);
	uid_t uid;
	char buf[1024], *bufp;
	struct passwd pwd, *pw;
	size_t buflen;
	int error;
	static size_t buflen_hint = 1024;

	memset(result, 0, sizeof(*result));
	if (name) {
		result->major_status =
			gss_pname_to_uid(&result->minor_status,
			    name, argp->mech, &uid);
		if (result->major_status == GSS_S_COMPLETE) {
			result->uid = uid;
			buflen = buflen_hint;
			for (;;) {
				pw = NULL;
				bufp = buf;
				if (buflen > sizeof(buf))
					bufp = malloc(buflen);
				if (bufp == NULL)
					break;
				error = getpwuid_r(uid, &pwd, bufp, buflen,
				    &pw);
				if (error != ERANGE)
					break;
				if (buflen > sizeof(buf))
					free(bufp);
				buflen += 1024;
				if (buflen > buflen_hint)
					buflen_hint = buflen;
			}
			if (pw) {
				int len = NGROUPS;
				int groups[NGROUPS];
				result->gid = pw->pw_gid;
				getgrouplist(pw->pw_name, pw->pw_gid,
				    groups, &len);
				result->gidlist.gidlist_len = len;
				result->gidlist.gidlist_val =
					mem_alloc(len * sizeof(int));
				memcpy(result->gidlist.gidlist_val, groups,
				    len * sizeof(int));
				gssd_verbose_out("gssd_pname_to_uid: mapped"
				    " to uid=%d, gid=%d\n", (int)result->uid,
				    (int)result->gid);
			} else {
				result->gid = 65534;
				result->gidlist.gidlist_len = 0;
				result->gidlist.gidlist_val = NULL;
				gssd_verbose_out("gssd_pname_to_uid: mapped"
				    " to uid=%d, but no groups\n",
				    (int)result->uid);
			}
			if (bufp != NULL && buflen > sizeof(buf))
				free(bufp);
		} else
			gssd_verbose_out("gssd_pname_to_uid: failed major=0x%x"
			    " minor=%d\n", (unsigned int)result->major_status,
			    (int)result->minor_status);
	} else {
		result->major_status = GSS_S_BAD_NAME;
		result->minor_status = 0;
		gssd_verbose_out("gssd_pname_to_uid: no name\n");
	}

	return (TRUE);
}

bool_t
gssd_acquire_cred_1_svc(acquire_cred_args *argp, acquire_cred_res *result, struct svc_req *rqstp)
{
	gss_name_t desired_name = GSS_C_NO_NAME;
	gss_cred_id_t cred;
	char ccname[PATH_MAX + 5 + 1], *cp, *cp2;
	int gotone;
#ifndef WITHOUT_KERBEROS
	gss_buffer_desc namebuf;
	uint32_t minstat;
	krb5_error_code kret;
#endif

	memset(result, 0, sizeof(*result));
	if (argp->desired_name) {
		desired_name = gssd_find_resource(argp->desired_name);
		if (!desired_name) {
			result->major_status = GSS_S_BAD_NAME;
			gssd_verbose_out("gssd_acquire_cred: no desired name"
			    " found\n");
			return (TRUE);
		}
	}

#ifndef WITHOUT_KERBEROS
	if (hostbased_initiator_cred != 0 && argp->desired_name != 0 &&
	    argp->uid == 0 && argp->cred_usage == GSS_C_INITIATE) {
		/* This is a host based initiator name in the keytab file. */
		snprintf(ccname, sizeof(ccname), "FILE:%s",
		    GSSD_CREDENTIAL_CACHE_FILE);
		setenv("KRB5CCNAME", ccname, TRUE);
		result->major_status = gss_display_name(&result->minor_status,
		    desired_name, &namebuf, NULL);
		gssd_verbose_out("gssd_acquire_cred: desired name for host "
		    "based initiator cred major=0x%x minor=%d\n",
		    (unsigned int)result->major_status,
		    (int)result->minor_status);
		if (result->major_status != GSS_S_COMPLETE)
			return (TRUE);
		if (namebuf.length > PATH_MAX + 5) {
			result->minor_status = 0;
			result->major_status = GSS_S_FAILURE;
			return (TRUE);
		}
		memcpy(ccname, namebuf.value, namebuf.length);
		ccname[namebuf.length] = '\0';
		if ((cp = strchr(ccname, '@')) != NULL)
			*cp = '/';
		kret = gssd_get_cc_from_keytab(ccname);
		gssd_verbose_out("gssd_acquire_cred: using keytab entry for "
		    "%s, kerberos ret=%d\n", ccname, (int)kret);
		gss_release_buffer(&minstat, &namebuf);
		if (kret != 0) {
			result->minor_status = kret;
			result->major_status = GSS_S_FAILURE;
			return (TRUE);
		}
	} else
#endif /* !WITHOUT_KERBEROS */
	if (ccfile_dirlist[0] != '\0' && argp->desired_name == 0) {
		/*
		 * For the "-s" case and no name provided as an
		 * argument, search the directory list for an appropriate
		 * credential cache file. If the search fails, return failure.
		 */
		gotone = 0;
		cp = ccfile_dirlist;
		do {
			cp2 = strchr(cp, ':');
			if (cp2 != NULL)
				*cp2 = '\0';
			gotone = find_ccache_file(cp, argp->uid, ccname);
			if (gotone != 0)
				break;
			if (cp2 != NULL)
				*cp2++ = ':';
			cp = cp2;
		} while (cp != NULL && *cp != '\0');
		if (gotone == 0) {
			result->major_status = GSS_S_CREDENTIALS_EXPIRED;
			gssd_verbose_out("gssd_acquire_cred: no cred cache"
			    " file found\n");
			return (TRUE);
		}
		setenv("KRB5CCNAME", ccname, TRUE);
	} else {
		/*
		 * If there wasn't a "-s" option or the name has
		 * been provided as an argument, do it the old way.
		 * When a name is provided, it will normally exist in the
		 * default keytab file and the uid will be root.
		 */
		if (argp->desired_name != 0 && argp->uid != 0) {
			if (debug_level == 0)
				syslog(LOG_ERR, "gss_acquire_cred:"
				    " principal_name for non-root");
			else
				fprintf(stderr, "gss_acquire_cred:"
				    " principal_name for non-root\n");
		}
		snprintf(ccname, sizeof(ccname), "FILE:/tmp/krb5cc_%d",
		    (int) argp->uid);
		setenv("KRB5CCNAME", ccname, TRUE);
	}

	result->major_status = gss_acquire_cred(&result->minor_status,
	    desired_name, argp->time_req, argp->desired_mechs,
	    argp->cred_usage, &cred, &result->actual_mechs, &result->time_rec);
	gssd_verbose_out("gssd_acquire_cred: done major=0x%x minor=%d\n",
	    (unsigned int)result->major_status, (int)result->minor_status);

	if (result->major_status == GSS_S_COMPLETE)
		result->output_cred = gssd_make_resource(cred);
	else
		result->output_cred = 0;

	return (TRUE);
}

bool_t
gssd_set_cred_option_1_svc(set_cred_option_args *argp, set_cred_option_res *result, struct svc_req *rqstp)
{
	gss_cred_id_t cred = gssd_find_resource(argp->cred);

	memset(result, 0, sizeof(*result));
	if (!cred) {
		result->major_status = GSS_S_CREDENTIALS_EXPIRED;
		gssd_verbose_out("gssd_set_cred: no credentials\n");
		return (TRUE);
	}

	result->major_status = gss_set_cred_option(&result->minor_status,
	    &cred, argp->option_name, &argp->option_value);
	gssd_verbose_out("gssd_set_cred: done major=0x%x minor=%d\n",
	    (unsigned int)result->major_status, (int)result->minor_status);

	return (TRUE);
}

bool_t
gssd_release_cred_1_svc(release_cred_args *argp, release_cred_res *result, struct svc_req *rqstp)
{
	gss_cred_id_t cred = gssd_find_resource(argp->cred);

	if (cred) {
		result->major_status = gss_release_cred(&result->minor_status,
		    &cred);
		gssd_delete_resource(argp->cred);
	} else {
		result->major_status = GSS_S_COMPLETE;
		result->minor_status = 0;
	}
	gssd_verbose_out("gssd_release_cred: done major=0x%x minor=%d\n",
	    (unsigned int)result->major_status, (int)result->minor_status);

	return (TRUE);
}

bool_t
gssd_display_status_1_svc(display_status_args *argp, display_status_res *result, struct svc_req *rqstp)
{

	result->message_context = argp->message_context;
	result->major_status = gss_display_status(&result->minor_status,
	    argp->status_value, argp->status_type, argp->mech_type,
	    &result->message_context, &result->status_string);
	gssd_verbose_out("gssd_display_status: done major=0x%x minor=%d\n",
	    (unsigned int)result->major_status, (int)result->minor_status);

	return (TRUE);
}

int
gssd_1_freeresult(SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
	/*
	 * We don't use XDR to free the results - anything which was
	 * allocated came from GSS-API. We use xdr_result to figure
	 * out what to do.
	 */
	OM_uint32 junk;

	if (xdr_result == (xdrproc_t) xdr_init_sec_context_res) {
		init_sec_context_res *p = (init_sec_context_res *) result;
		gss_release_buffer(&junk, &p->output_token);
	} else if (xdr_result == (xdrproc_t) xdr_accept_sec_context_res) {
		accept_sec_context_res *p = (accept_sec_context_res *) result;
		gss_release_buffer(&junk, &p->output_token);
	} else if (xdr_result == (xdrproc_t) xdr_delete_sec_context_res) {
		delete_sec_context_res *p = (delete_sec_context_res *) result;
		gss_release_buffer(&junk, &p->output_token);
	} else if (xdr_result == (xdrproc_t) xdr_export_sec_context_res) {
		export_sec_context_res *p = (export_sec_context_res *) result;
		if (p->interprocess_token.length)
			memset(p->interprocess_token.value, 0,
			    p->interprocess_token.length);
		gss_release_buffer(&junk, &p->interprocess_token);
	} else if (xdr_result == (xdrproc_t) xdr_export_name_res) {
		export_name_res *p = (export_name_res *) result;
		gss_release_buffer(&junk, &p->exported_name);
	} else if (xdr_result == (xdrproc_t) xdr_acquire_cred_res) {
		acquire_cred_res *p = (acquire_cred_res *) result;
		gss_release_oid_set(&junk, &p->actual_mechs);
	} else if (xdr_result == (xdrproc_t) xdr_pname_to_uid_res) {
		pname_to_uid_res *p = (pname_to_uid_res *) result;
		if (p->gidlist.gidlist_val)
			free(p->gidlist.gidlist_val);
	} else if (xdr_result == (xdrproc_t) xdr_display_status_res) {
		display_status_res *p = (display_status_res *) result;
		gss_release_buffer(&junk, &p->status_string);
	}

	return (TRUE);
}

/*
 * Search a directory for the most likely candidate to be used as the
 * credential cache for a uid. If successful, return 1 and fill the
 * file's path id into "rpath". Otherwise, return 0.
 */
static int
find_ccache_file(const char *dirpath, uid_t uid, char *rpath)
{
	DIR *dirp;
	struct dirent *dp;
	struct stat sb;
	time_t exptime, oexptime;
	int gotone, len, rating, orating;
	char namepath[PATH_MAX + 5 + 1];
	char retpath[PATH_MAX + 5 + 1];

	dirp = opendir(dirpath);
	if (dirp == NULL)
		return (0);
	gotone = 0;
	orating = 0;
	oexptime = 0;
	while ((dp = readdir(dirp)) != NULL) {
		len = snprintf(namepath, sizeof(namepath), "%s/%s", dirpath,
		    dp->d_name);
		if (len < sizeof(namepath) &&
		    (hostbased_initiator_cred == 0 || strcmp(namepath,
		     GSSD_CREDENTIAL_CACHE_FILE) != 0) &&
		    strstr(dp->d_name, ccfile_substring) != NULL &&
		    lstat(namepath, &sb) >= 0 &&
		    sb.st_uid == uid &&
		    S_ISREG(sb.st_mode)) {
			len = snprintf(namepath, sizeof(namepath), "FILE:%s/%s",
			    dirpath, dp->d_name);
			if (len < sizeof(namepath) &&
			    is_a_valid_tgt_cache(namepath, uid, &rating,
			    &exptime) != 0) {
				if (gotone == 0 || rating > orating ||
				    (rating == orating && exptime > oexptime)) {
					orating = rating;
					oexptime = exptime;
					strcpy(retpath, namepath);
					gotone = 1;
				}
			}
		}
	}
	closedir(dirp);
	if (gotone != 0) {
		strcpy(rpath, retpath);
		return (1);
	}
	return (0);
}

/*
 * Try to determine if the file is a valid tgt cache file.
 * Check that the file has a valid tgt for a principal.
 * If it does, return 1, otherwise return 0.
 * It also returns a "rating" and the expiry time for the TGT, when found.
 * This "rating" is higher based on heuristics that make it more
 * likely to be the correct credential cache file to use. It can
 * be used by the caller, along with expiry time, to select from
 * multiple credential cache files.
 */
static int
is_a_valid_tgt_cache(const char *filepath, uid_t uid, int *retrating,
    time_t *retexptime)
{
#ifndef WITHOUT_KERBEROS
	krb5_context context;
	krb5_principal princ;
	krb5_ccache ccache;
	krb5_error_code retval;
	krb5_cc_cursor curse;
	krb5_creds krbcred;
	int gotone, orating, rating, ret;
	struct passwd *pw;
	char *cp, *cp2, *pname;
	time_t exptime;

	/* Find a likely name for the uid principal. */
	pw = getpwuid(uid);

	/*
	 * Do a bunch of krb5 library stuff to try and determine if
	 * this file is a credentials cache with an appropriate TGT
	 * in it.
	 */
	retval = krb5_init_context(&context);
	if (retval != 0)
		return (0);
	retval = krb5_cc_resolve(context, filepath, &ccache);
	if (retval != 0) {
		krb5_free_context(context);
		return (0);
	}
	ret = 0;
	orating = 0;
	exptime = 0;
	retval = krb5_cc_start_seq_get(context, ccache, &curse);
	if (retval == 0) {
		while ((retval = krb5_cc_next_cred(context, ccache, &curse,
		    &krbcred)) == 0) {
			gotone = 0;
			rating = 0;
			retval = krb5_unparse_name(context, krbcred.server,
			    &pname);
			if (retval == 0) {
				cp = strchr(pname, '/');
				if (cp != NULL) {
					*cp++ = '\0';
					if (strcmp(pname, "krbtgt") == 0 &&
					    krbcred.times.endtime > time(NULL)
					    ) {
						gotone = 1;
						/*
						 * Test to see if this is a
						 * tgt for cross-realm auth.
						 * Rate it higher, if it is not.
						 */
						cp2 = strchr(cp, '@');
						if (cp2 != NULL) {
							*cp2++ = '\0';
							if (strcmp(cp, cp2) ==
							    0)
								rating++;
						}
					}
				}
				free(pname);
			}
			if (gotone != 0) {
				retval = krb5_unparse_name(context,
				    krbcred.client, &pname);
				if (retval == 0) {
					cp = strchr(pname, '@');
					if (cp != NULL) {
						*cp++ = '\0';
						if (pw != NULL && strcmp(pname,
						    pw->pw_name) == 0)
							rating++;
						if (strchr(pname, '/') == NULL)
							rating++;
						if (pref_realm[0] != '\0' &&
						    strcmp(cp, pref_realm) == 0)
							rating++;
					}
				}
				free(pname);
				if (rating > orating) {
					orating = rating;
					exptime = krbcred.times.endtime;
				} else if (rating == orating &&
				    krbcred.times.endtime > exptime)
					exptime = krbcred.times.endtime;
				ret = 1;
			}
			krb5_free_cred_contents(context, &krbcred);
		}
		krb5_cc_end_seq_get(context, ccache, &curse);
	}
	krb5_cc_close(context, ccache);
	krb5_free_context(context);
	if (ret != 0) {
		*retrating = orating;
		*retexptime = exptime;
	}
	return (ret);
#else /* WITHOUT_KERBEROS */
	return (0);
#endif /* !WITHOUT_KERBEROS */
}

#ifndef WITHOUT_KERBEROS
/*
 * This function attempts to do essentially a "kinit -k" for the principal
 * name provided as the argument, so that there will be a TGT in the
 * credential cache.
 */
static krb5_error_code
gssd_get_cc_from_keytab(const char *name)
{
	krb5_error_code ret, opt_ret, princ_ret, cc_ret, kt_ret, cred_ret;
	krb5_context context;
	krb5_principal principal;
	krb5_keytab kt;
	krb5_creds cred;
	krb5_get_init_creds_opt *opt;
	krb5_deltat start_time = 0;
	krb5_ccache ccache;

	ret = krb5_init_context(&context);
	if (ret != 0)
		return (ret);
	opt_ret = cc_ret = kt_ret = cred_ret = 1;	/* anything non-zero */
	princ_ret = ret = krb5_parse_name(context, name, &principal);
	if (ret == 0)
		opt_ret = ret = krb5_get_init_creds_opt_alloc(context, &opt);
	if (ret == 0)
		cc_ret = ret = krb5_cc_default(context, &ccache);
	if (ret == 0)
		ret = krb5_cc_initialize(context, ccache, principal);
	if (ret == 0) {
		krb5_get_init_creds_opt_set_default_flags(context, "gssd",
		    krb5_principal_get_realm(context, principal), opt);
		kt_ret = ret = krb5_kt_default(context, &kt);
	}
	if (ret == 0)
		cred_ret = ret = krb5_get_init_creds_keytab(context, &cred,
		    principal, kt, start_time, NULL, opt);
	if (ret == 0)
		ret = krb5_cc_store_cred(context, ccache, &cred);
	if (kt_ret == 0)
		krb5_kt_close(context, kt);
	if (cc_ret == 0)
		krb5_cc_close(context, ccache);
	if (opt_ret == 0)
		krb5_get_init_creds_opt_free(context, opt);
	if (princ_ret == 0)
		krb5_free_principal(context, principal);
	if (cred_ret == 0)
		krb5_free_cred_contents(context, &cred);
	krb5_free_context(context);
	return (ret);
}

/*
 * Acquire a gss credential for a uid.
 */
static OM_uint32
gssd_get_user_cred(OM_uint32 *min_statp, uid_t uid, gss_cred_id_t *credp)
{
	gss_buffer_desc principal_desc;
	gss_name_t name;
	OM_uint32 maj_stat, min_stat;
	gss_OID_set mechlist;
	struct passwd *pw;

	pw = getpwuid(uid);
	if (pw == NULL) {
		*min_statp = 0;
		return (GSS_S_FAILURE);
	}

	/*
	 * The mechanism must be set to KerberosV for acquisition
	 * of credentials to work reliably.
	 */
	maj_stat = gss_create_empty_oid_set(min_statp, &mechlist);
	if (maj_stat != GSS_S_COMPLETE)
		return (maj_stat);
	maj_stat = gss_add_oid_set_member(min_statp, GSS_KRB5_MECH_OID_X,
	    &mechlist);
	if (maj_stat != GSS_S_COMPLETE) {
		gss_release_oid_set(&min_stat, &mechlist);
		return (maj_stat);
	}

	principal_desc.value = (void *)pw->pw_name;
	principal_desc.length = strlen(pw->pw_name);
	maj_stat = gss_import_name(min_statp, &principal_desc,
	    GSS_C_NT_USER_NAME, &name);
	if (maj_stat != GSS_S_COMPLETE) {
		gss_release_oid_set(&min_stat, &mechlist);
		return (maj_stat);
	}
	/* Acquire the credentials. */
	maj_stat = gss_acquire_cred(min_statp, name, 0, mechlist,
	    GSS_C_INITIATE, credp, NULL, NULL);
	gss_release_name(&min_stat, &name);
	gss_release_oid_set(&min_stat, &mechlist);
	return (maj_stat);
}
#endif /* !WITHOUT_KERBEROS */

void gssd_terminate(int sig __unused)
{

#ifndef WITHOUT_KERBEROS
	if (hostbased_initiator_cred != 0)
		unlink(GSSD_CREDENTIAL_CACHE_FILE);
#endif
	gssd_syscall("");
	exit(0);
}

