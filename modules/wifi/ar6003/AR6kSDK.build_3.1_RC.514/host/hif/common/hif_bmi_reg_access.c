//------------------------------------------------------------------------------
// <copyright file="hif_bmi_diag_access.c" company="Atheros">
//    Copyright (c) 2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// common BMI access handling for register-based HIFs
// This module implements BMI message exchanges on behalf of the BMI module for
// HIFs that are based on a register access model
//
// Author(s): ="Atheros"
//==============================================================================

#include "a_config.h"
#include "athdefs.h"
#include "a_types.h"
#include "a_osapi.h"
#define ATH_MODULE_NAME bmi
#include "a_debug.h"
#define ATH_DEBUG_BMI  ATH_DEBUG_MAKE_MODULE_MASK(0)
#include "hif.h"
#include "bmi.h"
#include "htc_api.h"
#include "target_reg_table.h"
#include "host_reg_table.h"

#define BMI_COMMUNICATION_TIMEOUT       100000

static A_BOOL pendingEventsFuncCheck = FALSE;
static A_UINT32 commandCredits = 0;
static A_UINT32 *pBMICmdCredits = &commandCredits;

/* BMI Access routines */
static A_STATUS
bmiBufferSend(HIF_DEVICE *device,
              A_UCHAR *buffer,
              A_UINT32 length)
{
    A_STATUS status;
    A_UINT32 timeout;
    A_UINT32 address;
    A_UINT32 mboxAddress[HTC_MAILBOX_NUM_MAX];

    HIFConfigureDevice(device, HIF_DEVICE_GET_MBOX_ADDR,
                       &mboxAddress[0], sizeof(mboxAddress));

    *pBMICmdCredits = 0;
    timeout = BMI_COMMUNICATION_TIMEOUT;

    while(timeout-- && !(*pBMICmdCredits)) {
        /* Read the counter register to get the command credits */
        address = COUNT_DEC_ADDRESS + (HTC_MAILBOX_NUM_MAX + ENDPOINT1) * 4;
        /* hit the credit counter with a 4-byte access, the first byte read will hit the counter and cause
         * a decrement, while the remaining 3 bytes has no effect.  The rationale behind this is to
         * make all HIF accesses 4-byte aligned */
        status = HIFReadWrite(device, address, (A_UINT8 *)pBMICmdCredits, 4,
            HIF_RD_SYNC_BYTE_INC, NULL);
        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to decrement the command credit count register\n"));
            return A_ERROR;
        }
        /* the counter is only 8=bits, ignore anything in the upper 3 bytes */
        (*pBMICmdCredits) &= 0xFF;
    }

    if (*pBMICmdCredits) {
        address = mboxAddress[ENDPOINT1];
        status = HIFReadWrite(device, address, buffer, length,
            HIF_WR_SYNC_BYTE_INC, NULL);
        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to send the BMI data to the device\n"));
            return A_ERROR;
        }
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("BMI Communication timeout - bmiBufferSend\n"));
        return A_ERROR;
    }

    return status;
}

