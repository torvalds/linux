//------------------------------------------------------------------------------
// <copyright file="bmi.c" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
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
//
// Author(s): ="Atheros"
//==============================================================================


#include "hif.h"
#include "bmi.h"
#include "bmi_internal.h"

#ifdef DEBUG
static ATH_DEBUG_MASK_DESCRIPTION bmi_debug_desc[] = {
    { ATH_DEBUG_BMI , "BMI Tracing"},
};

ATH_DEBUG_INSTANTIATE_MODULE_VAR(bmi,
                                 "bmi",
                                 "Boot Manager Interface",
                                 ATH_DEBUG_MASK_DEFAULTS,
                                 ATH_DEBUG_DESCRIPTION_COUNT(bmi_debug_desc),
                                 bmi_debug_desc);

#endif

/*
Although we had envisioned BMI to run on top of HTC, this is not how the
final implementation ended up. On the Target side, BMI is a part of the BSP
and does not use the HTC protocol nor even DMA -- it is intentionally kept
very simple.
*/

static A_UCHAR *pBMICmdBuf;
#define MAX_BMI_CMDBUF_SZ (BMI_DATASZ_MAX + \
                       sizeof(A_UINT32) /* cmd */ + \
                       sizeof(A_UINT32) /* addr */ + \
                       sizeof(A_UINT32))/* length */
#define BMI_COMMAND_FITS(sz) ((sz) <= MAX_BMI_CMDBUF_SZ)
#define BMI_EXCHANGE_TIMEOUT_MS  1000

/* APIs visible to the driver */
void
BMIInit(void)
{
    bmiDone = FALSE;


    /*
     * On some platforms, it's not possible to DMA to a static variable
     * in a device driver (e.g. Linux loadable driver module).
     * So we need to A_MALLOC space for "command credits" and for commands.
     *
     * Note: implicitly relies on A_MALLOC to provide a buffer that is
     * suitable for DMA (or PIO).  This buffer will be passed down the
     * bus stack.
     */

    if (!pBMICmdBuf) {
        pBMICmdBuf = (A_UCHAR *)A_MALLOC_NOWAIT(MAX_BMI_CMDBUF_SZ);
        A_ASSERT(pBMICmdBuf);
    }

    A_REGISTER_MODULE_DEBUG_INFO(bmi);
}

void
BMICleanup(void)
{
#if 0
    if (pBMICmdCredits) {
        A_FREE(pBMICmdCredits);
        pBMICmdCredits = NULL;
    }
#endif

    if (pBMICmdBuf) {
        A_FREE(pBMICmdBuf);
        pBMICmdBuf = NULL;
    }
}

A_STATUS
BMIDone(HIF_DEVICE *device)
{
    A_STATUS status;
    A_UINT32 cid;

    if (bmiDone) {
        AR_DEBUG_PRINTF (ATH_DEBUG_BMI, ("BMIDone skipped\n"));
        return A_OK;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI Done: Enter (device: 0x%p)\n", device));
    bmiDone = TRUE;
    cid = BMI_DONE;
    A_MEMCPY(pBMICmdBuf,&cid,sizeof(cid));

    status = HIFExchangeBMIMsg(device, pBMICmdBuf, sizeof(cid), NULL, NULL, 0);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to write to the device\n"));
        return A_ERROR;
    }

    if (pBMICmdBuf) {
        A_FREE(pBMICmdBuf);
        pBMICmdBuf = NULL;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI Done: Exit\n"));

    return A_OK;
}

#ifndef HIF_MESSAGE_BASED
extern A_STATUS HIFRegBasedGetTargetInfo(HIF_DEVICE *device, struct bmi_target_info *targ_info);
#endif

A_STATUS
BMIGetTargetInfo(HIF_DEVICE *device, struct bmi_target_info *targ_info)
{
    if (bmiDone) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("BMI Get Target Info Command disallowed\n"));
        return A_ERROR;
    }

#ifndef HIF_MESSAGE_BASED
        /* getting the target ID requires special handling because of the variable length
         * message */
    return HIFRegBasedGetTargetInfo(device,targ_info);
#else
        /* TODO */
    return A_ERROR;
#endif
}

