/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/linker.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/if_rtwn_debug.h>

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8192c/r92c_priv.h>
#include <dev/rtwn/rtl8192c/r92c_reg.h>
#include <dev/rtwn/rtl8192c/r92c_var.h>


int
r92c_check_condition(struct rtwn_softc *sc, const uint8_t cond[])
{
	struct r92c_softc *rs = sc->sc_priv;
	uint8_t mask;
	int i;

	if (cond[0] == 0)
		return (1);

	RTWN_DPRINTF(sc, RTWN_DEBUG_RESET,
	    "%s: condition byte 0: %02X; chip %02X, board %02X\n",
	    __func__, cond[0], rs->chip, rs->board_type);

	if (!(rs->chip & R92C_CHIP_92C)) {
		if (rs->board_type == R92C_BOARD_TYPE_HIGHPA)
			mask = R92C_COND_RTL8188RU;
		else if (rs->board_type == R92C_BOARD_TYPE_MINICARD)
			mask = R92C_COND_RTL8188CE;
		else
			mask = R92C_COND_RTL8188CU;
	} else {
		if (rs->board_type == R92C_BOARD_TYPE_MINICARD)
			mask = R92C_COND_RTL8192CE;
		else
			mask = R92C_COND_RTL8192CU;
	}

	for (i = 0; i < RTWN_MAX_CONDITIONS && cond[i] != 0; i++)
		if ((cond[i] & mask) == mask)
			return (1);

	return (0);
}

int
r92c_llt_init(struct rtwn_softc *sc)
{
	int i, error;

	/* Reserve pages [0; page_count]. */
	for (i = 0; i < sc->page_count; i++) {
		if ((error = r92c_llt_write(sc, i, i + 1)) != 0)
			return (error);
	}
	/* NB: 0xff indicates end-of-list. */
	if ((error = r92c_llt_write(sc, i, 0xff)) != 0)
		return (error);
	/*
	 * Use pages [page_count + 1; pktbuf_count - 1]
	 * as ring buffer.
	 */
	for (++i; i < sc->pktbuf_count - 1; i++) {
		if ((error = r92c_llt_write(sc, i, i + 1)) != 0)
			return (error);
	}
	/* Make the last page point to the beginning of the ring buffer. */
	error = r92c_llt_write(sc, i, sc->page_count + 1);
	return (error);
}

int
r92c_set_page_size(struct rtwn_softc *sc)
{
	return (rtwn_write_1(sc, R92C_PBP, SM(R92C_PBP_PSRX, R92C_PBP_128) |
	    SM(R92C_PBP_PSTX, R92C_PBP_128)) == 0);
}

