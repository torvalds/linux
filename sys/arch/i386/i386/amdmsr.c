/*      $OpenBSD: amdmsr.c,v 1.12 2023/01/30 10:49:04 jsg Exp $	*/

/*
 * Copyright (c) 2008 Marc Balmer <mbalmer@openbsd.org>
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Enable MSR access for AMD Geode LX Processors with graphics processor
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/conf.h>

#include <machine/amdmsr.h>
#include <machine/cpufunc.h>

#ifdef APERTURE
static int amdmsr_open_cnt;
extern int allowaperture;
#endif

#define GLX_CPU_GLD_MSR_CAP	0x00002000
#define GLX_CPU_DID		0x0864		/* CPU device ID */
#define GLX_GP_GLD_MSR_CAP	0xa0002000
#define GLX_GP_DID		0x03d4		/* GP device ID */

struct amdmsr_softc {
	struct device		sc_dev;
};

struct cfdriver amdmsr_cd = {
	NULL, "amdmsr", DV_DULL
};

int	amdmsr_match(struct device *, void *, void *);
void	amdmsr_attach(struct device *, struct device *, void *);

const struct cfattach amdmsr_ca = {
	sizeof(struct amdmsr_softc), amdmsr_match, amdmsr_attach
};

int
amdmsr_probe(void)
{
#ifdef APERTURE
	u_int64_t gld_msr_cap;
	int family, model;

	family = (cpu_id >> 8) & 0xf;
	model  = (cpu_id >> 4) & 0xf;

	/* Check for AMD Geode LX CPU */
	if (strcmp(cpu_vendor, "AuthenticAMD") == 0 && family == 0x5 &&
	    model == 0x0a) {
		/* Check for graphics processor presence */
		gld_msr_cap = rdmsr(GLX_CPU_GLD_MSR_CAP);
		if (((gld_msr_cap >> 8) & 0x0fff) == GLX_CPU_DID) {
			gld_msr_cap = rdmsr(GLX_GP_GLD_MSR_CAP);
			if (((gld_msr_cap >> 8) & 0x0fff) == GLX_GP_DID)
				return 1;
		}
	}
#endif
	return 0;
}

int
amdmsr_match(struct device *parent, void *match, void *aux)
{
	const char **busname = (const char **)aux;

	return (strcmp(*busname, amdmsr_cd.cd_name) == 0);
}

void
amdmsr_attach(struct device *parent, struct device *self, void *aux)
{
	printf("\n");
}

int
amdmsropen(dev_t dev, int flags, int devtype, struct proc *p)
{
#ifdef APERTURE
	if (amdmsr_cd.cd_ndevs == 0 || minor(dev) != 0)
		return ENXIO;

	if (suser(p) != 0 || !allowaperture)
		return EPERM;
	/* allow only one simultaneous open() */
	if (amdmsr_open_cnt > 0)
		return EPERM;
	amdmsr_open_cnt++;
	return 0;
#else
	return ENXIO;
#endif
}

int
amdmsrclose(dev_t dev, int flags, int devtype, struct proc *p)
{
#ifdef APERTURE
	amdmsr_open_cnt--;
#endif
	return 0;
}

int
amdmsrioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct amdmsr_req *req = (struct amdmsr_req *)data;

	switch (cmd) {
	case RDMSR:
		req->val = rdmsr(req->addr);
		break;
	case WRMSR:
		wrmsr(req->addr, req->val);
		break;
	default:
		return EINVAL;
	}
	return 0;
}
