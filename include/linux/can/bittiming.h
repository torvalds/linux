/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2020 Pengutronix, Marc Kleine-Budde <kernel@pengutronix.de>
 * Copyright (c) 2021 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#ifndef _CAN_BITTIMING_H
#define _CAN_BITTIMING_H

#include <linux/netdevice.h>
#include <linux/can/netlink.h>

#define CAN_SYNC_SEG 1

#define CAN_BITRATE_UNSET 0
#define CAN_BITRATE_UNKNOWN (-1U)

#define CAN_CTRLMODE_TDC_MASK					\
	(CAN_CTRLMODE_TDC_AUTO | CAN_CTRLMODE_TDC_MANUAL)

/*
 * struct can_tdc - CAN FD Transmission Delay Compensation parameters
 *
 * At high bit rates, the propagation delay from the TX pin to the RX
 * pin of the transceiver causes measurement errors: the sample point
 * on the RX pin might occur on the previous bit.
 *
 * To solve this issue, ISO 11898-1 introduces in section 11.3.3
 * "Transmitter delay compensation" a SSP (Secondary Sample Point)
 * equal to the distance from the start of the bit time on the TX pin
 * to the actual measurement on the RX pin.
 *
 * This structure contains the parameters to calculate that SSP.
 *
 * -+----------- one bit ----------+-- TX pin
 *  |<--- Sample Point --->|
 *
 *                         --+----------- one bit ----------+-- RX pin
 *  |<-------- TDCV -------->|
 *                           |<------- TDCO ------->|
 *  |<----------- Secondary Sample Point ---------->|
 *
 * To increase precision, contrary to the other bittiming parameters
 * which are measured in time quanta, the TDC parameters are measured
 * in clock periods (also referred as "minimum time quantum" in ISO
 * 11898-1).
 *
 * @tdcv: Transmitter Delay Compensation Value. The time needed for
 *	the signal to propagate, i.e. the distance, in clock periods,
 *	from the start of the bit on the TX pin to when it is received
 *	on the RX pin. @tdcv depends on the controller modes:
 *
 *	  CAN_CTRLMODE_TDC_AUTO is set: The transceiver dynamically
 *	  measures @tdcv for each transmitted CAN FD frame and the
 *	  value provided here should be ignored.
 *
 *	  CAN_CTRLMODE_TDC_MANUAL is set: use the fixed provided @tdcv
 *	  value.
 *
 *	N.B. CAN_CTRLMODE_TDC_AUTO and CAN_CTRLMODE_TDC_MANUAL are
 *	mutually exclusive. Only one can be set at a time. If both
 *	CAN_TDC_CTRLMODE_AUTO and CAN_TDC_CTRLMODE_MANUAL are unset,
 *	TDC is disabled and all the values of this structure should be
 *	ignored.
 *
 * @tdco: Transmitter Delay Compensation Offset. Offset value, in
 *	clock periods, defining the distance between the start of the
 *	bit reception on the RX pin of the transceiver and the SSP
 *	position such that SSP = @tdcv + @tdco.
 *
 * @tdcf: Transmitter Delay Compensation Filter window. Defines the
 *	minimum value for the SSP position in clock periods. If the
 *	SSP position is less than @tdcf, then no delay compensations
 *	occur and the normal sampling point is used instead. The
 *	feature is enabled if and only if @tdcv is set to zero
 *	(automatic mode) and @tdcf is configured to a value greater
 *	than @tdco.
 */
struct can_tdc {
	u32 tdcv;
	u32 tdco;
	u32 tdcf;
};

/*
 * struct can_tdc_const - CAN hardware-dependent constant for
 *	Transmission Delay Compensation
 *
 * @tdcv_min: Transmitter Delay Compensation Value minimum value. If
 *	the controller does not support manual mode for tdcv
 *	(c.f. flag CAN_CTRLMODE_TDC_MANUAL) then this value is
 *	ignored.
 * @tdcv_max: Transmitter Delay Compensation Value maximum value. If
 *	the controller does not support manual mode for tdcv
 *	(c.f. flag CAN_CTRLMODE_TDC_MANUAL) then this value is
 *	ignored.
 *
 * @tdco_min: Transmitter Delay Compensation Offset minimum value.
 * @tdco_max: Transmitter Delay Compensation Offset maximum value.
 *	Should not be zero. If the controller does not support TDC,
 *	then the pointer to this structure should be NULL.
 *
 * @tdcf_min: Transmitter Delay Compensation Filter window minimum
 *	value. If @tdcf_max is zero, this value is ignored.
 * @tdcf_max: Transmitter Delay Compensation Filter window maximum
 *	value. Should be set to zero if the controller does not
 *	support this feature.
 */
struct can_tdc_const {
	u32 tdcv_min;
	u32 tdcv_max;
	u32 tdco_min;
	u32 tdco_max;
	u32 tdcf_min;
	u32 tdcf_max;
};

#ifdef CONFIG_CAN_CALC_BITTIMING
int can_calc_bittiming(const struct net_device *dev, struct can_bittiming *bt,
		       const struct can_bittiming_const *btc);

void can_calc_tdco(struct can_tdc *tdc, const struct can_tdc_const *tdc_const,
		   const struct can_bittiming *dbt,
		   u32 *ctrlmode, u32 ctrlmode_supported);
#else /* !CONFIG_CAN_CALC_BITTIMING */
static inline int
can_calc_bittiming(const struct net_device *dev, struct can_bittiming *bt,
		   const struct can_bittiming_const *btc)
{
	netdev_err(dev, "bit-timing calculation not available\n");
	return -EINVAL;
}

static inline void
can_calc_tdco(struct can_tdc *tdc, const struct can_tdc_const *tdc_const,
	      const struct can_bittiming *dbt,
	      u32 *ctrlmode, u32 ctrlmode_supported)
{
}
#endif /* CONFIG_CAN_CALC_BITTIMING */

int can_get_bittiming(const struct net_device *dev, struct can_bittiming *bt,
		      const struct can_bittiming_const *btc,
		      const u32 *bitrate_const,
		      const unsigned int bitrate_const_cnt);

/*
 * can_bit_time() - Duration of one bit
 *
 * Please refer to ISO 11898-1:2015, section 11.3.1.1 "Bit time" for
 * additional information.
 *
 * Return: the number of time quanta in one bit.
 */
static inline unsigned int can_bit_time(const struct can_bittiming *bt)
{
	return CAN_SYNC_SEG + bt->prop_seg + bt->phase_seg1 + bt->phase_seg2;
}

#endif /* !_CAN_BITTIMING_H */