void
r92c_init_bb_common(struct rtwn_softc *sc)
{
	struct r92c_softc *rs = sc->sc_priv;
	int i, j;

	/* Write BB initialization values. */
	for (i = 0; i < sc->bb_size; i++) {
		const struct rtwn_bb_prog *bb_prog = &sc->bb_prog[i];

		while (!rtwn_check_condition(sc, bb_prog->cond)) {
			KASSERT(bb_prog->next != NULL,
			    ("%s: wrong condition value (i %d)\n",
			    __func__, i));
			bb_prog = bb_prog->next;
		}

		for (j = 0; j < bb_prog->count; j++) {
			RTWN_DPRINTF(sc, RTWN_DEBUG_RESET,
			    "BB: reg 0x%03x, val 0x%08x\n",
			    bb_prog->reg[j], bb_prog->val[j]);

			rtwn_bb_write(sc, bb_prog->reg[j], bb_prog->val[j]);
			rtwn_delay(sc, 1);
		}
	}

	if (rs->chip & R92C_CHIP_92C_1T2R) {
		/* 8192C 1T only configuration. */
		rtwn_bb_setbits(sc, R92C_FPGA0_TXINFO, 0x03, 0x02);
		rtwn_bb_setbits(sc, R92C_FPGA1_TXINFO, 0x300033, 0x200022);
		rtwn_bb_setbits(sc, R92C_CCK0_AFESETTING, 0xff000000,
		    0x45000000);
		rtwn_bb_setbits(sc, R92C_OFDM0_TRXPATHENA, 0xff, 0x23);
		rtwn_bb_setbits(sc, R92C_OFDM0_AGCPARAM1, 0x30, 0x10);

		rtwn_bb_setbits(sc, 0xe74, 0x0c000000, 0x08000000);
		rtwn_bb_setbits(sc, 0xe78, 0x0c000000, 0x08000000);
		rtwn_bb_setbits(sc, 0xe7c, 0x0c000000, 0x08000000);
		rtwn_bb_setbits(sc, 0xe80, 0x0c000000, 0x08000000);
		rtwn_bb_setbits(sc, 0xe88, 0x0c000000, 0x08000000);
	}

	/* Write AGC values. */
	for (i = 0; i < sc->agc_size; i++) {
		const struct rtwn_agc_prog *agc_prog = &sc->agc_prog[i];

		while (!rtwn_check_condition(sc, agc_prog->cond)) {
			KASSERT(agc_prog->next != NULL,
			    ("%s: wrong condition value (2) (i %d)\n",
			    __func__, i));
			agc_prog = agc_prog->next;
		}

		for (j = 0; j < agc_prog->count; j++) {
			RTWN_DPRINTF(sc, RTWN_DEBUG_RESET,
			    "AGC: val 0x%08x\n", agc_prog->val[j]);

			rtwn_bb_write(sc, R92C_OFDM0_AGCRSSITABLE,
			    agc_prog->val[j]);
			rtwn_delay(sc, 1);
		}
	}

	if (rtwn_bb_read(sc, R92C_HSSI_PARAM2(0)) & R92C_HSSI_PARAM2_CCK_HIPWR)
		sc->sc_flags |= RTWN_FLAG_CCK_HIPWR;
}

int
r92c_init_rf_chain(struct rtwn_softc *sc,
    const struct rtwn_rf_prog *rf_prog, int chain)
{
	int i, j;

	RTWN_DPRINTF(sc, RTWN_DEBUG_RESET, "%s: chain %d\n",
	    __func__, chain);

	for (i = 0; rf_prog[i].reg != NULL; i++) {
		const struct rtwn_rf_prog *prog = &rf_prog[i];

		while (!rtwn_check_condition(sc, prog->cond)) {
			KASSERT(prog->next != NULL,
			    ("%s: wrong condition value (i %d)\n",
			    __func__, i));
			prog = prog->next;
		}

		for (j = 0; j < prog->count; j++) {
			RTWN_DPRINTF(sc, RTWN_DEBUG_RESET,
			    "RF: reg 0x%02x, val 0x%05x\n",
			    prog->reg[j], prog->val[j]);

			/*
			 * These are fake RF registers offsets that
			 * indicate a delay is required.
			 */
			/* NB: we are using 'value' to store required delay. */
			if (prog->reg[j] > 0xf8) {
				rtwn_delay(sc, prog->val[j]);
				continue;
			}

			rtwn_rf_write(sc, chain, prog->reg[j], prog->val[j]);
			rtwn_delay(sc, 1);
		}
	}

	return (i);
}

