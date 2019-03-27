/*-
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
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

#include <dev/rtwn/rtl8812a/r12a.h>
#include <dev/rtwn/rtl8812a/r12a_priv.h>
#include <dev/rtwn/rtl8812a/r12a_reg.h>
#include <dev/rtwn/rtl8812a/r12a_var.h>


int
r12a_check_condition(struct rtwn_softc *sc, const uint8_t cond[])
{
	struct r12a_softc *rs = sc->sc_priv;
	uint8_t mask[4];
	int i, j, nmasks;

	RTWN_DPRINTF(sc, RTWN_DEBUG_RESET,
	    "%s: condition byte 0: %02X; ext PA/LNA: %d/%d (2 GHz), "
	    "%d/%d (5 GHz)\n", __func__, cond[0], rs->ext_pa_2g,
	    rs->ext_lna_2g, rs->ext_pa_5g, rs->ext_lna_5g);

	if (cond[0] == 0)
		return (1);

	if (!rs->ext_pa_2g && !rs->ext_lna_2g &&
	    !rs->ext_pa_5g && !rs->ext_lna_5g)
		return (0);

	nmasks = 0;
	if (rs->ext_pa_2g) {
		mask[nmasks] = R12A_COND_GPA;
		mask[nmasks] |= R12A_COND_TYPE(rs->type_pa_2g);
		nmasks++;
	}
	if (rs->ext_pa_5g) {
		mask[nmasks] = R12A_COND_APA;
		mask[nmasks] |= R12A_COND_TYPE(rs->type_pa_5g);
		nmasks++;
	}
	if (rs->ext_lna_2g) {
		mask[nmasks] = R12A_COND_GLNA;
		mask[nmasks] |= R12A_COND_TYPE(rs->type_lna_2g);
		nmasks++;
	}
	if (rs->ext_lna_5g) {
		mask[nmasks] = R12A_COND_ALNA;
		mask[nmasks] |= R12A_COND_TYPE(rs->type_lna_5g);
		nmasks++;
	}

	for (i = 0; i < RTWN_MAX_CONDITIONS && cond[i] != 0; i++)
		for (j = 0; j < nmasks; j++)
			if ((cond[i] & mask[j]) == mask[j])
				return (1);

	return (0);
}

int
r12a_set_page_size(struct rtwn_softc *sc)
{
	return (rtwn_setbits_1(sc, R92C_PBP, R92C_PBP_PSTX_M,
	    R92C_PBP_512 << R92C_PBP_PSTX_S) == 0);
}

void
r12a_init_edca(struct rtwn_softc *sc)
{
	r92c_init_edca(sc);

	/* 80 MHz clock */
	rtwn_write_1(sc, R92C_USTIME_TSF, 0x50);
	rtwn_write_1(sc, R92C_USTIME_EDCA, 0x50);
}

void
r12a_init_bb(struct rtwn_softc *sc)
{
	int i, j;

	rtwn_setbits_1(sc, R92C_SYS_FUNC_EN, 0, R92C_SYS_FUNC_EN_USBA);

	/* Enable BB and RF. */
	rtwn_setbits_1(sc, R92C_SYS_FUNC_EN, 0,
	    R92C_SYS_FUNC_EN_BBRSTB | R92C_SYS_FUNC_EN_BB_GLB_RST);

	/* PathA RF Power On. */
	rtwn_write_1(sc, R92C_RF_CTRL,
	    R92C_RF_CTRL_EN | R92C_RF_CTRL_RSTB | R92C_RF_CTRL_SDMRSTB);

	/* PathB RF Power On. */
	rtwn_write_1(sc, R12A_RF_B_CTRL,
	    R92C_RF_CTRL_EN | R92C_RF_CTRL_RSTB | R92C_RF_CTRL_SDMRSTB);

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

	/* XXX meshpoint mode? */

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

			rtwn_bb_write(sc, 0x81c, agc_prog->val[j]);
			rtwn_delay(sc, 1);
		}
	}

	for (i = 0; i < sc->nrxchains; i++) {
		rtwn_bb_write(sc, R12A_INITIAL_GAIN(i), 0x22);
		rtwn_delay(sc, 1);
		rtwn_bb_write(sc, R12A_INITIAL_GAIN(i), 0x20);
		rtwn_delay(sc, 1);
	}

	rtwn_r12a_crystalcap_write(sc);

	if (rtwn_bb_read(sc, R12A_CCK_RPT_FORMAT) & R12A_CCK_RPT_FORMAT_HIPWR)
		sc->sc_flags |= RTWN_FLAG_CCK_HIPWR;
}

