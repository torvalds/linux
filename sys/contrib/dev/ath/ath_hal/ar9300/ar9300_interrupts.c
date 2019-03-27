/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"

#include "ar9300/ar9300.h"
#include "ar9300/ar9300reg.h"
#include "ar9300/ar9300phy.h"

/*
 * Checks to see if an interrupt is pending on our NIC
 *
 * Returns: TRUE    if an interrupt is pending
 *          FALSE   if not
 */
HAL_BOOL
ar9300_is_interrupt_pending(struct ath_hal *ah)
{
    u_int32_t sync_en_def = AR9300_INTR_SYNC_DEFAULT;
    u_int32_t host_isr;

    /*
     * Some platforms trigger our ISR before applying power to
     * the card, so make sure.
     */
    host_isr = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_ASYNC_CAUSE));
    if ((host_isr & AR_INTR_ASYNC_USED) && (host_isr != AR_INTR_SPURIOUS)) {
        return AH_TRUE;
    }

    host_isr = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE));
    if (AR_SREV_POSEIDON(ah)) {
        sync_en_def = AR9300_INTR_SYNC_DEF_NO_HOST1_PERR;
    }
    else if (AR_SREV_WASP(ah)) {
        sync_en_def = AR9340_INTR_SYNC_DEFAULT;
    }

    if ((host_isr & (sync_en_def | AR_INTR_SYNC_MASK_GPIO)) &&
        (host_isr != AR_INTR_SPURIOUS)) {
        return AH_TRUE;
    }

    return AH_FALSE;
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
#define MAP_ISR_S2_HAL_CST          6 /* Carrier sense timeout */
#define MAP_ISR_S2_HAL_GTT          6 /* Global transmit timeout */
#define MAP_ISR_S2_HAL_TIM          3 /* TIM */
#define MAP_ISR_S2_HAL_CABEND       0 /* CABEND */
#define MAP_ISR_S2_HAL_DTIMSYNC     7 /* DTIMSYNC */
#define MAP_ISR_S2_HAL_DTIM         7 /* DTIM */
#define MAP_ISR_S2_HAL_TSFOOR       4 /* Rx TSF out of range */
#define MAP_ISR_S2_HAL_BBPANIC      6 /* Panic watchdog IRQ from BB */
HAL_BOOL
ar9300_get_pending_interrupts(
    struct ath_hal *ah,
    HAL_INT *masked,
    HAL_INT_TYPE type,
    u_int8_t msi,
    HAL_BOOL nortc)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_BOOL  ret_val = AH_TRUE;
    u_int32_t isr = 0;
    u_int32_t mask2 = 0;
    u_int32_t sync_cause = 0;
    u_int32_t async_cause;
    u_int32_t msi_pend_addr_mask = 0;
    u_int32_t sync_en_def = AR9300_INTR_SYNC_DEFAULT;
    HAL_CAPABILITIES *p_cap = &AH_PRIVATE(ah)->ah_caps;

    *masked = 0;

    if (!nortc) {
        if (HAL_INT_MSI == type) {
            if (msi == HAL_MSIVEC_RXHP) {
                OS_REG_WRITE(ah, AR_ISR, AR_ISR_HP_RXOK);
                *masked = HAL_INT_RXHP;
                goto end;
            } else if (msi == HAL_MSIVEC_RXLP) {
                OS_REG_WRITE(ah, AR_ISR,
                    (AR_ISR_LP_RXOK | AR_ISR_RXMINTR | AR_ISR_RXINTM));
                *masked = HAL_INT_RXLP;
                goto end;
            } else if (msi == HAL_MSIVEC_TX) {
                OS_REG_WRITE(ah, AR_ISR, AR_ISR_TXOK);
                *masked = HAL_INT_TX;
                goto end;
            } else if (msi == HAL_MSIVEC_MISC) {
                /*
                 * For the misc MSI event fall through and determine the cause.
                 */
            }
        }
    }

    /* Make sure mac interrupt is pending in async interrupt cause register */
    async_cause = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_ASYNC_CAUSE));
    if (async_cause & AR_INTR_ASYNC_USED) {
        /*
         * RTC may not be on since it runs on a slow 32khz clock
         * so check its status to be sure
         */
        if (!nortc &&
            (OS_REG_READ(ah, AR_RTC_STATUS) & AR_RTC_STATUS_M) ==
             AR_RTC_STATUS_ON)
        {
            isr = OS_REG_READ(ah, AR_ISR);
        }
    }

    if (AR_SREV_POSEIDON(ah)) {
        sync_en_def = AR9300_INTR_SYNC_DEF_NO_HOST1_PERR;
    }
    else if (AR_SREV_WASP(ah)) {
        sync_en_def = AR9340_INTR_SYNC_DEFAULT;
    }

    /* Store away the async and sync cause registers */
    /* XXX Do this before the filtering done below */
