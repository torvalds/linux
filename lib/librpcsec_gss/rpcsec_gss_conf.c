/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Doug Rabson
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
 *	$FreeBSD$
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/queue.h>
#include <rpc/rpc.h>
#include <rpc/rpcsec_gss.h>

#include "rpcsec_gss_int.h"

#ifndef _PATH_GSS_MECH
#define _PATH_GSS_MECH	"/etc/gss/mech"
#endif

#ifndef _PATH_GSS_QOP
#define _PATH_GSS_QOP	"/etc/gss/qop"
#endif

struct mech_info {
	SLIST_ENTRY(mech_info) link;
	char		*name;
	gss_OID_desc	oid;
	const char	**qops;
	char		*lib;
	char		*kobj;
};
SLIST_HEAD(mech_info_list, mech_info);

static struct mech_info_list mechs = SLIST_HEAD_INITIALIZER(mechs);
static const char **mech_names;

struct qop_info {
	SLIST_ENTRY(qop_info) link;
	char		*name;
	char*		mech;
	u_int		qop;
};
SLIST_HEAD(qop_info_list, qop_info);

static struct qop_info_list qops = SLIST_HEAD_INITIALIZER(qops);

static int
_rpc_gss_string_to_oid(const char* s, gss_OID oid)
{
	int			number_count, i, j;
	int			byte_count;
	const char		*p, *q;
	char			*res;

	/*
	 * First figure out how many numbers in the oid, then
	 * calculate the compiled oid size.
	 */
	number_count = 0;
	for (p = s; p; p = q) {
		q = strchr(p, '.');
		if (q) q = q + 1;
		number_count++;
	}
	
	/*
	 * The first two numbers are in the first byte and each
	 * subsequent number is encoded in a variable byte sequence.
	 */
	if (number_count < 2)
		return (EINVAL);

	/*
	 * We do this in two passes. The first pass, we just figure
	 * out the size. Second time around, we actually encode the
	 * number.
	 */
	res = 0;
	for (i = 0; i < 2; i++) {
		byte_count = 0;
		for (p = s, j = 0; p; p = q, j++) {
			u_int number = 0;

			/*
			 * Find the end of this number.
			 */
			q = strchr(p, '.');
			if (q) q = q + 1;

			/*
			 * Read the number of of the string. Don't
			 * bother with anything except base ten.
			 */
			while (*p && *p != '.') {
				number = 10 * number + (*p - '0');
				p++;
			}

			/*
			 * Encode the number. The first two numbers
			 * are packed into the first byte. Subsequent
			 * numbers are encoded in bytes seven bits at
			 * a time with the last byte having the high
			 * bit set.
			 */
			if (j == 0) {
				if (res)
					*res = number * 40;
			} else if (j == 1) {
				if (res) {
					*res += number;
					res++;
				}
				byte_count++;
			} else if (j >= 2) {
				/*
				 * The number is encoded in seven bit chunks.
				 */
				u_int t;
				int bytes;

				bytes = 0;
				for (t = number; t; t >>= 7)
					bytes++;
				if (bytes == 0) bytes = 1;
				while (bytes) {
					if (res) {
						int bit = 7*(bytes-1);
						
						*res = (number >> bit) & 0x7f;
						if (bytes != 1)
							*res |= 0x80;
						res++;
					}
					byte_count++;
					bytes--;
				}
			}
		}
		if (!res) {
			res = malloc(byte_count);
			if (!res)
				return (ENOMEM);
			oid->length = byte_count;
			oid->elements = res;
		}
	}

	return (0);
}

static void
_rpc_gss_load_mech(void)
{
	FILE		*fp;
	char		buf[256];
	char		*p;
	char		*name, *oid, *lib, *kobj;
	struct mech_info *info;
	int		count;
	const char	**pp;

	if (SLIST_FIRST(&mechs))
		return;

	fp = fopen(_PATH_GSS_MECH, "r");
	if (!fp)
		return;

	count = 0;
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

		info = malloc(sizeof(struct mech_info));
		if (!info)
			break;
		if (_rpc_gss_string_to_oid(oid, &info->oid)) {
			free(info);
			continue;
		}
		info->name = strdup(name);
		info->qops = NULL;
		info->lib = strdup(lib);
		info->kobj = strdup(kobj);
		SLIST_INSERT_HEAD(&mechs, info, link);
		count++;
	}
	fclose(fp);

	mech_names = malloc((count + 1) * sizeof(char*));
	pp = mech_names;
	SLIST_FOREACH(info, &mechs, link) {
		*pp++ = info->name;
	}
	*pp = NULL;
}

