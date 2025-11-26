/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2020 Pengutronix, Marc Kleine-Budde <kernel@pengutronix.de>
 * Copyright (c) 2021-2025 Vincent Mailhol <mailhol@kernel.org>
 */

#ifndef _CAN_BITTIMING_H
#define _CAN_BITTIMING_H

#include <linux/netdevice.h>
#include <linux/can/netlink.h>

#define CAN_SYNC_SEG 1

#define CAN_BITRATE_UNSET 0
#define CAN_BITRATE_UNKNOWN (-1U)

#define CAN_CTRLMODE_FD_TDC_MASK				\
	(CAN_CTRLMODE_TDC_AUTO | CAN_CTRLMODE_TDC_MANUAL)
#define CAN_CTRLMODE_XL_TDC_MASK				\
	(CAN_CTRLMODE_XL_TDC_AUTO | CAN_CTRLMODE_XL_TDC_MANUAL)
#define CAN_CTRLMODE_TDC_AUTO_MASK				\
	(CAN_CTRLMODE_TDC_AUTO | CAN_CTRLMODE_XL_TDC_AUTO)
#define CAN_CTRLMODE_TDC_MANUAL_MASK				\
	(CAN_CTRLMODE_TDC_MANUAL | CAN_CTRLMODE_XL_TDC_MANUAL)

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

/* The transceiver decoding margin corresponds to t_Decode in ISO 11898-2 */
#define CAN_PWM_DECODE_NS 5
/* Maximum PWM symbol duration. Corresponds to t_SymbolNom_MAX - t_Decode */
#define CAN_PWM_NS_MAX (205 - CAN_PWM_DECODE_NS)

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

/*
 * struct can_pwm - CAN Pulse-Width Modulation (PWM) parameters
 *
 * @pwms: pulse width modulation short phase
 * @pwml: pulse width modulation long phase
 * @pwmo: pulse width modulation offset
 */
struct can_pwm {
	u32 pwms;
	u32 pwml;
	u32 pwmo;
};

/*
 * struct can_pwm - CAN hardware-dependent constants for Pulse-Width
 *	Modulation (PWM)
 *
 * @pwms_min: PWM short phase minimum value. Must be at least 1.
 * @pwms_max: PWM short phase maximum value
 * @pwml_min: PWM long phase minimum value. Must be at least 1.
 * @pwml_max: PWM long phase maximum value
 * @pwmo_min: PWM offset phase minimum value
 * @pwmo_max: PWM offset phase maximum value
 */
struct can_pwm_const {
	u32 pwms_min;
	u32 pwms_max;
	u32 pwml_min;
	u32 pwml_max;
	u32 pwmo_min;
	u32 pwmo_max;
};

struct data_bittiming_params {
	const struct can_bittiming_const *data_bittiming_const;
	struct can_bittiming data_bittiming;
	const struct can_tdc_const *tdc_const;
	const struct can_pwm_const *pwm_const;
	union {
		struct can_tdc tdc;
		struct can_pwm pwm;
	};
	const u32 *data_bitrate_const;
	unsigned int data_bitrate_const_cnt;
	int (*do_set_data_bittiming)(struct net_device *dev);
	int (*do_get_auto_tdcv)(const struct net_device *dev, u32 *tdcv);
};

#ifdef CONFIG_CAN_CALC_BITTIMING
int can_calc_bittiming(const struct net_device *dev, struct can_bittiming *bt,
		       const struct can_bittiming_const *btc, struct netlink_ext_ack *extack);

void can_calc_tdco(struct can_tdc *tdc, const struct can_tdc_const *tdc_const,
		   const struct can_bittiming *dbt,
		   u32 tdc_mask, u32 *ctrlmode, u32 ctrlmode_supported);

int can_calc_pwm(struct net_device *dev, struct netlink_ext_ack *extack);
#else /* !CONFIG_CAN_CALC_BITTIMING */
static inline int
can_calc_bittiming(const struct net_device *dev, struct can_bittiming *bt,
		   const struct can_bittiming_const *btc, struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG(extack, "bit-timing calculation not available\n");
	return -EINVAL;
}

static inline void
can_calc_tdco(struct can_tdc *tdc, const struct can_tdc_const *tdc_const,
	      const struct can_bittiming *dbt,
	      u32 tdc_mask, u32 *ctrlmode, u32 ctrlmode_supported)
{
}

static inline int
can_calc_pwm(struct net_device *dev, struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG(extack,
		       "bit-timing calculation not available: manually provide PWML and PWMS\n");
	return -EINVAL;
}
#endif /* CONFIG_CAN_CALC_BITTIMING */

void can_sjw_set_default(struct can_bittiming *bt);

int can_sjw_check(const struct net_device *dev, const struct can_bittiming *bt,
		  const struct can_bittiming_const *btc, struct netlink_ext_ack *extack);

int can_get_bittiming(const struct net_device *dev, struct can_bittiming *bt,
		      const struct can_bittiming_const *btc,
		      const u32 *bitrate_const,
		      const unsigned int bitrate_const_cnt,
		      struct netlink_ext_ack *extack);

int can_validate_pwm_bittiming(const struct net_device *dev,
			       const struct can_pwm *pwm,
			       struct netlink_ext_ack *extack);

/*
 * can_get_relative_tdco() - TDCO relative to the sample point
 *
 * struct can_tdc::tdco represents the absolute offset from TDCV. Some
 * controllers use instead an offset relative to the Sample Point (SP)
 * such that:
 *
 * SSP = TDCV + absolute TDCO
 *     = TDCV + SP + relative TDCO
 *
 * -+----------- one bit ----------+-- TX pin
 *  |<--- Sample Point --->|
 *
 *                         --+----------- one bit ----------+-- RX pin
 *  |<-------- TDCV -------->|
 *                           |<------------------------>| absolute TDCO
 *                           |<--- Sample Point --->|
 *                           |                      |<->| relative TDCO
 *  |<------------- Secondary Sample Point ------------>|
 */
static inline s32 can_get_relative_tdco(const struct data_bittiming_params *dbt_params)
{
	const struct can_bittiming *dbt = &dbt_params->data_bittiming;
	s32 sample_point_in_tc = (CAN_SYNC_SEG + dbt->prop_seg +
				  dbt->phase_seg1) * dbt->brp;

	return (s32)dbt_params->tdc.tdco - sample_point_in_tc;
}

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

/* Duration of one bit in minimum time quantum */
static inline unsigned int can_bit_time_tqmin(const struct can_bittiming *bt)
{
	return can_bit_time(bt) * bt->brp;
}

/* Convert a duration from minimum a minimum time quantum to nano seconds */
static inline u32 can_tqmin_to_ns(u32 tqmin, u32 clock_freq)
{
	return DIV_U64_ROUND_CLOSEST(mul_u32_u32(tqmin, NSEC_PER_SEC),
				     clock_freq);
}

#endif /* !_CAN_BITTIMING_H */