#ifdef	AH_INTERRUPT_DEBUGGING
	ah->ah_intrstate[0] = OS_REG_READ(ah, AR_ISR);
	ah->ah_intrstate[1] = OS_REG_READ(ah, AR_ISR_S0);
	ah->ah_intrstate[2] = OS_REG_READ(ah, AR_ISR_S1);
	ah->ah_intrstate[3] = OS_REG_READ(ah, AR_ISR_S2);
	ah->ah_intrstate[4] = OS_REG_READ(ah, AR_ISR_S3);
	ah->ah_intrstate[5] = OS_REG_READ(ah, AR_ISR_S4);
	ah->ah_intrstate[6] = OS_REG_READ(ah, AR_ISR_S5);

	/* XXX double reading? */
	ah->ah_syncstate = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE));
#endif

    sync_cause =
        OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE)) &
        (sync_en_def | AR_INTR_SYNC_MASK_GPIO);

    if (!isr && !sync_cause && !async_cause) {
        ret_val = AH_FALSE;
        goto end;
    }

    HALDEBUG(ah, HAL_DEBUG_INTERRUPT,
        "%s: isr=0x%x, sync_cause=0x%x, async_cause=0x%x\n",
	__func__,
	isr,
	sync_cause,
	async_cause);

    if (isr) {
        if (isr & AR_ISR_BCNMISC) {
            u_int32_t isr2;
            isr2 = OS_REG_READ(ah, AR_ISR_S2);

            /* Translate ISR bits to HAL values */
            mask2 |= ((isr2 & AR_ISR_S2_TIM) >> MAP_ISR_S2_HAL_TIM);
            mask2 |= ((isr2 & AR_ISR_S2_DTIM) >> MAP_ISR_S2_HAL_DTIM);
            mask2 |= ((isr2 & AR_ISR_S2_DTIMSYNC) >> MAP_ISR_S2_HAL_DTIMSYNC);
            mask2 |= ((isr2 & AR_ISR_S2_CABEND) >> MAP_ISR_S2_HAL_CABEND);
            mask2 |= ((isr2 & AR_ISR_S2_GTT) << MAP_ISR_S2_HAL_GTT);
            mask2 |= ((isr2 & AR_ISR_S2_CST) << MAP_ISR_S2_HAL_CST);
            mask2 |= ((isr2 & AR_ISR_S2_TSFOOR) >> MAP_ISR_S2_HAL_TSFOOR);
            mask2 |= ((isr2 & AR_ISR_S2_BBPANIC) >> MAP_ISR_S2_HAL_BBPANIC);

            if (!p_cap->halIsrRacSupport) {
                /*
                 * EV61133 (missing interrupts due to ISR_RAC):
                 * If not using ISR_RAC, clear interrupts by writing to ISR_S2.
                 * This avoids a race condition where a new BCNMISC interrupt
                 * could come in between reading the ISR and clearing the
                 * interrupt via the primary ISR.  We therefore clear the
                 * interrupt via the secondary, which avoids this race.
                 */ 
                OS_REG_WRITE(ah, AR_ISR_S2, isr2);
                isr &= ~AR_ISR_BCNMISC;
            }
        }

        /* Use AR_ISR_RAC only if chip supports it. 
         * See EV61133 (missing interrupts due to ISR_RAC) 
         */
        if (p_cap->halIsrRacSupport) {
            isr = OS_REG_READ(ah, AR_ISR_RAC);
        }
        if (isr == 0xffffffff) {
            *masked = 0;
            ret_val = AH_FALSE;
            goto end;
        }
 
        *masked = isr & HAL_INT_COMMON;

        /*
         * When interrupt mitigation is switched on, we fake a normal RX or TX
         * interrupt when we received a mitigated interrupt. This way, the upper
         * layer do not need to know about feature.
         */
        if (ahp->ah_intr_mitigation_rx) {
            /* Only Rx interrupt mitigation. No Tx intr. mitigation. */
            if (isr & (AR_ISR_RXMINTR | AR_ISR_RXINTM)) {
                *masked |= HAL_INT_RXLP;
            }
        }
        if (ahp->ah_intr_mitigation_tx) {
            if (isr & (AR_ISR_TXMINTR | AR_ISR_TXINTM)) {
                *masked |= HAL_INT_TX;
            }
        }

        if (isr & (AR_ISR_LP_RXOK | AR_ISR_RXERR)) {
            *masked |= HAL_INT_RXLP;
        }
        if (isr & AR_ISR_HP_RXOK) {
            *masked |= HAL_INT_RXHP;
        }
        if (isr & (AR_ISR_TXOK | AR_ISR_TXERR | AR_ISR_TXEOL)) {
            *masked |= HAL_INT_TX;

            if (!p_cap->halIsrRacSupport) {
                u_int32_t s0, s1;
                /*
                 * EV61133 (missing interrupts due to ISR_RAC):
                 * If not using ISR_RAC, clear interrupts by writing to
                 * ISR_S0/S1.
                 * This avoids a race condition where a new interrupt
                 * could come in between reading the ISR and clearing the
                 * interrupt via the primary ISR.  We therefore clear the
                 * interrupt via the secondary, which avoids this race.
                 */ 
                s0 = OS_REG_READ(ah, AR_ISR_S0);
                OS_REG_WRITE(ah, AR_ISR_S0, s0);
                s1 = OS_REG_READ(ah, AR_ISR_S1);
                OS_REG_WRITE(ah, AR_ISR_S1, s1);

                isr &= ~(AR_ISR_TXOK | AR_ISR_TXERR | AR_ISR_TXEOL);
            }
        }

        /*
         * Do not treat receive overflows as fatal for owl.
         */
        if (isr & AR_ISR_RXORN) {
#if __PKT_SERIOUS_ERRORS__
            HALDEBUG(ah, HAL_DEBUG_INTERRUPT,
                "%s: receive FIFO overrun interrupt\n", __func__);
#endif
        }

#if 0
        /* XXX Verify if this is fixed for Osprey */
        if (!p_cap->halAutoSleepSupport) {
            u_int32_t isr5 = OS_REG_READ(ah, AR_ISR_S5_S);
            if (isr5 & AR_ISR_S5_TIM_TIMER) {
                *masked |= HAL_INT_TIM_TIMER;
            }
        }
#endif
        if (isr & AR_ISR_GENTMR) {
            u_int32_t s5;

            if (p_cap->halIsrRacSupport) {
                /* Use secondary shadow registers if using ISR_RAC */
                s5 = OS_REG_READ(ah, AR_ISR_S5_S);
            } else {
                s5 = OS_REG_READ(ah, AR_ISR_S5);
            }
            if (isr & AR_ISR_GENTMR) {

                HALDEBUG(ah, HAL_DEBUG_INTERRUPT,
                    "%s: GENTIMER, ISR_RAC=0x%x ISR_S2_S=0x%x\n", __func__,
                    isr, s5);
                ahp->ah_intr_gen_timer_trigger =
                    MS(s5, AR_ISR_S5_GENTIMER_TRIG);
                ahp->ah_intr_gen_timer_thresh =
                    MS(s5, AR_ISR_S5_GENTIMER_THRESH);
                if (ahp->ah_intr_gen_timer_trigger) {
                    *masked |= HAL_INT_GENTIMER;
                }
            }
            if (!p_cap->halIsrRacSupport) {
                /*
                 * EV61133 (missing interrupts due to ISR_RAC):
                 * If not using ISR_RAC, clear interrupts by writing to ISR_S5.
                 * This avoids a race condition where a new interrupt
                 * could come in between reading the ISR and clearing the
                 * interrupt via the primary ISR.  We therefore clear the
                 * interrupt via the secondary, which avoids this race.
                 */ 
                OS_REG_WRITE(ah, AR_ISR_S5, s5);
                isr &= ~AR_ISR_GENTMR;
            }
        }

        *masked |= mask2;

        if (!p_cap->halIsrRacSupport) {
            /*
             * EV61133 (missing interrupts due to ISR_RAC):
             * If not using ISR_RAC, clear the interrupts we've read by
             * writing back ones in these locations to the primary ISR
             * (except for interrupts that have a secondary isr register -
             * see above).
             */ 
            OS_REG_WRITE(ah, AR_ISR, isr);

            /* Flush prior write */
            (void) OS_REG_READ(ah, AR_ISR);
        }

#ifdef AH_SUPPORT_AR9300
        if (*masked & HAL_INT_BBPANIC) {
            ar9300_handle_bb_panic(ah);
        }
#endif
    }

    if (async_cause) {
        if (nortc) {
            OS_REG_WRITE(ah,
                AR_HOSTIF_REG(ah, AR_INTR_ASYNC_CAUSE_CLR), async_cause);
            /* Flush prior write */
            (void) OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_ASYNC_CAUSE_CLR));
        } else {
#ifdef ATH_GPIO_USE_ASYNC_CAUSE
            if (async_cause & AR_INTR_ASYNC_CAUSE_GPIO) {
                ahp->ah_gpio_cause = (async_cause & AR_INTR_ASYNC_CAUSE_GPIO) >>
                                     AR_INTR_ASYNC_ENABLE_GPIO_S;
                *masked |= HAL_INT_GPIO;
            }
#endif
        }