void
r12a_init_rf(struct rtwn_softc *sc)
{
	int chain, i;

	for (chain = 0, i = 0; chain < sc->nrxchains; chain++, i++) {
		/* Write RF initialization values for this chain. */
		i += r92c_init_rf_chain(sc, &sc->rf_prog[i], chain);
	}
}

void
r12a_crystalcap_write(struct rtwn_softc *sc)
{
	struct r12a_softc *rs = sc->sc_priv;
	uint32_t reg;
	uint8_t val;

	val = rs->crystalcap & 0x3f;
	reg = rtwn_bb_read(sc, R92C_MAC_PHY_CTRL);
	reg = RW(reg, R12A_MAC_PHY_CRYSTALCAP, val | (val << 6));
	rtwn_bb_write(sc, R92C_MAC_PHY_CTRL, reg);
}

static void
r12a_rf_init_workaround(struct rtwn_softc *sc)
{

	rtwn_write_1(sc, R92C_RF_CTRL,
	    R92C_RF_CTRL_EN | R92C_RF_CTRL_SDMRSTB);
	rtwn_write_1(sc, R92C_RF_CTRL,
	    R92C_RF_CTRL_EN | R92C_RF_CTRL_RSTB |
	    R92C_RF_CTRL_SDMRSTB);
	rtwn_write_1(sc, R12A_RF_B_CTRL,
	    R92C_RF_CTRL_EN | R92C_RF_CTRL_SDMRSTB);
	rtwn_write_1(sc, R12A_RF_B_CTRL,
	    R92C_RF_CTRL_EN | R92C_RF_CTRL_RSTB |
	    R92C_RF_CTRL_SDMRSTB);
}

int
r12a_power_on(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)
	int ntries;

	r12a_rf_init_workaround(sc);

	/* Force PWM mode. */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_SPS0_CTRL + 1, 0, 0x01));

	/* Turn off ZCD. */
	RTWN_CHK(rtwn_setbits_2(sc, 0x014, 0x0180, 0));

	/* Enable LDO normal mode. */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_LPLDO_CTRL, R92C_LPLDO_CTRL_SLEEP,
	    0));

	/* GPIO 0...7 input mode. */
	RTWN_CHK(rtwn_write_1(sc, R92C_GPIO_IOSEL, 0));

	/* GPIO 11...8 input mode. */
	RTWN_CHK(rtwn_write_1(sc, R92C_MAC_PINMUX_CFG, 0));

	/* Enable WL suspend. */
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_HSUS, 0, 1));

	/* Enable 8051. */
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_SYS_FUNC_EN,
	    0, R92C_SYS_FUNC_EN_CPUEN, 1));

	/* Disable SW LPS. */
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_APFM_RSM, 0, 1));

	/* Wait for power ready bit. */
	for (ntries = 0; ntries < 5000; ntries++) {
		if (rtwn_read_4(sc, R92C_APS_FSMCO) & R92C_APS_FSMCO_SUS_HOST)
			break;
		rtwn_delay(sc, 10);
	}
	if (ntries == 5000) {
		device_printf(sc->sc_dev,
		    "timeout waiting for chip power up\n");
		return (ETIMEDOUT);
	}

	/* Disable WL suspend. */
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_HSUS, 0, 1));

	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, 0,
	    R92C_APS_FSMCO_APFM_ONMAC, 1));
	for (ntries = 0; ntries < 5000; ntries++) {
		if (!(rtwn_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_ONMAC))
			break;
		rtwn_delay(sc, 10);
	}
	if (ntries == 5000)
		return (ETIMEDOUT);

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC blocks. */
	RTWN_CHK(rtwn_write_2(sc, R92C_CR, 0x0000));
	RTWN_CHK(rtwn_setbits_2(sc, R92C_CR, 0,
	    R92C_CR_HCI_TXDMA_EN | R92C_CR_TXDMA_EN |
	    R92C_CR_HCI_RXDMA_EN | R92C_CR_RXDMA_EN |
	    R92C_CR_PROTOCOL_EN | R92C_CR_SCHEDULE_EN |
	    ((sc->sc_hwcrypto != RTWN_CRYPTO_SW) ? R92C_CR_ENSEC : 0) |
	    R92C_CR_CALTMR_EN));

	return (0);
}

