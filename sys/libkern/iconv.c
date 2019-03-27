/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/iconv.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/sx.h>
#include <sys/syslog.h>

#include "iconv_converter_if.h"

SYSCTL_DECL(_kern_iconv);

SYSCTL_NODE(_kern, OID_AUTO, iconv, CTLFLAG_RW, NULL, "kernel iconv interface");

MALLOC_DEFINE(M_ICONV, "iconv", "ICONV structures");
static MALLOC_DEFINE(M_ICONVDATA, "iconv_data", "ICONV data");

MODULE_VERSION(libiconv, 2);

static struct sx iconv_lock;

#ifdef notnow
/*
 * iconv converter instance
 */
struct iconv_converter {
	KOBJ_FIELDS;
	void *			c_data;
};
#endif

struct sysctl_oid *iconv_oid_hook = &sysctl___kern_iconv;

/*
 * List of loaded converters
 */
static TAILQ_HEAD(iconv_converter_list, iconv_converter_class)
    iconv_converters = TAILQ_HEAD_INITIALIZER(iconv_converters);

/*
 * List of supported/loaded charsets pairs
 */
static TAILQ_HEAD(, iconv_cspair)
    iconv_cslist = TAILQ_HEAD_INITIALIZER(iconv_cslist);
static int iconv_csid = 1;

static char iconv_unicode_string[] = "unicode";	/* save eight bytes when possible */

static void iconv_unregister_cspair(struct iconv_cspair *csp);

static int
iconv_mod_unload(void)
{
	struct iconv_cspair *csp;

	sx_xlock(&iconv_lock);
	TAILQ_FOREACH(csp, &iconv_cslist, cp_link) {
		if (csp->cp_refcount) {
			sx_xunlock(&iconv_lock);
			return EBUSY;
		}
	}

	while ((csp = TAILQ_FIRST(&iconv_cslist)) != NULL)
		iconv_unregister_cspair(csp);
	sx_xunlock(&iconv_lock);
	sx_destroy(&iconv_lock);
	return 0;
}

static int
iconv_mod_handler(module_t mod, int type, void *data)
{
	int error;

	switch (type) {
	    case MOD_LOAD:
		error = 0;
		sx_init(&iconv_lock, "iconv");
		break;
	    case MOD_UNLOAD:
		error = iconv_mod_unload();
		break;
	    default:
		error = EINVAL;
	}
	return error;
}

static moduledata_t iconv_mod = {
	"iconv", iconv_mod_handler, NULL
};

DECLARE_MODULE(iconv, iconv_mod, SI_SUB_DRIVERS, SI_ORDER_SECOND);

static int
iconv_register_converter(struct iconv_converter_class *dcp)
{
	kobj_class_compile((struct kobj_class*)dcp);
	dcp->refs++;
	TAILQ_INSERT_TAIL(&iconv_converters, dcp, cc_link);
	return 0;
}

static int
iconv_unregister_converter(struct iconv_converter_class *dcp)
{
	dcp->refs--;
	if (dcp->refs > 1) {
		ICDEBUG("converter has %d references left\n", dcp->refs);
		return EBUSY;
	}
	TAILQ_REMOVE(&iconv_converters, dcp, cc_link);
	kobj_class_free((struct kobj_class*)dcp);
	return 0;
}

static int
iconv_lookupconv(const char *name, struct iconv_converter_class **dcpp)
{
	struct iconv_converter_class *dcp;

	TAILQ_FOREACH(dcp, &iconv_converters, cc_link) {
		if (name == NULL)
			continue;
		if (strcmp(name, ICONV_CONVERTER_NAME(dcp)) == 0) {
			if (dcpp)
				*dcpp = dcp;
			return 0;
		}
	}
	return ENOENT;
}

static int
iconv_lookupcs(const char *to, const char *from, struct iconv_cspair **cspp)
{
	struct iconv_cspair *csp;

	TAILQ_FOREACH(csp, &iconv_cslist, cp_link) {
		if (strcasecmp(csp->cp_to, to) == 0 &&
		    strcasecmp(csp->cp_from, from) == 0) {
			if (cspp)
				*cspp = csp;
			return 0;
		}
	}
	return ENOENT;
}

