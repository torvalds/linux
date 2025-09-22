/*	$OpenBSD: pcex.c,v 1.5 2024/06/01 00:48:16 aoyama Exp $	*/

/*
 * Copyright (c) 2014 Kenji Aoyama.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * PC-9801 extension board slot direct access driver for LUNA-88K2.
 */

#include <sys/param.h>
#include <sys/systm.h>	/* tsleep()/wakeup() */
#include <sys/device.h>
#include <sys/ioctl.h>

#include <machine/autoconf.h>
#include <machine/board.h>	/* PC_BASE */
#include <machine/conf.h>
#include <machine/pcex.h>

#include <luna88k/cbus/cbusvar.h>

extern int hz;

#if 0
#define PCEX_DEBUG
#endif

/* autoconf stuff */
int pcex_match(struct device *, void *, void *);
void pcex_attach(struct device *, struct device *, void *);

struct pcex_softc {
	struct device sc_dev;
	int intr_use[NCBUSISR];
};

const struct cfattach pcex_ca = {
	sizeof(struct pcex_softc), pcex_match, pcex_attach
};

struct cfdriver pcex_cd = {
	NULL, "pcex", DV_DULL
};

/* prototypes */
int pcex_intr(void *);
int pcex_set_int(struct pcex_softc *, u_int);
int pcex_reset_int(struct pcex_softc *, u_int);
int pcex_wait_int(struct pcex_softc *, u_int);

int
pcex_match(struct device *parent, void *cf, void *aux)
{
	struct cbus_attach_args *caa = aux;

	if (strcmp(caa->ca_name, pcex_cd.cd_name))
		return 0;

	return 1;
}

void
pcex_attach(struct device *parent, struct device *self, void *args)
{
	struct pcex_softc *sc = (struct pcex_softc *)self;
	int i;

	for (i = 0; i < NCBUSISR; i++)
		sc->intr_use[i] = 0;

	printf("\n");
	return;
}

int
pcexopen(dev_t dev, int flag, int mode, struct proc *p)
{
	switch (minor(dev)) {
	case 0:	/* memory area */
	case 1:	/* I/O port area */
		return 0;
	default:
		return ENXIO;
	}
}

int
pcexclose(dev_t dev, int flag, int mode, struct proc *p)
{
	return (0);
}

paddr_t
pcexmmap(dev_t dev, off_t offset, int prot)
{
	paddr_t cookie = -1;

	switch (minor(dev)) {
	case 0:	/* memory area */
		if (offset >= 0 && offset < 0x1000000)
			cookie = (paddr_t)(PCEXMEM_BASE + offset);
		break;
	case 1:	/* I/O port area */
		if (offset >= 0 && offset < 0x10000)
			cookie = (paddr_t)(PCEXIO_BASE + offset);
		break;
	default:
		break;
	}

	return cookie;
}

int
pcexioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct pcex_softc *sc = NULL;
	u_int level;

	if (pcex_cd.cd_ndevs != 0)
		sc = pcex_cd.cd_devs[0];
	if (sc == NULL)
		return ENXIO;

	level = *(u_int *)data;

	switch(cmd) {
	case PCEXSETLEVEL:
		return pcex_set_int(sc, level);

	case PCEXRESETLEVEL:
		return pcex_reset_int(sc, level);

	case PCEXWAITINT:
		return pcex_wait_int(sc, level);

	default:
		return ENOTTY;
	}
}

int
pcex_set_int(struct pcex_softc *sc, u_int level)
{
	if (level > 6)
		return EINVAL;
	if (sc->intr_use[level] != 0)
		return EINVAL;	/* Duplicate */

	sc->intr_use[level] = 1;
	cbus_isrlink(pcex_intr, &(sc->intr_use[level]), level, IPL_NET,
	    sc->sc_dev.dv_xname);

	return 0;
}

int
pcex_reset_int(struct pcex_softc *sc, u_int level)
{
	if (level > 6)
		return EINVAL;
	if (sc->intr_use[level] == 0)
		return EINVAL;	/* Not registered */

	sc->intr_use[level] = 0;
	cbus_isrunlink(pcex_intr, level);

	return 0;
}

int
pcex_wait_int(struct pcex_softc *sc, u_int level)
{
	int ret;

	if (level > 6)
		return EINVAL;
	if (sc->intr_use[level] == 0)
		return EINVAL;	/* Not registered */

	ret = tsleep_nsec(&(sc->intr_use[level]), PWAIT | PCATCH, "pcex",
	    SEC_TO_NSEC(1));	/* XXX 1 sec. */

#ifdef PCEX_DEBUG
	if (ret == EWOULDBLOCK)
		printf("pcex_wait_int: timeout in tsleep_nsec\n");
#endif
	return ret;
}

int
pcex_intr(void *arg)
{
#ifdef PCEX_DEBUG
	printf("pcex_intr: called, arg=%p\n", arg);
#endif
	/* Just wakeup(9) for now */
	wakeup(arg);

	return 1;
}