static void
_rpc_gss_load_qop(void)
{
	FILE		*fp;
	char		buf[256];
	char		*p;
	char		*name, *num, *mech;
	struct mech_info *minfo;
	struct qop_info *info;
	int		count;
	const char	**mech_qops;
	const char	**pp;

	if (SLIST_FIRST(&qops))
		return;

	fp = fopen(_PATH_GSS_QOP, "r");
	if (!fp)
		return;

	while (fgets(buf, sizeof(buf), fp)) {
		if (*buf == '#')
			continue;
		p = buf;
		name = strsep(&p, "\t\n ");
		if (p) while (isspace(*p)) p++;
		num = strsep(&p, "\t\n ");
		if (p) while (isspace(*p)) p++;
		mech = strsep(&p, "\t\n ");
		if (!name || !num || !mech)
			continue;

		info = malloc(sizeof(struct qop_info));
		if (!info)
			break;
		info->name = strdup(name);
		info->qop = strtoul(name, 0, 0);
		info->mech = strdup(mech);
		SLIST_INSERT_HEAD(&qops, info, link);
	}
	fclose(fp);

	/*
	 * Compile lists of qops for each mechanism.
	 */
	SLIST_FOREACH(minfo, &mechs, link) {
		count = 0;
		SLIST_FOREACH(info, &qops, link) {
			if (strcmp(info->mech, minfo->name) == 0)
				count++;
		}
		mech_qops = malloc((count + 1) * sizeof(char*));
		pp = mech_qops;
		SLIST_FOREACH(info, &qops, link) {
			if (strcmp(info->mech, minfo->name) == 0)
				*pp++ = info->name;
		}
		*pp = NULL;
		minfo->qops = mech_qops;
	}
}

bool_t
rpc_gss_mech_to_oid(const char *mech, gss_OID *oid_ret)
{
	struct mech_info *info;

	_rpc_gss_load_mech();
	SLIST_FOREACH(info, &mechs, link) {
		if (!strcmp(info->name, mech)) {
			*oid_ret = &info->oid;
			return (TRUE);
		}
	}
	_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOENT);
	return (FALSE);
}

bool_t
rpc_gss_oid_to_mech(gss_OID oid, const char **mech_ret)
{
	struct mech_info *info;

	_rpc_gss_load_mech();
	SLIST_FOREACH(info, &mechs, link) {
		if (oid->length == info->oid.length
		    && !memcmp(oid->elements, info->oid.elements,
			oid->length)) {
			*mech_ret = info->name;
			return (TRUE);
		}
	}
	_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOENT);
	return (FALSE);
}

bool_t
rpc_gss_qop_to_num(const char *qop, const char *mech, u_int *num_ret)
{
	struct qop_info *info;

	_rpc_gss_load_qop();
	SLIST_FOREACH(info, &qops, link) {
		if (strcmp(info->name, qop) == 0
		    && strcmp(info->mech, mech) == 0) {
			*num_ret = info->qop;
			return (TRUE);
		}
	}
	_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOENT);
	return (FALSE);
}

const char *
_rpc_gss_num_to_qop(const char *mech, u_int num)
{
	struct qop_info *info;

	if (num == GSS_C_QOP_DEFAULT)
		return "default";

	_rpc_gss_load_qop();
	SLIST_FOREACH(info, &qops, link) {
		if (info->qop == num && strcmp(info->mech, mech) == 0) {
			return (info->name);
		}
	}
	return (NULL);
}

const char **
rpc_gss_get_mechanisms(void)
{

	_rpc_gss_load_mech();
	return (mech_names);
}

const char **
rpc_gss_get_mech_info(const char *mech, rpc_gss_service_t *service)
{
	struct mech_info *info;

	_rpc_gss_load_mech();
	_rpc_gss_load_qop();
	SLIST_FOREACH(info, &mechs, link) {
		if (!strcmp(mech, info->name)) {
			/*
			 * I'm not sure what to do with service
			 * here. The Solaris manpages are not clear on
			 * the subject and the OpenSolaris code just
			 * sets it to rpc_gss_svc_privacy
			 * unconditionally with a comment noting that
			 * it is bogus.
			 */
			*service = rpc_gss_svc_privacy;
			return info->qops;
		}
	}

	_rpc_gss_set_error(RPC_GSS_ER_SYSTEMERROR, ENOENT);
	return (NULL);
}

bool_t
rpc_gss_get_versions(u_int *vers_hi, u_int *vers_lo)
{

	*vers_hi = 1;
	*vers_lo = 1;
	return (TRUE);
}

bool_t
rpc_gss_is_installed(const char *mech)
{
	struct mech_info *info;

	_rpc_gss_load_mech();
	SLIST_FOREACH(info, &mechs, link)
		if (!strcmp(mech, info->name))
			return (TRUE);
	return (FALSE);
}

