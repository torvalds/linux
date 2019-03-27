/*-
 * Copyright (C) 2008 MARVELL INTERNATIONAL LTD.
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _TWSI_H_
#define	_TWSI_H_

#ifdef EXT_RESOURCES
#include <dev/extres/clk/clk.h>
#endif

struct twsi_baud_rate {
	uint32_t	raw;
	int		param;
	int		m;
	int		n;
};

struct twsi_softc {
	device_t	dev;
	struct resource	*res[1];	/* SYS_RES_MEMORY */
	struct mtx	mutex;
	device_t	iicbus;
#ifdef EXT_RESOURCES
	clk_t		clk_core;
	clk_t		clk_reg;
#endif

	bus_size_t	reg_data;
	bus_size_t	reg_slave_addr;
	bus_size_t	reg_slave_ext_addr;
	bus_size_t	reg_control;
	bus_size_t	reg_status;
	bus_size_t	reg_baud_rate;
	bus_size_t	reg_soft_reset;
	struct twsi_baud_rate  baud_rate[IIC_FASTEST + 1];
};

DECLARE_CLASS(twsi_driver);

#define	TWSI_BAUD_RATE_PARAM(M,N)	((((M) << 3) | ((N) & 0x7)) & 0x7f)

int twsi_attach(device_t);
int twsi_detach(device_t);

#endif /* _TWSI_H_ */
