/*-
 * Copyright 2013-2015 John Wehle <john@feith.com>
 * All rights reserved.
 *
 * Based on omap4_l2cache.c by Olivier Houchard
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/pl310.h>

void
platform_pl310_init(struct pl310_softc *sc)
{
	uint32_t aux;

	aux = pl310_read4(sc, PL310_AUX_CTRL);

	/*
	 * The Amlogic Linux platform code enables via AUX:
	 *
	 *   Early BRESP
	 *   Full Line of Zero (which must match processor setting)
	 *   Data Prefetch
	 *
	 * and additionally on the m6 enables:
	 *
	 *   Instruction Prefetch
	 *
	 * For the moment we only enable Data Prefetch ...
	 * further refinements can happen as things mature.
	 */

	/*
	 * Disable instruction prefetch.
	 */
	aux &= ~AUX_CTRL_INSTR_PREFETCH;

	/*
	 * Enable data prefetch.
	 */
	aux |= AUX_CTRL_DATA_PREFETCH;

	pl310_write4(sc, PL310_AUX_CTRL, aux);
}

void
platform_pl310_write_ctrl(struct pl310_softc *sc, uint32_t val)
{

	pl310_write4(sc, PL310_CTRL, val);
}

void
platform_pl310_write_debug(struct pl310_softc *sc, uint32_t val)
{

	pl310_write4(sc, PL310_DEBUG_CTRL, val);
}
