/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2020 Pengutronix, Marc Kleine-Budde <kernel@pengutronix.de>
 * Copyright (c) 2021 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#ifndef _CAN_BITTIMING_H
#define _CAN_BITTIMING_H

#include <linux/netdevice.h>
#include <linux/can/netlink.h>

#define CAN_SYNC_SEG 1


/* Kilobits and Megabits per second */
#define CAN_KBPS 1000UL
#define CAN_MBPS 1000000UL

/* Megahertz */
#define CAN_MHZ 1000000UL

/*
 * struct can_tdc - CAN FD Transmission Delay Compensation parameters
 *
 * At high bit rates, the propagation delay from the TX pin to the RX
 * pin of the transceiver causes measurement errors: the sample point
 * on the RX pin might occur on the previous bit.
 *
 * To solve this issue, ISO 11898-1 introduces in section 11.3.3
 * "Transmitter delay compensation" a SSP (Secondary Sample Point)
 * equal to the distance, in time quanta, from the start of the bit
 * time on the TX pin to the actual measurement on the RX pin.
 *
 * This structure contains the parameters to calculate that SSP.
 *
 * @tdcv: Transmitter Delay Compensation Value. Distance, in time
 *	quanta, from when the bit is sent on the TX pin to when it is
 *	received on the RX pin of the transmitter. Possible options:
 *
 *	  0: automatic mode. The controller dynamically measures @tdcv
 *	  for each transmitted CAN FD frame.
 *
 *	  Other values: manual mode. Use the fixed provided value.
 *
 * @tdco: Transmitter Delay Compensation Offset. Offset value, in time
 *	quanta, defining the distance between the start of the bit
 *	reception on the RX pin of the transceiver and the SSP
 *	position such that SSP = @tdcv + @tdco.
 *
 *	If @tdco is zero, then TDC is disabled and both @tdcv and
 *	@tdcf should be ignored.
 *
 * @tdcf: Transmitter Delay Compensation Filter window. Defines the
 *	minimum value for the SSP position in time quanta. If SSP is
 *	less than @tdcf, then no delay compensations occur and the
 *	normal sampling point is used instead. The feature is enabled
 *	if and only if @tdcv is set to zero (automatic mode) and @tdcf
 *	is configured to a value greater than @tdco.
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
 * @tdcv_max: Transmitter Delay Compensation Value maximum value.
 *	Should be set to zero if the controller does not support
 *	manual mode for tdcv.
 * @tdco_max: Transmitter Delay Compensation Offset maximum value.
 *	Should not be zero. If the controller does not support TDC,
 *	then the pointer to this structure should be NULL.
 * @tdcf_max: Transmitter Delay Compensation Filter window maximum
 *	value. Should be set to zero if the controller does not
 *	support this feature.
 */
struct can_tdc_const {
	u32 tdcv_max;
	u32 tdco_max;
	u32 tdcf_max;
};

#ifdef CONFIG_CAN_CALC_BITTIMING
int can_calc_bittiming(struct net_device *dev, struct can_bittiming *bt,
		       const struct can_bittiming_const *btc);

void can_calc_tdco(struct net_device *dev);
#else /* !CONFIG_CAN_CALC_BITTIMING */
static inline int
can_calc_bittiming(struct net_device *dev, struct can_bittiming *bt,
		   const struct can_bittiming_const *btc)
{
	netdev_err(dev, "bit-timing calculation not available\n");
	return -EINVAL;
}

static inline void can_calc_tdco(struct net_device *dev)
{
}
#endif /* CONFIG_CAN_CALC_BITTIMING */

int can_get_bittiming(struct net_device *dev, struct can_bittiming *bt,
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
