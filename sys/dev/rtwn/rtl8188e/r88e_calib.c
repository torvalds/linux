/*-
 * Copyright (c) 2016-2019 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#include <dev/rtwn/rtl8188e/r88e.h>
#include <dev/rtwn/rtl8188e/r88e_reg.h>


/* Registers to save and restore during IQ calibration. */
struct r88e_iq_cal_reg_vals {
	uint32_t	adda[16];
	uint8_t		txpause;
	uint8_t		bcn_ctrl[2];
	uint32_t	gpio_muxcfg;
	uint32_t	cck0_afesetting;
	uint32_t	ofdm0_trxpathena;
	uint32_t	ofdm0_trmuxpar;
	uint32_t	fpga0_rfifacesw0;
	uint32_t	fpga0_rfifacesw1;
	uint32_t	fpga0_rfifaceoe0;
	uint32_t	fpga0_rfifaceoe1;
	uint32_t	config_ant0;
	uint32_t	config_ant1;
};

static int
r88e_iq_calib_chain(struct rtwn_softc *sc, uint16_t tx[2], uint16_t rx[2])
{
	uint32_t status;

	/* Set Rx IQ calibration mode table. */
	rtwn_bb_write(sc, R92C_FPGA0_IQK, 0);
	rtwn_rf_write(sc, 0, R88E_RF_WE_LUT, 0x800a0);
	rtwn_rf_write(sc, 0, R92C_RF_RCK_OS, 0x30000);
	rtwn_rf_write(sc, 0, R92C_RF_TXPA_G(0), 0xf);
	rtwn_rf_write(sc, 0, R92C_RF_TXPA_G(1), 0xf117b);
	rtwn_bb_write(sc, R92C_FPGA0_IQK, 0x80800000);

	/* IQ calibration settings. */
	rtwn_bb_write(sc, R92C_TX_IQK, 0x01007c00);
	rtwn_bb_write(sc, R92C_RX_IQK, 0x81004800);

	/* IQ calibration settings for chain 0. */
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(0), 0x10008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(0), 0x30008c1c);
	rtwn_bb_write(sc, R92C_TX_IQK_PI(0), 0x82160804);
	rtwn_bb_write(sc, R92C_RX_IQK_PI(0), 0x28160000);

	/* LO calibration settings. */
	rtwn_bb_write(sc, R92C_IQK_AGC_RSP, 0x0046a911);

	/* We're doing LO and IQ calibration in one shot. */
	rtwn_bb_write(sc, R92C_IQK_AGC_PTS, 0xf9000000);
	rtwn_bb_write(sc, R92C_IQK_AGC_PTS, 0xf8000000);

	/* Give LO and IQ calibrations the time to complete. */
	rtwn_delay(sc, 10000);

	/* Read IQ calibration status. */
	status = rtwn_bb_read(sc, R92C_RX_POWER_IQK_AFTER(0));
	if (status & (1 << 28))
		return (0);	/* Tx failed. */

	/* Read Tx IQ calibration results. */
	tx[0] = MS(rtwn_bb_read(sc, R92C_TX_POWER_IQK_BEFORE(0)),
	    R92C_POWER_IQK_RESULT);
	tx[1] = MS(rtwn_bb_read(sc, R92C_TX_POWER_IQK_AFTER(0)),
	    R92C_POWER_IQK_RESULT);
	if (tx[0] == 0x142 || tx[1] == 0x042)
		return (0);	/* Tx failed. */

	rtwn_bb_write(sc, R92C_TX_IQK, 0x80007c00 | (tx[0] << 16) | tx[1]);

	/* Set Rx IQ calibration mode table. */
	rtwn_bb_write(sc, R92C_FPGA0_IQK, 0);
	rtwn_rf_write(sc, 0, R88E_RF_WE_LUT, 0x800a0);
	rtwn_rf_write(sc, 0, R92C_RF_RCK_OS, 0x30000);
	rtwn_rf_write(sc, 0, R92C_RF_TXPA_G(0), 0xf);
	rtwn_rf_write(sc, 0, R92C_RF_TXPA_G(1), 0xf7ffa);
	rtwn_bb_write(sc, R92C_FPGA0_IQK, 0x80800000);

	/* IQ calibration settings. */
	rtwn_bb_write(sc, R92C_RX_IQK, 0x01004800);

	/* IQ calibration settings for chain 0. */
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(0), 0x30008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(0), 0x10008c1c);
	rtwn_bb_write(sc, R92C_TX_IQK_PI(0), 0x82160c05);
	rtwn_bb_write(sc, R92C_RX_IQK_PI(0), 0x28160c05);

	/* LO calibration settings. */
	rtwn_bb_write(sc, R92C_IQK_AGC_RSP, 0x0046a911);

	/* We're doing LO and IQ calibration in one shot. */
	rtwn_bb_write(sc, R92C_IQK_AGC_PTS, 0xf9000000);
	rtwn_bb_write(sc, R92C_IQK_AGC_PTS, 0xf8000000);

	/* Give LO and IQ calibrations the time to complete. */
	rtwn_delay(sc, 10000);

	/* Read IQ calibration status. */
	status = rtwn_bb_read(sc, R92C_RX_POWER_IQK_AFTER(0));
	if (status & (1 << 27))
		return (1);	/* Rx failed. */

	/* Read Rx IQ calibration results. */
	rx[0] = MS(rtwn_bb_read(sc, R92C_RX_POWER_IQK_BEFORE(0)),
	    R92C_POWER_IQK_RESULT);
	rx[1] = MS(status, R92C_POWER_IQK_RESULT);
	if (rx[0] == 0x132 || rx[1] == 0x036)
		return (1);	/* Rx failed. */

	return (3);	/* Both Tx and Rx succeeded. */
}

