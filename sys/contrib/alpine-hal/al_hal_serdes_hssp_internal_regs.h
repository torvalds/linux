/*******************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/
#ifndef __AL_SERDES_INTERNAL_REGS_H__
#define  __AL_SERDES_INTERNAL_REGS_H__

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Per lane register fields
 ******************************************************************************/
/*
 * RX and TX lane hard reset
 * 0 - Hard reset is asserted
 * 1 - Hard reset is de-asserted
 */
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASS_REG_NUM			2
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASS_MASK			0x01
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASS_VAL_ASSERT		0x00
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASS_VAL_DEASSERT		0x01

/*
 * RX and TX lane hard reset control
 * 0 - Hard reset is taken from the interface pins
 * 1 - Hard reset is taken from registers
 */
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASSEN_REG_NUM		2
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASSEN_MASK			0x02
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASSEN_VAL_IFACE		0x00
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASSEN_VAL_REGS		0x02

/* RX lane power state control */
#define SERDES_IREG_FLD_LANEPCSPSTATE_RX_REG_NUM			3
#define SERDES_IREG_FLD_LANEPCSPSTATE_RX_MASK				0x1f
#define SERDES_IREG_FLD_LANEPCSPSTATE_RX_VAL_PD				0x01
#define SERDES_IREG_FLD_LANEPCSPSTATE_RX_VAL_P2				0x02
#define SERDES_IREG_FLD_LANEPCSPSTATE_RX_VAL_P1				0x04
#define SERDES_IREG_FLD_LANEPCSPSTATE_RX_VAL_P0S			0x08
#define SERDES_IREG_FLD_LANEPCSPSTATE_RX_VAL_P0				0x10

/* TX lane power state control */
#define SERDES_IREG_FLD_LANEPCSPSTATE_TX_REG_NUM			4
#define SERDES_IREG_FLD_LANEPCSPSTATE_TX_MASK				0x1f
#define SERDES_IREG_FLD_LANEPCSPSTATE_TX_VAL_PD				0x01
#define SERDES_IREG_FLD_LANEPCSPSTATE_TX_VAL_P2				0x02
#define SERDES_IREG_FLD_LANEPCSPSTATE_TX_VAL_P1				0x04
#define SERDES_IREG_FLD_LANEPCSPSTATE_TX_VAL_P0S			0x08
#define SERDES_IREG_FLD_LANEPCSPSTATE_TX_VAL_P0				0x10

/* RX lane word width */
#define SERDES_IREG_FLD_PCSRX_DATAWIDTH_REG_NUM				5
#define SERDES_IREG_FLD_PCSRX_DATAWIDTH_MASK				0x07
#define SERDES_IREG_FLD_PCSRX_DATAWIDTH_VAL_8				0x00
#define SERDES_IREG_FLD_PCSRX_DATAWIDTH_VAL_10				0x01
#define SERDES_IREG_FLD_PCSRX_DATAWIDTH_VAL_16				0x02
#define SERDES_IREG_FLD_PCSRX_DATAWIDTH_VAL_20				0x03
#define SERDES_IREG_FLD_PCSRX_DATAWIDTH_VAL_32				0x04
#define SERDES_IREG_FLD_PCSRX_DATAWIDTH_VAL_40				0x05

/* TX lane word width */
#define SERDES_IREG_FLD_PCSTX_DATAWIDTH_REG_NUM				5
#define SERDES_IREG_FLD_PCSTX_DATAWIDTH_MASK				0x70
#define SERDES_IREG_FLD_PCSTX_DATAWIDTH_VAL_8				0x00
#define SERDES_IREG_FLD_PCSTX_DATAWIDTH_VAL_10				0x10
#define SERDES_IREG_FLD_PCSTX_DATAWIDTH_VAL_16				0x20
#define SERDES_IREG_FLD_PCSTX_DATAWIDTH_VAL_20				0x30
#define SERDES_IREG_FLD_PCSTX_DATAWIDTH_VAL_32				0x40
#define SERDES_IREG_FLD_PCSTX_DATAWIDTH_VAL_40				0x50

/* RX lane rate select */
#define SERDES_IREG_FLD_PCSRX_DIVRATE_REG_NUM				6
#define SERDES_IREG_FLD_PCSRX_DIVRATE_MASK				0x07
#define SERDES_IREG_FLD_PCSRX_DIVRATE_VAL_1_8				0x00
#define SERDES_IREG_FLD_PCSRX_DIVRATE_VAL_1_4				0x01
#define SERDES_IREG_FLD_PCSRX_DIVRATE_VAL_1_2				0x02
#define SERDES_IREG_FLD_PCSRX_DIVRATE_VAL_1_1				0x03

/* TX lane rate select */
#define SERDES_IREG_FLD_PCSTX_DIVRATE_REG_NUM				6
#define SERDES_IREG_FLD_PCSTX_DIVRATE_MASK				0x70
#define SERDES_IREG_FLD_PCSTX_DIVRATE_VAL_1_8				0x00
#define SERDES_IREG_FLD_PCSTX_DIVRATE_VAL_1_4				0x10
#define SERDES_IREG_FLD_PCSTX_DIVRATE_VAL_1_2				0x20
#define SERDES_IREG_FLD_PCSTX_DIVRATE_VAL_1_1				0x30

