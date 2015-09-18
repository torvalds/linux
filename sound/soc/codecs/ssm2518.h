/*
 * SSM2518 amplifier audio driver
 *
 * Copyright 2013 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2.
 */

#ifndef __SND_SOC_CODECS_SSM2518_H__
#define __SND_SOC_CODECS_SSM2518_H__

#define SSM2518_SYSCLK 0

enum ssm2518_sysclk_src {
	SSM2518_SYSCLK_SRC_MCLK = 0,
	SSM2518_SYSCLK_SRC_BCLK = 1,
};

#endif