A_STATUS
BMIReadMemory(HIF_DEVICE *device,
              A_UINT32 address,
              A_UCHAR *buffer,
              A_UINT32 length)
{
    A_UINT32 cid;
    A_STATUS status;
    A_UINT32 offset;
    A_UINT32 remaining, rxlen;

    A_ASSERT(BMI_COMMAND_FITS(BMI_DATASZ_MAX + sizeof(cid) + sizeof(address) + sizeof(length)));
    memset (pBMICmdBuf, 0, BMI_DATASZ_MAX + sizeof(cid) + sizeof(address) + sizeof(length));

    if (bmiDone) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Command disallowed\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI,
       			("BMI Read Memory: Enter (device: 0x%p, address: 0x%x, length: %d)\n",
        			device, address, length));

    cid = BMI_READ_MEMORY;

    remaining = length;

    while (remaining)
    {
        rxlen = (remaining < BMI_DATASZ_MAX) ? remaining : BMI_DATASZ_MAX;
        offset = 0;
        A_MEMCPY(&(pBMICmdBuf[offset]), &cid, sizeof(cid));
        offset += sizeof(cid);
        A_MEMCPY(&(pBMICmdBuf[offset]), &address, sizeof(address));
        offset += sizeof(address);
        A_MEMCPY(&(pBMICmdBuf[offset]), &rxlen, sizeof(rxlen));
        offset += sizeof(length);

        status = HIFExchangeBMIMsg(device,
                                   pBMICmdBuf,
                                   offset,
                                   pBMICmdBuf, /* note we reuse the same buffer to receive on */
                                   &rxlen,
                                   BMI_EXCHANGE_TIMEOUT_MS);

        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to read from the device\n"));
            return A_ERROR;
        }
        A_MEMCPY(&buffer[length - remaining], pBMICmdBuf, rxlen);
        remaining -= rxlen; address += rxlen;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI Read Memory: Exit\n"));
    return A_OK;
}

A_STATUS
BMIWriteMemory(HIF_DEVICE *device,
               A_UINT32 address,
               A_UCHAR *buffer,
               A_UINT32 length)
{
    A_UINT32 cid;
    A_STATUS status;
    A_UINT32 offset;
    A_UINT32 remaining, txlen;
    const A_UINT32 header = sizeof(cid) + sizeof(address) + sizeof(length);
    A_UCHAR alignedBuffer[BMI_DATASZ_MAX];
    A_UCHAR *src;

    A_ASSERT(BMI_COMMAND_FITS(BMI_DATASZ_MAX + header));
    memset (pBMICmdBuf, 0, BMI_DATASZ_MAX + header);

    if (bmiDone) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Command disallowed\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI,
         ("BMI Write Memory: Enter (device: 0x%p, address: 0x%x, length: %d)\n",
         device, address, length));

    cid = BMI_WRITE_MEMORY;

    remaining = length;
    while (remaining)
    {
        src = &buffer[length - remaining];
        if (remaining < (BMI_DATASZ_MAX - header)) {
            if (remaining & 3) {
                /* align it with 4 bytes */
                remaining = remaining + (4 - (remaining & 3));
                memcpy(alignedBuffer, src, remaining);
                src = alignedBuffer;
            } 
            txlen = remaining;
        } else {
            txlen = (BMI_DATASZ_MAX - header);
        }
        offset = 0;
        A_MEMCPY(&(pBMICmdBuf[offset]), &cid, sizeof(cid));
        offset += sizeof(cid);
        A_MEMCPY(&(pBMICmdBuf[offset]), &address, sizeof(address));
        offset += sizeof(address);
        A_MEMCPY(&(pBMICmdBuf[offset]), &txlen, sizeof(txlen));
        offset += sizeof(txlen);
        A_MEMCPY(&(pBMICmdBuf[offset]), src, txlen);
        offset += txlen;
        status = HIFExchangeBMIMsg(device, pBMICmdBuf, offset, NULL, NULL, BMI_EXCHANGE_TIMEOUT_MS);
        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to write to the device\n"));
            return A_ERROR;
        }
        remaining -= txlen; address += txlen;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI Write Memory: Exit\n"));

    return A_OK;
}