/*
 * PMA serial RX-to-TX loop-back enable (from AGC to IO Driver). Serial receive
 * to transmit loopback: 0 - Disables loopback 1 - Transmits the untimed,
 * partial equalized RX signal out the transmit IO pins
 */
#define SERDES_IREG_FLD_LB_RX2TXUNTIMEDEN_REG_NUM			7
#define SERDES_IREG_FLD_LB_RX2TXUNTIMEDEN				0x10

/*
 * PMA TX-to-RX buffered serial loop-back enable (bypasses IO Driver). Serial
 * transmit to receive buffered loopback: 0 - Disables loopback 1 - Loops back
 * the TX serializer output into the CDR
 */
#define SERDES_IREG_FLD_LB_TX2RXBUFTIMEDEN_REG_NUM			7
#define SERDES_IREG_FLD_LB_TX2RXBUFTIMEDEN				0x20

/*
 * PMA TX-to-RX I/O serial loop-back enable (loop back done directly from TX to
 * RX pads). Serial IO loopback from the transmit lane IO pins to the receive
 * lane IO pins: 0 - Disables loopback 1 - Loops back the driver IO signal to
 * the RX IO pins
 */
#define SERDES_IREG_FLD_LB_TX2RXIOTIMEDEN_REG_NUM			7
#define SERDES_IREG_FLD_LB_TX2RXIOTIMEDEN				0x40

/*
 * PMA Parallel RX-to-TX loop-back enable. Parallel loopback from the PMA
 * receive lane 20-bit data ports, to the transmit lane 20-bit data ports 0 -
 * Disables loopback 1 - Loops back the 20-bit receive data port to the
 * transmitter
 */
#define SERDES_IREG_FLD_LB_PARRX2TXTIMEDEN_REG_NUM			7
#define SERDES_IREG_FLD_LB_PARRX2TXTIMEDEN				0x80

/*
 * PMA CDR recovered-clock loopback enable; asserted when PARRX2TXTIMEDEN is 1.
 * Transmit bit clock select: 0 - Selects synthesizer bit clock for transmit 1
 * - Selects CDR clock for transmit
 */
#define SERDES_IREG_FLD_LB_CDRCLK2TXEN_REG_NUM				7
#define SERDES_IREG_FLD_LB_CDRCLK2TXEN					0x01

/* Receive lane BIST enable. Active High */
#define SERDES_IREG_FLD_PCSRXBIST_EN_REG_NUM				8
#define SERDES_IREG_FLD_PCSRXBIST_EN					0x01

/* TX lane BIST enable. Active High */
#define SERDES_IREG_FLD_PCSTXBIST_EN_REG_NUM				8
#define SERDES_IREG_FLD_PCSTXBIST_EN					0x02

/*
 * RX BIST completion signal 0 - Indicates test is not completed 1 - Indicates
 * the test has completed, and will remain high until a new test is initiated
 */
#define SERDES_IREG_FLD_RXBIST_DONE_REG_NUM				8
#define SERDES_IREG_FLD_RXBIST_DONE					0x04

/*
 * RX BIST error count overflow indicator. Indicates an overflow in the number
 * of byte errors identified during the course of the test. This word is stable
 * to sample when *_DONE_* signal has asserted
 */
#define SERDES_IREG_FLD_RXBIST_ERRCOUNT_OVERFLOW_REG_NUM		8
#define SERDES_IREG_FLD_RXBIST_ERRCOUNT_OVERFLOW			0x08

/*
 * RX BIST locked indicator 0 - Indicates BIST is not word locked and error
 * comparisons have not begun yet 1 - Indicates BIST is word locked and error
 * comparisons have begun
 */
#define SERDES_IREG_FLD_RXBIST_RXLOCKED_REG_NUM				8
#define SERDES_IREG_FLD_RXBIST_RXLOCKED					0x10

/*
 * RX BIST error count word. Indicates the number of byte errors identified
 * during the course of the test. This word is stable to sample when *_DONE_*
 * signal has asserted
 */
#define SERDES_IREG_FLD_RXBIST_ERRCOUNT_MSB_REG_NUM			9
#define SERDES_IREG_FLD_RXBIST_ERRCOUNT_LSB_REG_NUM			10

/* Tx params */
#define SERDES_IREG_TX_DRV_1_REG_NUM					21
#define SERDES_IREG_TX_DRV_1_HLEV_MASK					0x7
#define SERDES_IREG_TX_DRV_1_HLEV_SHIFT					0
#define SERDES_IREG_TX_DRV_1_LEVN_MASK					0xf8
#define SERDES_IREG_TX_DRV_1_LEVN_SHIFT					3

#define SERDES_IREG_TX_DRV_2_REG_NUM					22
#define SERDES_IREG_TX_DRV_2_LEVNM1_MASK				0xf
#define SERDES_IREG_TX_DRV_2_LEVNM1_SHIFT				0
#define SERDES_IREG_TX_DRV_2_LEVNM2_MASK				0x30
#define SERDES_IREG_TX_DRV_2_LEVNM2_SHIFT				4

#define SERDES_IREG_TX_DRV_3_REG_NUM					23
#define SERDES_IREG_TX_DRV_3_LEVNP1_MASK				0x7
#define SERDES_IREG_TX_DRV_3_LEVNP1_SHIFT				0
#define SERDES_IREG_TX_DRV_3_SLEW_MASK					0x18
#define SERDES_IREG_TX_DRV_3_SLEW_SHIFT					3

