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

#include <dev/rtwn/usb/rtwn_usb_var.h>

#include <dev/rtwn/rtl8192c/r92c_var.h>

#include <dev/rtwn/rtl8192c/usb/r92cu.h>
#include <dev/rtwn/rtl8192c/usb/r92cu_reg.h>


void
r92cu_init_bb(struct rtwn_softc *sc)
{

	/* Enable BB and RF. */
	rtwn_setbits_2(sc, R92C_SYS_FUNC_EN, 0,
	    R92C_SYS_FUNC_EN_BBRSTB | R92C_SYS_FUNC_EN_BB_GLB_RST |
	    R92C_SYS_FUNC_EN_DIO_RF);

	rtwn_write_2(sc, R92C_AFE_PLL_CTRL, 0xdb83);

	rtwn_write_1(sc, R92C_RF_CTRL,
	    R92C_RF_CTRL_EN | R92C_RF_CTRL_RSTB | R92C_RF_CTRL_SDMRSTB);
	rtwn_write_1(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_USBA | R92C_SYS_FUNC_EN_USBD |
	    R92C_SYS_FUNC_EN_BB_GLB_RST | R92C_SYS_FUNC_EN_BBRSTB);

	rtwn_write_1(sc, R92C_LDOHCI12_CTRL, 0x0f);
	rtwn_write_1(sc, 0x15, 0xe9);
	rtwn_write_1(sc, R92C_AFE_XTAL_CTRL + 1, 0x80);

	r92c_init_bb_common(sc);
}

int
r92cu_power_on(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)
	uint32_t reg;
	int ntries;

	/* Wait for autoload done bit. */
	for (ntries = 0; ntries < 5000; ntries++) {
		if (rtwn_read_1(sc, R92C_APS_FSMCO) & R92C_APS_FSMCO_PFM_ALDN)
			break;
		rtwn_delay(sc, 10);
	}
	if (ntries == 5000) {
		device_printf(sc->sc_dev,
		    "timeout waiting for chip autoload\n");
		return (ETIMEDOUT);
	}

	/* Unlock ISO/CLK/Power control register. */
	RTWN_CHK(rtwn_write_1(sc, R92C_RSV_CTRL, 0));

	/* Move SPS into PWM mode. */
	RTWN_CHK(rtwn_write_1(sc, R92C_SPS0_CTRL, 0x2b));

	/* just in case if power_off() was not properly executed. */
	rtwn_delay(sc, 100);

	reg = rtwn_read_1(sc, R92C_LDOV12D_CTRL);
	if (!(reg & R92C_LDOV12D_CTRL_LDV12_EN)) {
		RTWN_CHK(rtwn_write_1(sc, R92C_LDOV12D_CTRL,
		    reg | R92C_LDOV12D_CTRL_LDV12_EN));

		rtwn_delay(sc, 100);

		RTWN_CHK(rtwn_setbits_1(sc, R92C_SYS_ISO_CTRL,
		    R92C_SYS_ISO_CTRL_MD2PP, 0));
	}

	/* Auto enable WLAN. */
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, 0,
	    R92C_APS_FSMCO_APFM_ONMAC, 1));

	for (ntries = 0; ntries < 5000; ntries++) {
		if (!(rtwn_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_ONMAC))
			break;
		rtwn_delay(sc, 10);
	}
	if (ntries == 5000) {
		device_printf(sc->sc_dev,
		    "timeout waiting for MAC auto ON\n");
		return (ETIMEDOUT);
	}

	/* Enable radio, GPIO and LED functions. */
	RTWN_CHK(rtwn_write_2(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_HSUS |
	    R92C_APS_FSMCO_PDN_EN |
	    R92C_APS_FSMCO_PFM_ALDN));

	/* Release RF digital isolation. */
	RTWN_CHK(rtwn_setbits_1_shift(sc, R92C_SYS_ISO_CTRL,
	    R92C_SYS_ISO_CTRL_DIOR, 0, 1));

	/* Initialize MAC. */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_APSD_CTRL,
	    R92C_APSD_CTRL_OFF, 0));
	for (ntries = 0; ntries < 1000; ntries++) {
		if (!(rtwn_read_1(sc, R92C_APSD_CTRL) &
		    R92C_APSD_CTRL_OFF_STATUS))
			break;
		rtwn_delay(sc, 50);
	}
	if (ntries == 1000) {
		device_printf(sc->sc_dev,
		    "timeout waiting for MAC initialization\n");
		return (ETIMEDOUT);
	}

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC blocks. */
	RTWN_CHK(rtwn_setbits_2(sc, R92C_CR, 0,
	    R92C_CR_HCI_TXDMA_EN | R92C_CR_TXDMA_EN |
	    R92C_CR_HCI_RXDMA_EN | R92C_CR_RXDMA_EN |
	    R92C_CR_PROTOCOL_EN | R92C_CR_SCHEDULE_EN |
	    ((sc->sc_hwcrypto != RTWN_CRYPTO_SW) ? R92C_CR_ENSEC : 0) |
	    R92C_CR_CALTMR_EN));

	RTWN_CHK(rtwn_write_1(sc, 0xfe10, 0x19));

	return (0);
