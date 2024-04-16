// SPDX-License-Identifier: GPL-2.0
#ifndef __FSL_FTM_H__
#define __FSL_FTM_H__

#define FTM_SC       0x0 /* Status And Control */
#define FTM_CNT      0x4 /* Counter */
#define FTM_MOD      0x8 /* Modulo */

#define FTM_CNTIN    0x4C /* Counter Initial Value */
#define FTM_STATUS   0x50 /* Capture And Compare Status */
#define FTM_MODE     0x54 /* Features Mode Selection */
#define FTM_SYNC     0x58 /* Synchronization */
#define FTM_OUTINIT  0x5C /* Initial State For Channels Output */
#define FTM_OUTMASK  0x60 /* Output Mask */
#define FTM_COMBINE  0x64 /* Function For Linked Channels */
#define FTM_DEADTIME 0x68 /* Deadtime Insertion Control */
#define FTM_EXTTRIG  0x6C /* FTM External Trigger */
#define FTM_POL      0x70 /* Channels Polarity */
#define FTM_FMS      0x74 /* Fault Mode Status */
#define FTM_FILTER   0x78 /* Input Capture Filter Control */
#define FTM_FLTCTRL  0x7C /* Fault Control */
#define FTM_QDCTRL   0x80 /* Quadrature Decoder Control And Status */
#define FTM_CONF     0x84 /* Configuration */
#define FTM_FLTPOL   0x88 /* FTM Fault Input Polarity */
#define FTM_SYNCONF  0x8C /* Synchronization Configuration */
#define FTM_INVCTRL  0x90 /* FTM Inverting Control */
#define FTM_SWOCTRL  0x94 /* FTM Software Output Control */
#define FTM_PWMLOAD  0x98 /* FTM PWM Load */

#define FTM_SC_CLK_MASK_SHIFT	3
#define FTM_SC_CLK_MASK		(3 << FTM_SC_CLK_MASK_SHIFT)
#define FTM_SC_TOF		0x80
#define FTM_SC_TOIE		0x40
#define FTM_SC_CPWMS		0x20
#define FTM_SC_CLKS		0x18
#define FTM_SC_PS_1		0x0
#define FTM_SC_PS_2		0x1
#define FTM_SC_PS_4		0x2
#define FTM_SC_PS_8		0x3
#define FTM_SC_PS_16		0x4
#define FTM_SC_PS_32		0x5
#define FTM_SC_PS_64		0x6
#define FTM_SC_PS_128		0x7
#define FTM_SC_PS_MASK		0x7

#define FTM_MODE_FAULTIE	0x80
#define FTM_MODE_FAULTM		0x60
#define FTM_MODE_CAPTEST	0x10
#define FTM_MODE_PWMSYNC	0x8
#define FTM_MODE_WPDIS		0x4
#define FTM_MODE_INIT		0x2
#define FTM_MODE_FTMEN		0x1

/* NXP Errata: The PHAFLTREN and PHBFLTREN bits are tide to zero internally
 * and these bits cannot be set. Flextimer cannot use Filter in
 * Quadrature Decoder Mode.
 * https://community.nxp.com/thread/467648#comment-1010319
 */
#define FTM_QDCTRL_PHAFLTREN	0x80
#define FTM_QDCTRL_PHBFLTREN	0x40
#define FTM_QDCTRL_PHAPOL	0x20
#define FTM_QDCTRL_PHBPOL	0x10
#define FTM_QDCTRL_QUADMODE	0x8
#define FTM_QDCTRL_QUADDIR	0x4
#define FTM_QDCTRL_TOFDIR	0x2
#define FTM_QDCTRL_QUADEN	0x1

#define FTM_FMS_FAULTF		0x80
#define FTM_FMS_WPEN		0x40
#define FTM_FMS_FAULTIN		0x10
#define FTM_FMS_FAULTF3		0x8
#define FTM_FMS_FAULTF2		0x4
#define FTM_FMS_FAULTF1		0x2
#define FTM_FMS_FAULTF0		0x1

#define FTM_CSC_BASE		0xC
#define FTM_CSC_MSB		0x20
#define FTM_CSC_MSA		0x10
#define FTM_CSC_ELSB		0x8
#define FTM_CSC_ELSA		0x4
#define FTM_CSC(_channel)	(FTM_CSC_BASE + ((_channel) * 8))

#define FTM_CV_BASE		0x10
#define FTM_CV(_channel)	(FTM_CV_BASE + ((_channel) * 8))

#define FTM_PS_MAX		7

#endif