/* Rx params */
#define SERDES_IREG_RX_CALEQ_1_REG_NUM					24
#define SERDES_IREG_RX_CALEQ_1_DCGAIN_MASK				0x7
#define SERDES_IREG_RX_CALEQ_1_DCGAIN_SHIFT				0
/* DFE post-shaping tap 3dB frequency */
#define SERDES_IREG_RX_CALEQ_1_DFEPSTAP3DB_MASK				0x38
#define SERDES_IREG_RX_CALEQ_1_DFEPSTAP3DB_SHIFT			3

#define SERDES_IREG_RX_CALEQ_2_REG_NUM					25
/* DFE post-shaping tap gain */
#define SERDES_IREG_RX_CALEQ_2_DFEPSTAPGAIN_MASK			0x7
#define SERDES_IREG_RX_CALEQ_2_DFEPSTAPGAIN_SHIFT			0
/* DFE first tap gain control */
#define SERDES_IREG_RX_CALEQ_2_DFETAP1GAIN_MASK				0x78
#define SERDES_IREG_RX_CALEQ_2_DFETAP1GAIN_SHIFT			3

#define SERDES_IREG_RX_CALEQ_3_REG_NUM					26
#define SERDES_IREG_RX_CALEQ_3_DFETAP2GAIN_MASK				0xf
#define SERDES_IREG_RX_CALEQ_3_DFETAP2GAIN_SHIFT			0
#define SERDES_IREG_RX_CALEQ_3_DFETAP3GAIN_MASK				0xf0
#define SERDES_IREG_RX_CALEQ_3_DFETAP3GAIN_SHIFT			4

#define SERDES_IREG_RX_CALEQ_4_REG_NUM					27
#define SERDES_IREG_RX_CALEQ_4_DFETAP4GAIN_MASK				0xf
#define SERDES_IREG_RX_CALEQ_4_DFETAP4GAIN_SHIFT			0
#define SERDES_IREG_RX_CALEQ_4_LOFREQAGCGAIN_MASK			0x70
#define SERDES_IREG_RX_CALEQ_4_LOFREQAGCGAIN_SHIFT			4

#define SERDES_IREG_RX_CALEQ_5_REG_NUM					28
#define SERDES_IREG_RX_CALEQ_5_PRECAL_CODE_SEL_MASK			0x7
#define SERDES_IREG_RX_CALEQ_5_PRECAL_CODE_SEL_SHIFT			0
#define SERDES_IREG_RX_CALEQ_5_HIFREQAGCCAP_MASK			0xf8
#define SERDES_IREG_RX_CALEQ_5_HIFREQAGCCAP_SHIFT			3

/* RX lane best eye point measurement result */
#define SERDES_IREG_RXEQ_BEST_EYE_MSB_VAL_REG_NUM			29
#define SERDES_IREG_RXEQ_BEST_EYE_LSB_VAL_REG_NUM			30
#define SERDES_IREG_RXEQ_BEST_EYE_LSB_VAL_MASK				0x3F

/*
 * Adaptive RX Equalization enable
 * 0 - Disables adaptive RX equalization.
 * 1 - Enables adaptive RX equalization.
 */
#define SERDES_IREG_FLD_PCSRXEQ_START_REG_NUM				31
#define SERDES_IREG_FLD_PCSRXEQ_START					(1 << 0)

/*
 * Enables an eye diagram measurement
 * within the PHY.
 * 0 - Disables eye diagram measurement
 * 1 - Enables eye diagram measurement
 */
#define SERDES_IREG_FLD_RXCALEYEDIAGFSMIN_START_REG_NUM			31
#define SERDES_IREG_FLD_RXCALEYEDIAGFSMIN_START				(1 << 1)


/*
 * RX lane single roam eye point measurement start signal.
 * If asserted, single measurement at fix XADJUST and YADJUST is started.
 */
#define SERDES_IREG_FLD_RXCALROAMEYEMEASIN_CYCLEEN_REG_NUM		31
#define SERDES_IREG_FLD_RXCALROAMEYEMEASIN_CYCLEEN_START		(1 << 2)


/*
 * PHY Eye diagram measurement status
 * signal
 * 0 - Indicates eye diagram results are not
 * valid for sampling
 * 1 - Indicates eye diagram is complete and
 * results are valid for sampling
 */
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_DONE_REG_NUM			32
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_DONE				(1 << 0)

/*
 * Eye diagram error signal. Indicates if the
 * measurement was invalid because the eye
 * diagram was interrupted by the link entering
 * electrical idle.
 * 0 - Indicates eye diagram is valid
 * 1- Indicates an error occurred, and the eye
 * diagram measurement should be re-run
 */
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_ERR_REG_NUM			32
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_ERR				(1 << 1)

/*
 * PHY Adaptive Equalization status
 * 0 - Indicates Adaptive Equalization results are not valid for sampling
 * 1 - Indicates Adaptive Equalization is complete and results are valid for
 *     sampling
 */
#define SERDES_IREG_FLD_RXCALROAMEYEMEASDONE_REG_NUM			32
#define SERDES_IREG_FLD_RXCALROAMEYEMEASDONE				(1 << 2)