static void
r88e_iq_calib_run(struct rtwn_softc *sc, int n, uint16_t tx[2],
     uint16_t rx[2], struct r88e_iq_cal_reg_vals *vals)
{
	/* Registers to save and restore during IQ calibration. */
	static const uint16_t reg_adda[16] = {
		0x85c, 0xe6c, 0xe70, 0xe74,
		0xe78, 0xe7c, 0xe80, 0xe84,
		0xe88, 0xe8c, 0xed0, 0xed4,
		0xed8, 0xedc, 0xee0, 0xeec
	};
	int i;
	uint32_t hssi_param1;

	if (n == 0) {
		for (i = 0; i < nitems(reg_adda); i++)
			vals->adda[i] = rtwn_bb_read(sc, reg_adda[i]);

		vals->txpause = rtwn_read_1(sc, R92C_TXPAUSE);
		vals->bcn_ctrl[0] = rtwn_read_1(sc, R92C_BCN_CTRL(0));
		vals->bcn_ctrl[1] = rtwn_read_1(sc, R92C_BCN_CTRL(1));
		vals->gpio_muxcfg = rtwn_read_4(sc, R92C_GPIO_MUXCFG);
	}

	rtwn_bb_write(sc, reg_adda[0], 0x0b1b25a0);
	for (i = 1; i < nitems(reg_adda); i++)
		rtwn_bb_write(sc, reg_adda[i], 0x0bdb25a0);

	hssi_param1 = rtwn_bb_read(sc, R92C_HSSI_PARAM1(0));
	if (!(hssi_param1 & R92C_HSSI_PARAM1_PI)) {
		rtwn_bb_write(sc, R92C_HSSI_PARAM1(0),
		    hssi_param1 | R92C_HSSI_PARAM1_PI);
		rtwn_bb_write(sc, R92C_HSSI_PARAM1(1),
		    hssi_param1 | R92C_HSSI_PARAM1_PI);
	}

	if (n == 0) {
		vals->cck0_afesetting = rtwn_bb_read(sc, R92C_CCK0_AFESETTING);
		vals->ofdm0_trxpathena =
		    rtwn_bb_read(sc, R92C_OFDM0_TRXPATHENA);
		vals->ofdm0_trmuxpar = rtwn_bb_read(sc, R92C_OFDM0_TRMUXPAR);
		vals->fpga0_rfifacesw0 =
		    rtwn_bb_read(sc, R92C_FPGA0_RFIFACESW(0));
		vals->fpga0_rfifacesw1 =
		    rtwn_bb_read(sc, R92C_FPGA0_RFIFACESW(1));
		vals->fpga0_rfifaceoe0 =
		    rtwn_bb_read(sc, R92C_FPGA0_RFIFACEOE(0));
		vals->fpga0_rfifaceoe1 =
		    rtwn_bb_read(sc, R92C_FPGA0_RFIFACEOE(1));
		vals->config_ant0 = rtwn_bb_read(sc, R92C_CONFIG_ANT(0));
		vals->config_ant1 = rtwn_bb_read(sc, R92C_CONFIG_ANT(1));
	}

	rtwn_bb_setbits(sc, R92C_CCK0_AFESETTING, 0, 0x0f000000);
	rtwn_bb_write(sc, R92C_OFDM0_TRXPATHENA, 0x03a05600);
	rtwn_bb_write(sc, R92C_OFDM0_TRMUXPAR, 0x000800e4);
	rtwn_bb_write(sc, R92C_FPGA0_RFIFACESW(1), 0x22204000);
	rtwn_bb_setbits(sc, R92C_FPGA0_RFIFACESW(0), 0, 0x04000400);
	rtwn_bb_setbits(sc, R92C_FPGA0_RFIFACEOE(0), 0x400, 0);
	rtwn_bb_setbits(sc, R92C_FPGA0_RFIFACEOE(1), 0x400, 0);

	rtwn_write_1(sc, R92C_TXPAUSE,
	    R92C_TX_QUEUE_AC | R92C_TX_QUEUE_MGT | R92C_TX_QUEUE_HIGH);
	rtwn_write_1(sc, R92C_BCN_CTRL(0),
	    vals->bcn_ctrl[0] & ~R92C_BCN_CTRL_EN_BCN);
	rtwn_write_1(sc, R92C_BCN_CTRL(1),
	    vals->bcn_ctrl[1] & ~R92C_BCN_CTRL_EN_BCN);
	rtwn_write_1(sc, R92C_GPIO_MUXCFG,
	    vals->gpio_muxcfg & ~R92C_GPIO_MUXCFG_ENBT);

	rtwn_bb_write(sc, R92C_CONFIG_ANT(0), 0x0f600000);

	rtwn_bb_write(sc, R92C_FPGA0_IQK, 0x80800000);
	rtwn_bb_write(sc, R92C_TX_IQK, 0x01007c00);
	rtwn_bb_write(sc, R92C_RX_IQK, 0x01004800);

	/* Run IQ calibration twice. */
	for (i = 0; i < 2; i++) {
		int ret;

		ret = r88e_iq_calib_chain(sc, tx, rx);
		if (ret == 0) {
			RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB, "%s: Tx failed.\n",
			    __func__);
			tx[0] = 0xff;
			tx[1] = 0xff;
			rx[0] = 0xff;
			rx[1] = 0xff;
		} else if (ret == 1) {
			RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB, "%s: Rx failed.\n",
			    __func__);
			rx[0] = 0xff;
			rx[1] = 0xff;
		} else if (ret == 3) {
			RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB, "%s: Both Tx and Rx"
			    " succeeded.\n", __func__);
		}
	}

	RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB,
	    "%s: results for run %d: tx[0] 0x%x, tx[1] 0x%x, rx[0] 0x%x, "
	    "rx[1] 0x%x\n", __func__, n, tx[0], tx[1], rx[0], rx[1]);

	rtwn_bb_write(sc, R92C_CCK0_AFESETTING, vals->cck0_afesetting);
	rtwn_bb_write(sc, R92C_OFDM0_TRXPATHENA, vals->ofdm0_trxpathena);
	rtwn_bb_write(sc, R92C_FPGA0_RFIFACESW(0), vals->fpga0_rfifacesw0);
	rtwn_bb_write(sc, R92C_FPGA0_RFIFACESW(1), vals->fpga0_rfifacesw1);
	rtwn_bb_write(sc, R92C_OFDM0_TRMUXPAR, vals->ofdm0_trmuxpar);
	rtwn_bb_write(sc, R92C_FPGA0_RFIFACEOE(0), vals->fpga0_rfifaceoe0);
	rtwn_bb_write(sc, R92C_FPGA0_RFIFACEOE(1), vals->fpga0_rfifaceoe1);
	rtwn_bb_write(sc, R92C_CONFIG_ANT(0), vals->config_ant0);
	rtwn_bb_write(sc, R92C_CONFIG_ANT(1), vals->config_ant1);

	rtwn_bb_write(sc, R92C_FPGA0_IQK, 0);
	rtwn_bb_write(sc, R92C_LSSI_PARAM(0), 0x00032ed3);

	if (n != 0) {
		if (!(hssi_param1 & R92C_HSSI_PARAM1_PI)) {
			rtwn_bb_write(sc, R92C_HSSI_PARAM1(0), hssi_param1);
			rtwn_bb_write(sc, R92C_HSSI_PARAM1(1), hssi_param1);
		}

		for (i = 0; i < nitems(reg_adda); i++)
			rtwn_bb_write(sc, reg_adda[i], vals->adda[i]);

		rtwn_write_1(sc, R92C_TXPAUSE, vals->txpause);
		rtwn_write_1(sc, R92C_BCN_CTRL(0), vals->bcn_ctrl[0]);
		rtwn_write_1(sc, R92C_BCN_CTRL(1), vals->bcn_ctrl[1]);
		rtwn_write_4(sc, R92C_GPIO_MUXCFG, vals->gpio_muxcfg);
	}
}

