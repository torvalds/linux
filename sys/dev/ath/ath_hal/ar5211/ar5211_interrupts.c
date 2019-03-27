/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2006 Atheros Communications, Inc.
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

#include "ar5211/ar5211.h"
#include "ar5211/ar5211reg.h"

/*
 * Checks to see if an interrupt is pending on our NIC
 *
 * Returns: TRUE    if an interrupt is pending
 *          FALSE   if not
 */
HAL_BOOL
ar5211IsInterruptPending(struct ath_hal *ah)
{
	return OS_REG_READ(ah, AR_INTPEND) != 0;
}

/*
 * Reads the Interrupt Status Register value from the NIC, thus deasserting
 * the interrupt line, and returns both the masked and unmasked mapped ISR
 * values.  The value returned is mapped to abstract the hw-specific bit
 * locations in the Interrupt Status Register.
 *
 * Returns: A hardware-abstracted bitmap of all non-masked-out
 *          interrupts pending, as well as an unmasked value
 */
HAL_BOOL
ar5211GetPendingInterrupts(struct ath_hal *ah, HAL_INT *masked)
{
	uint32_t isr;

	isr = OS_REG_READ(ah, AR_ISR_RAC);
	if (isr == 0xffffffff) {
		*masked = 0;
		return AH_FALSE;
	}

	*masked = isr & HAL_INT_COMMON;

	if (isr & AR_ISR_HIUERR)
		*masked |= HAL_INT_FATAL;
	if (isr & (AR_ISR_RXOK | AR_ISR_RXERR))
		*masked |= HAL_INT_RX;
	if (isr & (AR_ISR_TXOK | AR_ISR_TXDESC | AR_ISR_TXERR | AR_ISR_TXEOL))
		*masked |= HAL_INT_TX;
	/*
	 * Receive overrun is usually non-fatal on Oahu/Spirit.
	 * BUT on some parts rx could fail and the chip must be reset.
	 * So we force a hardware reset in all cases.
	 */
	if ((isr & AR_ISR_RXORN) && AH_PRIVATE(ah)->ah_rxornIsFatal) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: receive FIFO overrun interrupt\n", __func__);
		*masked |= HAL_INT_FATAL;
	}

	/*
	 * On fatal errors collect ISR state for debugging.
	 */
	if (*masked & HAL_INT_FATAL) {
		AH_PRIVATE(ah)->ah_fatalState[0] = isr;
		AH_PRIVATE(ah)->ah_fatalState[1] = OS_REG_READ(ah, AR_ISR_S0_S);
		AH_PRIVATE(ah)->ah_fatalState[2] = OS_REG_READ(ah, AR_ISR_S1_S);
		AH_PRIVATE(ah)->ah_fatalState[3] = OS_REG_READ(ah, AR_ISR_S2_S);
		AH_PRIVATE(ah)->ah_fatalState[4] = OS_REG_READ(ah, AR_ISR_S3_S);
		AH_PRIVATE(ah)->ah_fatalState[5] = OS_REG_READ(ah, AR_ISR_S4_S);
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: fatal error, ISR_RAC=0x%x ISR_S2_S=0x%x\n",
		    __func__, isr, AH_PRIVATE(ah)->ah_fatalState[3]);
	}
	return AH_TRUE;
}

HAL_INT
ar5211GetInterrupts(struct ath_hal *ah)
{
	return AH5211(ah)->ah_maskReg;
}

/*
 * Atomically enables NIC interrupts.  Interrupts are passed in
 * via the enumerated bitmask in ints.
 */
HAL_INT
ar5211SetInterrupts(struct ath_hal *ah, HAL_INT ints)
{
	struct ath_hal_5211 *ahp = AH5211(ah);
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
		/* XXX??? */
		(void) OS_REG_READ(ah, AR_IER);	/* flush write to HW */
	}

	mask = ints & HAL_INT_COMMON;
	if (ints & HAL_INT_TX) {
		if (ahp->ah_txOkInterruptMask)
			mask |= AR_IMR_TXOK;
		if (ahp->ah_txErrInterruptMask)
			mask |= AR_IMR_TXERR;
		if (ahp->ah_txDescInterruptMask)
			mask |= AR_IMR_TXDESC;
		if (ahp->ah_txEolInterruptMask)
			mask |= AR_IMR_TXEOL;
	}
	if (ints & HAL_INT_RX)
		mask |= AR_IMR_RXOK | AR_IMR_RXERR | AR_IMR_RXDESC;
	if (ints & HAL_INT_FATAL) {
		/*
		 * NB: ar5212Reset sets MCABT+SSERR+DPERR in AR_IMR_S2
		 *     so enabling HIUERR enables delivery.
		 */
		mask |= AR_IMR_HIUERR;
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