/*
 *
 * PHY Adaptive Equalization Status Signal
 * 0 – Indicates adaptive equalization results
 * are not valid for sampling
 * 1 – Indicates adaptive equalization is
 * complete and results are valid for sampling.
 */
#define SERDES_IREG_FLD_RXEQ_DONE_REG_NUM				32
#define SERDES_IREG_FLD_RXEQ_DONE					(1 << 3)


/*
 * 7-bit eye diagram time adjust control
 * - 6-bits per UI
 * - spans 2 UI
 */
#define SERDES_IREG_FLD_RXCALROAMXADJUST_REG_NUM			33

/* 6-bit eye diagram voltage adjust control - spans +/-300mVdiff */
#define SERDES_IREG_FLD_RXCALROAMYADJUST_REG_NUM			34

/*
 * Eye diagram status signal. Safe for
 * sampling when *DONE* signal has
 * asserted
 * 14'h0000 - Completely Closed Eye
 * 14'hFFFF - Completely Open Eye
 */
#define	SERDES_IREG_FLD_RXCALEYEDIAGFSM_EYESUM_MSB_REG_NUM		35
#define	SERDES_IREG_FLD_RXCALEYEDIAGFSM_EYESUM_MSB_MAKE			0xFF
#define	SERDES_IREG_FLD_RXCALEYEDIAGFSM_EYESUM_MSB_SHIFT		0

#define	SERDES_IREG_FLD_RXCALEYEDIAGFSM_EYESUM_LSB_REG_NUM		36
#define	SERDES_IREG_FLD_RXCALEYEDIAGFSM_EYESUM_LSB_MAKE			0x3F
#define	SERDES_IREG_FLD_RXCALEYEDIAGFSM_EYESUM_LSB_SHIFT		0

/*
 * RX lane single roam eye point measurement result.
 * If 0, eye is open at current XADJUST and YADJUST settings.
 */
#define SERDES_IREG_FLD_RXCALROAMEYEMEAS_ACC_MSB_REG_NUM		37
#define SERDES_IREG_FLD_RXCALROAMEYEMEAS_ACC_LSB_REG_NUM		38

/*
 * Override enable for CDR lock to reference clock
 * 0 - CDR is always locked to reference
 * 1 - CDR operation mode (Lock2Reference or Lock2data are controlled internally
 *     depending on the incoming signal and ppm status)
 */
#define SERDES_IREG_FLD_RXLOCK2REF_OVREN_REG_NUM			39
#define SERDES_IREG_FLD_RXLOCK2REF_OVREN				(1 << 1)

/*
 * Selects Eye to capture based on edge
 * 0 - Capture 1st Eye in Eye Diagram
 * 1 - Capture 2nd Eye in Eye Diagram measurement
 */
#define SERDES_IREG_FLD_RXROAM_XORBITSEL_REG_NUM			39
#define SERDES_IREG_FLD_RXROAM_XORBITSEL				(1 << 2)
#define SERDES_IREG_FLD_RXROAM_XORBITSEL_1ST				0
#define SERDES_IREG_FLD_RXROAM_XORBITSEL_2ND				(1 << 2)

/*
 * RX Signal detect. 0 indicates no signal, 1 indicates signal detected.
 */
#define SERDES_IREG_FLD_RXRANDET_REG_NUM				41
#define SERDES_IREG_FLD_RXRANDET_STAT					0x20

/*
 * RX data polarity inversion control:
 * 1'b0: no inversion
 * 1'b1: invert polarity
 */
#define SERDES_IREG_FLD_POLARITY_RX_REG_NUM				46
#define SERDES_IREG_FLD_POLARITY_RX_INV					(1 << 0)

/*
 * TX data polarity inversion control:
 * 1'b0: no inversion
 * 1'b1: invert polarity
 */
#define SERDES_IREG_FLD_POLARITY_TX_REG_NUM				46
#define SERDES_IREG_FLD_POLARITY_TX_INV					(1 << 1)

/* LANEPCSPSTATE* override enable (Active low) */
#define SERDES_IREG_FLD_LANEPCSPSTATE_LOCWREN_REG_NUM			85
#define SERDES_IREG_FLD_LANEPCSPSTATE_LOCWREN				(1 << 0)

/* LB* override enable (Active low) */
#define SERDES_IREG_FLD_LB_LOCWREN_REG_NUM				85
#define SERDES_IREG_FLD_LB_LOCWREN					(1 << 1)

/* PCSRX* override enable (Active low) */
#define SERDES_IREG_FLD_PCSRX_LOCWREN_REG_NUM				85
#define SERDES_IREG_FLD_PCSRX_LOCWREN					(1 << 4)

/* PCSRXBIST* override enable (Active low) */
#define SERDES_IREG_FLD_PCSRXBIST_LOCWREN_REG_NUM			85
#define SERDES_IREG_FLD_PCSRXBIST_LOCWREN				(1 << 5)

/* PCSRXEQ* override enable (Active low) */
#define SERDES_IREG_FLD_PCSRXEQ_LOCWREN_REG_NUM				85
#define SERDES_IREG_FLD_PCSRXEQ_LOCWREN					(1 << 6)

/* PCSTX* override enable (Active low) */
#define SERDES_IREG_FLD_PCSTX_LOCWREN_REG_NUM				85
#define SERDES_IREG_FLD_PCSTX_LOCWREN					(1 << 7)

