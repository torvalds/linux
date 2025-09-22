/*	$OpenBSD: mem.c,v 1.10 2024/12/30 02:46:00 guenther Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1991,1992,1994, The University of Utah and
 * the Computer Systems Laboratory (CSL).  All rights reserved.
 *
 * Subject to your agreements with CMU,
 * permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 *	Utah $Hdr: mem.c 1.9 94/12/16$
 */
/*
 * Mach Operating System
 * Copyright (c) 1992 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/filio.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/atomic.h>

#include <uvm/uvm_extern.h>

#include <machine/conf.h>
#include <machine/bus.h>
#include <machine/iomod.h>
#include <machine/autoconf.h>
#include <machine/pmap.h>

#include <hppa/dev/cpudevs.h>
#include <hppa/dev/viper.h>

#define	VIPER_HPA	0xfffbf000

/* registers on the PCXL2 MIOC */
struct l2_mioc {
	u_int32_t	pad[0x20];	/* 0x000 */
	u_int32_t	mioc_control;	/* 0x080 MIOC control bits */
	u_int32_t	mioc_status;	/* 0x084 MIOC status bits */
	u_int32_t	pad1[6];	/* 0x088 */
	u_int32_t	sltcv;		/* 0x0a0 L2 cache control */
#define	SLTCV_AVWL	0x00002000	/* extra cycle for addr valid write low */
#define	SLTCV_UP4COUT	0x00001000	/* update cache on CPU castouts */
#define	SLTCV_EDCEN	0x08000000	/* enable error correction */
#define	SLTCV_EDTAG	0x10000000	/* enable diagtag */
#define	SLTCV_CHKTP	0x20000000	/* enable parity checking */
#define	SLTCV_LOWPWR	0x40000000	/* low power mode */
#define	SLTCV_ENABLE	0x80000000	/* enable L2 cache */
#define	SLTCV_BITS	"\020\15avwl\16up4cout\24edcen\25edtag\26chktp\27lowpwr\30l2ena"
	u_int32_t	tagmask;	/* 0x0a4 L2 cache tag mask */
	u_int32_t	diagtag;	/* 0x0a8 L2 invalidates tag */
	u_int32_t	sltestat;	/* 0x0ac L2 last logged tag read */
	u_int32_t	slteadd;	/* 0x0b0 L2 pa of -- " -- */
	u_int32_t	pad2[3];	/* 0x0b4 */
	u_int32_t	mtcv;		/* 0x0c0 MIOC timings */
	u_int32_t	ref;		/* 0x0cc MIOC refresh timings */
	u_int32_t	pad3[4];	/* 0x0d0 */
	u_int32_t	mderradd;	/* 0x0e0 addr of most evil mem error */
	u_int32_t	pad4;		/* 0x0e4 */
	u_int32_t	dmaerr;		/* 0x0e8 addr of most evil dma error */
	u_int32_t	dioerr;		/* 0x0ec addr of most evil dio error */
	u_int32_t	gsc_timeout;	/* 0x0f0 1-compl of GSC timeout delay */
	u_int32_t	hidmamem;	/* 0x0f4 amount of phys mem installed */
	u_int32_t	pad5[2];	/* 0x0f8 */
	u_int32_t	memcomp[16];	/* 0x100 memory address comparators */
	u_int32_t	memmask[16];	/* 0x140 masks for -- " -- */
	u_int32_t	memtest;	/* 0x180 test address decoding */
	u_int32_t	pad6[0xf];	/* 0x184 */
	u_int32_t	outchk;		/* 0x1c0 address decoding output */
	u_int32_t	pad7[0x168];	/* 0x200 */
	u_int32_t	gsc15x_config;	/* 0x7a0 writev enable */
};

struct mem_softc {
	struct device sc_dev;

	volatile struct vi_trs *sc_vp;
	volatile struct l2_mioc *sc_l2;
};

int	memmatch(struct device *, void *, void *);
void	memattach(struct device *, struct device *, void *);

const struct cfattach mem_ca = {
	sizeof(struct mem_softc), memmatch, memattach
};