static A_STATUS
bmiBufferReceive(HIF_DEVICE *device,
                 A_UCHAR *buffer,
                 A_UINT32 length,
                 A_BOOL want_timeout)
{
    A_STATUS status;
    A_UINT32 address;
    A_UINT32 mboxAddress[HTC_MAILBOX_NUM_MAX];
    HIF_PENDING_EVENTS_INFO     hifPendingEvents;
    static HIF_PENDING_EVENTS_FUNC getPendingEventsFunc = NULL;

    if (!pendingEventsFuncCheck) {
            /* see if the HIF layer implements an alternative function to get pending events
             * do this only once! */
        HIFConfigureDevice(device,
                           HIF_DEVICE_GET_PENDING_EVENTS_FUNC,
                           &getPendingEventsFunc,
                           sizeof(HIF_PENDING_EVENTS_FUNC));
        pendingEventsFuncCheck = TRUE;
    }

    HIFConfigureDevice(device, HIF_DEVICE_GET_MBOX_ADDR,
                       &mboxAddress[0], sizeof(mboxAddress));

    /*
     * During normal bootup, small reads may be required.
     * Rather than issue an HIF Read and then wait as the Target
     * adds successive bytes to the FIFO, we wait here until
     * we know that response data is available.
     *
     * This allows us to cleanly timeout on an unexpected
     * Target failure rather than risk problems at the HIF level.  In
     * particular, this avoids SDIO timeouts and possibly garbage
     * data on some host controllers.  And on an interconnect
     * such as Compact Flash (as well as some SDIO masters) which
     * does not provide any indication on data timeout, it avoids
     * a potential hang or garbage response.
     *
     * Synchronization is more difficult for reads larger than the
     * size of the MBOX FIFO (128B), because the Target is unable
     * to push the 129th byte of data until AFTER the Host posts an
     * HIF Read and removes some FIFO data.  So for large reads the
     * Host proceeds to post an HIF Read BEFORE all the data is
     * actually available to read.  Fortunately, large BMI reads do
     * not occur in practice -- they're supported for debug/development.
     *
     * So Host/Target BMI synchronization is divided into these cases:
     *  CASE 1: length < 4
     *        Should not happen
     *
     *  CASE 2: 4 <= length <= 128
     *        Wait for first 4 bytes to be in FIFO
     *        If CONSERVATIVE_BMI_READ is enabled, also wait for
     *        a BMI command credit, which indicates that the ENTIRE
     *        response is available in the the FIFO
     *
     *  CASE 3: length > 128
     *        Wait for the first 4 bytes to be in FIFO
     *
     * For most uses, a small timeout should be sufficient and we will
     * usually see a response quickly; but there may be some unusual
     * (debug) cases of BMI_EXECUTE where we want an larger timeout.
     * For now, we use an unbounded busy loop while waiting for
     * BMI_EXECUTE.
     *
     * If BMI_EXECUTE ever needs to support longer-latency execution,
     * especially in production, this code needs to be enhanced to sleep
     * and yield.  Also note that BMI_COMMUNICATION_TIMEOUT is currently
     * a function of Host processor speed.
     */
    if (length >= 4) { /* NB: Currently, always true */
        /*
         * NB: word_available is declared static for esoteric reasons
         * having to do with protection on some OSes.
         */
        static A_UINT32 word_available;
        A_UINT32 timeout;

        word_available = 0;
        timeout = BMI_COMMUNICATION_TIMEOUT;
        while((!want_timeout || timeout--) && !word_available) {

            if (getPendingEventsFunc != NULL) {
                status = getPendingEventsFunc(device,
                                              &hifPendingEvents,
                                              NULL);
                if (status != A_OK) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMI: Failed to get pending events \n"));
                    break;
                }

                if (hifPendingEvents.AvailableRecvBytes >= sizeof(A_UINT32)) {
                    word_available = 1;
                }
                continue;
            }

            status = HIFReadWrite(device, RX_LOOKAHEAD_VALID_ADDRESS, (A_UINT8 *)&word_available,
                sizeof(word_available), HIF_RD_SYNC_BYTE_INC, NULL);
            if (status != A_OK) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to read RX_LOOKAHEAD_VALID register\n"));
                return A_ERROR;
            }
            /* We did a 4-byte read to the same register; all we really want is one bit */
            word_available &= (1 << ENDPOINT1);
        }

        if (!word_available) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("BMI Communication timeout - bmiBufferReceive FIFO empty\n"));
            return A_ERROR;
        }
    }

#define CONSERVATIVE_BMI_READ 0
#if CONSERVATIVE_BMI_READ
    /*
     * This is an extra-conservative CREDIT check.  It guarantees
     * that ALL data is available in the FIFO before we start to
     * read from the interconnect.
     *
     * This credit check is useless when firmware chooses to
     * allow multiple outstanding BMI Command Credits, since the next
     * credit will already be present.  To restrict the Target to one
     * BMI Command Credit, see HI_OPTION_BMI_CRED_LIMIT.
     *
     * And for large reads (when HI_OPTION_BMI_CRED_LIMIT is set)
     * we cannot wait for the next credit because the Target's FIFO
     * will not hold the entire response.  So we need the Host to
     * start to empty the FIFO sooner.  (And again, large reads are
     * not used in practice; they are for debug/development only.)
     *
     * For a more conservative Host implementation (which would be
     * safer for a Compact Flash interconnect):
     *   Set CONSERVATIVE_BMI_READ (above) to 1
     *   Set HI_OPTION_BMI_CRED_LIMIT and
     *   reduce BMI_DATASZ_MAX to 32 or 64
     */
    if ((length > 4) && (length < 128)) { /* check against MBOX FIFO size */
        A_UINT32 timeout;

        *pBMICmdCredits = 0;
        timeout = BMI_COMMUNICATION_TIMEOUT;
        while((!want_timeout || timeout--) && !(*pBMICmdCredits) {
            /* Read the counter register to get the command credits */
            address = COUNT_ADDRESS + (HTC_MAILBOX_NUM_MAX + ENDPOINT1) * 1;
            /* read the counter using a 4-byte read.  Since the counter is NOT auto-decrementing,
             * we can read this counter multiple times using a non-incrementing address mode.
             * The rationale here is to make all HIF accesses a multiple of 4 bytes */
            status = HIFReadWrite(device, address, (A_UINT8 *)pBMICmdCredits, sizeof(*pBMICmdCredits),
                HIF_RD_SYNC_BYTE_FIX, NULL);
            if (status != A_OK) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to read the command credit count register\n"));
                return A_ERROR;
            }
                /* we did a 4-byte read to the same count register so mask off upper bytes */
            (*pBMICmdCredits) &= 0xFF;
        }

        if (!(*pBMICmdCredits)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("BMI Communication timeout- bmiBufferReceive no credit\n"));
            return A_ERROR;
        }
    }