/*
 * group registers:
 * SERDES_IREG_FLD_RXCALEYEDIAGFSMIN_LOCWREN,
 * SERDES_IREG_FLD_RXCALROAMEYEMEASIN_LOCWREN
 * SERDES_IREG_FLD_RXCALROAMXADJUST_LOCWREN
 */
#define SERDES_IREG_FLD_RXCAL_LOCWREN_REG_NUM				86

/* PCSTXBIST* override enable (Active low) */
#define SERDES_IREG_FLD_PCSTXBIST_LOCWREN_REG_NUM			86
#define SERDES_IREG_FLD_PCSTXBIST_LOCWREN				(1 << 0)

/* Override RX_CALCEQ through the internal registers (Active low) */
#define SERDES_IREG_FLD_RX_DRV_OVERRIDE_EN_REG_NUM			86
#define SERDES_IREG_FLD_RX_DRV_OVERRIDE_EN				(1 << 3)

#define SERDES_IREG_FLD_RXCALEYEDIAGFSMIN_LOCWREN_REG_NUM		86
#define SERDES_IREG_FLD_RXCALEYEDIAGFSMIN_LOCWREN			(1 << 4)


/* RXCALROAMEYEMEASIN* override enable - Active Low */
#define SERDES_IREG_FLD_RXCALROAMEYEMEASIN_LOCWREN_REG_NUM		86
#define SERDES_IREG_FLD_RXCALROAMEYEMEASIN_LOCWREN			(1 << 6)

/* RXCALROAMXADJUST* override enable - Active Low */
#define SERDES_IREG_FLD_RXCALROAMXADJUST_LOCWREN_REG_NUM		86
#define SERDES_IREG_FLD_RXCALROAMXADJUST_LOCWREN			(1 << 7)

/* RXCALROAMYADJUST* override enable - Active Low */
#define SERDES_IREG_FLD_RXCALROAMYADJUST_LOCWREN_REG_NUM		87
#define SERDES_IREG_FLD_RXCALROAMYADJUST_LOCWREN			(1 << 0)

/* RXCDRCALFOSC* override enable. Active Low */
#define SERDES_IREG_FLD_RXCDRCALFOSC_LOCWREN_REG_NUM			87
#define SERDES_IREG_FLD_RXCDRCALFOSC_LOCWREN				(1 << 1)

/* Over-write enable for RXEYEDIAGFSM_INITXVAL */
#define SERDES_IREG_FLD_RXEYEDIAGFSM_LOCWREN_REG_NUM			87
#define SERDES_IREG_FLD_RXEYEDIAGFSM_LOCWREN				(1 << 2)

/* Over-write enable for CMNCLKGENMUXSEL_TXINTERNAL */
#define SERDES_IREG_FLD_RXTERMHIZ_LOCWREN_REG_NUM			87
#define SERDES_IREG_FLD_RXTERMHIZ_LOCWREN				(1 << 3)

/* TXCALTCLKDUTY* override enable. Active Low */
#define SERDES_IREG_FLD_TXCALTCLKDUTY_LOCWREN_REG_NUM			87
#define SERDES_IREG_FLD_TXCALTCLKDUTY_LOCWREN				(1 << 4)

/* Override TX_DRV through the internal registers (Active low) */
#define SERDES_IREG_FLD_TX_DRV_OVERRIDE_EN_REG_NUM			87
#define SERDES_IREG_FLD_TX_DRV_OVERRIDE_EN				(1 << 5)

/*******************************************************************************
 * Common lane register fields - PMA
 ******************************************************************************/
/*
 * Common lane hard reset control
 * 0 - Hard reset is taken from the interface pins
 * 1 - Hard reset is taken from registers
 */
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASSEN_SYNTH_REG_NUM		2
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASSEN_SYNTH_MASK		0x01
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASSEN_SYNTH_VAL_IFACE	0x00
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASSEN_SYNTH_VAL_REGS	0x01

/*
 * Common lane hard reset
 * 0 - Hard reset is asserted
 * 1 - Hard reset is de-asserted
 */
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASS_SYNTH_REG_NUM		2
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASS_SYNTH_MASK		0x02
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASS_SYNTH_VAL_ASSERT	0x00
#define SERDES_IREG_FLD_CMNCTLPOR_HARDRSTBYPASS_SYNTH_VAL_DEASSERT	0x02

/* Synth power state control */
#define SERDES_IREG_FLD_CMNPCSPSTATE_SYNTH_REG_NUM			3
#define SERDES_IREG_FLD_CMNPCSPSTATE_SYNTH_MASK				0x1f
#define SERDES_IREG_FLD_CMNPCSPSTATE_SYNTH_VAL_PD			0x01
#define SERDES_IREG_FLD_CMNPCSPSTATE_SYNTH_VAL_P2			0x02
#define SERDES_IREG_FLD_CMNPCSPSTATE_SYNTH_VAL_P1			0x04
#define SERDES_IREG_FLD_CMNPCSPSTATE_SYNTH_VAL_P0S			0x08
#define SERDES_IREG_FLD_CMNPCSPSTATE_SYNTH_VAL_P0			0x10

/* Transmit datapath FIFO enable (Active High) */
#define SERDES_IREG_FLD_CMNPCS_TXENABLE_REG_NUM				8
#define SERDES_IREG_FLD_CMNPCS_TXENABLE					(1 << 2)

