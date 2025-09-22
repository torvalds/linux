/*	$OpenBSD: nvram.c,v 1.7 2016/08/03 17:29:18 jcs Exp $ */

/*
 * Copyright (c) 2004 Joshua Stein <jcs@openbsd.org>
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <sys/conf.h>

#include <dev/ic/mc146818reg.h>

/* checksum is calculated over bytes 2 to 31 and stored in byte 32 */
#define NVRAM_CSUM_START	(MC_NVRAM_START + 2)
#define NVRAM_CSUM_END		(MC_NVRAM_START + 31)
#define NVRAM_CSUM_LOC		(MC_NVRAM_START + 32)

#define NVRAM_SIZE		(128 - MC_NVRAM_START)

/* #define NVRAM_DEBUG 1 */

void nvramattach(int);

int nvramopen(dev_t dev, int flag, int mode, struct proc *p);
int nvramclose(dev_t dev, int flag, int mode, struct proc *p);
int nvramread(dev_t dev, struct uio *uio, int flags);

int nvram_csum_valid(void);
int nvram_get_byte(int byteno);

static int nvram_initialized;

void
nvramattach(int num)
{
	if (num > 1)
		return;

	if (nvram_initialized || nvram_csum_valid()) {
#ifdef NVRAM_DEBUG
		printf("nvram: initialized\n");
#endif
		nvram_initialized = 1;
	}
}

int
nvramopen(dev_t dev, int flag, int mode, struct proc *p)
{
	/* TODO: re-calc checksum on every open? */

	if ((minor(dev) != 0) || (!nvram_initialized))
		return (ENXIO);

	if ((flag & FWRITE))
		return (EPERM);

	return (0);
}

int
nvramclose(dev_t dev, int flag, int mode, struct proc *p)
{
	return (0);
}

int
nvramread(dev_t dev, struct uio *uio, int flags)
{
	u_char buf[NVRAM_SIZE];
	off_t pos = uio->uio_offset;
	u_char *tmp;
	size_t count = ulmin(sizeof(buf), uio->uio_resid);
	int ret;

	if (!nvram_initialized)
		return (ENXIO);

	if (uio->uio_offset < 0)
		return (EINVAL);

	if (uio->uio_resid == 0)
		return (0);

#ifdef NVRAM_DEBUG
	printf("attempting to read %zu bytes at offset %lld\n", count, pos);
#endif

	for (tmp = buf; count-- > 0 && pos < NVRAM_SIZE; ++pos, ++tmp)
		*tmp = nvram_get_byte(pos);

#ifdef NVRAM_DEBUG
	printf("nvramread read %td bytes (%s)\n", (tmp - buf), tmp);
#endif

	ret = uiomove(buf, (tmp - buf), uio);

	uio->uio_offset += uio->uio_resid;

	return (ret);
}

int
nvram_get_byte(int byteno)
{
	if (!nvram_initialized)
		return (ENXIO);

	return (mc146818_read(NULL, byteno + MC_NVRAM_START) & 0xff);
}

int
nvram_csum_valid(void)
{
	u_short csum = 0;
	u_short csumexpect;
	int nreg;

	for (nreg = NVRAM_CSUM_START; nreg <= NVRAM_CSUM_END; nreg++)
		csum += mc146818_read(NULL, nreg);

	csumexpect = mc146818_read(NULL, NVRAM_CSUM_LOC) << 8 |
	    mc146818_read(NULL, NVRAM_CSUM_LOC + 1);

#ifdef NVRAM_DEBUG
	printf("nvram: checksum is %x, expecting %x\n", (csum & 0xffff),
		csumexpect);
#endif

	return ((csum & 0xffff) == csumexpect);
}
