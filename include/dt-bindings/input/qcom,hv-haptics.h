/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 */

/* definitions for drive waveform shape */
#define WF_SQUARE			0   /* LRA only */
#define WF_SINE				1   /* LRA only */
#define WF_NO_MODULATION		2   /* ERM only */

/* definitions for brake mode */
#define BRAKE_OPEN_LOOP			0
#define BRAKE_CLOSE_LOOP		1
#define BRAKE_PREDICTIVE		2
#define BRAKE_AUTO			3

/* definitions for brake sine signal gain */
#define BRAKE_SINE_GAIN_X1		0
#define BRAKE_SINE_GAIN_X2		1
#define BRAKE_SINE_GAIN_X4		2
#define BRAKE_SINE_GAIN_X8		3

/* definitions for pattern sample period */
#define S_PERIOD_T_LRA			0
#define S_PERIOD_T_LRA_DIV_2		1
#define S_PERIOD_T_LRA_DIV_4		2
#define S_PERIOD_T_LRA_DIV_8		3
#define S_PERIOD_T_LRA_X_2		4
#define S_PERIOD_T_LRA_X_4		5
#define S_PERIOD_T_LRA_X_8		6
/* F_8KHZ to F_48KHZ periods can only be specified for FIFO based effects */
#define S_PERIOD_F_8KHZ			8
#define S_PERIOD_F_16KHZ		9
#define S_PERIOD_F_24KHZ		10
#define S_PERIOD_F_32KHZ		11
#define S_PERIOD_F_44P1KHZ		12
#define S_PERIOD_F_48KHZ		13
