/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Devicetree bindings definitions for tlv320adc3xxx driver.
 *
 * Copyright (C) 2021 Axis Communications AB
 */
#ifndef __DT_TLV320ADC3XXX_H
#define __DT_TLV320ADC3XXX_H

#define ADC3XXX_GPIO_DISABLED		0 /* I/O buffers powered down */
#define ADC3XXX_GPIO_INPUT		1 /* Various non-GPIO inputs */
#define ADC3XXX_GPIO_GPI		2 /* General purpose input */
#define ADC3XXX_GPIO_GPO		3 /* General purpose output */
#define ADC3XXX_GPIO_CLKOUT		4 /* Source set in reg. CLKOUT_MUX */
#define ADC3XXX_GPIO_INT1		5 /* INT1 output */
#define ADC3XXX_GPIO_INT2		6 /* INT2 output */
/* value 7 is reserved */
#define ADC3XXX_GPIO_SECONDARY_BCLK	8 /* Codec interface secondary BCLK */
#define ADC3XXX_GPIO_SECONDARY_WCLK	9 /* Codec interface secondary WCLK */
#define ADC3XXX_GPIO_ADC_MOD_CLK	10 /* Clock output for digital mics */
/* values 11-15 reserved */

#define ADC3XXX_MICBIAS_OFF		0 /* Micbias pin powered off */
#define ADC3XXX_MICBIAS_2_0V		1 /* Micbias pin set to 2.0V */
#define ADC3XXX_MICBIAS_2_5V		2 /* Micbias pin set to 2.5V */
#define ADC3XXX_MICBIAS_AVDD		3 /* Use AVDD voltage for micbias pin */

#endif /* __DT_TLV320ADC3XXX_H */