void
r12a_power_off(struct rtwn_softc *sc)
{
	struct r12a_softc *rs = sc->sc_priv;
	int error, ntries;

	/* Stop Rx. */
	error = rtwn_write_1(sc, R92C_CR, 0);
	if (error == ENXIO)	/* hardware gone */
		return;

	/* Move card to Low Power state. */
	/* Block all Tx queues. */
	rtwn_write_1(sc, R92C_TXPAUSE, R92C_TX_QUEUE_ALL);

	for (ntries = 0; ntries < 10; ntries++) {
		/* Should be zero if no packet is transmitting. */
		if (rtwn_read_4(sc, R88E_SCH_TXCMD) == 0)
			break;

		rtwn_delay(sc, 5000);
	}
	if (ntries == 10) {
		device_printf(sc->sc_dev, "%s: failed to block Tx queues\n",
		    __func__);
		return;
	}

	/* Turn off 3-wire. */
	rtwn_write_1(sc, R12A_HSSI_PARAM1(0), 0x04);
	rtwn_write_1(sc, R12A_HSSI_PARAM1(1), 0x04);

	/* CCK and OFDM are disabled, and clock are gated. */
	rtwn_setbits_1(sc, R92C_SYS_FUNC_EN, R92C_SYS_FUNC_EN_BBRSTB, 0);

	rtwn_delay(sc, 1);

	/* Reset whole BB. */
	rtwn_setbits_1(sc, R92C_SYS_FUNC_EN, R92C_SYS_FUNC_EN_BB_GLB_RST, 0);

	/* Reset MAC TRX. */
	rtwn_write_1(sc, R92C_CR,
	    R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN);

	/* check if removed later. (?) */
	rtwn_setbits_1_shift(sc, R92C_CR, R92C_CR_ENSEC, 0, 1);

	/* Respond TxOK to scheduler */
	rtwn_setbits_1(sc, R92C_DUAL_TSF_RST, 0, R92C_DUAL_TSF_RST_TXOK);

	/* If firmware in ram code, do reset. */
#ifndef RTWN_WITHOUT_UCODE
	if (rtwn_read_1(sc, R92C_MCUFWDL) & R92C_MCUFWDL_RAM_DL_SEL)
		r12a_fw_reset(sc, RTWN_FW_RESET_SHUTDOWN);
#endif

	/* Reset MCU. */
	rtwn_setbits_1_shift(sc, R92C_SYS_FUNC_EN, R92C_SYS_FUNC_EN_CPUEN,
	    0, 1);
	rtwn_write_1(sc, R92C_MCUFWDL, 0);

	/* Move card to Disabled state. */
	/* Turn off 3-wire. */
	rtwn_write_1(sc, R12A_HSSI_PARAM1(0), 0x04);
	rtwn_write_1(sc, R12A_HSSI_PARAM1(1), 0x04);

	/* Reset BB, close RF. */
	rtwn_setbits_1(sc, R92C_SYS_FUNC_EN, R92C_SYS_FUNC_EN_BB_GLB_RST, 0);

	rtwn_delay(sc, 1);

	/* SPS PWM mode. */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, 0xff,
	    R92C_APS_FSMCO_SOP_RCK | R92C_APS_FSMCO_SOP_ABG, 3);

	/* ANA clock = 500k. */
	rtwn_setbits_1(sc, R92C_SYS_CLKR, R92C_SYS_CLKR_ANA8M, 0);

	/* Turn off MAC by HW state machine */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, 0, R92C_APS_FSMCO_APFM_OFF,
	    1);
	for (ntries = 0; ntries < 10; ntries++) {
		/* Wait until it will be disabled. */
		if ((rtwn_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_OFF) == 0)
			break;

		rtwn_delay(sc, 5000);
	}
	if (ntries == 10) {
		device_printf(sc->sc_dev, "%s: could not turn off MAC\n",
		    __func__);
		return;
	}

	/* Reset 8051. */
	rtwn_setbits_1_shift(sc, R92C_SYS_FUNC_EN, R92C_SYS_FUNC_EN_CPUEN,
	    0, 1);

	/* Fill the default value of host_CPU handshake field. */
	rtwn_write_1(sc, R92C_MCUFWDL,
	    R92C_MCUFWDL_EN | R92C_MCUFWDL_CHKSUM_RPT);

	rtwn_setbits_1(sc, R92C_GPIO_IO_SEL, 0xf0, 0xc0);

	/* GPIO 11 input mode, 10...8 output mode. */
	rtwn_write_1(sc, R92C_MAC_PINMUX_CFG, 0x07);

	/* GPIO 7...0, output = input */
	rtwn_write_1(sc, R92C_GPIO_OUT, 0);

	/* GPIO 7...0 output mode. */
	rtwn_write_1(sc, R92C_GPIO_IOSEL, 0xff);

	rtwn_write_1(sc, R92C_GPIO_MOD, 0);

	/* Turn on ZCD. */
	rtwn_setbits_2(sc, 0x014, 0, 0x0180);

	/* Force PFM mode. */
	rtwn_setbits_1(sc, R92C_SPS0_CTRL + 1, 0x01, 0);

	/* LDO sleep mode. */
	rtwn_setbits_1(sc, R92C_LPLDO_CTRL, 0, R92C_LPLDO_CTRL_SLEEP);

	/* ANA clock = 500k. */
	rtwn_setbits_1(sc, R92C_SYS_CLKR, R92C_SYS_CLKR_ANA8M, 0);

	/* SOP option to disable BG/MB. */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, 0xff,
	    R92C_APS_FSMCO_SOP_RCK, 3);

	/* Disable RFC_0. */
	rtwn_setbits_1(sc, R92C_RF_CTRL, R92C_RF_CTRL_RSTB, 0);

	/* Disable RFC_1. */
	rtwn_setbits_1(sc, R12A_RF_B_CTRL, R92C_RF_CTRL_RSTB, 0);

	/* Enable WL suspend. */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, 0, R92C_APS_FSMCO_AFSM_HSUS,
	    1);

	rs->rs_flags &= ~R12A_IQK_RUNNING;
}

void
r12a_init_intr(struct rtwn_softc *sc)
{
	rtwn_write_4(sc, R88E_HIMR, 0);
	rtwn_write_4(sc, R88E_HIMRE, 0);
}

void
r12a_init_antsel(struct rtwn_softc *sc)
{
	uint32_t reg;

	rtwn_write_1(sc, R92C_LEDCFG2, 0x82);
	rtwn_bb_setbits(sc, R92C_FPGA0_RFPARAM(0), 0, 0x2000);
	reg = rtwn_bb_read(sc, R92C_FPGA0_RFIFACEOE(0));
	sc->sc_ant = MS(reg, R92C_FPGA0_RFIFACEOE0_ANT);
}