A_STATUS
BMIExecute(HIF_DEVICE *device,
           A_UINT32 address,
           A_UINT32 *param)
{
    A_UINT32 cid;
    A_STATUS status;
    A_UINT32 offset;
    A_UINT32 paramLen;

    A_ASSERT(BMI_COMMAND_FITS(sizeof(cid) + sizeof(address) + sizeof(param)));
    memset (pBMICmdBuf, 0, sizeof(cid) + sizeof(address) + sizeof(param));

    if (bmiDone) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Command disallowed\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI,
       ("BMI Execute: Enter (device: 0x%p, address: 0x%x, param: %d)\n",
        device, address, *param));

    cid = BMI_EXECUTE;

    offset = 0;
    A_MEMCPY(&(pBMICmdBuf[offset]), &cid, sizeof(cid));
    offset += sizeof(cid);
    A_MEMCPY(&(pBMICmdBuf[offset]), &address, sizeof(address));
    offset += sizeof(address);
    A_MEMCPY(&(pBMICmdBuf[offset]), param, sizeof(*param));
    offset += sizeof(*param);
    paramLen = sizeof(*param);
    status = HIFExchangeBMIMsg(device, pBMICmdBuf, offset, pBMICmdBuf, &paramLen, 0);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to read from the device\n"));
        return A_ERROR;
    }

    A_MEMCPY(param, pBMICmdBuf, sizeof(*param));

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI Execute: Exit (param: %d)\n", *param));
    return A_OK;
}

A_STATUS
BMISetAppStart(HIF_DEVICE *device,
               A_UINT32 address)
{
    A_UINT32 cid;
    A_STATUS status;
    A_UINT32 offset;

    A_ASSERT(BMI_COMMAND_FITS(sizeof(cid) + sizeof(address)));
    memset (pBMICmdBuf, 0, sizeof(cid) + sizeof(address));

    if (bmiDone) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Command disallowed\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI,
       ("BMI Set App Start: Enter (device: 0x%p, address: 0x%x)\n",
        device, address));

    cid = BMI_SET_APP_START;

    offset = 0;
    A_MEMCPY(&(pBMICmdBuf[offset]), &cid, sizeof(cid));
    offset += sizeof(cid);
    A_MEMCPY(&(pBMICmdBuf[offset]), &address, sizeof(address));
    offset += sizeof(address);

    status = HIFExchangeBMIMsg(device, pBMICmdBuf, offset, NULL, NULL, 0);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to write to the device\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI Set App Start: Exit\n"));
    return A_OK;
}

A_STATUS
BMIReadSOCRegister(HIF_DEVICE *device,
                   A_UINT32 address,
                   A_UINT32 *param)
{
    A_UINT32 cid;
    A_STATUS status;
    A_UINT32 offset,paramLen;

    A_ASSERT(BMI_COMMAND_FITS(sizeof(cid) + sizeof(address)));
    memset (pBMICmdBuf, 0, sizeof(cid) + sizeof(address));

    if (bmiDone) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Command disallowed\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI,
       ("BMI Read SOC Register: Enter (device: 0x%p, address: 0x%x)\n",
       device, address));

    cid = BMI_READ_SOC_REGISTER;

    offset = 0;
    A_MEMCPY(&(pBMICmdBuf[offset]), &cid, sizeof(cid));
    offset += sizeof(cid);
    A_MEMCPY(&(pBMICmdBuf[offset]), &address, sizeof(address));
    offset += sizeof(address);
    paramLen = sizeof(*param);
    status = HIFExchangeBMIMsg(device, pBMICmdBuf, offset, pBMICmdBuf, &paramLen, BMI_EXCHANGE_TIMEOUT_MS);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to read from the device\n"));
        return A_ERROR;
    }
    A_MEMCPY(param, pBMICmdBuf, sizeof(*param));

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI Read SOC Register: Exit (value: %d)\n", *param));
    return A_OK;
}