/*
 * RX lost of signal detector enable
 * - 0 - disable
 * - 1 - enable
 */
#define SERDES_IREG_FLD_RXLOSDET_ENABLE_REG_NUM				13
#define SERDES_IREG_FLD_RXLOSDET_ENABLE					AL_BIT(4)

/* Signal Detect Threshold Level */
#define SERDES_IREG_FLD_RXELECIDLE_SIGDETTHRESH_REG_NUM			15
#define SERDES_IREG_FLD_RXELECIDLE_SIGDETTHRESH_MASK			AL_FIELD_MASK(2, 0)

/* LOS Detect Threshold Level */
#define SERDES_IREG_FLD_RXLOSDET_THRESH_REG_NUM				15
#define SERDES_IREG_FLD_RXLOSDET_THRESH_MASK				AL_FIELD_MASK(4, 3)
#define SERDES_IREG_FLD_RXLOSDET_THRESH_SHIFT				3

#define SERDES_IREG_FLD_RXEQ_COARSE_ITER_NUM_REG_NUM			30
#define SERDES_IREG_FLD_RXEQ_COARSE_ITER_NUM_MASK			0x7f
#define SERDES_IREG_FLD_RXEQ_COARSE_ITER_NUM_SHIFT			0

#define SERDES_IREG_FLD_RXEQ_FINE_ITER_NUM_REG_NUM			31
#define SERDES_IREG_FLD_RXEQ_FINE_ITER_NUM_MASK				0x7f
#define SERDES_IREG_FLD_RXEQ_FINE_ITER_NUM_SHIFT			0

#define SERDES_IREG_FLD_RXEQ_COARSE_RUN1_MASK_REG_NUM			32
#define SERDES_IREG_FLD_RXEQ_COARSE_RUN1_MASK_MASK			0xff
#define SERDES_IREG_FLD_RXEQ_COARSE_RUN1_MASK_SHIFT			0

#define SERDES_IREG_FLD_RXEQ_COARSE_RUN2_MASK_REG_NUM			33
#define SERDES_IREG_FLD_RXEQ_COARSE_RUN2_MASK_MASK			0x1
#define SERDES_IREG_FLD_RXEQ_COARSE_RUN2_MASK_SHIFT			0

#define SERDES_IREG_FLD_RXEQ_COARSE_STEP_REG_NUM			33
#define SERDES_IREG_FLD_RXEQ_COARSE_STEP_MASK				0x3e
#define SERDES_IREG_FLD_RXEQ_COARSE_STEP_SHIFT				1

#define SERDES_IREG_FLD_RXEQ_FINE_RUN1_MASK_REG_NUM			34
#define SERDES_IREG_FLD_RXEQ_FINE_RUN1_MASK_MASK			0xff
#define SERDES_IREG_FLD_RXEQ_FINE_RUN1_MASK_SHIFT			0

#define SERDES_IREG_FLD_RXEQ_FINE_RUN2_MASK_REG_NUM			35
#define SERDES_IREG_FLD_RXEQ_FINE_RUN2_MASK_MASK			0x1
#define SERDES_IREG_FLD_RXEQ_FINE_RUN2_MASK_SHIFT			0

#define SERDES_IREG_FLD_RXEQ_FINE_STEP_REG_NUM				35
#define SERDES_IREG_FLD_RXEQ_FINE_STEP_MASK				0x3e
#define SERDES_IREG_FLD_RXEQ_FINE_STEP_SHIFT				1

#define SERDES_IREG_FLD_RXEQ_LOOKUP_CODE_EN_REG_NUM			36
#define SERDES_IREG_FLD_RXEQ_LOOKUP_CODE_EN_MASK			0xff
#define SERDES_IREG_FLD_RXEQ_LOOKUP_CODE_EN_SHIFT			0

#define SERDES_IREG_FLD_RXEQ_LOOKUP_LASTCODE_REG_NUM			37
#define SERDES_IREG_FLD_RXEQ_LOOKUP_LASTCODE_MASK			0x7
#define SERDES_IREG_FLD_RXEQ_LOOKUP_LASTCODE_SHIFT			0

#define SERDES_IREG_FLD_RXEQ_DCGAIN_LUP0_REG_NUM			43
#define SERDES_IREG_FLD_RXEQ_DCGAIN_LUP0_MASK				0x7
#define SERDES_IREG_FLD_RXEQ_DCGAIN_LUP0_SHIFT				0

#define SERDES_IREG_FLD_TX_BIST_PAT_REG_NUM(byte_num)			(56 + (byte_num))
#define SERDES_IREG_FLD_TX_BIST_PAT_NUM_BYTES				10

/*
 * Selects the transmit BIST mode:
 * 0 - Uses the 80-bit internal memory pattern (w/ OOB)
 * 1 - Uses a 27 PRBS pattern
 * 2 - Uses a 223 PRBS pattern
 * 3 - Uses a 231 PRBS pattern
 * 4 - Uses a 1010 clock pattern
 * 5 and above - Reserved
 */