#if ATH_SUPPORT_MCI
        if ((async_cause & AR_INTR_ASYNC_CAUSE_MCI) &&
            p_cap->halMciSupport)
        {
            u_int32_t int_raw, int_rx_msg;

            int_rx_msg = OS_REG_READ(ah, AR_MCI_INTERRUPT_RX_MSG_RAW);
            int_raw = OS_REG_READ(ah, AR_MCI_INTERRUPT_RAW);

            if ((int_raw == 0xdeadbeef) || (int_rx_msg == 0xdeadbeef))
            {
                HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                    "(MCI) Get 0xdeadbeef during MCI int processing"
                    "new int_raw=0x%08x, new rx_msg_raw=0x%08x, "
                    "int_raw=0x%08x, rx_msg_raw=0x%08x\n",
                    int_raw, int_rx_msg, ahp->ah_mci_int_raw, 
                    ahp->ah_mci_int_rx_msg);
            }
            else {
                if (ahp->ah_mci_int_raw || ahp->ah_mci_int_rx_msg) {
                    ahp->ah_mci_int_rx_msg |= int_rx_msg;
                    ahp->ah_mci_int_raw |= int_raw;
                }
                else {
                    ahp->ah_mci_int_rx_msg = int_rx_msg;
                    ahp->ah_mci_int_raw = int_raw;
                }

                *masked |= HAL_INT_MCI;
                ahp->ah_mci_rx_status = OS_REG_READ(ah, AR_MCI_RX_STATUS);
                if (int_rx_msg & AR_MCI_INTERRUPT_RX_MSG_CONT_INFO) {
                    ahp->ah_mci_cont_status = 
                                    OS_REG_READ(ah, AR_MCI_CONT_STATUS);
                    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                        "(MCI) cont_status=0x%08x\n", ahp->ah_mci_cont_status);
                }
                OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RX_MSG_RAW, 
                    int_rx_msg);
                OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RAW, int_raw);

                HALDEBUG(ah, HAL_DEBUG_INTERRUPT, "%s:AR_INTR_SYNC_MCI\n", __func__);
            }
        }