static int
iconv_register_cspair(const char *to, const char *from,
	struct iconv_converter_class *dcp, void *data,
	struct iconv_cspair **cspp)
{
	struct iconv_cspair *csp;
	char *cp;
	int csize, ucsto, ucsfrom;

	if (iconv_lookupcs(to, from, NULL) == 0)
		return EEXIST;
	csize = sizeof(*csp);
	ucsto = strcmp(to, iconv_unicode_string) == 0;
	if (!ucsto)
		csize += strlen(to) + 1;
	ucsfrom = strcmp(from, iconv_unicode_string) == 0;
	if (!ucsfrom)
		csize += strlen(from) + 1;
	csp = malloc(csize, M_ICONV, M_WAITOK);
	bzero(csp, csize);
	csp->cp_id = iconv_csid++;
	csp->cp_dcp = dcp;
	cp = (char*)(csp + 1);
	if (!ucsto) {
		strcpy(cp, to);
		csp->cp_to = cp;
		cp += strlen(cp) + 1;
	} else
		csp->cp_to = iconv_unicode_string;
	if (!ucsfrom) {
		strcpy(cp, from);
		csp->cp_from = cp;
	} else
		csp->cp_from = iconv_unicode_string;
	csp->cp_data = data;

	TAILQ_INSERT_TAIL(&iconv_cslist, csp, cp_link);
	*cspp = csp;
	return 0;
}

static void
iconv_unregister_cspair(struct iconv_cspair *csp)
{
	TAILQ_REMOVE(&iconv_cslist, csp, cp_link);
	if (csp->cp_data)
		free(csp->cp_data, M_ICONVDATA);
	free(csp, M_ICONV);
}

/*
 * Lookup and create an instance of converter.
 * Currently this layer didn't have associated 'instance' structure
 * to avoid unnesessary memory allocation.
 */
int
iconv_open(const char *to, const char *from, void **handle)
{
	struct iconv_cspair *csp, *cspfrom, *cspto;
	struct iconv_converter_class *dcp;
	const char *cnvname;
	int error;

	/*
	 * First, lookup fully qualified cspairs
	 */
	error = iconv_lookupcs(to, from, &csp);
	if (error == 0)
		return ICONV_CONVERTER_OPEN(csp->cp_dcp, csp, NULL, handle);

	/*
	 * Well, nothing found. Now try to construct a composite conversion
	 * ToDo: add a 'capability' field to converter
	 */
	TAILQ_FOREACH(dcp, &iconv_converters, cc_link) {
		cnvname = ICONV_CONVERTER_NAME(dcp);
		if (cnvname == NULL)
			continue;
		error = iconv_lookupcs(cnvname, from, &cspfrom);
		if (error)
			continue;
		error = iconv_lookupcs(to, cnvname, &cspto);
		if (error)
			continue;
		/*
		 * Fine, we're found a pair which can be combined together
		 */
		return ICONV_CONVERTER_OPEN(dcp, cspto, cspfrom, handle);
	}
	return ENOENT;
}

int
iconv_close(void *handle)
{
	return ICONV_CONVERTER_CLOSE(handle);
}

int
iconv_conv(void *handle, const char **inbuf,
	size_t *inbytesleft, char **outbuf, size_t *outbytesleft)
{
	return ICONV_CONVERTER_CONV(handle, inbuf, inbytesleft, outbuf, outbytesleft, 0, 0);
}

int
iconv_conv_case(void *handle, const char **inbuf,
	size_t *inbytesleft, char **outbuf, size_t *outbytesleft, int casetype)
{
	return ICONV_CONVERTER_CONV(handle, inbuf, inbytesleft, outbuf, outbytesleft, 0, casetype);
}

int
iconv_convchr(void *handle, const char **inbuf,
	size_t *inbytesleft, char **outbuf, size_t *outbytesleft)
{
	return ICONV_CONVERTER_CONV(handle, inbuf, inbytesleft, outbuf, outbytesleft, 1, 0);
}