#undef RTWN_CHK
}

void
r92cu_power_off(struct rtwn_softc *sc)
{
#ifndef RTWN_WITHOUT_UCODE
	struct r92c_softc *rs = sc->sc_priv;
#endif
	uint32_t reg;
	int error;

	/* Deinit C2H event handler. */
#ifndef RTWN_WITHOUT_UCODE
	callout_stop(&rs->rs_c2h_report);
	rs->rs_c2h_paused = 0;
	rs->rs_c2h_pending = 0;
	rs->rs_c2h_timeout = hz;
#endif

	/* Block all Tx queues. */
	error = rtwn_write_1(sc, R92C_TXPAUSE, R92C_TX_QUEUE_ALL);
	if (error == ENXIO)	/* hardware gone */
		return;

	/* Disable RF */
	rtwn_rf_write(sc, 0, 0, 0);

	rtwn_write_1(sc, R92C_APSD_CTRL, R92C_APSD_CTRL_OFF);

	/* Reset BB state machine */
	rtwn_write_1(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_USBD | R92C_SYS_FUNC_EN_USBA |
	    R92C_SYS_FUNC_EN_BB_GLB_RST);
	rtwn_write_1(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_USBD | R92C_SYS_FUNC_EN_USBA);

	/*
	 * Reset digital sequence
	 */
#ifndef RTWN_WITHOUT_UCODE
	if (rtwn_read_1(sc, R92C_MCUFWDL) & R92C_MCUFWDL_RDY) {
		/* Reset MCU ready status */
		rtwn_write_1(sc, R92C_MCUFWDL, 0);

		/* If firmware in ram code, do reset */
		r92c_fw_reset(sc, RTWN_FW_RESET_SHUTDOWN);
	}
#endif

	/* Reset MAC and Enable 8051 */
	rtwn_write_1(sc, R92C_SYS_FUNC_EN + 1,
	    (R92C_SYS_FUNC_EN_CPUEN |
	     R92C_SYS_FUNC_EN_ELDR |
	     R92C_SYS_FUNC_EN_HWPDN) >> 8);

	/* Reset MCU ready status */
	rtwn_write_1(sc, R92C_MCUFWDL, 0);

	/* Disable MAC clock */
	rtwn_write_2(sc, R92C_SYS_CLKR,
	    R92C_SYS_CLKR_ANAD16V_EN |
	    R92C_SYS_CLKR_ANA8M |
	    R92C_SYS_CLKR_LOADER_EN |
	    R92C_SYS_CLKR_80M_SSC_DIS |
	    R92C_SYS_CLKR_SYS_EN |
	    R92C_SYS_CLKR_RING_EN |
	    0x4000);

	/* Disable AFE PLL */
	rtwn_write_1(sc, R92C_AFE_PLL_CTRL, 0x80);

	/* Gated AFE DIG_CLOCK */
	rtwn_write_2(sc, R92C_AFE_XTAL_CTRL, 0x880F);

	/* Isolated digital to PON */
	rtwn_write_1(sc, R92C_SYS_ISO_CTRL,
	    R92C_SYS_ISO_CTRL_MD2PP |
	    R92C_SYS_ISO_CTRL_PA2PCIE |
	    R92C_SYS_ISO_CTRL_PD2CORE |
	    R92C_SYS_ISO_CTRL_IP2MAC |
	    R92C_SYS_ISO_CTRL_DIOP |
	    R92C_SYS_ISO_CTRL_DIOE);

	/*
	 * Pull GPIO PIN to balance level and LED control
	 */
	/* 1. Disable GPIO[7:0] */
	rtwn_write_2(sc, R92C_GPIO_IOSEL, 0x0000);

	reg = rtwn_read_4(sc, R92C_GPIO_PIN_CTRL) & ~0x0000ff00;
	reg |= ((reg << 8) & 0x0000ff00) | 0x00ff0000;
	rtwn_write_4(sc, R92C_GPIO_PIN_CTRL, reg);

	/* Disable GPIO[10:8] */
	rtwn_write_1(sc, R92C_MAC_PINMUX_CFG, 0x00);

	reg = rtwn_read_2(sc, R92C_GPIO_IO_SEL) & ~0x00f0;
	reg |= (((reg & 0x000f) << 4) | 0x0780);
	rtwn_write_2(sc, R92C_GPIO_IO_SEL, reg);

	/* Disable LED0 & 1 */
	rtwn_write_2(sc, R92C_LEDCFG0, 0x8080);

	/*
	 * Reset digital sequence
	 */
	/* Disable ELDR clock */
	rtwn_write_2(sc, R92C_SYS_CLKR,
	    R92C_SYS_CLKR_ANAD16V_EN |
	    R92C_SYS_CLKR_ANA8M |
	    R92C_SYS_CLKR_LOADER_EN |
	    R92C_SYS_CLKR_80M_SSC_DIS |
	    R92C_SYS_CLKR_SYS_EN |
	    R92C_SYS_CLKR_RING_EN |
	    0x4000);

	/* Isolated ELDR to PON */
	rtwn_write_1(sc, R92C_SYS_ISO_CTRL + 1,
	    (R92C_SYS_ISO_CTRL_DIOR |
	     R92C_SYS_ISO_CTRL_PWC_EV12V) >> 8);

	/*
	 * Disable analog sequence
	 */
	/* Disable A15 power */
	rtwn_write_1(sc, R92C_LDOA15_CTRL, R92C_LDOA15_CTRL_OBUF);
	/* Disable digital core power */
	rtwn_setbits_1(sc, R92C_LDOV12D_CTRL,
	    R92C_LDOV12D_CTRL_LDV12_EN, 0);

	/* Enter PFM mode */
	rtwn_write_1(sc, R92C_SPS0_CTRL, 0x23);

	/* Set USB suspend */
	rtwn_write_2(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_APDM_HOST |
	    R92C_APS_FSMCO_AFSM_HSUS |
	    R92C_APS_FSMCO_PFM_ALDN);

	/* Lock ISO/CLK/Power control register. */
	rtwn_write_1(sc, R92C_RSV_CTRL, 0x0E);
}

