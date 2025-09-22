/*	$OpenBSD: obiovar.h,v 1.3 2008/06/26 05:42:11 ray Exp $	*/
/*	$NetBSD: obiovar.h,v 1.1 2006/09/01 21:26:18 uwe Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1995 Chris G. Demetriou
 * Copyright (c) 1992 Berkeley Software Design, Inc.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Berkeley Software
 *      Design, Inc.
 * 4. The name of Berkeley Software Design must not be used to endorse
 *    or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      BSDI Id: isavar.h,v 1.5 1992/12/01 18:06:00 karels Exp 
 */

#ifndef _LANDISK_OBIOVAR_H_
#define _LANDISK_OBIOVAR_H_

#include <machine/bus.h>

/* obio tag */
extern struct _bus_space obio_bus_io;
extern struct _bus_space obio_bus_mem;

/*
 * obio bus attach arguments
 */
struct obiobus_attach_args {
	const char *oba_busname;	/* XXX should be common */

	bus_space_tag_t oba_iot;	/* obio i/o space tag */
	bus_space_tag_t oba_memt;	/* obio mem space tag */
};

/*
 * obio bus resources.
 */

struct obio_io {
	int or_addr;
	int or_size;
};

struct obio_iomem {
	int or_addr;
	int or_size;
};

struct obio_irq {
	int or_irq;
};

/*
 * driver attach arguments
 */
struct obio_attach_args {
	bus_space_tag_t oa_iot;		/* i/o space tag */
	bus_space_tag_t oa_memt;	/* memory space tag */

	struct obio_io *oa_io;		/* I/O resources */
	int oa_nio;

	struct obio_iomem *oa_iomem;	/* memory resources */
	int oa_niomem;

	struct obio_irq *oa_irq;	/* IRQ resources */
	int oa_nirq;

	void	*oa_aux;		/* driver specific */
};

#define	IOBASEUNK	-1	/* i/o address is unknown */
#define	IRQUNK		-1	/* interrupt request line is unknown */
#define	MADDRUNK	-1	/* shared memory address is unknown */


/*
 * master bus
 */
struct obio_softc {
	struct device sc_dev;		/* base device */

	bus_space_tag_t sc_iot;		/* io space tag */
	bus_space_tag_t sc_memt;	/* mem space tag */
};

#define		cf_iobase		cf_loc[0]
#define		cf_iosize		cf_loc[1]
#define		cf_maddr		cf_loc[2]
#define		cf_msize		cf_loc[3]
#define		cf_irq			cf_loc[4]

void *obio_intr_establish(int, int, int (*)(void *), void *, const char *);
void obio_intr_disestablish(void *);

#endif /* _LANDISK_OBIOVAR_H_ */
