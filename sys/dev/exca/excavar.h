/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause AND BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 M. Warner Losh.
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
 *
 * This software may be derived from NetBSD i82365.c and other files with
 * the following copyright:
 *
 * Copyright (c) 1997 Marc Horowitz.  All rights reserved.
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
 *	This product includes software developed by Marc Horowitz.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#ifndef _SYS_DEV_EXCA_EXCAVAR_H
#define _SYS_DEV_EXCA_EXCAVAR_H

/*
 * Structure to manage the ExCA part of the chip.
 */
struct exca_softc;
typedef uint8_t (exca_getb_fn)(struct exca_softc *, int);
typedef void (exca_putb_fn)(struct exca_softc *, int, uint8_t);

struct exca_softc 
{
	device_t	dev;
	int		memalloc;
	struct		pccard_mem_handle mem[EXCA_MEM_WINS];
	int		ioalloc;
	struct		pccard_io_handle io[EXCA_IO_WINS];
	bus_space_tag_t	bst;
	bus_space_handle_t bsh;
	uint32_t	flags;
#define EXCA_SOCKET_PRESENT	0x00000001
#define EXCA_HAS_MEMREG_WIN	0x00000002
#define EXCA_CARD_OK		0x00000004
#define EXCA_EVENT		0x80000000
	uint32_t	offset;
	int		chipset;
#define EXCA_CARDBUS	0
#define	EXCA_I82365	1		/* Intel i82365SL-A/B or clone */
#define EXCA_I82365SL_DF 2		/* Intel i82365sl-DF step */
#define	EXCA_VLSI	3		/* VLSI chip */
#define	EXCA_PD6710	4		/* Cirrus logic PD6710 */
#define	EXCA_PD6722	5		/* Cirrus logic PD6722 */
#define EXCA_PD6729	6		/* Cirrus Logic PD6729 */
#define	EXCA_VG365	7		/* Vadem 365 */
#define	EXCA_VG465      8		/* Vadem 465 */
#define	EXCA_VG468	9		/* Vadem 468 */
#define	EXCA_VG469	10		/* Vadem 469 */
#define	EXCA_RF5C296	11		/* Ricoh RF5C296 */
#define	EXCA_RF5C396	12		/* Ricoh RF5C396 */
#define	EXCA_IBM	13		/* IBM clone */
#define	EXCA_IBM_KING	14		/* IBM KING PCMCIA Controller */
#define EXCA_BOGUS	-1		/* Invalid/not present/etc */
	exca_getb_fn	*getb;
	exca_putb_fn	*putb;
	device_t	pccarddev;
	uint32_t	status;		/* status, hw dependent */
};

void exca_init(struct exca_softc *sc, device_t dev, 
    bus_space_tag_t, bus_space_handle_t, uint32_t);
void exca_insert(struct exca_softc *sc);
int exca_io_map(struct exca_softc *sc, int width, struct resource *r);
int exca_io_unmap_res(struct exca_softc *sc, struct resource *res);
int exca_is_pcic(struct exca_softc *sc);
int exca_mem_map(struct exca_softc *sc, int kind, struct resource *res);
int exca_mem_set_flags(struct exca_softc *sc, struct resource *res,
    uint32_t flags);
int exca_mem_set_offset(struct exca_softc *sc, struct resource *res,
    uint32_t cardaddr, uint32_t *deltap);
int exca_mem_unmap_res(struct exca_softc *sc, struct resource *res);
int exca_probe_slots(device_t dev, struct exca_softc *exca,
    bus_space_tag_t iot, bus_space_handle_t ioh);
void exca_removal(struct exca_softc *);
void exca_reset(struct exca_softc *, device_t child);

/* bus/device interfaces */
int exca_activate_resource(struct exca_softc *exca, device_t child, int type,
    int rid, struct resource *res);
int exca_deactivate_resource(struct exca_softc *exca, device_t child, int type,
    int rid, struct resource *res);

static __inline uint8_t
exca_getb(struct exca_softc *sc, int reg)
{
	return (sc->getb(sc, reg));
}

static __inline void
exca_putb(struct exca_softc *sc, int reg, uint8_t val)
{
	sc->putb(sc, reg, val);
}

static __inline void
exca_setb(struct exca_softc *sc, int reg, uint8_t mask)
{
	exca_putb(sc, reg, exca_getb(sc, reg) | mask);
}

static __inline void
exca_clrb(struct exca_softc *sc, int reg, uint8_t mask)
{
	exca_putb(sc, reg, exca_getb(sc, reg) & ~mask);
}

#endif /* !_SYS_DEV_EXCA_EXCAVAR_H */
