/*-
 * Copyright (c) 2017 Kevin Lo <kevlo@FreeBSD.org>
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

#include <dev/rtwn/rtl8192e/r92e.h>
#include <dev/rtwn/rtl8192e/r92e_reg.h>
#include <dev/rtwn/rtl8192e/r92e_priv.h>
#include <dev/rtwn/rtl8192e/r92e_var.h>

int
r92e_llt_init(struct rtwn_softc *sc)
{
	int ntries, error;

	error = rtwn_setbits_4(sc, R92C_AUTO_LLT, 0, R92C_AUTO_LLT_INIT);
	if (error != 0)
		return (error);
	for (ntries = 0; ntries < 1000; ntries++) {
		if (!(rtwn_read_4(sc, R92C_AUTO_LLT) & R92C_AUTO_LLT_INIT))
			return (0);
		rtwn_delay(sc, 1);
	}
	return (ETIMEDOUT);
}

static void
r92e_crystalcap_write(struct rtwn_softc *sc)
{
	struct r92e_softc *rs = sc->sc_priv;
	uint32_t reg;
	uint8_t val;

	val = rs->crystalcap & 0x3f;
	reg = rtwn_bb_read(sc, R92E_AFE_XTAL_CTRL);
	rtwn_bb_write(sc, R92E_AFE_XTAL_CTRL,
	    RW(reg, R92E_AFE_XTAL_CTRL_ADDR, val | val << 6));
	rtwn_bb_write(sc, R92C_AFE_XTAL_CTRL, 0x000f81fb);
}

void
r92e_init_bb(struct rtwn_softc *sc)
{
	int i, j;

	rtwn_setbits_2(sc, R92C_SYS_FUNC_EN, 0,
	    R92C_SYS_FUNC_EN_USBA | R92C_SYS_FUNC_EN_USBD);

	/* Enable BB and RF. */
	rtwn_setbits_2(sc, R92C_SYS_FUNC_EN, 0,
	    R92C_SYS_FUNC_EN_BBRSTB | R92C_SYS_FUNC_EN_BB_GLB_RST |
	    R92C_SYS_FUNC_EN_DIO_RF);

	/* PathA RF Power On. */
	rtwn_write_1(sc, R92C_RF_CTRL,
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

	rtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(0), 0x00040022);
	rtwn_delay(sc, 1);
	rtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(0), 0x00040020);
	rtwn_delay(sc, 1);

	r92e_crystalcap_write(sc);
}

void
r92e_init_rf(struct rtwn_softc *sc)
{
	struct r92e_softc *rs = sc->sc_priv;
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

		/* Cache RF register CHNLBW. */
		rs->rf_chnlbw[chain] = rtwn_rf_read(sc, chain, R92C_RF_CHNLBW);
	}

	/* Turn CCK and OFDM blocks on. */
	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, 0, R92C_RFMOD_CCK_EN);
	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, 0, R92C_RFMOD_OFDM_EN);
}

static void
r92e_adj_crystal(struct rtwn_softc *sc)
{

	rtwn_setbits_1(sc, R92C_AFE_PLL_CTRL, R92C_AFE_PLL_CTRL_FREF_SEL, 0);
	rtwn_setbits_4(sc, R92E_APE_PLL_CTRL_EXT, 0x00000380, 0);
	rtwn_setbits_1(sc, R92C_AFE_PLL_CTRL, 0x40, 0);
	rtwn_setbits_4(sc, R92E_APE_PLL_CTRL_EXT, 0x00200000, 0);
}

int
r92e_power_on(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)
	int ntries;

	if (rtwn_read_4(sc, R92C_SYS_CFG) & R92C_SYS_CFG_TRP_BT_EN)
		RTWN_CHK(rtwn_write_1(sc, R92C_LDO_SWR_CTRL, 0xc3));
	else {
		RTWN_CHK(rtwn_setbits_4(sc, R92E_LDOV12_CTRL, 0x00100000,
		    0x00500000));
		RTWN_CHK(rtwn_write_1(sc, R92C_LDO_SWR_CTRL, 0x83));
	}

	r92e_adj_crystal(sc);

	/* Enable WL suspend. */
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_AFSM_PCIE, 0, 1));

	/* Disable HWPDN, SW LPS and WL suspend. */
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_APFM_RSM | R92C_APS_FSMCO_AFSM_HSUS |
	    R92C_APS_FSMCO_AFSM_PCIE | R92C_APS_FSMCO_APDM_HPDN, 0, 1));

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

	/* Release WLON reset. */
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, 0,
	    R92C_APS_FSMCO_RDY_MACON, 2));

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
	RTWN_CHK(rtwn_write_2(sc, R92C_CR, 0));
	RTWN_CHK(rtwn_setbits_2(sc, R92C_CR, 0,
	    R92C_CR_HCI_TXDMA_EN | R92C_CR_TXDMA_EN |
	    R92C_CR_HCI_RXDMA_EN | R92C_CR_RXDMA_EN |
	    R92C_CR_PROTOCOL_EN | R92C_CR_SCHEDULE_EN |
	    ((sc->sc_hwcrypto != RTWN_CRYPTO_SW) ? R92C_CR_ENSEC : 0) |
	    R92C_CR_CALTMR_EN));

	return (0);
}

