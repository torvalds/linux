//------------------------------------------------------------------------------
// <copyright file="ath_spi_hw.h" company="Atheros">
//    Copyright (c) 2008 Atheros Corporation.  All rights reserved.
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
// Author(s): ="Atheros"
//==============================================================================

#ifndef ATH_SPI_HW_H_
#define ATH_SPI_HW_H_


typedef struct _SDHCD_HW_DEVICE {
    UINT8           InitStateMask;
#define SDHC_REGISTERED            0x10
#define SDHC_COMMON_INIT           0x40
#define SDHC_HW_INIT               0x80
    PSDHCD_DEVICE   pDevice;               /* back pointer to the common layer */
    BOOL            IrqEnabled;
    SDHCD_DEVICE    SpiCommon;             /* storage for the SPI common layer */
    PVOID           pWorker;
    CT_WORKER_TASK  IOCompleteWorkTask;    /* work task for deferred I/O completion */
    HANDLE          hIstEventSPIGpioIRQ;   /* interrupt service event */
    HANDLE          hIstSPIGpioIRQ;        /* interrupt service thread */
    BOOL            ShutDown;              /* shutdown IST */
    DWORD           SysIntrSPIGpioIRQ;     /* system interrupt for GPIO interrupt */
    BOOL            CommonBufferDMA;       /* common buffer is used flag */
    UINT8           *pDmaCommonBuffer;     /* if common buffer is used, this is the buffer */
	UINT8           *pDmaCommonPhysicalBuffer;     /* if common buffer is used, this is the buffer */
	PVOID           pSpiContext;
}SDHCD_HW_DEVICE, *PSDHCD_HW_DEVICE;

#define GET_HW_DEVICE(pDevice) ((PSDHCD_HW_DEVICE)((pDevice)->pHWDevice))

SDHCD_HW_DEVICE *InitializeSPIHW(PTSTR pRegPath);
void CleanupSPIHW(SDHCD_HW_DEVICE *pHWDevice);

#endif /*ATH_SPI_HW_H_*/