#define RTWN_IQ_CAL_MAX_TOLERANCE 5
static int
r88e_iq_calib_compare_results(struct rtwn_softc *sc, uint16_t tx1[2],
    uint16_t rx1[2], uint16_t tx2[2], uint16_t rx2[2])
{
	int i, tx_ok, rx_ok;

	tx_ok = rx_ok = 0;
	for (i = 0; i < 2; i++)	{
		if (tx1[i] == 0xff || tx2[i] == 0xff ||
		    rx1[i] == 0xff || rx2[i] == 0xff)
			continue;

		tx_ok = (abs(tx1[i] - tx2[i]) <= RTWN_IQ_CAL_MAX_TOLERANCE);
		rx_ok = (abs(rx1[i] - rx2[i]) <= RTWN_IQ_CAL_MAX_TOLERANCE);
	}

	return (tx_ok && rx_ok);
}
#undef RTWN_IQ_CAL_MAX_TOLERANCE

static void
r88e_iq_calib_write_results(struct rtwn_softc *sc, uint16_t tx[2],
    uint16_t rx[2])
{
	uint32_t reg, val, x;
	long y, tx_c;

	if (tx[0] == 0xff || tx[1] == 0xff)
		return;

	reg = rtwn_bb_read(sc, R92C_OFDM0_TXIQIMBALANCE(0));
	val = ((reg >> 22) & 0x3ff);
	x = tx[0];
	if (x & 0x00000200)
		x |= 0xfffffc00;
	reg = (((x * val) >> 8) & 0x3ff);
	rtwn_bb_setbits(sc, R92C_OFDM0_TXIQIMBALANCE(0), 0x3ff, reg);
	rtwn_bb_setbits(sc, R92C_OFDM0_ECCATHRESHOLD, 0x80000000,
	    ((x * val) & 0x80) << 24);

	y = tx[1];
	if (y & 0x00000200)
		y |= 0xfffffc00;
	tx_c = (y * val) >> 8;
	rtwn_bb_setbits(sc, R92C_OFDM0_TXAFE(0), 0xf0000000,
	    (tx_c & 0x3c0) << 22);
	rtwn_bb_setbits(sc, R92C_OFDM0_TXIQIMBALANCE(0), 0x003f0000,
	    (tx_c & 0x3f) << 16);
	rtwn_bb_setbits(sc, R92C_OFDM0_ECCATHRESHOLD, 0x20000000,
	    ((y * val) & 0x80) << 22);

	if (rx[0] == 0xff || rx[1] == 0xff)
		return;

	rtwn_bb_setbits(sc, R92C_OFDM0_RXIQIMBALANCE(0), 0x3ff,
	    rx[0] & 0x3ff);
	rtwn_bb_setbits(sc, R92C_OFDM0_RXIQIMBALANCE(0), 0xfc00,
	    (rx[1] & 0x3f) << 10);
	rtwn_bb_setbits(sc, R92C_OFDM0_RXIQEXTANTA, 0xf0000000,
	    (rx[1] & 0x3c0) << 22);
}