A_STATUS
BMIWriteSOCRegister(HIF_DEVICE *device,
                    A_UINT32 address,
                    A_UINT32 param)
{
    A_UINT32 cid;
    A_STATUS status;
    A_UINT32 offset;

    A_ASSERT(BMI_COMMAND_FITS(sizeof(cid) + sizeof(address) + sizeof(param)));
    memset (pBMICmdBuf, 0, sizeof(cid) + sizeof(address) + sizeof(param));

    if (bmiDone) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Command disallowed\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI,
     ("BMI Write SOC Register: Enter (device: 0x%p, address: 0x%x, param: %d)\n",
     device, address, param));

    cid = BMI_WRITE_SOC_REGISTER;

    offset = 0;
    A_MEMCPY(&(pBMICmdBuf[offset]), &cid, sizeof(cid));
    offset += sizeof(cid);
    A_MEMCPY(&(pBMICmdBuf[offset]), &address, sizeof(address));
    offset += sizeof(address);
    A_MEMCPY(&(pBMICmdBuf[offset]), &param, sizeof(param));
    offset += sizeof(param);
    status = HIFExchangeBMIMsg(device, pBMICmdBuf, offset, NULL, NULL, 0);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to write to the device\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI Read SOC Register: Exit\n"));
    return A_OK;
}

A_STATUS
BMIrompatchInstall(HIF_DEVICE *device,
                   A_UINT32 ROM_addr,
                   A_UINT32 RAM_addr,
                   A_UINT32 nbytes,
                   A_UINT32 do_activate,
                   A_UINT32 *rompatch_id)
{
    A_UINT32 cid;
    A_STATUS status;
    A_UINT32 offset,responseLen;

    A_ASSERT(BMI_COMMAND_FITS(sizeof(cid) + sizeof(ROM_addr) + sizeof(RAM_addr) +
				sizeof(nbytes) + sizeof(do_activate)));
    memset(pBMICmdBuf, 0, sizeof(cid) + sizeof(ROM_addr) + sizeof(RAM_addr) +
			sizeof(nbytes) + sizeof(do_activate));

    if (bmiDone) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Command disallowed\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI,
         ("BMI rompatch Install: Enter (device: 0x%p, ROMaddr: 0x%x, RAMaddr: 0x%x length: %d activate: %d)\n",
         device, ROM_addr, RAM_addr, nbytes, do_activate));

    cid = BMI_ROMPATCH_INSTALL;

    offset = 0;
    A_MEMCPY(&(pBMICmdBuf[offset]), &cid, sizeof(cid));
    offset += sizeof(cid);
    A_MEMCPY(&(pBMICmdBuf[offset]), &ROM_addr, sizeof(ROM_addr));
    offset += sizeof(ROM_addr);
    A_MEMCPY(&(pBMICmdBuf[offset]), &RAM_addr, sizeof(RAM_addr));
    offset += sizeof(RAM_addr);
    A_MEMCPY(&(pBMICmdBuf[offset]), &nbytes, sizeof(nbytes));
    offset += sizeof(nbytes);
    A_MEMCPY(&(pBMICmdBuf[offset]), &do_activate, sizeof(do_activate));
    offset += sizeof(do_activate);
    responseLen = sizeof(*rompatch_id);
    status = HIFExchangeBMIMsg(device, pBMICmdBuf, offset, pBMICmdBuf, &responseLen, BMI_EXCHANGE_TIMEOUT_MS);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to install ROM patch\n"));
        return A_ERROR;
    }

    A_MEMCPY(rompatch_id, pBMICmdBuf, sizeof(*rompatch_id));

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI rompatch Install: (rompatch_id=%d)\n", *rompatch_id));
    return A_OK;
}

A_STATUS
BMIrompatchUninstall(HIF_DEVICE *device,
                     A_UINT32 rompatch_id)
{
    A_UINT32 cid;
    A_STATUS status;
    A_UINT32 offset;

    A_ASSERT(BMI_COMMAND_FITS(sizeof(cid) + sizeof(rompatch_id)));
    memset (pBMICmdBuf, 0, sizeof(cid) + sizeof(rompatch_id));

    if (bmiDone) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Command disallowed\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI,
         ("BMI rompatch Uninstall: Enter (device: 0x%p, rompatch_id: %d)\n",
         								 device, rompatch_id));

    cid = BMI_ROMPATCH_UNINSTALL;

    offset = 0;
    A_MEMCPY(&(pBMICmdBuf[offset]), &cid, sizeof(cid));
    offset += sizeof(cid);
    A_MEMCPY(&(pBMICmdBuf[offset]), &rompatch_id, sizeof(rompatch_id));
    offset += sizeof(rompatch_id);
    status = HIFExchangeBMIMsg(device, pBMICmdBuf, offset, NULL, NULL, 0);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to write to the device\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI rompatch UNinstall: (rompatch_id=0x%x)\n", rompatch_id));
    return A_OK;
}

