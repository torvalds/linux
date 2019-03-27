/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2002 by Peter Grehan. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#ifndef _MACIO_MACIOVAR_H_
#define _MACIO_MACIOVAR_H_

/*
 * The addr space size
 * XXX it would be better if this could be determined by querying the
 *     PCI device, but there isn't an access method for this
 */
#define MACIO_REG_SIZE  0x7ffff

/*
 * Feature Control Registers (FCR)
 */
#define HEATHROW_FCR	0x38
#define KEYLARGO_FCR0	0x38
#define KEYLARGO_FCR1	0x3c
#define KEYLARGO_FCR2	0x40

#define FCR_ENET_ENABLE	0x60000000
#define FCR_ENET_RESET	0x80000000

#define FCR1_I2S0_CLK_ENABLE	0x00001000
#define FCR1_I2S0_ENABLE	0x00002000

/* Used only by macio_enable_wireless() for now. */
#define KEYLARGO_GPIO_BASE	0x6a
#define KEYLARGO_EXTINT_GPIO_REG_BASE	0x58

/*
 * Format of a macio reg property entry.
 */
struct macio_reg {
	u_int32_t	mr_base;
	u_int32_t	mr_size;
};

/*
 * Per macio device structure.
 */
struct macio_devinfo {
	int        mdi_interrupts[6];
	int	   mdi_ninterrupts;
	int        mdi_base;
	struct ofw_bus_devinfo mdi_obdinfo;
	struct resource_list mdi_resources;
};

extern int macio_enable_wireless(device_t dev, bool enable);

#endif /* _MACIO_MACIOVAR_H_ */