struct cfdriver mem_cd = {
	NULL, "mem", DV_DULL
};

caddr_t zeropage;

int
memmatch(struct device *parent, void *cfdata, void *aux)
{
	struct confargs *ca = aux;

	if (ca->ca_type.iodc_type != HPPA_TYPE_MEMORY ||
	    ca->ca_type.iodc_sv_model != HPPA_MEMORY_PDEP)
		return 0;

	return 1;
}

void
memattach(struct device *parent, struct device *self, void *aux)
{
	struct pdc_iodc_minit pdc_minit PDC_ALIGNMENT;
	struct mem_softc *sc = (struct mem_softc *)self;
	struct confargs *ca = aux;
	int err;

	printf (":");

	/* XXX check if we are dealing w/ Viper */
	if (ca->ca_hpa == (hppa_hpa_t)VIPER_HPA) {

		sc->sc_vp = (struct vi_trs *)
		    &((struct iomod *)ca->ca_hpa)->priv_trs;

		printf(" viper rev %x,", sc->sc_vp->vi_status.hw_rev);

		/* XXX other values seem to blow it up */
		if (sc->sc_vp->vi_status.hw_rev == 0) {
			u_int32_t vic;
			int s, settimeout;

			switch (cpu_hvers) {
			case HPPA_BOARD_HP705:
			case HPPA_BOARD_HP710:
			case HPPA_BOARD_HP715_33:
			case HPPA_BOARD_HP715S_33:
			case HPPA_BOARD_HP715T_33:
			case HPPA_BOARD_HP715_50:
			case HPPA_BOARD_HP715S_50:
			case HPPA_BOARD_HP715T_50:
			case HPPA_BOARD_HP715_75:
			case HPPA_BOARD_HP720:
			case HPPA_BOARD_HP725_50:
			case HPPA_BOARD_HP725_75:
			case HPPA_BOARD_HP730_66:
			case HPPA_BOARD_HP750_66:
				settimeout = 1;
				break;
			default:
				settimeout = 0;
				break;
			}
			if (sc->sc_dev.dv_cfdata->cf_flags & 1)
				settimeout = !settimeout;

#ifdef DEBUG
			printf(" ctrl %b", VI_CTRL, VI_CTRL_BITS);
#endif
			s = splhigh();
			vic = VI_CTRL;
			vic &= ~VI_CTRL_CORE_DEN;
			vic &= ~VI_CTRL_SGC0_DEN;
			vic &= ~VI_CTRL_SGC1_DEN;
			vic |=  VI_CTRL_EISA_DEN;
			vic |=  VI_CTRL_CORE_PRF;

			if (settimeout && (vic & VI_CTRL_VSC_TOUT) == 0)
				vic |= (850 << 19);	/* clks */

			sc->sc_vp->vi_control = vic;

			__asm volatile("stwas %1, 0(%0)"
			    :: "r" (&VI_CTRL), "r" (vic) : "memory");
			splx(s);
#ifdef DEBUG
			printf (" >> %b,", vic, VI_CTRL_BITS);
#endif
		} else {
			/* set at least VI_CTRL_EISA_DEN ? */
		}
	} else
		sc->sc_vp = NULL;

	if ((err = pdc_call((iodcio_t)pdc, 0, PDC_IODC, PDC_IODC_NINIT,
	    &pdc_minit, ca->ca_hpa, PAGE0->imm_spa_size)) < 0)
		pdc_minit.max_spa = PAGE0->imm_max_mem;

	printf(" size %d", pdc_minit.max_spa / (1024*1024));
	if (pdc_minit.max_spa % (1024*1024))
		printf(".%d", pdc_minit.max_spa % (1024*1024));
	printf("MB");

	/* L2 cache controller is a part of the memory controller on PCXL2 */
	if (cpu_type == hpcxl2) {
		sc->sc_l2 = (struct l2_mioc *)ca->ca_hpa;
#ifdef DEBUG
		printf(", sltcv %b", sc->sc_l2->sltcv, SLTCV_BITS);
#endif
		/* sc->sc_l2->sltcv |= SLTCV_UP4COUT; */
		if (sc->sc_l2->sltcv & SLTCV_ENABLE) {
			u_int32_t tagmask = sc->sc_l2->tagmask >> 20;

			printf(", %dMB L2 cache", tagmask + 1);
		}
	}

	printf("\n");
}