void
r92c_init_rf(struct rtwn_softc *sc)
{
	struct r92c_softc *rs = sc->sc_priv;
	uint32_t reg, type;
	int i, chain, idx, off;

	for (chain = 0, i = 0; chain < sc->nrxchains; chain++, i++) {
		/* Save RF_ENV control type. */
		idx = chain / 2;
		off = (chain % 2) * 16;
		reg = rtwn_bb_read(sc, R92C_FPGA0_RFIFACESW(idx));
		type = (reg >> off) & 0x10;

		/* Set RF_ENV enable. */
		rtwn_bb_setbits(sc, R92C_FPGA0_RFIFACEOE(chain),
		    0, 0x100000);
		rtwn_delay(sc, 1);
		/* Set RF_ENV output high. */
		rtwn_bb_setbits(sc, R92C_FPGA0_RFIFACEOE(chain),
		    0, 0x10);
		rtwn_delay(sc, 1);
		/* Set address and data lengths of RF registers. */
		rtwn_bb_setbits(sc, R92C_HSSI_PARAM2(chain),
		    R92C_HSSI_PARAM2_ADDR_LENGTH, 0);
		rtwn_delay(sc, 1);
		rtwn_bb_setbits(sc, R92C_HSSI_PARAM2(chain),
		    R92C_HSSI_PARAM2_DATA_LENGTH, 0);
		rtwn_delay(sc, 1);

		/* Write RF initialization values for this chain. */
		i += r92c_init_rf_chain(sc, &sc->rf_prog[i], chain);

		/* Restore RF_ENV control type. */
		rtwn_bb_setbits(sc, R92C_FPGA0_RFIFACESW(idx),
		    0x10 << off, type << off);

		/* Cache RF register CHNLBW. */
		rs->rf_chnlbw[chain] = rtwn_rf_read(sc, chain,
		    R92C_RF_CHNLBW);
	}

	if ((rs->chip & (R92C_CHIP_UMC_A_CUT | R92C_CHIP_92C)) ==
	    R92C_CHIP_UMC_A_CUT) {
		rtwn_rf_write(sc, 0, R92C_RF_RX_G1, 0x30255);
		rtwn_rf_write(sc, 0, R92C_RF_RX_G2, 0x50a00);
	}

	/* Turn CCK and OFDM blocks on. */
	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, 0, R92C_RFMOD_CCK_EN);
	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, 0, R92C_RFMOD_OFDM_EN);
}

void
r92c_init_edca(struct rtwn_softc *sc)
{
	/* SIFS */
	rtwn_write_2(sc, R92C_SPEC_SIFS, 0x100a);
	rtwn_write_2(sc, R92C_MAC_SPEC_SIFS, 0x100a);
	rtwn_write_2(sc, R92C_SIFS_CCK, 0x100a);
	rtwn_write_2(sc, R92C_SIFS_OFDM, 0x100a);
	/* TXOP */
	rtwn_write_4(sc, R92C_EDCA_BE_PARAM, 0x005ea42b);
	rtwn_write_4(sc, R92C_EDCA_BK_PARAM, 0x0000a44f);
	rtwn_write_4(sc, R92C_EDCA_VI_PARAM, 0x005ea324);
	rtwn_write_4(sc, R92C_EDCA_VO_PARAM, 0x002fa226);
}

void
r92c_init_ampdu(struct rtwn_softc *sc)
{

	/* Setup AMPDU aggregation. */
	rtwn_write_4(sc, R92C_AGGLEN_LMT, 0x99997631);	/* MCS7~0 */
	rtwn_write_1(sc, R92C_AGGR_BREAK_TIME, 0x16);
	rtwn_write_2(sc, R92C_MAX_AGGR_NUM, 0x0708);
}

void
r92c_init_antsel(struct rtwn_softc *sc)
{
	uint32_t reg;

	if (sc->ntxchains != 1 || sc->nrxchains != 1)
		return;

	rtwn_setbits_1(sc, R92C_LEDCFG2, 0, 0x80);
	rtwn_bb_setbits(sc, R92C_FPGA0_RFPARAM(0), 0, 0x2000);
	reg = rtwn_bb_read(sc, R92C_FPGA0_RFIFACEOE(0));
	sc->sc_ant = MS(reg, R92C_FPGA0_RFIFACEOE0_ANT);	/* XXX */
	rtwn_setbits_1(sc, R92C_LEDCFG2, 0x80, 0);
}

void
r92c_pa_bias_init(struct rtwn_softc *sc)
{
	struct r92c_softc *rs = sc->sc_priv;
	int i;

	for (i = 0; i < sc->nrxchains; i++) {
		if (rs->pa_setting & (1 << i))
			continue;
		r92c_rf_write(sc, i, R92C_RF_IPA, 0x0f406);
		r92c_rf_write(sc, i, R92C_RF_IPA, 0x4f406);
		r92c_rf_write(sc, i, R92C_RF_IPA, 0x8f406);
		r92c_rf_write(sc, i, R92C_RF_IPA, 0xcf406);
	}
	if (!(rs->pa_setting & 0x10))
		rtwn_setbits_1(sc, 0x16, 0xf0, 0x90);
}
