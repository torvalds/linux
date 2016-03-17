/*
 * Copyright Everest Semiconductor Co.,Ltd
 *
 * Author: David Yang <yangxiaohua@everest-semi.com>
 *
 * Based on ES8323.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _ES8316_H
#define _ES8316_H

/* ES8316 register space */
/*
* RESET Control
*/
#define ES8316_RESET_REG00             0x00
/*
* Clock Managerment
*/
#define ES8316_CLKMGR_CLKSW_REG01      0x01
#define ES8316_CLKMGR_CLKSEL_REG02     0x02
#define ES8316_CLKMGR_ADCOSR_REG03     0x03
#define ES8316_CLKMGR_ADCDIV1_REG04    0x04
#define ES8316_CLKMGR_ADCDIV2_REG05    0x05
#define ES8316_CLKMGR_DACDIV1_REG06    0x06
#define ES8316_CLKMGR_DACDIV2_REG07    0x07
#define ES8316_CLKMGR_CPDIV_REG08      0x08
/*
* SDP Control
*/
#define ES8316_SDP_MS_BCKDIV_REG09     0x09
#define ES8316_SDP_ADCFMT_REG0A        0x0a
#define ES8316_SDP_DACFMT_REG0B        0x0b
/*
* System Control
*/
#define ES8316_SYS_VMIDSEL_REG0C       0x0c
#define ES8316_SYS_PDN_REG0D           0x0d
#define ES8316_SYS_LP1_REG0E           0x0e
#define ES8316_SYS_LP2_REG0F           0x0f
#define ES8316_SYS_VMIDLOW_REG10       0x10
#define ES8316_SYS_VSEL_REG11          0x11
#define ES8316_SYS_REF_REG12           0x12
/*
* HP Mixer
*/
#define ES8316_HPMIX_SEL_REG13         0x13
#define ES8316_HPMIX_SWITCH_REG14      0x14
#define ES8316_HPMIX_PDN_REG15         0x15
#define ES8316_HPMIX_VOL_REG16         0x16
/*
* Charge Pump Headphone driver
*/
#define ES8316_CPHP_OUTEN_REG17        0x17
#define ES8316_CPHP_ICAL_VOL_REG18     0x18
#define ES8316_CPHP_PDN1_REG19         0x19
#define ES8316_CPHP_PDN2_REG1A         0x1a
#define ES8316_CPHP_LDOCTL_REG1B       0x1b
/*
* Calibration
*/
#define ES8316_CAL_TYPE_REG1C         0x1c
#define ES8316_CAL_SET_REG1D          0x1d
#define ES8316_CAL_HPLIV_REG1E        0x1e
#define ES8316_CAL_HPRIV_REG1F        0x1f
#define ES8316_CAL_HPLMV_REG20        0x20
#define ES8316_CAL_HPRMV_REG21        0x21
/*
* ADC Control
*/
#define ES8316_ADC_PDN_LINSEL_REG22   0x22
#define ES8316_ADC_PGAGAIN_REG23      0x23
#define ES8316_ADC_D2SEPGA_REG24      0x24
#define ES8316_ADC_DMIC_REG25         0x25
#define ES8316_ADC_MUTE_REG26         0x26
#define ES8316_ADC_VOLUME_REG27       0x27
#define ES8316_ADC_ALC1_REG29         0x29
#define ES8316_ADC_ALC2_REG2A         0x2a
#define ES8316_ADC_ALC3_REG2B         0x2b
#define ES8316_ADC_ALC4_REG2C         0x2c
#define ES8316_ADC_ALC5_REG2D         0x2d
#define ES8316_ADC_ALC6_REG2E         0x2e
/*
* DAC Control
*/
#define ES8316_DAC_PDN_REG2F          0x2f
#define ES8316_DAC_SET1_REG30         0x30
#define ES8316_DAC_SET2_REG31         0x31
#define ES8316_DAC_SET3_REG32         0x32
#define ES8316_DAC_VOLL_REG33         0x33
#define ES8316_DAC_VOLR_REG34         0x34
/*
* GPIO
*/
#define ES8316_GPIO_SEL_REG4D         0x4D
#define ES8316_GPIO_DEBUNCE_INT_REG4E 0x4E
#define ES8316_GPIO_FLAG              0x4F
/*
* TEST MODE
*/
#define ES8316_TESTMODE_REG50         0x50
#define ES8316_TEST1_REG51            0x51
#define ES8316_TEST2_REG52            0x52
#define ES8316_TEST3_REG53            0x53

