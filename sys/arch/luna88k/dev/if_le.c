/*	$OpenBSD: if_le.c,v 1.13 2024/11/28 13:13:04 aoyama Exp $	*/
/*	$NetBSD: if_le.c,v 1.33 1996/11/20 18:56:52 gwr Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Adam Glass and Gordon W. Ross.
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
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* based on OpenBSD: sys/arch/sun3/dev/if_le.c */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/device.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_media.h>

#include <machine/autoconf.h>
#include <machine/board.h>
#include <machine/cpu.h>

#include <dev/ic/lancereg.h>
#include <dev/ic/lancevar.h>
#include <dev/ic/am7990reg.h>
#include <dev/ic/am7990var.h>

#include <luna88k/luna88k/isr.h>

#define TRI_PORT_RAM_LANCE_OFFSET	0x10000	/* first 64KB is used by XP */

#ifndef TRI_PORT_RAM_LANCE_SIZE
#define TRI_PORT_RAM_LANCE_SIZE		0x10000	/* 64KB is the default */
#endif

/*
 * LANCE registers.
 * The real stuff is in dev/ic/am7990reg.h
 */
struct lereg1 {
	volatile uint16_t	ler1_rdp;	/* data port */
	volatile unsigned	:16 ;		/* 16bit gap */
	volatile uint16_t	ler1_rap;	/* register select port */
};

/*
 * Ethernet software status per interface.
 * The real stuff is in dev/ic/am7990var.h
 */
struct	le_softc {
	struct	am7990_softc sc_am7990;	/* glue to MI code */

	struct	lereg1 *sc_r1;		/* LANCE registers */
};

int	le_match(struct device *, void *, void *);
void	le_attach(struct device *, struct device *, void *);

const struct cfattach le_ca = {
	sizeof(struct le_softc), le_match, le_attach
};

void lewrcsr(struct lance_softc *, uint16_t, uint16_t);
uint16_t lerdcsr(struct lance_softc *, uint16_t);
void myetheraddr(uint8_t *);

void
lewrcsr(struct lance_softc *sc, uint16_t port, uint16_t val)
{
	register struct lereg1 *ler1 = ((struct le_softc *)sc)->sc_r1;

	ler1->ler1_rap = port;
	ler1->ler1_rdp = val;
}

uint16_t
lerdcsr(struct lance_softc *sc, uint16_t port)
{
	register struct lereg1 *ler1 = ((struct le_softc *)sc)->sc_r1;
	uint16_t val;

	ler1->ler1_rap = port;
	val = ler1->ler1_rdp;
	return (val);
}

int
le_match(struct device *parent, void *cf, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, le_cd.cd_name))
		return (0);

	return (1);
}

void
le_attach(struct device *parent, struct device *self, void *aux)
{
	struct le_softc *lesc = (struct le_softc *)self;
	struct lance_softc *sc = &lesc->sc_am7990.lsc;
	struct mainbus_attach_args *ma = aux;

	lesc->sc_r1 = (struct lereg1 *)ma->ma_addr;	/* LANCE */

	sc->sc_mem = (void *)(TRI_PORT_RAM + TRI_PORT_RAM_LANCE_OFFSET);
	sc->sc_conf3 = LE_C3_BSWP;
	sc->sc_addr = (u_long)sc->sc_mem & 0xffffff;
	sc->sc_memsize = TRI_PORT_RAM_LANCE_SIZE;

	myetheraddr(sc->sc_arpcom.ac_enaddr);

	sc->sc_copytodesc = lance_copytobuf_contig;
	sc->sc_copyfromdesc = lance_copyfrombuf_contig;
	sc->sc_copytobuf = lance_copytobuf_contig;
	sc->sc_copyfrombuf = lance_copyfrombuf_contig;
	sc->sc_zerobuf = lance_zerobuf_contig;

	sc->sc_rdcsr = lerdcsr;
	sc->sc_wrcsr = lewrcsr;
	sc->sc_hwreset = NULL;
	sc->sc_hwinit = NULL;

	am7990_config(&lesc->sc_am7990);

	isrlink_autovec(am7990_intr, (void *)sc, ma->ma_ilvl, ISRPRI_NET,
	    self->dv_xname);
}

/*
 * Partially taken from NetBSD/luna68k
 * 
 * LUNA-88K has FUSE ROM, which contains MAC address.  The FUSE ROM
 * contents are stored in fuse_rom_data[] during cpu_startup(). 
 * 
 * LUNA-88K2 has 16Kbit NVSRAM on its ethercard, whose contents are
 * accessible 4bit-wise by ctl register operation.  The register is
 * mapped at 0xF1000008.
 */

extern int machtype;
extern char fuse_rom_data[];

void
myetheraddr(uint8_t *ether)
{
	unsigned i, loc;
	volatile struct { uint32_t ctl; } *ds1220;

	switch (machtype) {
	case LUNA_88K:
		/*
		 * fuse_rom_data[] begins with "ENADDR=00000Axxxxxx"
		 */
		loc = 7;
		for (i = 0; i < 6; i++) {
			int u, l;

			u = fuse_rom_data[loc];
			u = (u < 'A') ? u & 0xf : u - 'A' + 10;
			l = fuse_rom_data[loc + 1];
			l = (l < 'A') ? l & 0xf : l - 'A' + 10;

			ether[i] = l | (u << 4);
			loc += 2;
		}
		break;
	case LUNA_88K2: 
		ds1220 = (void *)(LANCE_ADDR + 8);
		loc = 12;
		for (i = 0; i < 6; i++) {
			unsigned u, l, hex;

			ds1220->ctl = (loc) << 16;
			u = 0xf0 & (ds1220->ctl >> 12);
			ds1220->ctl = (loc + 1) << 16;
			l = 0x0f & (ds1220->ctl >> 16);
			hex = (u < '9') ? l : l + 9;

			ds1220->ctl = (loc + 2) << 16;
			u = 0xf0 & (ds1220->ctl >> 12);
			ds1220->ctl = (loc + 3) << 16;
			l = 0x0f & (ds1220->ctl >> 16);

			ether[i] = ((u < '9') ? l : l + 9) | (hex << 4);
			loc += 4;
		}
		break;
	default:
		ether[0] = 0x00; ether[1] = 0x00; ether[2] = 0x0a;
		ether[3] = 0xDE; ether[4] = 0xAD; ether[5] = 0x00;
		break;
	}
}