int
iconv_convchr_case(void *handle, const char **inbuf,
	size_t *inbytesleft, char **outbuf, size_t *outbytesleft, int casetype)
{
	return ICONV_CONVERTER_CONV(handle, inbuf, inbytesleft, outbuf, outbytesleft, 1, casetype);
}

int
towlower(int c, void *handle)
{
	return ICONV_CONVERTER_TOLOWER(handle, c);
}

int
towupper(int c, void *handle)
{
	return ICONV_CONVERTER_TOUPPER(handle, c);
}

/*
 * Give a list of loaded converters. Each name terminated with 0.
 * An empty string terminates the list.
 */
static int
iconv_sysctl_drvlist(SYSCTL_HANDLER_ARGS)
{
	struct iconv_converter_class *dcp;
	const char *name;
	char spc;
	int error;

	error = 0;
	sx_slock(&iconv_lock);
	TAILQ_FOREACH(dcp, &iconv_converters, cc_link) {
		name = ICONV_CONVERTER_NAME(dcp);
		if (name == NULL)
			continue;
		error = SYSCTL_OUT(req, name, strlen(name) + 1);
		if (error)
			break;
	}
	sx_sunlock(&iconv_lock);
	if (error)
		return error;
	spc = 0;
	error = SYSCTL_OUT(req, &spc, sizeof(spc));
	return error;
}

SYSCTL_PROC(_kern_iconv, OID_AUTO, drvlist, CTLFLAG_RD | CTLTYPE_OPAQUE,
	    NULL, 0, iconv_sysctl_drvlist, "S,xlat", "registered converters");

/*
 * List all available charset pairs.
 */
static int
iconv_sysctl_cslist(SYSCTL_HANDLER_ARGS)
{
	struct iconv_cspair *csp;
	struct iconv_cspair_info csi;
	int error;

	error = 0;
	bzero(&csi, sizeof(csi));
	csi.cs_version = ICONV_CSPAIR_INFO_VER;
	sx_slock(&iconv_lock);
	TAILQ_FOREACH(csp, &iconv_cslist, cp_link) {
		csi.cs_id = csp->cp_id;
		csi.cs_refcount = csp->cp_refcount;
		csi.cs_base = csp->cp_base ? csp->cp_base->cp_id : 0;
		strcpy(csi.cs_to, csp->cp_to);
		strcpy(csi.cs_from, csp->cp_from);
		error = SYSCTL_OUT(req, &csi, sizeof(csi));
		if (error)
			break;
	}
	sx_sunlock(&iconv_lock);
	return error;
}

SYSCTL_PROC(_kern_iconv, OID_AUTO, cslist, CTLFLAG_RD | CTLTYPE_OPAQUE,
	    NULL, 0, iconv_sysctl_cslist, "S,xlat", "registered charset pairs");

int
iconv_add(const char *converter, const char *to, const char *from)
{
	struct iconv_converter_class *dcp;
	struct iconv_cspair *csp;

	if (iconv_lookupconv(converter, &dcp) != 0)
		return EINVAL;

	return iconv_register_cspair(to, from, dcp, NULL, &csp);
}

/*
 * Add new charset pair
 */
static int
iconv_sysctl_add(SYSCTL_HANDLER_ARGS)
{
	struct iconv_converter_class *dcp;
	struct iconv_cspair *csp;
	struct iconv_add_in din;
	struct iconv_add_out dout;
	int error;

	error = SYSCTL_IN(req, &din, sizeof(din));
	if (error)
		return error;
	if (din.ia_version != ICONV_ADD_VER)
		return EINVAL;
	if (din.ia_datalen > ICONV_CSMAXDATALEN)
		return EINVAL;
	if (strnlen(din.ia_from, sizeof(din.ia_from)) >= ICONV_CSNMAXLEN)
		return EINVAL;
	if (strnlen(din.ia_to, sizeof(din.ia_to)) >= ICONV_CSNMAXLEN)
		return EINVAL;
	if (strnlen(din.ia_converter, sizeof(din.ia_converter)) >= ICONV_CNVNMAXLEN)
		return EINVAL;
	if (iconv_lookupconv(din.ia_converter, &dcp) != 0)
		return EINVAL;
	sx_xlock(&iconv_lock);
	error = iconv_register_cspair(din.ia_to, din.ia_from, dcp, NULL, &csp);
	if (error) {
		sx_xunlock(&iconv_lock);
		return error;
	}
	if (din.ia_datalen) {
		csp->cp_data = malloc(din.ia_datalen, M_ICONVDATA, M_WAITOK);
		error = copyin(din.ia_data, csp->cp_data, din.ia_datalen);
		if (error)
			goto bad;
	}
	dout.ia_csid = csp->cp_id;
	error = SYSCTL_OUT(req, &dout, sizeof(dout));
	if (error)
		goto bad;
	sx_xunlock(&iconv_lock);
	ICDEBUG("%s => %s, %d bytes\n",din.ia_from, din.ia_to, din.ia_datalen);
	return 0;
bad:
	iconv_unregister_cspair(csp);
	sx_xunlock(&iconv_lock);
	return error;
}

