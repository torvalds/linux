/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2008 by Nathan Whitehorn. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACIO_MACGPIOVAR_H_
#define _MACIO_MACGPIOVAR_H_

/* relative offsets into gpio space */
#define GPIO_EXTINT_BASE	0x08
#define GPIO_BASE		0x1a

#define GPIO_EXTINT_COUNT	0x12
#define GPIO_COUNT		0x11

#define GPIO_LEVELS_0	0x50
#define GPIO_LEVELS_1	0x54

/* gpio bit definitions */
#define GPIO_DATA		0x01 /* GPIO data */
#define GPIO_LEVEL_RO		0x02 /* read-only level on pin */
#define GPIO_DDR_INPUT		0x00 /* use for input */
#define GPIO_DDR_OUTPUT		0x04 /* use for output */

uint8_t	macgpio_read(device_t dev);
void	macgpio_write(device_t dev,uint8_t);

#endif /* _MACIO_MACGPIOVAR_H_ */