#endif
    }

    if (sync_cause) {
        int host1_fatal, host1_perr, radm_cpl_timeout, local_timeout;

        host1_fatal = AR_SREV_WASP(ah) ? 
            AR9340_INTR_SYNC_HOST1_FATAL : AR9300_INTR_SYNC_HOST1_FATAL;
        host1_perr = AR_SREV_WASP(ah) ?
            AR9340_INTR_SYNC_HOST1_PERR : AR9300_INTR_SYNC_HOST1_PERR;
        radm_cpl_timeout = AR_SREV_WASP(ah) ? 
            0x0 : AR9300_INTR_SYNC_RADM_CPL_TIMEOUT;
        local_timeout = AR_SREV_WASP(ah) ?
            AR9340_INTR_SYNC_LOCAL_TIMEOUT : AR9300_INTR_SYNC_LOCAL_TIMEOUT;

        if (sync_cause & host1_fatal) {
#if __PKT_SERIOUS_ERRORS__
            HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
                "%s: received PCI FATAL interrupt\n", __func__);
#endif
           *masked |= HAL_INT_FATAL; /* Set FATAL INT flag here;*/
        }
        if (sync_cause & host1_perr) {
#if __PKT_SERIOUS_ERRORS__
            HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
                "%s: received PCI PERR interrupt\n", __func__);
