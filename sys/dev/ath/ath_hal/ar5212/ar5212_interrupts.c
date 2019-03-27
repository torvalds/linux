/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
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

#include "ar5212/ar5212.h"
#include "ar5212/ar5212reg.h"
#include "ar5212/ar5212phy.h"


/*
 * Checks to see if an interrupt is pending on our NIC
 *
 * Returns: TRUE    if an interrupt is pending
 *          FALSE   if not
 */
HAL_BOOL
ar5212IsInterruptPending(struct ath_hal *ah)
{
	/* 
	 * Some platforms trigger our ISR before applying power to
	 * the card, so make sure the INTPEND is really 1, not 0xffffffff.
	 */
	return (OS_REG_READ(ah, AR_INTPEND) == AR_INTPEND_TRUE);
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
ar5212GetPendingInterrupts(struct ath_hal *ah, HAL_INT *masked)
{
	uint32_t isr, isr0, isr1;
	uint32_t mask2;
	struct ath_hal_5212 *ahp = AH5212(ah);

	isr = OS_REG_READ(ah, AR_ISR);
	mask2 = 0;
	if (isr & AR_ISR_BCNMISC) {
		uint32_t isr2 = OS_REG_READ(ah, AR_ISR_S2);
		if (isr2 & AR_ISR_S2_TIM)
			mask2 |= HAL_INT_TIM;
		if (isr2 & AR_ISR_S2_DTIM)
			mask2 |= HAL_INT_DTIM;
		if (isr2 & AR_ISR_S2_DTIMSYNC)
			mask2 |= HAL_INT_DTIMSYNC;
		if (isr2 & AR_ISR_S2_CABEND)
			mask2 |= HAL_INT_CABEND;
		if (isr2 & AR_ISR_S2_TBTT)
			mask2 |= HAL_INT_TBTT;
	}
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
	if (isr & (AR_ISR_TXOK | AR_ISR_TXDESC | AR_ISR_TXERR | AR_ISR_TXEOL)) {
		*masked |= HAL_INT_TX;
		isr0 = OS_REG_READ(ah, AR_ISR_S0_S);
		ahp->ah_intrTxqs |= MS(isr0, AR_ISR_S0_QCU_TXOK);
		ahp->ah_intrTxqs |= MS(isr0, AR_ISR_S0_QCU_TXDESC);
		isr1 = OS_REG_READ(ah, AR_ISR_S1_S);
		ahp->ah_intrTxqs |= MS(isr1, AR_ISR_S1_QCU_TXERR);
		ahp->ah_intrTxqs |= MS(isr1, AR_ISR_S1_QCU_TXEOL);
	}

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
	*masked |= mask2;

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
ar5212GetInterrupts(struct ath_hal *ah)
{
	return AH5212(ah)->ah_maskReg;
}

/*
 * Atomically enables NIC interrupts.  Interrupts are passed in
 * via the enumerated bitmask in ints.
 */
HAL_INT
ar5212SetInterrupts(struct ath_hal *ah, HAL_INT ints)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	uint32_t omask = ahp->ah_maskReg;
	uint32_t mask, mask2;

	HALDEBUG(ah, HAL_DEBUG_INTERRUPT, "%s: 0x%x => 0x%x\n",
	    __func__, omask, ints);

	if (omask & HAL_INT_GLOBAL) {
		HALDEBUG(ah, HAL_DEBUG_INTERRUPT, "%s: disable IER\n", __func__);
		OS_REG_WRITE(ah, AR_IER, AR_IER_DISABLE);
		(void) OS_REG_READ(ah, AR_IER);   /* flush write to HW */
	}

	mask = ints & HAL_INT_COMMON;
	mask2 = 0;
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
	if (ints & (HAL_INT_BMISC)) {
		mask |= AR_IMR_BCNMISC;
		if (ints & HAL_INT_TIM)
			mask2 |= AR_IMR_S2_TIM;
		if (ints & HAL_INT_DTIM)
			mask2 |= AR_IMR_S2_DTIM;
		if (ints & HAL_INT_DTIMSYNC)
			mask2 |= AR_IMR_S2_DTIMSYNC;
		if (ints & HAL_INT_CABEND)
			mask2 |= AR_IMR_S2_CABEND;
		if (ints & HAL_INT_TBTT)
			mask2 |= AR_IMR_S2_TBTT;
	}
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
	OS_REG_WRITE(ah, AR_IMR_S2,
	    (OS_REG_READ(ah, AR_IMR_S2) &~ AR_IMR_SR2_BCNMISC) | mask2);
	ahp->ah_maskReg = ints;

	/* Re-enable interrupts if they were enabled before. */
	if (ints & HAL_INT_GLOBAL) {
		HALDEBUG(ah, HAL_DEBUG_INTERRUPT, "%s: enable IER\n", __func__);
		OS_REG_WRITE(ah, AR_IER, AR_IER_ENABLE);
	}
	return omask;
}