void
viper_setintrwnd(u_int32_t mask)
{
	struct mem_softc *sc;

	sc = mem_cd.cd_devs[0];

	if (sc->sc_vp)
		sc->sc_vp->vi_intrwd = mask;
}

void
viper_eisa_en(void)
{
	struct mem_softc *sc;

	sc = mem_cd.cd_devs[0];
	if (sc->sc_vp) {
		u_int32_t vic;
		int s;

		s = splhigh();
		vic = VI_CTRL;
		vic &= ~VI_CTRL_EISA_DEN;
		sc->sc_vp->vi_control = vic;
		__asm volatile("stwas %1, 0(%0)"
		    :: "r" (&VI_CTRL), "r" (vic) : "memory");
		splx(s);
	}
}

int
mmopen(dev_t dev, int flag, int ioflag, struct proc *p)
{
	extern int allowkmem;

	switch (minor(dev)) {
	case 0:
	case 1:
		if ((int)atomic_load_int(&securelevel) <= 0 ||
		    atomic_load_int(&allowkmem))
			break;
		return (EPERM);
	case 2:
	case 12:
		break;
	default:
		return (ENXIO);
	}
	return (0);
}

int
mmclose(dev_t dev, int flag, int mode, struct proc *p)
{
	return (0);
}

int
mmrw(dev_t dev, struct uio *uio, int flags)
{
	struct iovec *iov;
	vaddr_t	v, o;
	int error = 0;
	size_t c;

	while (uio->uio_resid > 0 && error == 0) {
		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				panic("mmrw");
			continue;
		}
		switch (minor(dev)) {

		case 0:				/*  /dev/mem  */

			/* If the address isn't in RAM, bail. */
			v = uio->uio_offset;
			if (atop(v) > physmem) {
				error = EFAULT;
				/* this will break us out of the loop */
				continue;
			}
			c = ptoa(physmem) - v;
			c = ulmin(c, uio->uio_resid);
			error = uiomove((caddr_t)v, c, uio);
			break;

		case 1:				/*  /dev/kmem  */
			v = uio->uio_offset;
			o = v & PGOFSET;
			c = ulmin(uio->uio_resid, PAGE_SIZE - o);
			if (atop(v) > physmem && !uvm_kernacc((caddr_t)v,
			    c, (uio->uio_rw == UIO_READ) ? B_READ : B_WRITE)) {
				error = EFAULT;
				/* this will break us out of the loop */
				continue;
			}
			error = uiomove((caddr_t)v, c, uio);
			break;

		case 2:				/*  /dev/null  */
			if (uio->uio_rw == UIO_WRITE)
				uio->uio_resid = 0;
			return (0);

		case 12:			/*  /dev/zero  */
			/* Write to /dev/zero is ignored. */
			if (uio->uio_rw == UIO_WRITE) {
				uio->uio_resid = 0;
				return (0);
			}
			/*
			 * On the first call, allocate and zero a page
			 * of memory for use with /dev/zero.
			 */
			if (zeropage == NULL) 
				zeropage = malloc(PAGE_SIZE, M_TEMP,
				    M_WAITOK | M_ZERO);
			c = ulmin(iov->iov_len, PAGE_SIZE);
			error = uiomove(zeropage, c, uio);
			break;

		default:
			return (ENXIO);
		}
	}

	return (error);
}

paddr_t
mmmmap(dev_t dev, off_t off, int prot)
{
	if (minor(dev) != 0)
		return (-1);

	/*
	 * Allow access only in RAM.
	 */
#if 0
	if (off < ptoa(firstusablepage) ||
	    off >= ptoa(lastusablepage + 1))
		return (-1);
#endif
	return (off);
}

int
mmioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
        switch (cmd) {
        case FIOASYNC:
                /* handled by fd layer */
                return 0;
        }

	return (ENOTTY);
}