static A_STATUS
_BMIrompatchChangeActivation(HIF_DEVICE *device,
                             A_UINT32 rompatch_count,
                             A_UINT32 *rompatch_list,
                             A_UINT32 do_activate)
{
    A_UINT32 cid;
    A_STATUS status;
    A_UINT32 offset;
    A_UINT32 length;

    A_ASSERT(BMI_COMMAND_FITS(BMI_DATASZ_MAX + sizeof(cid) + sizeof(rompatch_count)));
    memset(pBMICmdBuf, 0, BMI_DATASZ_MAX + sizeof(cid) + sizeof(rompatch_count));

    if (bmiDone) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Command disallowed\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI,
         ("BMI Change rompatch Activation: Enter (device: 0x%p, count: %d)\n",
           device, rompatch_count));

    cid = do_activate ? BMI_ROMPATCH_ACTIVATE : BMI_ROMPATCH_DEACTIVATE;

    offset = 0;
    A_MEMCPY(&(pBMICmdBuf[offset]), &cid, sizeof(cid));
    offset += sizeof(cid);
    A_MEMCPY(&(pBMICmdBuf[offset]), &rompatch_count, sizeof(rompatch_count));
    offset += sizeof(rompatch_count);
    length = rompatch_count * sizeof(*rompatch_list);
    A_MEMCPY(&(pBMICmdBuf[offset]), rompatch_list, length);
    offset += length;
    status = HIFExchangeBMIMsg(device, pBMICmdBuf, offset, NULL, NULL, 0);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to write to the device\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI Change rompatch Activation: Exit\n"));

    return A_OK;
}

A_STATUS
BMIrompatchActivate(HIF_DEVICE *device,
                    A_UINT32 rompatch_count,
                    A_UINT32 *rompatch_list)
{
    return _BMIrompatchChangeActivation(device, rompatch_count, rompatch_list, 1);
}

A_STATUS
BMIrompatchDeactivate(HIF_DEVICE *device,
                      A_UINT32 rompatch_count,
                      A_UINT32 *rompatch_list)
{
    return _BMIrompatchChangeActivation(device, rompatch_count, rompatch_list, 0);
}

A_STATUS
BMILZData(HIF_DEVICE *device,
          A_UCHAR *buffer,
          A_UINT32 length)
{
    A_UINT32 cid;
    A_STATUS status;
    A_UINT32 offset;
    A_UINT32 remaining, txlen;
    const A_UINT32 header = sizeof(cid) + sizeof(length);

    A_ASSERT(BMI_COMMAND_FITS(BMI_DATASZ_MAX+header));
    memset (pBMICmdBuf, 0, BMI_DATASZ_MAX+header);

    if (bmiDone) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Command disallowed\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI,
         ("BMI Send LZ Data: Enter (device: 0x%p, length: %d)\n",
         device, length));

    cid = BMI_LZ_DATA;

    remaining = length;
    while (remaining)
    {
        txlen = (remaining < (BMI_DATASZ_MAX - header)) ?
                                       remaining : (BMI_DATASZ_MAX - header);
        offset = 0;
        A_MEMCPY(&(pBMICmdBuf[offset]), &cid, sizeof(cid));
        offset += sizeof(cid);
        A_MEMCPY(&(pBMICmdBuf[offset]), &txlen, sizeof(txlen));
        offset += sizeof(txlen);
        A_MEMCPY(&(pBMICmdBuf[offset]), &buffer[length - remaining], txlen);
        offset += txlen;
        status = HIFExchangeBMIMsg(device, pBMICmdBuf, offset, NULL, NULL, 0);
        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to write to the device\n"));
            return A_ERROR;
        }
        remaining -= txlen;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI LZ Data: Exit\n"));

    return A_OK;
}

