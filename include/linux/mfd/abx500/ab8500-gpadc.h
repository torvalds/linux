/*
 * Copyright (C) 2010 ST-Ericsson SA
 * Licensed under GPLv2.
 *
 * Author: Arun R Murthy <arun.murthy@stericsson.com>
 * Author: Daniel Willerud <daniel.willerud@stericsson.com>
 */

#ifndef	_AB8500_GPADC_H
#define _AB8500_GPADC_H

/* GPADC source: From datasheet(ADCSwSel[4:0] in GPADCCtrl2) */
#define BAT_CTRL	0x01
#define BTEMP_BALL	0x02
#define MAIN_CHARGER_V	0x03
#define ACC_DETECT1	0x04
#define ACC_DETECT2	0x05
#define ADC_AUX1	0x06
#define ADC_AUX2	0x07
#define MAIN_BAT_V	0x08
#define VBUS_V		0x09
#define MAIN_CHARGER_C	0x0A
#define USB_CHARGER_C	0x0B
#define BK_BAT_V	0x0C
#define DIE_TEMP	0x0D

struct ab8500_gpadc;

struct ab8500_gpadc *ab8500_gpadc_get(char *name);
int ab8500_gpadc_convert(struct ab8500_gpadc *gpadc, u8 channel);
int ab8500_gpadc_read_raw(struct ab8500_gpadc *gpadc, u8 channel);
int ab8500_gpadc_ad_to_voltage(struct ab8500_gpadc *gpadc,
    u8 channel, int ad_value);

#endif /* _AB8500_GPADC_H */