#endif
        }

        if (sync_cause & radm_cpl_timeout) {
            HALDEBUG(ah, HAL_DEBUG_INTERRUPT,
                "%s: AR_INTR_SYNC_RADM_CPL_TIMEOUT\n",
                __func__);

            OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_RC), AR_RC_HOSTIF);
            OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_RC), 0);
            *masked |= HAL_INT_FATAL;
        }
        if (sync_cause & local_timeout) {
            HALDEBUG(ah, HAL_DEBUG_INTERRUPT,
                "%s: AR_INTR_SYNC_LOCAL_TIMEOUT\n",
                __func__);
        }

#ifndef ATH_GPIO_USE_ASYNC_CAUSE
        if (sync_cause & AR_INTR_SYNC_MASK_GPIO) {
            ahp->ah_gpio_cause = (sync_cause & AR_INTR_SYNC_MASK_GPIO) >>
                                 AR_INTR_SYNC_ENABLE_GPIO_S;
            *masked |= HAL_INT_GPIO;
            HALDEBUG(ah, HAL_DEBUG_INTERRUPT,
                "%s: AR_INTR_SYNC_GPIO\n", __func__);
        }
#endif

        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE_CLR), sync_cause);
        /* Flush prior write */
        (void) OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE_CLR));
    }

end:
    if (HAL_INT_MSI == type) {
        /*
         * WAR for Bug EV#75887
         * In normal case, SW read HOST_INTF_PCIE_MSI (0x40A4) and write
         * into ah_msi_reg.  Then use value of ah_msi_reg to set bit#25
         * when want to enable HW write the cfg_msi_pending.
         * Sometimes, driver get MSI interrupt before read 0x40a4 and
         * ah_msi_reg is initialization value (0x0).
         * We don't know why "MSI interrupt earlier than driver read" now...
         */
        if (!ahp->ah_msi_reg) {
            ahp->ah_msi_reg = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_PCIE_MSI));
        }
        if (AR_SREV_POSEIDON(ah)) {
            msi_pend_addr_mask = AR_PCIE_MSI_HW_INT_PENDING_ADDR_MSI_64;
        } else {
            msi_pend_addr_mask = AR_PCIE_MSI_HW_INT_PENDING_ADDR;
        }
        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_PCIE_MSI),
            ((ahp->ah_msi_reg | AR_PCIE_MSI_ENABLE) & msi_pend_addr_mask));

    }

    return ret_val;
}

HAL_INT
ar9300_get_interrupts(struct ath_hal *ah)
{
    return AH9300(ah)->ah_mask_reg;
}

