/*
 * ADF4350/ADF4351 SPI PLL driver
 *
 * Copyright 2012 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#ifndef IIO_PLL_ADF4350_H_
#define IIO_PLL_ADF4350_H_

/* Registers */
#define ADF4350_REG0	0
#define ADF4350_REG1	1
#define ADF4350_REG2	2
#define ADF4350_REG3	3
#define ADF4350_REG4	4
#define ADF4350_REG5	5

/* REG0 Bit Definitions */
#define ADF4350_REG0_FRACT(x)			(((x) & 0xFFF) << 3)
#define ADF4350_REG0_INT(x)			(((x) & 0xFFFF) << 15)

/* REG1 Bit Definitions */
#define ADF4350_REG1_MOD(x)			(((x) & 0xFFF) << 3)
#define ADF4350_REG1_PHASE(x)			(((x) & 0xFFF) << 15)
#define ADF4350_REG1_PRESCALER			(1 << 27)

/* REG2 Bit Definitions */
#define ADF4350_REG2_COUNTER_RESET_EN		(1 << 3)
#define ADF4350_REG2_CP_THREESTATE_EN		(1 << 4)
#define ADF4350_REG2_POWER_DOWN_EN		(1 << 5)
#define ADF4350_REG2_PD_POLARITY_POS		(1 << 6)
#define ADF4350_REG2_LDP_6ns			(1 << 7)
#define ADF4350_REG2_LDP_10ns			(0 << 7)
#define ADF4350_REG2_LDF_FRACT_N		(0 << 8)
#define ADF4350_REG2_LDF_INT_N			(1 << 8)
#define ADF4350_REG2_CHARGE_PUMP_CURR_uA(x)	(((((x)-312) / 312) & 0xF) << 9)
#define ADF4350_REG2_DOUBLE_BUFF_EN		(1 << 13)
#define ADF4350_REG2_10BIT_R_CNT(x)		((x) << 14)
#define ADF4350_REG2_RDIV2_EN			(1 << 24)
#define ADF4350_REG2_RMULT2_EN			(1 << 25)
#define ADF4350_REG2_MUXOUT(x)			((x) << 26)
#define ADF4350_REG2_NOISE_MODE(x)		((x) << 29)
#define ADF4350_MUXOUT_THREESTATE		0
#define ADF4350_MUXOUT_DVDD			1
#define ADF4350_MUXOUT_GND			2
#define ADF4350_MUXOUT_R_DIV_OUT		3
#define ADF4350_MUXOUT_N_DIV_OUT		4
#define ADF4350_MUXOUT_ANALOG_LOCK_DETECT	5
#define ADF4350_MUXOUT_DIGITAL_LOCK_DETECT	6

/* REG3 Bit Definitions */
#define ADF4350_REG3_12BIT_CLKDIV(x)		((x) << 3)
#define ADF4350_REG3_12BIT_CLKDIV_MODE(x)	((x) << 16)
#define ADF4350_REG3_12BIT_CSR_EN		(1 << 18)
#define ADF4351_REG3_CHARGE_CANCELLATION_EN	(1 << 21)
#define ADF4351_REG3_ANTI_BACKLASH_3ns_EN	(1 << 22)
#define ADF4351_REG3_BAND_SEL_CLOCK_MODE_HIGH	(1 << 23)

/* REG4 Bit Definitions */
#define ADF4350_REG4_OUTPUT_PWR(x)		((x) << 3)
#define ADF4350_REG4_RF_OUT_EN			(1 << 5)
#define ADF4350_REG4_AUX_OUTPUT_PWR(x)		((x) << 6)
#define ADF4350_REG4_AUX_OUTPUT_EN		(1 << 8)
#define ADF4350_REG4_AUX_OUTPUT_FUND		(1 << 9)
#define ADF4350_REG4_AUX_OUTPUT_DIV		(0 << 9)
#define ADF4350_REG4_MUTE_TILL_LOCK_EN		(1 << 10)
#define ADF4350_REG4_VCO_PWRDOWN_EN		(1 << 11)
#define ADF4350_REG4_8BIT_BAND_SEL_CLKDIV(x)	((x) << 12)
#define ADF4350_REG4_RF_DIV_SEL(x)		((x) << 20)
#define ADF4350_REG4_FEEDBACK_DIVIDED		(0 << 23)
#define ADF4350_REG4_FEEDBACK_FUND		(1 << 23)

/* REG5 Bit Definitions */
#define ADF4350_REG5_LD_PIN_MODE_LOW		(0 << 22)
#define ADF4350_REG5_LD_PIN_MODE_DIGITAL	(1 << 22)
#define ADF4350_REG5_LD_PIN_MODE_HIGH		(3 << 22)

/* Specifications */
#define ADF4350_MAX_OUT_FREQ		4400000000ULL /* Hz */
#define ADF4350_MIN_OUT_FREQ		137500000 /* Hz */
#define ADF4351_MIN_OUT_FREQ		34375000 /* Hz */
#define ADF4350_MIN_VCO_FREQ		2200000000ULL /* Hz */
#define ADF4350_MAX_FREQ_45_PRESC	3000000000ULL /* Hz */
#define ADF4350_MAX_FREQ_PFD		32000000 /* Hz */
#define ADF4350_MAX_BANDSEL_CLK		125000 /* Hz */
#define ADF4350_MAX_FREQ_REFIN		250000000 /* Hz */
#define ADF4350_MAX_MODULUS		4095
#define ADF4350_MAX_R_CNT		1023


/**
 * struct adf4350_platform_data - platform specific information
 * @name:		Optional device name.
 * @clkin:		REFin frequency in Hz.
 * @channel_spacing:	Channel spacing in Hz (influences MODULUS).
 * @power_up_frequency:	Optional, If set in Hz the PLL tunes to the desired
 *			frequency on probe.
 * @ref_div_factor:	Optional, if set the driver skips dynamic calculation
 *			and uses this default value instead.
 * @ref_doubler_en:	Enables reference doubler.
 * @ref_div2_en:	Enables reference divider.
 * @r2_user_settings:	User defined settings for ADF4350/1 REGISTER_2.
 * @r3_user_settings:	User defined settings for ADF4350/1 REGISTER_3.
 * @r4_user_settings:	User defined settings for ADF4350/1 REGISTER_4.
 * @gpio_lock_detect:	Optional, if set with a valid GPIO number,
 *			pll lock state is tested upon read.
 *			If not used - set to -1.
 */

struct adf4350_platform_data {
	char			name[32];
	unsigned long		clkin;
	unsigned long		channel_spacing;
	unsigned long long	power_up_frequency;

	unsigned short		ref_div_factor; /* 10-bit R counter */
	bool			ref_doubler_en;
	bool			ref_div2_en;

	unsigned		r2_user_settings;
	unsigned		r3_user_settings;
	unsigned		r4_user_settings;
	int			gpio_lock_detect;
};

#endif /* IIO_PLL_ADF4350_H_ */
