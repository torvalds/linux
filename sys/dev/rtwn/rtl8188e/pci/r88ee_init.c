/*-
 * Copyright (c) 2017 Farhan Khan <khanzf@gmail.com>
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

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/pci/rtwn_pci_var.h>

#include <dev/rtwn/rtl8192c/r92c.h>

#include <dev/rtwn/rtl8188e/pci/r88ee.h>
#include <dev/rtwn/rtl8188e/pci/r88ee_reg.h>

void
r88ee_init_bb(struct rtwn_softc *sc)
{

	/* Enable BB and RF. */
	rtwn_setbits_2(sc, R92C_SYS_FUNC_EN, 0,
	    R92C_SYS_FUNC_EN_BBRSTB | R92C_SYS_FUNC_EN_BB_GLB_RST |
	    R92C_SYS_FUNC_EN_DIO_RF);

	rtwn_write_1(sc, R92C_RF_CTRL,
	    R92C_RF_CTRL_EN | R92C_RF_CTRL_RSTB | R92C_RF_CTRL_SDMRSTB);
	rtwn_write_1(sc, R92C_SYS_FUNC_EN, R92C_SYS_FUNC_EN_PPLL |
	    R92C_SYS_FUNC_EN_PCIEA | R92C_SYS_FUNC_EN_DIO_PCIE |
	    R92C_SYS_FUNC_EN_BB_GLB_RST | R92C_SYS_FUNC_EN_BBRSTB);

	r88e_init_bb_common(sc);
}

void
r88ee_init_intr(struct rtwn_softc *sc)
{
	/* Disable interrupts. */
	rtwn_write_4(sc, R88E_HIMR, 0x00000000);
	rtwn_write_4(sc, R88E_HIMRE, 0x00000000);
}

int
r88ee_power_on(struct rtwn_softc *sc)
{
	int ntries;

	/* Disable XTAL output for power saving. */
	rtwn_setbits_1(sc, R88E_XCK_OUT_CTRL, R88E_XCK_OUT_CTRL_EN, 0);

	/* Unlock ISO/CLK/Power control register. */
	rtwn_setbits_2(sc, R92C_APS_FSMCO, R92C_APS_FSMCO_APDM_HPDN, 0);
	rtwn_write_1(sc, R92C_RSV_CTRL, 0);

	/* Wait for power ready bit */
	for(ntries = 0; ntries < 5000; ntries++) {
		if (rtwn_read_4(sc, R92C_APS_FSMCO) & R92C_APS_FSMCO_SUS_HOST)
			break;
		rtwn_delay(sc, 10);
	}
	if (ntries == 5000) {
		device_printf(sc->sc_dev,
		    "timeout waiting for chip power up\n");
		return (ETIMEDOUT);
	}

	/* Reset BB. */
	rtwn_setbits_1(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_BBRSTB | R92C_SYS_FUNC_EN_BB_GLB_RST, 0);

	/* schmit trigger */
	rtwn_setbits_1(sc, R92C_AFE_XTAL_CTRL + 2, 0, 0x80);

	/* Disable HWPDN. */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_APDM_HPDN, 0, 1);

	/* Disable WL suspend. */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_AFSM_PCIE, 0, 1);

	/* Auto-enable WLAN */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    0, R92C_APS_FSMCO_APFM_ONMAC, 1);
	for (ntries = 0; ntries < 5000; ntries++) {
		if (!(rtwn_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_ONMAC))
			break;
		rtwn_delay(sc, 10);
	}
	if (ntries == 5000)
		return (ETIMEDOUT);

	rtwn_setbits_1(sc, R92C_PCIE_CTRL_REG + 2, 0, 0x04);

	/* Enable LDO normal mode. */
	rtwn_setbits_1(sc, R92C_LPLDO_CTRL, R92C_LPLDO_CTRL_SLEEP, 0);

	rtwn_setbits_1(sc, R92C_APS_FSMCO, 0, R92C_APS_FSMCO_PDN_EN);
	rtwn_setbits_1(sc, R92C_PCIE_CTRL_REG + 2, 0, 0x04);
	rtwn_setbits_1(sc, R92C_AFE_XTAL_CTRL_EXT + 1, 0, 0x02);
	rtwn_setbits_1(sc, R92C_SYS_CLKR, 0, 0x08);
	rtwn_setbits_2(sc, R92C_GPIO_MUXCFG, R92C_GPIO_MUXCFG_ENSIC, 0);

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC blocks. */
	rtwn_write_2(sc, R92C_CR, 0);
	rtwn_setbits_2(sc, R92C_CR, 0,
	    R92C_CR_HCI_TXDMA_EN | R92C_CR_TXDMA_EN |
	    R92C_CR_HCI_RXDMA_EN | R92C_CR_RXDMA_EN |
	    R92C_CR_PROTOCOL_EN | R92C_CR_SCHEDULE_EN |
	    ((sc->sc_hwcrypto != RTWN_CRYPTO_SW) ? R92C_CR_ENSEC : 0) |
	    R92C_CR_CALTMR_EN);

	rtwn_write_4(sc, R92C_INT_MIG, 0);
	rtwn_write_4(sc, R92C_MCUTST_1, 0);

	return (0);
}