/*
 * Atomically enables NIC interrupts.  Interrupts are passed in
 * via the enumerated bitmask in ints.
 */
HAL_INT
ar9300_set_interrupts(struct ath_hal *ah, HAL_INT ints, HAL_BOOL nortc)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t omask = ahp->ah_mask_reg;
    u_int32_t mask, mask2, msi_mask = 0;
    u_int32_t msi_pend_addr_mask = 0;
    u_int32_t sync_en_def = AR9300_INTR_SYNC_DEFAULT;
    HAL_CAPABILITIES *p_cap = &AH_PRIVATE(ah)->ah_caps;

    HALDEBUG(ah, HAL_DEBUG_INTERRUPT,
        "%s: 0x%x => 0x%x\n", __func__, omask, ints);

    if (omask & HAL_INT_GLOBAL) {
        HALDEBUG(ah, HAL_DEBUG_INTERRUPT, "%s: disable IER\n", __func__);

        if (ah->ah_config.ath_hal_enable_msi) {
            OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_PRIO_ASYNC_ENABLE), 0);
            /* flush write to HW */
            (void)OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_PRIO_ASYNC_ENABLE));
        }

        if (!nortc) {
            OS_REG_WRITE(ah, AR_IER, AR_IER_DISABLE);
            (void) OS_REG_READ(ah, AR_IER);   /* flush write to HW */
        }

        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_ENABLE), 0);
        /* flush write to HW */
        (void) OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_ENABLE));
        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_ASYNC_ENABLE), 0);
        /* flush write to HW */
        (void) OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_ASYNC_ENABLE));
    }

    if (!nortc) {
        /* reference count for global IER */
        if (ints & HAL_INT_GLOBAL) {
#ifdef AH_DEBUG
            HALDEBUG(ah, HAL_DEBUG_INTERRUPT,
                "%s: Request HAL_INT_GLOBAL ENABLED\n", __func__);
#if 0
            if (OS_ATOMIC_READ(&ahp->ah_ier_ref_count) == 0) {
                HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
                    "%s: WARNING: ah_ier_ref_count is 0 "
                    "and attempting to enable IER\n",
                    __func__);
            }
#endif
#endif
#if 0
            if (OS_ATOMIC_READ(&ahp->ah_ier_ref_count) > 0) {
                OS_ATOMIC_DEC(&ahp->ah_ier_ref_count);
            } 
#endif
        } else {
            HALDEBUG(ah, HAL_DEBUG_INTERRUPT,
                "%s: Request HAL_INT_GLOBAL DISABLED\n", __func__);
            OS_ATOMIC_INC(&ahp->ah_ier_ref_count);
        }
        HALDEBUG(ah, HAL_DEBUG_INTERRUPT,
            "%s: ah_ier_ref_count = %d\n", __func__, ahp->ah_ier_ref_count);

        mask = ints & HAL_INT_COMMON;
        mask2 = 0;
        msi_mask = 0;

        if (ints & HAL_INT_TX) {
            if (ahp->ah_intr_mitigation_tx) {
                mask |= AR_IMR_TXMINTR | AR_IMR_TXINTM;
            } else if (ahp->ah_tx_ok_interrupt_mask) {
                mask |= AR_IMR_TXOK;
            }
            msi_mask |= AR_INTR_PRIO_TX;
            if (ahp->ah_tx_err_interrupt_mask) {
                mask |= AR_IMR_TXERR;
            }
            if (ahp->ah_tx_eol_interrupt_mask) {
                mask |= AR_IMR_TXEOL;
            }
        }
        if (ints & HAL_INT_RX) {
            mask |= AR_IMR_RXERR | AR_IMR_RXOK_HP;
            if (ahp->ah_intr_mitigation_rx) {
                mask &= ~(AR_IMR_RXOK_LP);
                mask |=  AR_IMR_RXMINTR | AR_IMR_RXINTM;
            } else {
                mask |= AR_IMR_RXOK_LP;
            }
            msi_mask |= AR_INTR_PRIO_RXLP | AR_INTR_PRIO_RXHP;
            if (! p_cap->halAutoSleepSupport) {
                mask |= AR_IMR_GENTMR;
            }
        }

        if (ints & (HAL_INT_BMISC)) {
            mask |= AR_IMR_BCNMISC;
            if (ints & HAL_INT_TIM) {
                mask2 |= AR_IMR_S2_TIM;
            }
            if (ints & HAL_INT_DTIM) {
                mask2 |= AR_IMR_S2_DTIM;
            }
            if (ints & HAL_INT_DTIMSYNC) {
                mask2 |= AR_IMR_S2_DTIMSYNC;
            }
            if (ints & HAL_INT_CABEND) {
                mask2 |= (AR_IMR_S2_CABEND);
            }
            if (ints & HAL_INT_TSFOOR) {
                mask2 |= AR_IMR_S2_TSFOOR;
            }
        }

        if (ints & (HAL_INT_GTT | HAL_INT_CST)) {
            mask |= AR_IMR_BCNMISC;
            if (ints & HAL_INT_GTT) {
                mask2 |= AR_IMR_S2_GTT;
            }
            if (ints & HAL_INT_CST) {
                mask2 |= AR_IMR_S2_CST;
            }
        }

        if (ints & HAL_INT_BBPANIC) {
            /* EV92527 - MAC secondary interrupt must enable AR_IMR_BCNMISC */
            mask |= AR_IMR_BCNMISC;
            mask2 |= AR_IMR_S2_BBPANIC;
        }

        if (ints & HAL_INT_GENTIMER) {
            HALDEBUG(ah, HAL_DEBUG_INTERRUPT,
                "%s: enabling gen timer\n", __func__);
            mask |= AR_IMR_GENTMR;
        }

        /* Write the new IMR and store off our SW copy. */
        HALDEBUG(ah, HAL_DEBUG_INTERRUPT, "%s: new IMR 0x%x\n", __func__, mask);
        OS_REG_WRITE(ah, AR_IMR, mask);
        ahp->ah_mask2Reg &= ~(AR_IMR_S2_TIM |
                        AR_IMR_S2_DTIM |
                        AR_IMR_S2_DTIMSYNC |
                        AR_IMR_S2_CABEND |
                        AR_IMR_S2_CABTO  |
                        AR_IMR_S2_TSFOOR |
                        AR_IMR_S2_GTT |
                        AR_IMR_S2_CST |
                        AR_IMR_S2_BBPANIC);
        ahp->ah_mask2Reg |= mask2;
        OS_REG_WRITE(ah, AR_IMR_S2, ahp->ah_mask2Reg );
        ahp->ah_mask_reg = ints;

        if (! p_cap->halAutoSleepSupport) {
            if (ints & HAL_INT_TIM_TIMER) {
                OS_REG_SET_BIT(ah, AR_IMR_S5, AR_IMR_S5_TIM_TIMER);
            }
            else {
                OS_REG_CLR_BIT(ah, AR_IMR_S5, AR_IMR_S5_TIM_TIMER);
            }
        }
    }

    /* Re-enable interrupts if they were enabled before. */