#define RTWN_IQ_CAL_NRUN	3
void
r88e_iq_calib(struct rtwn_softc *sc)
{
	struct r88e_iq_cal_reg_vals vals;
	uint16_t tx[RTWN_IQ_CAL_NRUN][2], rx[RTWN_IQ_CAL_NRUN][2];
	int n, valid;

	KASSERT(sc->ntxchains == 1,
	    ("%s: only 1T1R configuration is supported!\n", __func__));

	valid = 0;
	for (n = 0; n < RTWN_IQ_CAL_NRUN; n++) {
		r88e_iq_calib_run(sc, n, tx[n], rx[n], &vals);

		if (n == 0)
			continue;

		/* Valid results remain stable after consecutive runs. */
		valid = r88e_iq_calib_compare_results(sc, tx[n - 1],
		    rx[n - 1], tx[n], rx[n]);
		if (valid)
			break;
	}

	if (valid)
		r88e_iq_calib_write_results(sc, tx[n], rx[n]);
}
#undef RTWN_IQ_CAL_NRUN

void
r88e_temp_measure(struct rtwn_softc *sc)
{
	rtwn_rf_write(sc, 0, R88E_RF_T_METER, R88E_RF_T_METER_START);
}

uint8_t
r88e_temp_read(struct rtwn_softc *sc)
{
	return (MS(rtwn_rf_read(sc, 0, R88E_RF_T_METER),
	    R88E_RF_T_METER_VAL));
}
