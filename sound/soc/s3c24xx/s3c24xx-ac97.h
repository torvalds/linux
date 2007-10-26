/*
 * s3c24xx-ac97.c  --  ALSA Soc Audio Layer
 *
 * (c) 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  Revision history
 *    10th Nov 2006   Initial version.
 */

#ifndef S3C24XXAC97_H_
#define S3C24XXAC97_H_

#define AC_CMD_ADDR(x) (x << 16)
#define AC_CMD_DATA(x) (x & 0xffff)

#ifdef CONFIG_CPU_S3C2440
#define IRQ_S3C244x_AC97 IRQ_S3C2440_AC97
#else
#define IRQ_S3C244x_AC97 IRQ_S3C2443_AC97
#endif

extern struct snd_soc_cpu_dai s3c2443_ac97_dai[];

#endif /*S3C24XXAC97_H_*/