#define SERDES_IREG_FLD_CMNPCSBIST_MODESEL_REG_NUM			80
#define SERDES_IREG_FLD_CMNPCSBIST_MODESEL_MASK				0x07
#define SERDES_IREG_FLD_CMNPCSBIST_MODESEL_VAL_USER			0x00
#define SERDES_IREG_FLD_CMNPCSBIST_MODESEL_VAL_PRBS7			0x01
#define SERDES_IREG_FLD_CMNPCSBIST_MODESEL_VAL_PRBS23			0x02
#define SERDES_IREG_FLD_CMNPCSBIST_MODESEL_VAL_PRBS31			0x03
#define SERDES_IREG_FLD_CMNPCSBIST_MODESEL_VAL_CLK1010			0x04

/* Single-Bit error injection enable (on posedge) */
#define SERDES_IREG_FLD_TXBIST_BITERROR_EN_REG_NUM			80
#define SERDES_IREG_FLD_TXBIST_BITERROR_EN				0x20

/* CMNPCIEGEN3* override enable (Active Low) */
#define SERDES_IREG_FLD_CMNPCIEGEN3_LOCWREN_REG_NUM			95
#define SERDES_IREG_FLD_CMNPCIEGEN3_LOCWREN				(1 << 2)

/* CMNPCS* override enable (Active Low) */
#define SERDES_IREG_FLD_CMNPCS_LOCWREN_REG_NUM				95
#define SERDES_IREG_FLD_CMNPCS_LOCWREN					(1 << 3)

/* CMNPCSBIST* override enable (Active Low) */
#define SERDES_IREG_FLD_CMNPCSBIST_LOCWREN_REG_NUM			95
#define SERDES_IREG_FLD_CMNPCSBIST_LOCWREN				(1 << 4)

/* CMNPCSPSTATE* override enable (Active Low) */
#define SERDES_IREG_FLD_CMNPCSPSTATE_LOCWREN_REG_NUM			95
#define SERDES_IREG_FLD_CMNPCSPSTATE_LOCWREN				(1 << 5)

/*  PCS_EN* override enable (Active Low) */
#define SERDES_IREG_FLD_PCS_LOCWREN_REG_NUM				96
#define SERDES_IREG_FLD_PCS_LOCWREN					(1 << 3)

/* Eye diagram sample count */
#define SERDES_IREG_FLD_EYE_DIAG_SAMPLE_CNT_MSB_REG_NUM			150
#define SERDES_IREG_FLD_EYE_DIAG_SAMPLE_CNT_MSB_MASK			0xff
#define SERDES_IREG_FLD_EYE_DIAG_SAMPLE_CNT_MSB_SHIFT			0

#define SERDES_IREG_FLD_EYE_DIAG_SAMPLE_CNT_LSB_REG_NUM			151
#define SERDES_IREG_FLD_EYE_DIAG_SAMPLE_CNT_LSB_MASK			0xff
#define SERDES_IREG_FLD_EYE_DIAG_SAMPLE_CNT_LSB_SHIFT			0

/* override control */
#define SERDES_IREG_FLD_RXLOCK2REF_LOCWREN_REG_NUM			230
#define SERDES_IREG_FLD_RXLOCK2REF_LOCWREN				1 << 0

#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_BERTHRESHOLD1_REG_NUM		623
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_BERTHRESHOLD1_MASK		0xff
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_BERTHRESHOLD1_SHIFT		0

#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_BERTHRESHOLD2_REG_NUM		624
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_BERTHRESHOLD2_MASK		0xff
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_BERTHRESHOLD2_SHIFT		0

/* X and Y coefficient return value */
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_X_Y_VALWEIGHT_REG_NUM		626
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_XVALWEIGHT_MASK			0x0F
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_XVALWEIGHT_SHIFT		0
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_YVALWEIGHT_MASK			0xF0
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_YVALWEIGHT_SHIFT		4

/* X coarse scan step */
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_XVALCOARSE_REG_NUM		627
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_XVALCOARSE_MASK			0x7F
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_XVALCOARSE_SHIFT		0

/* X fine scan step */
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_XVALFINE_REG_NUM		628
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_XVALFINE_MASK			0x7F
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_XVALFINE_SHIFT			0

/* Y coarse scan step */
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_YVALCOARSE_REG_NUM		629
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_YVALCOARSE_MASK			0x0F
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_YVALCOARSE_SHIFT		0

/* Y fine scan step */
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_YVALFINE_REG_NUM		630
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_YVALFINE_MASK			0x0F
#define SERDES_IREG_FLD_RXCALEYEDIAGFSM_YVALFINE_SHIFT			0

#define SERDES_IREG_FLD_PPMDRIFTCOUNT1_REG_NUM				157

#define SERDES_IREG_FLD_PPMDRIFTCOUNT2_REG_NUM				158

#define SERDES_IREG_FLD_PPMDRIFTMAX1_REG_NUM				159

#define SERDES_IREG_FLD_PPMDRIFTMAX2_REG_NUM				160

#define SERDES_IREG_FLD_SYNTHPPMDRIFTMAX1_REG_NUM			163

#define SERDES_IREG_FLD_SYNTHPPMDRIFTMAX2_REG_NUM			164

/*******************************************************************************
 * Common lane register fields - PCS
 ******************************************************************************/
#define SERDES_IREG_FLD_PCS_VPCSIF_OVR_RATE_REG_NUM			3
#define SERDES_IREG_FLD_PCS_VPCSIF_OVR_RATE_MASK			AL_FIELD_MASK(5, 4)
#define SERDES_IREG_FLD_PCS_VPCSIF_OVR_RATE_SHIFT			4

