/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2004 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */
#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"

#include "ar5210/ar5210.h"
#include "ar5210/ar5210reg.h"

/*
 * Return non-zero if an interrupt is pending.
 */
HAL_BOOL
ar5210IsInterruptPending(struct ath_hal *ah)
{
	return (OS_REG_READ(ah, AR_INTPEND) ? AH_TRUE : AH_FALSE);
}

/*
 * Read the Interrupt Status Register value and return
 * an abstracted bitmask of the data found in the ISR.
 * Note that reading the ISR clear pending interrupts.
 */
HAL_BOOL
ar5210GetPendingInterrupts(struct ath_hal *ah, HAL_INT *masked)
{
#define	AR_FATAL_INT \
    (AR_ISR_MCABT_INT | AR_ISR_SSERR_INT | AR_ISR_DPERR_INT | AR_ISR_RXORN_INT)
	struct ath_hal_5210 *ahp = AH5210(ah);
	uint32_t isr;

	isr = OS_REG_READ(ah, AR_ISR);
	if (isr == 0xffffffff) {
		*masked = 0;
		return AH_FALSE;
	}

	/*
	 * Mask interrupts that have no device-independent
	 * representation; these are added back below.  We
	 * also masked with the abstracted IMR to insure no
	 * status bits leak through that weren't requested
	 * (e.g. RXNOFRM) and that might confuse the caller.
	 */
	*masked = (isr & (HAL_INT_COMMON - HAL_INT_BNR)) & ahp->ah_maskReg;

	if (isr & AR_FATAL_INT)
		*masked |= HAL_INT_FATAL;
	if (isr & (AR_ISR_RXOK_INT | AR_ISR_RXERR_INT))
		*masked |= HAL_INT_RX;
	if (isr & (AR_ISR_TXOK_INT | AR_ISR_TXDESC_INT | AR_ISR_TXERR_INT | AR_ISR_TXEOL_INT))
		*masked |= HAL_INT_TX;

	/*
	 * On fatal errors collect ISR state for debugging.
	 */
	if (*masked & HAL_INT_FATAL) {
		AH_PRIVATE(ah)->ah_fatalState[0] = isr;
	}

	return AH_TRUE;
#undef AR_FATAL_INT
}

HAL_INT
ar5210GetInterrupts(struct ath_hal *ah)
{
	return AH5210(ah)->ah_maskReg;
}

HAL_INT
ar5210SetInterrupts(struct ath_hal *ah, HAL_INT ints)
{
	struct ath_hal_5210 *ahp = AH5210(ah);
	uint32_t omask = ahp->ah_maskReg;
	uint32_t mask;

	HALDEBUG(ah, HAL_DEBUG_INTERRUPT, "%s: 0x%x => 0x%x\n",
	    __func__, omask, ints);

	/*
	 * Disable interrupts here before reading & modifying
	 * the mask so that the ISR does not modify the mask
	 * out from under us.
	 */
	if (omask & HAL_INT_GLOBAL) {
		HALDEBUG(ah, HAL_DEBUG_INTERRUPT, "%s: disable IER\n", __func__);
		OS_REG_WRITE(ah, AR_IER, AR_IER_DISABLE);
	}

	mask = ints & (HAL_INT_COMMON - HAL_INT_BNR);
	if (ints & HAL_INT_RX)
		mask |= AR_IMR_RXOK_INT | AR_IMR_RXERR_INT;
	if (ints & HAL_INT_TX) {
		if (ahp->ah_txOkInterruptMask)
			mask |= AR_IMR_TXOK_INT;
		if (ahp->ah_txErrInterruptMask)
			mask |= AR_IMR_TXERR_INT;
		if (ahp->ah_txDescInterruptMask)
			mask |= AR_IMR_TXDESC_INT;
		if (ahp->ah_txEolInterruptMask)
			mask |= AR_IMR_TXEOL_INT;
	}

	/* Write the new IMR and store off our SW copy. */
	HALDEBUG(ah, HAL_DEBUG_INTERRUPT, "%s: new IMR 0x%x\n", __func__, mask);
	OS_REG_WRITE(ah, AR_IMR, mask);
	ahp->ah_maskReg = ints;

	/* Re-enable interrupts as appropriate. */
	if (ints & HAL_INT_GLOBAL) {
		HALDEBUG(ah, HAL_DEBUG_INTERRUPT, "%s: enable IER\n", __func__);
		OS_REG_WRITE(ah, AR_IER, AR_IER_ENABLE);
	}

	return omask;
}