#define ES8316_IFACE            ES8316_SDP_MS_BCKDIV_REG09
#define ES8316_ADC_IFACE        ES8316_SDP_ADCFMT_REG0A
#define ES8316_DAC_IFACE        ES8316_SDP_DACFMT_REG0B

#define ES8316_REGNUM      84

/* REGISTER 0X01 CLOCK MANAGER */
#define ES8316_CLKMGR_MCLK_DIV_MASK	(0X1<<7)
#define ES8316_CLKMGR_MCLK_DIV_NML	(0X0<<7)
#define ES8316_CLKMGR_MCLK_DIV_1	(0X1<<7)
#define ES8316_CLKMGR_ADC_MCLK_MASK	(0X1<<3)
#define ES8316_CLKMGR_ADC_MCLK_EN	(0X1<<3)
#define ES8316_CLKMGR_ADC_MCLK_DIS	(0X0<<3)
#define ES8316_CLKMGR_DAC_MCLK_MASK	(0X1<<2)
#define ES8316_CLKMGR_DAC_MCLK_EN	(0X1<<2)
#define ES8316_CLKMGR_DAC_MCLK_DIS	(0X0<<2)
#define ES8316_CLKMGR_ADC_ANALOG_MASK	(0X1<<1)
#define ES8316_CLKMGR_ADC_ANALOG_EN	(0X1<<1)
#define ES8316_CLKMGR_ADC_ANALOG_DIS	(0X0<<1)
#define ES8316_CLKMGR_DAC_ANALOG_MASK	(0X1<<0)
#define ES8316_CLKMGR_DAC_ANALOG_EN	(0X1<<0)
#define ES8316_CLKMGR_DAC_ANALOG_DIS	(0X0<<0)

/* REGISTER 0X0A */
#define ES8316_ADCWL_MASK	(0x7 << 2)
#define ES8316_ADCWL_32		(0x4 << 2)
#define ES8316_ADCWL_24		(0x0 << 2)
#define ES8316_ADCWL_20		(0x1 << 2)
#define ES8316_ADCWL_18		(0x2 << 2)
#define ES8316_ADCWL_16		(0x3 << 2)
#define ES8316_ADCFMT_MASK	(0x3 << 0)
#define ES8316_ADCFMT_I2S	(0x0 << 0)
#define ES8316_ADCWL_LEFT	(0x1 << 0)
#define ES8316_ADCWL_RIGHT	(0x2 << 0)
#define ES8316_ADCWL_PCM	(0x3 << 0)

/* REGISTER 0X0B */
#define ES8316_DACWL_MASK	(0x7 << 2)
#define ES8316_DACWL_32		(0x4 << 2)
#define ES8316_DACWL_24		(0x0 << 2)
#define ES8316_DACWL_20		(0x1 << 2)
#define ES8316_DACWL_18		(0x2 << 2)
#define ES8316_DACWL_16		(0x3 << 2)
#define ES8316_DACFMT_MASK	(0x3 << 0)
#define ES8316_DACFMT_I2S	(0x0 << 0)
#define ES8316_DACWL_LEFT	(0x1 << 0)
#define ES8316_DACWL_RIGHT	(0x2 << 0)
#define ES8316_DACWL_PCM	(0x3 << 0)

#endif