SYSCTL_PROC(_kern_iconv, OID_AUTO, add, CTLFLAG_RW | CTLTYPE_OPAQUE,
	    NULL, 0, iconv_sysctl_add, "S,xlat", "register charset pair");

/*
 * Default stubs for converters
 */
int
iconv_converter_initstub(struct iconv_converter_class *dp)
{
	return 0;
}

int
iconv_converter_donestub(struct iconv_converter_class *dp)
{
	return 0;
}

int
iconv_converter_tolowerstub(int c, void *handle)
{
	return (c);
}

int
iconv_converter_handler(module_t mod, int type, void *data)
{
	struct iconv_converter_class *dcp = data;
	int error;

	switch (type) {
	    case MOD_LOAD:
		sx_xlock(&iconv_lock);
		error = iconv_register_converter(dcp);
		if (error) {
			sx_xunlock(&iconv_lock);
			break;
		}
		error = ICONV_CONVERTER_INIT(dcp);
		if (error)
			iconv_unregister_converter(dcp);
		sx_xunlock(&iconv_lock);
		break;
	    case MOD_UNLOAD:
		sx_xlock(&iconv_lock);
		ICONV_CONVERTER_DONE(dcp);
		error = iconv_unregister_converter(dcp);
		sx_xunlock(&iconv_lock);
		break;
	    default:
		error = EINVAL;
	}
	return error;
}

/*
 * Common used functions (don't use with unicode)
 */
char *
iconv_convstr(void *handle, char *dst, const char *src)
{
	char *p = dst;
	size_t inlen, outlen;
	int error;

	if (handle == NULL) {
		strcpy(dst, src);
		return dst;
	}
	inlen = outlen = strlen(src);
	error = iconv_conv(handle, NULL, NULL, &p, &outlen);
	if (error)
		return NULL;
	error = iconv_conv(handle, &src, &inlen, &p, &outlen);
	if (error)
		return NULL;
	*p = 0;
	return dst;
}

void *
iconv_convmem(void *handle, void *dst, const void *src, int size)
{
	const char *s = src;
	char *d = dst;
	size_t inlen, outlen;
	int error;

	if (size == 0)
		return dst;
	if (handle == NULL) {
		memcpy(dst, src, size);
		return dst;
	}
	inlen = outlen = size;
	error = iconv_conv(handle, NULL, NULL, &d, &outlen);
	if (error)
		return NULL;
	error = iconv_conv(handle, &s, &inlen, &d, &outlen);
	if (error)
		return NULL;
	return dst;
}

int
iconv_lookupcp(char **cpp, const char *s)
{
	if (cpp == NULL) {
		ICDEBUG("warning a NULL list passed\n", "");
		return ENOENT;
	}
	for (; *cpp; cpp++)
		if (strcmp(*cpp, s) == 0)
			return 0;
	return ENOENT;
}

/*
 * Return if fsname is in use of not
 */
int
iconv_vfs_refcount(const char *fsname)
{
	struct vfsconf *vfsp;

	vfsp = vfs_byname(fsname);
	if (vfsp != NULL && vfsp->vfc_refcount > 0)
		return (EBUSY);
	return (0);
}