#endif

    address = mboxAddress[ENDPOINT1];
    status = HIFReadWrite(device, address, buffer, length, HIF_RD_SYNC_BYTE_INC, NULL);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to read the BMI data from the device\n"));
        return A_ERROR;
    }

    return A_OK;
}


A_STATUS HIFRegBasedGetTargetInfo(HIF_DEVICE *device, struct bmi_target_info *targ_info)
{
    A_STATUS status;
    A_UINT32 cid;

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI Get Target Info: Enter (device: 0x%p)\n", device));
    cid = BMI_GET_TARGET_INFO;

    status = bmiBufferSend(device, (A_UCHAR *)&cid, sizeof(cid));
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to write to the device\n"));
        return A_ERROR;
    }

    status = bmiBufferReceive(device, (A_UCHAR *)&targ_info->target_ver,
                                                sizeof(targ_info->target_ver), TRUE);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to read Target Version from the device\n"));
        return A_ERROR;
    }

    if (targ_info->target_ver == TARGET_VERSION_SENTINAL) {
        /* Determine how many bytes are in the Target's targ_info */
        status = bmiBufferReceive(device, (A_UCHAR *)&targ_info->target_info_byte_count,
                                            sizeof(targ_info->target_info_byte_count), TRUE);
        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to read Target Info Byte Count from the device\n"));
            return A_ERROR;
        }

        /*
         * The Target's targ_info doesn't match the Host's targ_info.
         * We need to do some backwards compatibility work to make this OK.
         */
        A_ASSERT(targ_info->target_info_byte_count == sizeof(*targ_info));

        /* Read the remainder of the targ_info */
        status = bmiBufferReceive(device,
                        ((A_UCHAR *)targ_info)+sizeof(targ_info->target_info_byte_count),
                        sizeof(*targ_info)-sizeof(targ_info->target_info_byte_count), TRUE);
        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to read Target Info (%d bytes) from the device\n",
                                            targ_info->target_info_byte_count));
            return A_ERROR;
        }
    } else {
        /*
         * Target must be an AR6001 whose firmware does not
         * support BMI_GET_TARGET_INFO.  Construct the data
         * that it would have sent.
         */
        targ_info->target_info_byte_count=sizeof(*targ_info);
        targ_info->target_type=TARGET_TYPE_AR6001;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI Get Target Info: Exit (ver: 0x%x type: 0x%x)\n",
                                    targ_info->target_ver, targ_info->target_type));

    return A_OK;
}

A_STATUS HIFExchangeBMIMsg(HIF_DEVICE *device,
                           A_UINT8    *pSendMessage,
                           A_UINT32   Length,
                           A_UINT8    *pResponseMessage,
                           A_UINT32   *pResponseLength,
                           A_UINT32   TimeoutMS)
{
    A_STATUS status = A_OK;

    do {

        status = bmiBufferSend(device, pSendMessage, Length);
        if (A_FAILED(status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("BMI : Unable to Send Message to device \n"));
            break;
        }

        if (pResponseMessage != NULL) {
            status = bmiBufferReceive(device, pResponseMessage, *pResponseLength, TimeoutMS ? TRUE : FALSE);
            if (A_FAILED(status)) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("BMI : Unable to read response from device \n"));
                break;
            }
        }

    } while (FALSE);

    return status;
}

/* TODO .. the following APIs are a relic of the old register based interface */

A_STATUS
BMIRawWrite(HIF_DEVICE *device, A_UCHAR *buffer, A_UINT32 length)
{
    return bmiBufferSend(device, buffer, length);
}

A_STATUS
BMIRawRead(HIF_DEVICE *device, A_UCHAR *buffer, A_UINT32 length, A_BOOL want_timeout)
{
    return bmiBufferReceive(device, buffer, length, want_timeout);
}



