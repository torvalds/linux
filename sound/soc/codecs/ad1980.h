/*
 * ad1980.h  --  ad1980 Soc Audio driver
 *
 * WARNING:
 *
 * Because Analog Devices Inc. discontinued the ad1980 sound chip since
 * Sep. 2009, this ad1980 driver is not maintained, tested and supported
 * by ADI now.
 */

#ifndef _AD1980_H
#define _AD1980_H
/* Bit definition of Power-Down Control/Status Register */
#define ADC		0x0001
#define DAC		0x0002
#define ANL		0x0004
#define REF		0x0008
#define PR0		0x0100
#define PR1		0x0200
#define PR2		0x0400
#define PR3		0x0800
#define PR4		0x1000
#define PR5		0x2000
#define PR6		0x4000

#endif