#if HAL_INTR_REFCOUNT_DISABLE
    if ((ints & HAL_INT_GLOBAL)) {
#else
    if ((ints & HAL_INT_GLOBAL) && (OS_ATOMIC_READ(&ahp->ah_ier_ref_count) == 0)) {
#endif
        HALDEBUG(ah, HAL_DEBUG_INTERRUPT, "%s: enable IER\n", __func__);
        
        if (!nortc) {
            OS_REG_WRITE(ah, AR_IER, AR_IER_ENABLE);
        }

        mask = AR_INTR_MAC_IRQ;
#ifdef ATH_GPIO_USE_ASYNC_CAUSE
        if (ints & HAL_INT_GPIO) {
            if (ahp->ah_gpio_mask) {
                mask |= SM(ahp->ah_gpio_mask, AR_INTR_ASYNC_MASK_GPIO);
            }
        }
#endif

#if ATH_SUPPORT_MCI
        if (ints & HAL_INT_MCI) {
            mask |= AR_INTR_ASYNC_MASK_MCI;
        }
#endif

        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_ASYNC_ENABLE), mask);
        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_ASYNC_MASK), mask);

        if (ah->ah_config.ath_hal_enable_msi) {
            OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_PRIO_ASYNC_ENABLE),
                msi_mask);
            OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_PRIO_ASYNC_MASK),
                msi_mask);
            if (AR_SREV_POSEIDON(ah)) {
                msi_pend_addr_mask = AR_PCIE_MSI_HW_INT_PENDING_ADDR_MSI_64;
            } else {
                msi_pend_addr_mask = AR_PCIE_MSI_HW_INT_PENDING_ADDR;
            }
            OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_PCIE_MSI),
                ((ahp->ah_msi_reg | AR_PCIE_MSI_ENABLE) & msi_pend_addr_mask));
        }

        /*
         * debug - enable to see all synchronous interrupts status
         * Enable synchronous GPIO interrupts as well, since some async
         * GPIO interrupts don't wake the chip up.
         */
        mask = 0;
