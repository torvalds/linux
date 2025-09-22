/*	$OpenBSD: lance.c,v 1.4 2023/01/10 17:10:57 miod Exp $	*/
/*	$NetBSD: lance.c,v 1.1 2013/01/13 14:10:55 tsutsui Exp $	*/

/*
 * Copyright (c) 2013 Izumi Tsutsui.  All rights reserved.
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
/*-
 * Copyright (c) 2004 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by UCHIYAMA Yasushi.
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
 * LANCE driver for LUNA
 * based on sys/arch/ews4800mips/stand/common/lance.c
 */

#include <luna88k/stand/boot/samachdep.h>

#include <dev/ic/am7990reg.h>
#include <dev/ic/lancereg.h>

#include <luna88k/stand/boot/lance.h>

static void lance_setup(struct le_softc *);
static int lance_set_initblock(struct le_softc *);
static int lance_do_initialize(struct le_softc *);

#define NLE	1	/* XXX for now */
static struct le_softc lesc[NLE];

void *
lance_attach(uint unit, void *reg, void *mem, uint8_t *eaddr)
{
	struct le_softc *sc;

	if (unit >= NLE) {
		printf("%s: invalid unit number\n", __func__);
		return NULL;
	}
	sc = &lesc[unit];

	if (sc->sc_reg != NULL) {
		printf("%s: unit %d is already attached\n", __func__, unit);
		return NULL;
	}
	sc->sc_reg = reg;
	sc->sc_mem = mem;
	memcpy(sc->sc_enaddr, eaddr, 6);

	return sc;
}

void *
lance_cookie(uint unit)
{
	struct le_softc *sc;

	if (unit >= NLE)
		return NULL;

	sc = &lesc[unit];

	if (sc->sc_reg == NULL)
		return NULL;

	return sc;
}

uint8_t *
lance_eaddr(void *cookie)
{
	struct le_softc *sc = cookie;

	if (sc == NULL || sc->sc_reg == NULL)
		return NULL;

	return sc->sc_enaddr;
}

int
lance_init(void *cookie)
{
	struct le_softc *sc = cookie;

	lance_setup(sc);

	if (!lance_set_initblock(sc))
		return 0;

	if (!lance_do_initialize(sc))
		return 0;

	return 1;
}

int
lance_get(void *cookie, void *data, size_t maxlen)
{
	struct le_softc *sc = cookie;
	struct lereg *lereg = sc->sc_reg;
	struct lemem *lemem = sc->sc_mem;
	struct lermd_v *rmd;
	uint16_t csr;
	int len = -1;

	lereg->ler_rap = LE_CSR0;
	if ((lereg->ler_rdp & LE_C0_RINT) != 0)
		lereg->ler_rdp = LE_C0_RINT;
	rmd = &lemem->lem_rmd[sc->sc_currmd];
	if ((rmd->rmd1_bits & LE_R1_OWN) != 0)
		return -1;

	csr = lereg->ler_rdp;
#if 0
	if ((csr & LE_C0_ERR) != 0)
		printf("%s: RX poll error (CSR=0x%x)\n", __func__, csr);
#endif
	if ((rmd->rmd1_bits & LE_R1_ERR) != 0) {
		printf("%s: RX error (rmd status=0x%x)\n", __func__,
		    rmd->rmd1_bits);
		goto out;
	}

	len = rmd->rmd3;
	if (len < LEMINSIZE + 4 || len > LEMTU) {
		printf("%s: RX error (bad length %d)\n", __func__, len);
		goto out;
	}
	len -= 4;
	memcpy(data, (void *)lemem->lem_rbuf[sc->sc_currmd], min(len, maxlen));

 out:
	rmd->rmd2 = -LEMTU;
	rmd->rmd1_bits = LE_R1_OWN;	/* return to LANCE */
	sc->sc_currmd = LE_NEXTRMD(sc->sc_currmd);

	return len;
}

int
lance_put(void *cookie, void *data, size_t len)
{
	struct le_softc *sc = cookie;
	struct lereg *lereg = sc->sc_reg;
	struct lemem *lemem = sc->sc_mem;
	struct letmd_v *tmd;
	uint16_t stat;
	int timeout;

	lereg->ler_rap = LE_CSR0;
	stat = lereg->ler_rdp;
	lereg->ler_rdp =
	    stat & (LE_C0_BABL | LE_C0_CERR | LE_C0_MISS | LE_C0_TINT);
#if 0
	if (stat & (LE_C0_BABL | LE_C0_CERR | LE_C0_MISS | LE_C0_MERR))
		printf("%s: TX error before xmit csr0=0x%x\n",
		    __func__, stat);
#endif

	/* setup TX descriptor */
	tmd = &lemem->lem_tmd[sc->sc_curtmd];
	while (tmd->tmd1_bits & LE_T1_OWN)
		continue;
	tmd->tmd1_bits = LE_T1_STP | LE_T1_ENP;
	memcpy((void *)lemem->lem_tbuf[sc->sc_curtmd], data, len);
	tmd->tmd2 = -max(len, LEMINSIZE);
	tmd->tmd3 = 0;

	/* start TX */
	tmd->tmd1_bits |= LE_T1_OWN;
	lereg->ler_rap = LE_CSR0;
	lereg->ler_rdp = LE_C0_TDMD;

	/* check TX complete */
	timeout = 0;
	do {
		lereg->ler_rap = LE_CSR0;
		stat = lereg->ler_rdp;
#if 0
		if (stat & LE_C0_ERR) {
			printf("%s: TX error (CSR0=%x)\n", __func__, stat);
			if (stat & LE_C0_CERR) {
				lereg->ler_rdp = LE_C0_CERR;
			}
		}
#endif
		if (timeout++ > 1000) {
			printf("%s: TX timeout (CSR0=%x)\n", __func__, stat);
			return 0;
		}
	} while ((stat & LE_C0_TINT) == 0);

	lereg->ler_rdp = LE_C0_TINT;

	sc->sc_curtmd = LE_NEXTTMD(sc->sc_curtmd);

	return 1;
}

