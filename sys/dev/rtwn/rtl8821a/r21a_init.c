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

#include <dev/rtwn/rtl8812a/r12a_var.h>

#include <dev/rtwn/rtl8821a/r21a.h>
#include <dev/rtwn/rtl8821a/r21a_priv.h>
#include <dev/rtwn/rtl8821a/r21a_reg.h>


int
r21a_power_on(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)
	int ntries;

	/* Clear suspend and power down bits.*/
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_APDM_HPDN, 0, 1));

	/* Disable GPIO9 as EXT WAKEUP. */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_GPIO_INTM + 2, 0x01, 0));

	/* Enable WL suspend. */
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_AFSM_PCIE, 0, 1));

	/* Enable LDOA12 MACRO block for all interfaces. */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_LDOA15_CTRL, 0, R92C_LDOA15_CTRL_EN));

	/* Disable BT_GPS_SEL pins. */
	RTWN_CHK(rtwn_setbits_1(sc, 0x067, 0x10, 0));

	/* 1 ms delay. */
	rtwn_delay(sc, 1000);

	/* Release analog Ips to digital isolation. */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_SYS_ISO_CTRL,
	    R92C_SYS_ISO_CTRL_IP2MAC, 0));

	/* Disable SW LPS and WL suspend. */
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_APFM_RSM |
	    R92C_APS_FSMCO_AFSM_HSUS |
	    R92C_APS_FSMCO_AFSM_PCIE, 0, 1));

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

	/* Disable HWPDN. */
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_APDM_HPDN, 0, 1));

	/* Disable WL suspend. */
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_AFSM_PCIE, 0, 1));

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

	/* Switch DPDT_SEL_P output from WL BB. */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_LEDCFG3, 0, 0x01));

	/* switch for PAPE_G/PAPE_A from WL BB; switch LNAON from WL BB. */
	RTWN_CHK(rtwn_setbits_1(sc, 0x067, 0, 0x30));

	RTWN_CHK(rtwn_setbits_1(sc, 0x025, 0x40, 0));

	/* Enable falling edge triggering interrupt. */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_GPIO_INTM + 1, 0, 0x02));

	/* Enable GPIO9 interrupt mode. */
	RTWN_CHK(rtwn_setbits_1(sc, 0x063, 0, 0x02));

	/* Enable GPIO9 input mode. */
	RTWN_CHK(rtwn_setbits_1(sc, 0x062, 0x02, 0));

	/* Enable HSISR GPIO interrupt. */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_HSIMR, 0, 0x01));

	/* Enable HSISR GPIO9 interrupt. */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_HSIMR + 2, 0, 0x02));

	/* XTAL trim. */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_APE_PLL_CTRL_EXT + 2, 0xFF, 0x82));

	RTWN_CHK(rtwn_setbits_1(sc, R92C_AFE_MISC, 0, 0x40));

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC blocks. */
	RTWN_CHK(rtwn_write_2(sc, R92C_CR, 0x0000));
	RTWN_CHK(rtwn_setbits_2(sc, R92C_CR, 0,
	    R92C_CR_HCI_TXDMA_EN | R92C_CR_TXDMA_EN |
	    R92C_CR_HCI_RXDMA_EN | R92C_CR_RXDMA_EN |
	    R92C_CR_PROTOCOL_EN | R92C_CR_SCHEDULE_EN |
	    ((sc->sc_hwcrypto != RTWN_CRYPTO_SW) ? R92C_CR_ENSEC : 0) |
	    R92C_CR_CALTMR_EN));

	if (rtwn_read_4(sc, R92C_SYS_CFG) & R92C_SYS_CFG_TRP_BT_EN)
		RTWN_CHK(rtwn_setbits_1(sc, R92C_LDO_SWR_CTRL, 0, 0x40));

	return (0);
#undef RTWN_CHK
}

void
r21a_power_off(struct rtwn_softc *sc)
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
		r21a_fw_reset(sc, RTWN_FW_RESET_SHUTDOWN);
