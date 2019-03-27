/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Olivier Houchard.
 * All rights reserved.
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * The machine-dependent part of the arm/pl310 driver for imx6 SoCs.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/pl310.h>
#include <machine/platformvar.h>

#include <arm/freescale/imx/imx6_machdep.h>

#include "platform_pl310_if.h"

void
imx6_pl310_init(platform_t plat, struct pl310_softc *sc)
{
	uint32_t reg;

	/*
	 * Enable power saving modes:
	 *  - Dynamic Gating stops the clock when the controller is idle.
	 *  - Standby stops the clock when the cores are in WFI mode.
	 */
	reg = pl310_read4(sc, PL310_POWER_CTRL);
	reg |= POWER_CTRL_ENABLE_GATING | POWER_CTRL_ENABLE_STANDBY;
	pl310_write4(sc, PL310_POWER_CTRL, reg);

	pl310_set_ram_latency(sc, PL310_TAG_RAM_CTRL,  4, 2, 3);
	pl310_set_ram_latency(sc, PL310_DATA_RAM_CTRL, 4, 2, 3);
}
