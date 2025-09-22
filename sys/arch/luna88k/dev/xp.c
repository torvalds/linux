/* $OpenBSD: xp.c,v 1.4 2024/06/26 01:40:49 jsg Exp $ */
/* $NetBSD: xp.c,v 1.1 2016/12/03 17:38:02 tsutsui Exp $ */

/*-
 * Copyright (c) 2016 Izumi Tsutsui.  All rights reserved.
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

/*
 * LUNA's Hitachi HD647180 "XP" I/O processor driver
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/errno.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/board.h>
#include <machine/xpio.h>

#define XP_SHM_BASE	TRI_PORT_RAM
#define XP_SHM_SIZE	0x00010000	/* 64KB for XP; rest 64KB for lance */
#define XP_TAS_ADDR	OBIO_TAS

struct xp_softc {
	struct device	sc_dev;

	vaddr_t		sc_shm_base;
	vsize_t		sc_shm_size;
	vaddr_t		sc_tas;

	bool		sc_isopen;
};

static int xp_match(struct device *, void *, void *);
static void xp_attach(struct device *, struct device *, void *);

const struct cfattach xp_ca = {
	sizeof (struct xp_softc), xp_match, xp_attach
};

struct cfdriver xp_cd = {
	NULL, "xp", DV_DULL
};

/* #define XP_DEBUG */

#ifdef XP_DEBUG
#define XP_DEBUG_ALL	0xff
uint32_t xp_debug = 0;
#define DPRINTF(x, y)	if (xp_debug & (x)) printf y
#else
#define DPRINTF(x, y)	/* nothing */
#endif

static bool xp_matched;

/*
 * PIO 0 port C is connected to XP's reset line
 *
 * XXX: PIO port functions should be shared with machdep.c for DIP SWs
 */
#define PIO_ADDR	OBIO_PIO0_BASE
#define PORT_A		(0 * 4)
#define PORT_B		(1 * 4)
#define PORT_C		(2 * 4)
#define CTRL		(3 * 4)

/* PIO0 Port C bit definition */
#define XP_INT1_REQ	0	/* INTR B */
	/* unused */		/* IBF B */
#define XP_INT1_ENA	2	/* INTE B */
#define XP_INT5_REQ	3	/* INTR A */
#define XP_INT5_ENA	4	/* INTE A */
	/* unused */		/* IBF A */
#define PARITY		6	/* PC6 output to enable parity error */
#define XP_RESET	7	/* PC7 output to reset HD647180 XP */

/* Port control for PC6 and PC7 */
#define ON		1
#define OFF		0

static uint8_t
put_pio0c(uint8_t bit, uint8_t set)
{
	volatile uint8_t * const pio0 = (uint8_t *)PIO_ADDR;

	pio0[CTRL] = (bit << 1) | (set & 0x01);

	return pio0[PORT_C];
}

static int
xp_match(struct device *parent, void *cf, void *aux)
{
	struct mainbus_attach_args *maa = aux;

	/* only one XP processor */
	if (xp_matched)
		return 0;

	if (strcmp(maa->ma_name, xp_cd.cd_name))
		return 0;

	if (maa->ma_addr != XP_SHM_BASE)
		return 0;

	xp_matched = true;
	return 1;
}

static void
xp_attach(struct device *parent, struct device *self, void *aux)
{
	struct xp_softc *sc = (void *)self;

	printf(": HD647180X I/O processor\n");

	sc->sc_shm_base = XP_SHM_BASE;
	sc->sc_shm_size = XP_SHM_SIZE;
	sc->sc_tas      = XP_TAS_ADDR;
}

int
xpopen(dev_t dev, int flags, int mode, struct proc *p)
{
	struct xp_softc *sc;
	int unit;
	
	DPRINTF(XP_DEBUG_ALL, ("%s\n", __func__));

	unit = minor(dev);
	if (unit >= xp_cd.cd_ndevs)
		return ENXIO;
	sc = xp_cd.cd_devs[unit];
	if (sc == NULL)
		return ENXIO;
	if (sc->sc_isopen)
		return EBUSY;

	sc->sc_isopen = true;

	return 0;
}

int
xpclose(dev_t dev, int flags, int mode, struct proc *p)
{
	struct xp_softc *sc;
	int unit;

	DPRINTF(XP_DEBUG_ALL, ("%s\n", __func__));

	unit = minor(dev);
	if (unit >= xp_cd.cd_ndevs)
		return ENXIO;
	sc = xp_cd.cd_devs[unit];
	sc->sc_isopen = false;

	return 0;
}

int
xpioctl(dev_t dev, u_long cmd, void *addr, int flags, struct proc *p)
{
	struct xp_softc *sc;
	int unit, error;
	struct xp_download *downld;
	uint8_t *loadbuf;
	size_t loadsize;

	DPRINTF(XP_DEBUG_ALL, ("%s\n", __func__));

	unit = minor(dev);
	if (unit >= xp_cd.cd_ndevs)
		return ENXIO;
	sc = xp_cd.cd_devs[unit];

	switch (cmd) {
	case XPIOCDOWNLD:
		downld = addr;
		loadsize = downld->size;
		if (loadsize == 0 || loadsize > sc->sc_shm_size) {
			return EINVAL;
		}

		loadbuf = malloc(loadsize, M_DEVBUF, M_WAITOK);
		if (loadbuf == NULL) {
			return ENOMEM;
		}
		error = copyin(downld->data, loadbuf, loadsize);
		if (error == 0) {
			put_pio0c(XP_RESET, ON);
			delay(100);
			memcpy((void *)sc->sc_shm_base, loadbuf, loadsize);
			delay(100);
			put_pio0c(XP_RESET, OFF);
		} else {
			DPRINTF(XP_DEBUG_ALL, ("%s: ioctl failed (err =  %d)\n",
			    __func__, error));
		}

		free(loadbuf, M_DEVBUF, loadsize);
		return error;

	default:
		return ENOTTY;
	}
}

paddr_t
xpmmap(dev_t dev, off_t offset, int prot)
{
	struct xp_softc *sc;
	int unit;
	paddr_t pa;

	pa = -1;

	unit = minor(dev);
	sc = xp_cd.cd_devs[unit];

	if (offset >= 0 &&
	    offset < sc->sc_shm_size) {
		pa = (paddr_t)(trunc_page(sc->sc_shm_base) + offset);
	}

	return pa;
}
