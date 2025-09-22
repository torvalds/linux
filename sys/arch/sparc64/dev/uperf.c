/*	$OpenBSD: uperf.c,v 1.9 2022/10/16 01:22:39 jsg Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/ioctl.h>
#include <sys/conf.h>

#include <machine/conf.h>

#include <dev/sun/uperfio.h>
#include <arch/sparc64/dev/uperfvar.h>

struct cfdriver uperf_cd = {
	NULL, "uperf", DV_DULL
};

int uperf_getcntsrc(struct uperf_softc *, struct uperf_io *);
int uperf_findbyval(struct uperf_softc *, int, u_int, int *);
int uperf_findbysrc(struct uperf_softc *, int, int, u_int32_t *);
int uperf_setcntsrc(struct uperf_softc *, struct uperf_io *);

int
uperfopen(dev_t dev, int flags, int mode, struct proc *p)
{
	if (minor(dev) >= uperf_cd.cd_ndevs)
		return (ENXIO);
	if (uperf_cd.cd_devs[minor(dev)] == NULL)
		return (ENXIO);
	return (0);
}

int
uperfclose(dev_t dev, int flags, int mode, struct proc *p)
{
	return (0);
}

int
uperfioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct uperf_softc *usc = uperf_cd.cd_devs[minor(dev)];
	struct uperf_io *io = (struct uperf_io *)data;
	int error = EINVAL;

	switch (cmd) {
	case UPIO_GCNTSRC:
		error = uperf_getcntsrc(usc, io);
		break;
	case UPIO_SCNTSRC:
		error = uperf_setcntsrc(usc, io);
		break;
	case UPIO_CLRCNT:
		error = usc->usc_clrcnt(usc->usc_cookie, io->cnt_flags);
		break;
	case UPIO_GETCNT:
		error = usc->usc_getcnt(usc->usc_cookie, io->cnt_flags,
		    &io->cnt_val0, &io->cnt_val1);
		break;
	}

	return (error);
}

int
uperf_getcntsrc(struct uperf_softc *usc, struct uperf_io *io)
{
	u_int cnt0_src, cnt1_src;
	int error;

	error = usc->usc_getcntsrc(usc->usc_cookie, io->cnt_flags,
	    &cnt0_src, &cnt1_src);
	if (error)
		return (error);

	if (io->cnt_flags & UPERF_CNT0) {
		error = uperf_findbyval(usc, UPERF_CNT0,
		    cnt0_src, &io->cnt_src0);
		if (error)
			return (error);
	}

	if (io->cnt_flags & UPERF_CNT1) {
		error = uperf_findbyval(usc, UPERF_CNT1,
		    cnt1_src, &io->cnt_src1);
		if (error)
			return (error);
	}
	return (0);
}

int
uperf_findbyval(struct uperf_softc *usc, int cnt, u_int uval, int *rval)
{
	struct uperf_src *srcs = usc->usc_srcs;

	if (srcs->us_src == 0)
		return (EINVAL);

	while (srcs->us_src != -1) {
		if (srcs->us_val == uval && srcs->us_flags & cnt) {
			*rval = srcs->us_src;
			return (0);
		}
		srcs++;
	}
	return (EINVAL);
}

int
uperf_setcntsrc(struct uperf_softc *usc, struct uperf_io *io)
{
	u_int32_t cnt0_src, cnt1_src;
	int error;

	cnt0_src = cnt1_src = 0;

	if (io->cnt_flags & UPERF_CNT0) {
		error = uperf_findbysrc(usc, UPERF_CNT0,
		    io->cnt_src0, &cnt0_src);
		if (error)
			return (error);
	}
	if (io->cnt_flags & UPERF_CNT1) {
		error = uperf_findbysrc(usc, UPERF_CNT1,
		    io->cnt_src1, &cnt1_src);
		if (error)
			return (error);
	}
	return ((usc->usc_setcntsrc)(usc->usc_cookie, io->cnt_flags,
	    cnt0_src, cnt1_src));
}

int
uperf_findbysrc(struct uperf_softc *usc, int cnt, int src, u_int32_t *rval)
{
	struct uperf_src *srcs = usc->usc_srcs;

	if (srcs->us_src == 0)
		return (EINVAL);

	while (srcs->us_src != -1) {
		if (srcs->us_src == src && srcs->us_flags & cnt) {
			*rval = srcs->us_val;
			return (0);
		}
		srcs++;
	}
	return (EINVAL);
}