void
r88ee_power_off(struct rtwn_softc *sc)
{
	uint8_t reg;
	int ntries;

	/* Disable any kind of TX reports. */
	rtwn_setbits_1(sc, R88E_TX_RPT_CTRL,
	    R88E_TX_RPT1_ENA | R88E_TX_RPT2_ENA, 0);

	rtwn_write_1(sc, R92C_PCIE_CTRL_REG + 1, 0xFF);

	/* Move card to Low Power State. */
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

	/* Reset MAC TRX */
	rtwn_write_1(sc, R92C_CR,
	    R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN |
	    R92C_CR_TXDMA_EN | R92C_CR_RXDMA_EN |
	    R92C_CR_PROTOCOL_EN | R92C_CR_SCHEDULE_EN);

	/* Disable h/w encryption. */
	rtwn_setbits_1_shift(sc, R92C_CR, R92C_CR_ENSEC, 0, 1);

	/* Respond TxOK to scheduler */
	rtwn_setbits_1(sc, R92C_DUAL_TSF_RST, 0, 0x20);

	/* If firmware in ram code, do reset. */
#ifndef RTWN_WITHOUT_UCODE
	if (rtwn_read_1(sc, R92C_MCUFWDL) & R92C_MCUFWDL_RDY)
		r88e_fw_reset(sc, RTWN_FW_RESET_SHUTDOWN);
#endif

	/* Reset MCU ready status. */
	rtwn_write_1(sc, R92C_MCUFWDL, 0);

	/* Disable 32k. */
	rtwn_setbits_1(sc, R88E_32K_CTRL, 0x01, 0);

	/* Move card to Disabled state. */
	/* Turn off RF. */
	rtwn_write_1(sc, R92C_RF_CTRL, 0);

	/* LDO Sleep mode. */
	rtwn_setbits_1(sc, R92C_LPLDO_CTRL, 0, R92C_LPLDO_CTRL_SLEEP);

	/* Turn off MAC by HW state machine */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, 0,
	    R92C_APS_FSMCO_APFM_OFF, 1);

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

	/* schmit trigger */
	rtwn_setbits_1(sc, R92C_AFE_XTAL_CTRL + 2, 0, 0x80);

	/* Reset MCU IO Wrapper. */
	reg = rtwn_read_1(sc, R92C_RSV_CTRL + 1);
	rtwn_write_1(sc, R92C_RSV_CTRL + 1, reg & ~0x08);
	rtwn_write_1(sc, R92C_RSV_CTRL + 1, reg | 0x08);

	/* marked as 'For Power Consumption' code. */
	rtwn_write_1(sc, R92C_GPIO_OUT, rtwn_read_1(sc, R92C_GPIO_IN));
	rtwn_write_1(sc, R92C_GPIO_IOSEL, 0xff);

	rtwn_write_1(sc, R92C_GPIO_IO_SEL,
	    rtwn_read_1(sc, R92C_GPIO_IO_SEL) << 4);
	rtwn_setbits_1(sc, R92C_GPIO_MOD, 0, 0x0f);

	/* Set LNA, TRSW, EX_PA Pin to output mode. */
	rtwn_write_4(sc, R88E_BB_PAD_CTRL, 0x00080808);
}

void
r88ee_post_init(struct rtwn_softc *sc)
{

	/* Enable per-packet TX report. */
	rtwn_setbits_1(sc, R88E_TX_RPT_CTRL, 0, R88E_TX_RPT1_ENA);

	/* Disable Tx if MACID is not associated. */
	rtwn_write_4(sc, R88E_MACID_NO_LINK, 0xffffffff);
	rtwn_write_4(sc, R88E_MACID_NO_LINK + 4, 0xffffffff);
	r88e_macid_enable_link(sc, RTWN_MACID_BC, 1);

	/* Perform LO and IQ calibrations. */
	r88e_iq_calib(sc);
	/* Perform LC calibration. */
	r92c_lc_calib(sc);

	/* Enable Rx DMA */
	rtwn_write_1(sc, R92C_PCIE_CTRL_REG + 1, 0);

	if (sc->sc_ratectl_sysctl == RTWN_RATECTL_FW) {
		/* No support (yet?) for f/w rate adaptation. */
		sc->sc_ratectl = RTWN_RATECTL_NET80211;
	} else
		sc->sc_ratectl = sc->sc_ratectl_sysctl;
}
