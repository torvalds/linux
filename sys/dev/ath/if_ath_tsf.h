/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#ifndef	__IF_ATH_TSF_H__
#define	__IF_ATH_TSF_H__

/*
 * Extend 15-bit time stamp from rx descriptor to
 * a full 64-bit TSF using the specified TSF.
 */
static __inline u_int64_t
ath_extend_tsf15(u_int32_t rstamp, u_int64_t tsf)
{
	if ((tsf & 0x7fff) < rstamp)
		tsf -= 0x8000;

	return ((tsf &~ 0x7fff) | rstamp);
}

/*
 * Extend 32-bit time stamp from rx descriptor to
 * a full 64-bit TSF using the specified TSF.
 */
static __inline u_int64_t
ath_extend_tsf32(u_int32_t rstamp, u_int64_t tsf)
{
	u_int32_t tsf_low = tsf & 0xffffffff;
	u_int64_t tsf64 = (tsf & ~0xffffffffULL) | rstamp;

	if (rstamp > tsf_low && (rstamp - tsf_low > 0x10000000))
		tsf64 -= 0x100000000ULL;

	if (rstamp < tsf_low && (tsf_low - rstamp > 0x10000000))
		tsf64 += 0x100000000ULL;

	return tsf64;
}

/*
 * Extend the TSF from the RX descriptor to a full 64 bit TSF.
 * Earlier hardware versions only wrote the low 15 bits of the
 * TSF into the RX descriptor; later versions (AR5416 and up)
 * include the 32 bit TSF value.
 */
static __inline u_int64_t
ath_extend_tsf(struct ath_softc *sc, u_int32_t rstamp, u_int64_t tsf)
{
	if (sc->sc_rxtsf32)
		return ath_extend_tsf32(rstamp, tsf);
	else
		return ath_extend_tsf15(rstamp, tsf);
}

#endif