#ifndef ATH_GPIO_USE_ASYNC_CAUSE
        if (ints & HAL_INT_GPIO) {
            mask |= SM(ahp->ah_gpio_mask, AR_INTR_SYNC_MASK_GPIO);
        }
#endif
        if (AR_SREV_POSEIDON(ah)) {
            sync_en_def = AR9300_INTR_SYNC_DEF_NO_HOST1_PERR;
        }
        else if (AR_SREV_WASP(ah)) {
            sync_en_def = AR9340_INTR_SYNC_DEFAULT;
        }

        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_ENABLE),
            (sync_en_def | mask));
        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_MASK),
            (sync_en_def | mask));

        HALDEBUG(ah,  HAL_DEBUG_INTERRUPT,
            "AR_IMR 0x%x IER 0x%x\n",
            OS_REG_READ(ah, AR_IMR), OS_REG_READ(ah, AR_IER));
    }

    return omask;
}

void
ar9300_set_intr_mitigation_timer(
    struct ath_hal* ah,
    HAL_INT_MITIGATION reg,
    u_int32_t value)
{
#ifdef AR5416_INT_MITIGATION
    switch (reg) {
    case HAL_INT_THRESHOLD:
        OS_REG_WRITE(ah, AR_MIRT, 0);
        break;
    case HAL_INT_RX_LASTPKT:
        OS_REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_LAST, value);
        break;
    case HAL_INT_RX_FIRSTPKT:
        OS_REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_FIRST, value);
        break;
    case HAL_INT_TX_LASTPKT:
        OS_REG_RMW_FIELD(ah, AR_TIMT, AR_TIMT_LAST, value);
        break;
    case HAL_INT_TX_FIRSTPKT:
        OS_REG_RMW_FIELD(ah, AR_TIMT, AR_TIMT_FIRST, value);
        break;
    default:
        break;
    }
#endif
}

u_int32_t
ar9300_get_intr_mitigation_timer(struct ath_hal* ah, HAL_INT_MITIGATION reg)
{
    u_int32_t val = 0;
#ifdef AR5416_INT_MITIGATION
    switch (reg) {
    case HAL_INT_THRESHOLD:
        val = OS_REG_READ(ah, AR_MIRT);
        break;
    case HAL_INT_RX_LASTPKT:
        val = OS_REG_READ(ah, AR_RIMT) & 0xFFFF;
        break;
    case HAL_INT_RX_FIRSTPKT:
        val = OS_REG_READ(ah, AR_RIMT) >> 16;
        break;
    case HAL_INT_TX_LASTPKT:
        val = OS_REG_READ(ah, AR_TIMT) & 0xFFFF;
        break;
    case HAL_INT_TX_FIRSTPKT:
        val = OS_REG_READ(ah, AR_TIMT) >> 16;
        break;
    default:
        break;
    }
#endif
    return val;
}