void
r92e_power_off(struct rtwn_softc *sc)
{
	int error, ntries;

	/* Stop Rx. */
	error = rtwn_write_1(sc, R92C_CR, 0);
	if (error == ENXIO)	/* hardware gone */
		return;

	/* Move card to Low Power state. */
	/* Block all Tx queues. */
	rtwn_write_1(sc, R92C_TXPAUSE, R92C_TX_QUEUE_ALL);

	for (ntries = 0; ntries < 5000; ntries++) {
		/* Should be zero if no packet is transmitting. */
		if (rtwn_read_4(sc, R88E_SCH_TXCMD) == 0)
			break;

		rtwn_delay(sc, 10);
	}
	if (ntries == 5000) {
		device_printf(sc->sc_dev, "%s: failed to block Tx queues\n",
		    __func__);
		return;
	}

	/* CCK and OFDM are disabled, and clock are gated. */
	rtwn_setbits_1(sc, R92C_SYS_FUNC_EN, R92C_SYS_FUNC_EN_BBRSTB, 0);

	rtwn_delay(sc, 1);

	/* Reset whole BB. */
	rtwn_setbits_1(sc, R92C_SYS_FUNC_EN, R92C_SYS_FUNC_EN_BB_GLB_RST, 0);

	/* Reset MAC TRX. */
	rtwn_write_1(sc, R92C_CR,
	    R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN);

	/* Check if removed later. */
	rtwn_setbits_1_shift(sc, R92C_CR, R92C_CR_ENSEC, 0, 1);

	/* Respond TxOK to scheduler */
	rtwn_setbits_1(sc, R92C_DUAL_TSF_RST, 0, R92C_DUAL_TSF_RST_TXOK);

	/* Reset MCU. */
	rtwn_write_1(sc, R92C_MCUFWDL, 0);

#ifndef RTWN_WITHOUT_UCODE
	/* Reset MCU IO wrapper. */
	rtwn_setbits_1(sc, R92C_RSV_CTRL + 1, 0x01, 0);

	rtwn_setbits_1_shift(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_CPUEN, 0, 1);

	/* Enable MCU IO wrapper. */
	rtwn_setbits_1(sc, R92C_RSV_CTRL + 1, 0, 0x01);
#endif

	/* Move card to Disabled state. */
	/* Turn off RF. */
	rtwn_write_1(sc, R92C_RF_CTRL, 0);

	/* Switch DPDT_SEL_P output. */
	rtwn_setbits_1(sc, R92C_LEDCFG2, 0x80, 0);

	/* Turn off MAC by HW state machine */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, 0, R92C_APS_FSMCO_APFM_OFF,
	    1);

	for (ntries = 0; ntries < 5000; ntries++) {
		/* Wait until it will be disabled. */
		if ((rtwn_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_OFF) == 0)
			break;

		rtwn_delay(sc, 10);
	}
	if (ntries == 5000) {
		device_printf(sc->sc_dev, "%s: could not turn off MAC\n",
		    __func__);
		return;
	}

	/* SOP option to disable BG/MB. */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, 0xff,
	   R92C_APS_FSMCO_SOP_RCK, 3);

	/* Unlock small LDO Register. */
	rtwn_setbits_1(sc, 0xcc, 0, 0x4);

	/* Disable small LDO. */
	rtwn_setbits_1(sc, R92C_SPS0_CTRL, 0x1, 0);

	/* Enable WL suspend. */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, R92C_APS_FSMCO_AFSM_PCIE,
	    R92C_APS_FSMCO_AFSM_HSUS, 1);

	/* Enable SW LPS. */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, 0,
	    R92C_APS_FSMCO_APFM_RSM, 1);
}
