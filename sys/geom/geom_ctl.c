/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Poul-Henning Kamp
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Poul-Henning Kamp
 * and NAI Labs, the Security Research Division of Network Associates, Inc.
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
 * 3. The names of the authors may not be used to endorse or promote
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_geom.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/bio.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/sbuf.h>

#include <sys/lock.h>
#include <sys/mutex.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <geom/geom.h>
#include <geom/geom_int.h>
#define GCTL_TABLE 1
#include <geom/geom_ctl.h>

#include <machine/stdarg.h>

static d_ioctl_t g_ctl_ioctl;

static struct cdevsw g_ctl_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_ioctl =	g_ctl_ioctl,
	.d_name =	"g_ctl",
};

void
g_ctl_init(void)
{

	make_dev_credf(MAKEDEV_ETERNAL, &g_ctl_cdevsw, 0, NULL,
	    UID_ROOT, GID_OPERATOR, 0640, PATH_GEOM_CTL);
	KASSERT(GCTL_PARAM_RD == VM_PROT_READ,
		("GCTL_PARAM_RD != VM_PROT_READ"));
	KASSERT(GCTL_PARAM_WR == VM_PROT_WRITE,
		("GCTL_PARAM_WR != VM_PROT_WRITE"));
}

/*
 * Report an error back to the user in ascii format.  Return nerror
 * or EINVAL if nerror isn't specified.
 */
int
gctl_error(struct gctl_req *req, const char *fmt, ...)
{
	va_list ap;

	if (req == NULL)
		return (EINVAL);

	/* We only record the first error */
	if (sbuf_done(req->serror)) {
		if (!req->nerror)
			req->nerror = EEXIST;
		return (req->nerror);
	}
	if (!req->nerror)
		req->nerror = EINVAL;

	va_start(ap, fmt);
	sbuf_vprintf(req->serror, fmt, ap);
	va_end(ap);
	sbuf_finish(req->serror);
	if (g_debugflags & G_F_CTLDUMP)
		printf("gctl %p error \"%s\"\n", req, sbuf_data(req->serror));
	return (req->nerror);
}

/*
 * Allocate space and copyin() something.
 * XXX: this should really be a standard function in the kernel.
 */
static void *
geom_alloc_copyin(struct gctl_req *req, void *uaddr, size_t len)
{
	void *ptr;

	ptr = g_malloc(len, M_WAITOK);
	req->nerror = copyin(uaddr, ptr, len);
	if (!req->nerror)
		return (ptr);
	g_free(ptr);
	return (NULL);
}

static void
gctl_copyin(struct gctl_req *req)
{
	struct gctl_req_arg *ap;
	char *p;
	u_int i;

	if (req->narg > GEOM_CTL_ARG_MAX) {
		gctl_error(req, "too many arguments");
		req->arg = NULL;
		return;
	}

	ap = geom_alloc_copyin(req, req->arg, req->narg * sizeof(*ap));
	if (ap == NULL) {
		gctl_error(req, "bad control request");
		req->arg = NULL;
		return;
	}

	/* Nothing have been copyin()'ed yet */
	for (i = 0; i < req->narg; i++) {
		ap[i].flag &= ~(GCTL_PARAM_NAMEKERNEL|GCTL_PARAM_VALUEKERNEL);
		ap[i].flag &= ~GCTL_PARAM_CHANGED;
		ap[i].kvalue = NULL;
	}

	for (i = 0; i < req->narg; i++) {
		if (ap[i].nlen < 1 || ap[i].nlen > SPECNAMELEN) {
			gctl_error(req,
			    "wrong param name length %d: %d", i, ap[i].nlen);
			break;
		}
		p = geom_alloc_copyin(req, ap[i].name, ap[i].nlen);
		if (p == NULL)
			break;
		if (p[ap[i].nlen - 1] != '\0') {
			gctl_error(req, "unterminated param name");
			g_free(p);
			break;
		}
		ap[i].name = p;
		ap[i].flag |= GCTL_PARAM_NAMEKERNEL;
		if (ap[i].len <= 0) {
			gctl_error(req, "negative param length");
			break;
		}
		p = geom_alloc_copyin(req, ap[i].value, ap[i].len);
		if (p == NULL)
			break;
		if ((ap[i].flag & GCTL_PARAM_ASCII) &&
		    p[ap[i].len - 1] != '\0') {
			gctl_error(req, "unterminated param value");
			g_free(p);
			break;
		}
		ap[i].kvalue = p;
		ap[i].flag |= GCTL_PARAM_VALUEKERNEL;
	}
	req->arg = ap;
	return;
}