A_STATUS
BMILZStreamStart(HIF_DEVICE *device,
                 A_UINT32 address)
{
    A_UINT32 cid;
    A_STATUS status;
    A_UINT32 offset;

    A_ASSERT(BMI_COMMAND_FITS(sizeof(cid) + sizeof(address)));
    memset (pBMICmdBuf, 0, sizeof(cid) + sizeof(address));

    if (bmiDone) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Command disallowed\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI,
         ("BMI LZ Stream Start: Enter (device: 0x%p, address: 0x%x)\n",
         device, address));

    cid = BMI_LZ_STREAM_START;
    offset = 0;
    A_MEMCPY(&(pBMICmdBuf[offset]), &cid, sizeof(cid));
    offset += sizeof(cid);
    A_MEMCPY(&(pBMICmdBuf[offset]), &address, sizeof(address));
    offset += sizeof(address);
    status = HIFExchangeBMIMsg(device, pBMICmdBuf, offset, NULL, NULL, 0);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to Start LZ Stream to the device\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI LZ Stream Start: Exit\n"));

    return A_OK;
}

A_STATUS
BMIFastDownload(HIF_DEVICE *device, A_UINT32 address, A_UCHAR *buffer, A_UINT32 length)
{
    A_STATUS status = A_ERROR;
    A_UINT32  lastWord = 0;
    A_UINT32  lastWordOffset = length & ~0x3;
    A_UINT32  unalignedBytes = length & 0x3;

    status = BMILZStreamStart (device, address);
    if (A_FAILED(status)) {
            return A_ERROR;
    }

    if (unalignedBytes) {
            /* copy the last word into a zero padded buffer */
        A_MEMCPY(&lastWord, &buffer[lastWordOffset], unalignedBytes);
    }

    status = BMILZData(device, buffer, lastWordOffset);

    if (A_FAILED(status)) {
        return A_ERROR;
    }

    if (unalignedBytes) {
        status = BMILZData(device, (A_UINT8 *)&lastWord, 4);
    }

    if (A_SUCCESS(status)) {
        //
        // Close compressed stream and open a new (fake) one.  This serves mainly to flush Target caches.
        //
        status = BMILZStreamStart (device, 0x00);
        if (A_FAILED(status)) {
           return A_ERROR;
        }
    }
	return status;
}

A_STATUS
BMInvramProcess(HIF_DEVICE *device, A_UCHAR *seg_name, A_UINT32 *retval)
{
    A_UINT32 cid;
    A_STATUS status;
    A_UINT32 offset;
    A_UINT32 retvalLen;

    A_ASSERT(BMI_COMMAND_FITS(sizeof(cid) + BMI_NVRAM_SEG_NAME_SZ));

    if (bmiDone) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Command disallowed\n"));
        return A_ERROR;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI,
         ("BMI NVRAM Process: Enter (device: 0x%p, name: %s)\n",
           device, seg_name));

    cid = BMI_NVRAM_PROCESS;
    offset = 0;
    A_MEMCPY(&(pBMICmdBuf[offset]), &cid, sizeof(cid));
    offset += sizeof(cid);
    A_MEMCPY(&(pBMICmdBuf[offset]), seg_name, BMI_NVRAM_SEG_NAME_SZ);
    offset += BMI_NVRAM_SEG_NAME_SZ;
    retvalLen = sizeof(*retval);
    status = HIFExchangeBMIMsg(device, pBMICmdBuf, offset, pBMICmdBuf, &retvalLen, 0);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unable to access the device\n"));
        return A_ERROR;
    }

    A_MEMCPY(retval, pBMICmdBuf, sizeof(*retval));

    AR_DEBUG_PRINTF(ATH_DEBUG_BMI, ("BMI NVRAM Process: Exit\n"));

    return A_OK;
}

#ifdef HIF_MESSAGE_BASED

/* TODO.. stubs.. for message-based HIFs, the RAW access APIs need to be changed
 */

A_STATUS
BMIRawWrite(HIF_DEVICE *device, A_UCHAR *buffer, A_UINT32 length)
{
        /* TODO */
    return A_ERROR;
}

A_STATUS
BMIRawRead(HIF_DEVICE *device, A_UCHAR *buffer, A_UINT32 length, A_BOOL want_timeout)
{
        /* TODO */
    return A_ERROR;
}

#endif


