/*
 * lm4857.h  --  ALSA Soc Audio Layer
 *
 * Copyright 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  Revision history
 *    18th Jun 2007   Initial version.
 */

#ifndef LM4857_H_
#define LM4857_H_

/* The register offsets in the cache array */
#define LM4857_MVOL 0
#define LM4857_LVOL 1
#define LM4857_RVOL 2
#define LM4857_CTRL 3

/* the shifts required to set these bits */
#define LM4857_3D 5
#define LM4857_WAKEUP 5
#define LM4857_EPGAIN 4

#endif /*LM4857_H_*/