static void
gctl_copyout(struct gctl_req *req)
{
	int error, i;
	struct gctl_req_arg *ap;

	if (req->nerror)
		return;
	error = 0;
	ap = req->arg;
	for (i = 0; i < req->narg; i++, ap++) {
		if (!(ap->flag & GCTL_PARAM_CHANGED))
			continue;
		error = copyout(ap->kvalue, ap->value, ap->len);
		if (!error)
			continue;
		req->nerror = error;
		return;
	}
	return;
}

static void
gctl_free(struct gctl_req *req)
{
	u_int i;

	sbuf_delete(req->serror);
	if (req->arg == NULL)
		return;
	for (i = 0; i < req->narg; i++) {
		if (req->arg[i].flag & GCTL_PARAM_NAMEKERNEL)
			g_free(req->arg[i].name);
		if ((req->arg[i].flag & GCTL_PARAM_VALUEKERNEL) &&
		    req->arg[i].len > 0)
			g_free(req->arg[i].kvalue);
	}
	g_free(req->arg);
}

static void
gctl_dump(struct gctl_req *req)
{
	struct gctl_req_arg *ap;
	u_int i;
	int j;

	printf("Dump of gctl request at %p:\n", req);
	if (req->nerror > 0) {
		printf("  nerror:\t%d\n", req->nerror);
		if (sbuf_len(req->serror) > 0)
			printf("  error:\t\"%s\"\n", sbuf_data(req->serror));
	}
	if (req->arg == NULL)
		return;
	for (i = 0; i < req->narg; i++) {
		ap = &req->arg[i];
		if (!(ap->flag & GCTL_PARAM_NAMEKERNEL))
			printf("  param:\t%d@%p", ap->nlen, ap->name);
		else
			printf("  param:\t\"%s\"", ap->name);
		printf(" [%s%s%d] = ",
		    ap->flag & GCTL_PARAM_RD ? "R" : "",
		    ap->flag & GCTL_PARAM_WR ? "W" : "",
		    ap->len);
		if (!(ap->flag & GCTL_PARAM_VALUEKERNEL)) {
			printf(" =@ %p", ap->value);
		} else if (ap->flag & GCTL_PARAM_ASCII) {
			printf("\"%s\"", (char *)ap->kvalue);
		} else if (ap->len > 0) {
			for (j = 0; j < ap->len && j < 512; j++)
				printf(" %02x", ((u_char *)ap->kvalue)[j]);
		} else {
			printf(" = %p", ap->kvalue);
		}
		printf("\n");
	}
}

int
gctl_set_param(struct gctl_req *req, const char *param, void const *ptr,
    int len)
{
	u_int i;
	struct gctl_req_arg *ap;

	for (i = 0; i < req->narg; i++) {
		ap = &req->arg[i];
		if (strcmp(param, ap->name))
			continue;
		if (!(ap->flag & GCTL_PARAM_WR))
			return (EPERM);
		ap->flag |= GCTL_PARAM_CHANGED;
		if (ap->len < len) {
			bcopy(ptr, ap->kvalue, ap->len);
			return (ENOSPC);
		}
		bcopy(ptr, ap->kvalue, len);
		return (0);
	}
	return (EINVAL);
}

void
gctl_set_param_err(struct gctl_req *req, const char *param, void const *ptr,
    int len)
{

	switch (gctl_set_param(req, param, ptr, len)) {
	case EPERM:
		gctl_error(req, "No write access %s argument", param);
		break;
	case ENOSPC:
		gctl_error(req, "Wrong length %s argument", param);
		break;
	case EINVAL:
		gctl_error(req, "Missing %s argument", param);
		break;
	}
}

void *
gctl_get_param(struct gctl_req *req, const char *param, int *len)
{
	u_int i;
	void *p;
	struct gctl_req_arg *ap;

	for (i = 0; i < req->narg; i++) {
		ap = &req->arg[i];
		if (strcmp(param, ap->name))
			continue;
		if (!(ap->flag & GCTL_PARAM_RD))
			continue;
		p = ap->kvalue;
		if (len != NULL)
			*len = ap->len;
		return (p);
	}
	return (NULL);
}