int
lance_end(void *cookie)
{
	struct le_softc *sc = cookie;
	struct lereg *lereg = sc->sc_reg;

	lereg->ler_rap = LE_CSR0;
	lereg->ler_rdp = LE_C0_STOP;

	return 1;
}

static int
lance_set_initblock(struct le_softc *sc)
{
	struct lereg *lereg = sc->sc_reg;
	uint32_t addr = (uint32_t)sc->sc_mem;

	lereg->ler_rap = LE_CSR0;
	lereg->ler_rdp = LE_C0_STOP;	/* disable all external activity */
	DELAY(100);

	/* Set the correct byte swapping mode */
	lereg->ler_rap = LE_CSR3;
	lereg->ler_rdp = LE_C3_BSWP;

	/* Low address of init block */
	lereg->ler_rap = LE_CSR1;
	lereg->ler_rdp = addr & 0xfffe;

	/* High address of init block */
	lereg->ler_rap = LE_CSR2;
	lereg->ler_rdp = (addr >> 16) & 0x00ff;
	DELAY(100);

	return 1;
}

static int
lance_do_initialize(struct le_softc *sc)
{
	struct lereg *lereg = sc->sc_reg;
	uint16_t reg;
	int timeout;

	sc->sc_curtmd = 0;
	sc->sc_currmd = 0;

	/* Initialize LANCE */
	lereg->ler_rap = LE_CSR0;
	lereg->ler_rdp = LE_C0_INIT;

	/* Wait interrupt */
	timeout = 1000000;
	do {
		lereg->ler_rap = LE_CSR0;
		reg = lereg->ler_rdp;
		if (--timeout == 0) {
			printf("le: init timeout (CSR=0x%x)\n", reg);
			return 0;
		}
		DELAY(1);
	} while ((reg & LE_C0_IDON) == 0);

	lereg->ler_rap = LE_CSR0;
	lereg->ler_rdp = LE_C0_STRT | LE_C0_IDON;

	return 1;
}

static void
lance_setup(struct le_softc *sc)
{
	struct lereg *lereg = sc->sc_reg;
	struct lemem *lemem = sc->sc_mem;
	uint32_t addr;
	int i;

	/* make sure to stop LANCE chip before setup memory */
	lereg->ler_rap = LE_CSR0;
	lereg->ler_rdp = LE_C0_STOP;

	memset(lemem, 0, sizeof *lemem);

	/* Init block */
	lemem->lem_mode = LE_MODE_NORMAL;
	lemem->lem_padr[0] = (sc->sc_enaddr[1] << 8) | sc->sc_enaddr[0];
	lemem->lem_padr[1] = (sc->sc_enaddr[3] << 8) | sc->sc_enaddr[2];
	lemem->lem_padr[2] = (sc->sc_enaddr[5] << 8) | sc->sc_enaddr[4];
	/* Logical address filter */
	for (i = 0; i < 4; i++)
		lemem->lem_ladrf[i] = 0x0000;

	/* Location of Rx descriptor ring */
	addr = (uint32_t)lemem->lem_rmd;
	lemem->lem_rdra = addr & 0xffff;
	lemem->lem_rlen = LE_RLEN | ((addr >> 16) & 0xff);

	/* Location of Tx descriptor ring */
	addr = (uint32_t)lemem->lem_tmd;
	lemem->lem_tdra = addr & 0xffff;
	lemem->lem_tlen = LE_TLEN | ((addr >> 16) & 0xff);

	/* Rx descriptor */
	for (i = 0; i < LERBUF; i++) {
		addr = (uint32_t)lemem->lem_rbuf[i];
		lemem->lem_rmd[i].rmd0 = addr & 0xffff;
		lemem->lem_rmd[i].rmd1_hadr = (addr >> 16) & 0xff;
		lemem->lem_rmd[i].rmd1_bits = LE_R1_OWN;
		lemem->lem_rmd[i].rmd2 = LE_XMD2_ONES | -LEMTU;
		lemem->lem_rmd[i].rmd3 = 0;
	}

	/* Tx descriptor */
	for (i = 0; i < LETBUF; i++) {
		addr = (uint32_t)lemem->lem_tbuf[i];
		lemem->lem_tmd[i].tmd0 = addr & 0xffff;
		lemem->lem_tmd[i].tmd1_hadr = (addr >> 16) & 0xff;
		lemem->lem_tmd[i].tmd1_bits = 0;
		lemem->lem_tmd[i].tmd2 = LE_XMD2_ONES | 0;
		lemem->lem_tmd[i].tmd3 = 0;
	}
}