void
r92cu_init_intr(struct rtwn_softc *sc)
{
	rtwn_write_4(sc, R92C_HISR, 0xffffffff);
	rtwn_write_4(sc, R92C_HIMR, 0xffffffff);
}

void
r92cu_init_tx_agg(struct rtwn_softc *sc)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);
	uint32_t reg;

	reg = rtwn_read_4(sc, R92C_TDECTRL);
	reg = RW(reg, R92C_TDECTRL_BLK_DESC_NUM, uc->tx_agg_desc_num);
	rtwn_write_4(sc, R92C_TDECTRL, reg);
}

void
r92cu_init_rx_agg(struct rtwn_softc *sc)
{

	/* Rx aggregation (DMA & USB). */
	rtwn_setbits_1(sc, R92C_TRXDMA_CTRL, 0,
	    R92C_TRXDMA_CTRL_RXDMA_AGG_EN);
	rtwn_setbits_1(sc, R92C_USB_SPECIAL_OPTION, 0,
	    R92C_USB_SPECIAL_OPTION_AGG_EN);

	/* XXX dehardcode */
	rtwn_write_1(sc, R92C_RXDMA_AGG_PG_TH, 48);
	rtwn_write_1(sc, R92C_USB_DMA_AGG_TO, 4);
	rtwn_write_1(sc, R92C_USB_AGG_TH, 8);
	rtwn_write_1(sc, R92C_USB_AGG_TO, 6);
}

void
r92cu_post_init(struct rtwn_softc *sc)
{

	rtwn_write_4(sc, R92C_POWER_STATUS, 0x5);

	/* Perform LO and IQ calibrations. */
	r92c_iq_calib(sc);
	/* Perform LC calibration. */
	r92c_lc_calib(sc);

	/* Fix USB interference issue. */
	rtwn_write_1(sc, 0xfe40, 0xe0);
	rtwn_write_1(sc, 0xfe41, 0x8d);
	rtwn_write_1(sc, 0xfe42, 0x80);

	r92c_pa_bias_init(sc);

	/* Fix for lower temperature. */
	rtwn_write_1(sc, 0x15, 0xe9);

#ifndef RTWN_WITHOUT_UCODE
	if (sc->sc_flags & RTWN_FW_LOADED) {
		struct r92c_softc *rs = sc->sc_priv;

		if (sc->sc_ratectl_sysctl == RTWN_RATECTL_FW) {
			/* XXX firmware RA does not work yet */
			sc->sc_ratectl = RTWN_RATECTL_NET80211;
		} else
			sc->sc_ratectl = sc->sc_ratectl_sysctl;

		/* Start C2H event handling. */
		callout_reset(&rs->rs_c2h_report, rs->rs_c2h_timeout,
		    r92c_handle_c2h_report, sc);
	} else
#endif
		sc->sc_ratectl = RTWN_RATECTL_NONE;
}