char const *
gctl_get_asciiparam(struct gctl_req *req, const char *param)
{
	u_int i;
	char const *p;
	struct gctl_req_arg *ap;

	for (i = 0; i < req->narg; i++) {
		ap = &req->arg[i];
		if (strcmp(param, ap->name))
			continue;
		if (!(ap->flag & GCTL_PARAM_RD))
			continue;
		p = ap->kvalue;
		if (ap->len < 1) {
			gctl_error(req, "No length argument (%s)", param);
			return (NULL);
		}
		if (p[ap->len - 1] != '\0') {
			gctl_error(req, "Unterminated argument (%s)", param);
			return (NULL);
		}
		return (p);
	}
	return (NULL);
}

void *
gctl_get_paraml(struct gctl_req *req, const char *param, int len)
{
	int i;
	void *p;

	p = gctl_get_param(req, param, &i);
	if (p == NULL)
		gctl_error(req, "Missing %s argument", param);
	else if (i != len) {
		p = NULL;
		gctl_error(req, "Wrong length %s argument", param);
	}
	return (p);
}

struct g_class *
gctl_get_class(struct gctl_req *req, char const *arg)
{
	char const *p;
	struct g_class *cp;

	p = gctl_get_asciiparam(req, arg);
	if (p == NULL)
		return (NULL);
	LIST_FOREACH(cp, &g_classes, class) {
		if (!strcmp(p, cp->name))
			return (cp);
	}
	return (NULL);
}

struct g_geom *
gctl_get_geom(struct gctl_req *req, struct g_class *mpr, char const *arg)
{
	char const *p;
	struct g_class *mp;
	struct g_geom *gp;

	p = gctl_get_asciiparam(req, arg);
	if (p == NULL)
		return (NULL);
	LIST_FOREACH(mp, &g_classes, class) {
		if (mpr != NULL && mpr != mp)
			continue;
		LIST_FOREACH(gp, &mp->geom, geom) {
			if (!strcmp(p, gp->name))
				return (gp);
		}
	}
	gctl_error(req, "Geom not found: \"%s\"", p);
	return (NULL);
}

struct g_provider *
gctl_get_provider(struct gctl_req *req, char const *arg)
{
	char const *p;
	struct g_provider *pp;

	p = gctl_get_asciiparam(req, arg);
	if (p == NULL)
		return (NULL);
	pp = g_provider_by_name(p);
	if (pp != NULL)
		return (pp);
	gctl_error(req, "Provider not found: \"%s\"", p);
	return (NULL);
}

static void
g_ctl_req(void *arg, int flag __unused)
{
	struct g_class *mp;
	struct gctl_req *req;
	char const *verb;

	g_topology_assert();
	req = arg;
	mp = gctl_get_class(req, "class");
	if (mp == NULL) {
		gctl_error(req, "Class not found");
		return;
	}
	if (mp->ctlreq == NULL) {
		gctl_error(req, "Class takes no requests");
		return;
	}
	verb = gctl_get_param(req, "verb", NULL);
	if (verb == NULL) {
		gctl_error(req, "Verb missing");
		return;
	}
	mp->ctlreq(req, mp, verb);
	g_topology_assert();
}


static int
g_ctl_ioctl_ctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread *td)
{
	struct gctl_req *req;
	int nerror;

	req = (void *)data;
	req->nerror = 0;
	/* It is an error if we cannot return an error text */
	if (req->lerror < 2)
		return (EINVAL);
	if (!useracc(req->error, req->lerror, VM_PROT_WRITE))
		return (EINVAL);

	req->serror = sbuf_new_auto();
	/* Check the version */
	if (req->version != GCTL_VERSION) {
		gctl_error(req, "kernel and libgeom version mismatch.");
		req->arg = NULL;
	} else {
		/* Get things on board */
		gctl_copyin(req);

		if (g_debugflags & G_F_CTLDUMP)
			gctl_dump(req);

		if (!req->nerror) {
			g_waitfor_event(g_ctl_req, req, M_WAITOK, NULL);
			gctl_copyout(req);
		}
	}
	if (sbuf_done(req->serror)) {
		copyout(sbuf_data(req->serror), req->error,
		    imin(req->lerror, sbuf_len(req->serror) + 1));
	}

	nerror = req->nerror;
	gctl_free(req);
	return (nerror);
}

static int
g_ctl_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread *td)
{
	int error;

	switch(cmd) {
	case GEOM_CTL:
		error = g_ctl_ioctl_ctl(dev, cmd, data, fflag, td);
		break;
	default:
		error = ENOIOCTL;
		break;
	}
	return (error);

}