#endif

	/* Reset MCU. */
	rtwn_setbits_1_shift(sc, R92C_SYS_FUNC_EN, R92C_SYS_FUNC_EN_CPUEN,
	    0, 1);
	rtwn_write_1(sc, R92C_MCUFWDL, 0);

	/* Move card to Disabled state. */
	/* Turn off RF. */
	rtwn_write_1(sc, R92C_RF_CTRL, 0);

	rtwn_setbits_1(sc, R92C_LEDCFG3, 0x01, 0);

	/* Enable rising edge triggering interrupt. */
	rtwn_setbits_1(sc, R92C_GPIO_INTM + 1, 0x02, 0);

	/* Release WLON reset. */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, 0,
	    R92C_APS_FSMCO_RDY_MACON, 2);

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

	/* Analog Ips to digital isolation. */
	rtwn_setbits_1(sc, R92C_SYS_ISO_CTRL, 0, R92C_SYS_ISO_CTRL_IP2MAC);

	/* Disable LDOA12 MACRO block. */
	rtwn_setbits_1(sc, R92C_LDOA15_CTRL, R92C_LDOA15_CTRL_EN, 0);

	/* Enable WL suspend. */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, R92C_APS_FSMCO_AFSM_PCIE,
	    R92C_APS_FSMCO_AFSM_HSUS, 1);

	/* Enable GPIO9 as EXT WAKEUP. */
	rtwn_setbits_1(sc, R92C_GPIO_INTM + 2, 0, 0x01);

	rs->rs_flags &= ~(R12A_IQK_RUNNING | R12A_RADAR_ENABLED);
}

int
r21a_check_condition(struct rtwn_softc *sc, const uint8_t cond[])
{
	struct r12a_softc *rs = sc->sc_priv;
	uint8_t mask;
	int i;

	RTWN_DPRINTF(sc, RTWN_DEBUG_RESET,
	    "%s: condition byte 0: %02X; ext 5ghz pa/lna %d/%d\n",
	    __func__, cond[0], rs->ext_pa_5g, rs->ext_lna_5g);

	if (cond[0] == 0)
		return (1);

	mask = 0;
	if (rs->ext_pa_5g)
		mask |= R21A_COND_EXT_PA_5G;
	if (rs->ext_lna_5g)
		mask |= R21A_COND_EXT_LNA_5G;
	if (rs->bt_coex)
		mask |= R21A_COND_BT;
	if (!rs->ext_pa_2g && !rs->ext_lna_2g &&
	    !rs->ext_pa_5g && !rs->ext_lna_5g && !rs->bt_coex)
		mask = R21A_COND_BOARD_DEF;

	if (mask == 0)
		return (0);

	for (i = 0; i < RTWN_MAX_CONDITIONS && cond[i] != 0; i++)
		if (cond[i] == mask)
			return (1);

	return (0);
}

void
r21a_crystalcap_write(struct rtwn_softc *sc)
{
	struct r12a_softc *rs = sc->sc_priv;
	uint32_t reg;
	uint8_t val;

	val = rs->crystalcap & 0x3f;
	reg = rtwn_bb_read(sc, R92C_MAC_PHY_CTRL);
	reg = RW(reg, R21A_MAC_PHY_CRYSTALCAP, val | (val << 6));
	rtwn_bb_write(sc, R92C_MAC_PHY_CTRL, reg);
}

int
r21a_init_bcnq1_boundary(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)
	RTWN_CHK(rtwn_write_1(sc, R88E_TXPKTBUF_BCNQ1_BDNY,
	    R21A_BCNQ0_BOUNDARY));
	RTWN_CHK(rtwn_write_1(sc, R21A_DWBCN1_CTRL + 1,
	    R21A_BCNQ0_BOUNDARY));
	RTWN_CHK(rtwn_setbits_1_shift(sc, R21A_DWBCN1_CTRL, 0,
	    R21A_DWBCN1_CTRL_SEL_EN, 2));

	return (0);
#undef RTWN_CHK
}

void
r21a_init_ampdu_fwhw(struct rtwn_softc *sc)
{
	rtwn_write_1(sc, R92C_FWHW_TXQ_CTRL,
	    R92C_FWHW_TXQ_CTRL_AMPDU_RTY_NEW);
	rtwn_write_4(sc, R92C_FAST_EDCA_CTRL, 0x03087777);
}