#define SERDES_IREG_FLD_PCS_VPCSIF_OVR_RATE_ENA_REG_NUM			6
#define SERDES_IREG_FLD_PCS_VPCSIF_OVR_RATE_ENA				AL_BIT(2)

#define SERDES_IREG_FLD_PCS_EBUF_FULL_D2R1_REG_NUM			18
#define SERDES_IREG_FLD_PCS_EBUF_FULL_D2R1_REG_MASK			0x1F
#define SERDES_IREG_FLD_PCS_EBUF_FULL_D2R1_REG_SHIFT			0

#define SERDES_IREG_FLD_PCS_EBUF_FULL_PCIE_G3_REG_NUM			19
#define SERDES_IREG_FLD_PCS_EBUF_FULL_PCIE_G3_REG_MASK			0x7C
#define SERDES_IREG_FLD_PCS_EBUF_FULL_PCIE_G3_REG_SHIFT			2

#define SERDES_IREG_FLD_PCS_EBUF_RD_THRESHOLD_D2R1_REG_NUM		20
#define SERDES_IREG_FLD_PCS_EBUF_RD_THRESHOLD_D2R1_REG_MASK		0x1F
#define SERDES_IREG_FLD_PCS_EBUF_RD_THRESHOLD_D2R1_REG_SHIFT		0

#define SERDES_IREG_FLD_PCS_EBUF_RD_THRESHOLD_PCIE_G3_REG_NUM		21
#define SERDES_IREG_FLD_PCS_EBUF_RD_THRESHOLD_PCIE_G3_REG_MASK		0x7C
#define SERDES_IREG_FLD_PCS_EBUF_RD_THRESHOLD_PCIE_G3_REG_SHIFT		2

#define SERDES_IREG_FLD_PCS_RXEQ_COARSE_ITER_NUM_REG_NUM		22
#define SERDES_IREG_FLD_PCS_RXEQ_COARSE_ITER_NUM_MASK			0x7f
#define SERDES_IREG_FLD_PCS_RXEQ_COARSE_ITER_NUM_SHIFT			0

#define SERDES_IREG_FLD_PCS_RXEQ_FINE_ITER_NUM_REG_NUM			34
#define SERDES_IREG_FLD_PCS_RXEQ_FINE_ITER_NUM_MASK			0x7f
#define SERDES_IREG_FLD_PCS_RXEQ_FINE_ITER_NUM_SHIFT			0

#define SERDES_IREG_FLD_PCS_RXEQ_COARSE_RUN1_MASK_REG_NUM		23
#define SERDES_IREG_FLD_PCS_RXEQ_COARSE_RUN1_MASK_MASK			0xff
#define SERDES_IREG_FLD_PCS_RXEQ_COARSE_RUN1_MASK_SHIFT			0

#define SERDES_IREG_FLD_PCS_RXEQ_COARSE_RUN2_MASK_REG_NUM		22
#define SERDES_IREG_FLD_PCS_RXEQ_COARSE_RUN2_MASK_MASK			0x80
#define SERDES_IREG_FLD_PCS_RXEQ_COARSE_RUN2_MASK_SHIFT			7

#define SERDES_IREG_FLD_PCS_RXEQ_COARSE_STEP_REG_NUM			24
#define SERDES_IREG_FLD_PCS_RXEQ_COARSE_STEP_MASK			0x3e
#define SERDES_IREG_FLD_PCS_RXEQ_COARSE_STEP_SHIFT			1

#define SERDES_IREG_FLD_PCS_RXEQ_FINE_RUN1_MASK_REG_NUM			35
#define SERDES_IREG_FLD_PCS_RXEQ_FINE_RUN1_MASK_MASK			0xff
#define SERDES_IREG_FLD_PCS_RXEQ_FINE_RUN1_MASK_SHIFT			0

#define SERDES_IREG_FLD_PCS_RXEQ_FINE_RUN2_MASK_REG_NUM			34
#define SERDES_IREG_FLD_PCS_RXEQ_FINE_RUN2_MASK_MASK			0x80
#define SERDES_IREG_FLD_PCS_RXEQ_FINE_RUN2_MASK_SHIFT			7

#define SERDES_IREG_FLD_PCS_RXEQ_FINE_STEP_REG_NUM			36
#define SERDES_IREG_FLD_PCS_RXEQ_FINE_STEP_MASK				0x1f
#define SERDES_IREG_FLD_PCS_RXEQ_FINE_STEP_SHIFT			0

#define SERDES_IREG_FLD_PCS_RXEQ_LOOKUP_CODE_EN_REG_NUM			37
#define SERDES_IREG_FLD_PCS_RXEQ_LOOKUP_CODE_EN_MASK			0xff
#define SERDES_IREG_FLD_PCS_RXEQ_LOOKUP_CODE_EN_SHIFT			0

#define SERDES_IREG_FLD_PCS_RXEQ_LOOKUP_LASTCODE_REG_NUM		36
#define SERDES_IREG_FLD_PCS_RXEQ_LOOKUP_LASTCODE_MASK			0xe0
#define SERDES_IREG_FLD_PCS_RXEQ_LOOKUP_LASTCODE_SHIFT			5

#ifdef __cplusplus
}
#endif

#endif /* __AL_serdes_REG_H */

