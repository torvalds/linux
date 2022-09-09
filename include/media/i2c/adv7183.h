/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * adv7183.h - definition for adv7183 inputs and outputs
 *
 * Copyright (c) 2011 Analog Devices Inc.
 */

#ifndef _ADV7183_H_
#define _ADV7183_H_

/* ADV7183 HW inputs */
#define ADV7183_COMPOSITE0  0  /* CVBS in on AIN1 */
#define ADV7183_COMPOSITE1  1  /* CVBS in on AIN2 */
#define ADV7183_COMPOSITE2  2  /* CVBS in on AIN3 */
#define ADV7183_COMPOSITE3  3  /* CVBS in on AIN4 */
#define ADV7183_COMPOSITE4  4  /* CVBS in on AIN5 */
#define ADV7183_COMPOSITE5  5  /* CVBS in on AIN6 */
#define ADV7183_COMPOSITE6  6  /* CVBS in on AIN7 */
#define ADV7183_COMPOSITE7  7  /* CVBS in on AIN8 */
#define ADV7183_COMPOSITE8  8  /* CVBS in on AIN9 */
#define ADV7183_COMPOSITE9  9  /* CVBS in on AIN10 */
#define ADV7183_COMPOSITE10 10 /* CVBS in on AIN11 */

#define ADV7183_SVIDEO0     11 /* Y on AIN1, C on AIN4 */
#define ADV7183_SVIDEO1     12 /* Y on AIN2, C on AIN5 */
#define ADV7183_SVIDEO2     13 /* Y on AIN3, C on AIN6 */

#define ADV7183_COMPONENT0  14 /* Y on AIN1, Pr on AIN4, Pb on AIN5 */
#define ADV7183_COMPONENT1  15 /* Y on AIN2, Pr on AIN3, Pb on AIN6 */

/* ADV7183 HW outputs */
#define ADV7183_8BIT_OUT    0
#define ADV7183_16BIT_OUT   1

#endif
