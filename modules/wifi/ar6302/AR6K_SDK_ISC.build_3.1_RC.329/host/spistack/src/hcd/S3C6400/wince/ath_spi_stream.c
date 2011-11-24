//------------------------------------------------------------------------------
// <copyright file="athspi_stream.c" company="Atheros">
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

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

     SPI driver streams interface

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#define DBG_DECLARE 7; 
#include <ctsystem.h>
#include <sdio_busdriver.h>
#include "ath_spi_hcd_if.h"
#include "ath_spi_hcd.h"
#include "ath_spi_hw.h"
#include "ath_spi_wince.h"

    /* macro to define the prefix for the exports */
#define STREAM_PREFIX(func)  ATS_##func

#define SPI_HOST_IOCTL_SLOT_OFF  0x1
#define SPI_HOST_IOCTL_SLOT_ON   0x2


BOOL DllEntry(HINSTANCE  hInstance,
              ULONG      Reason,
              LPVOID     pReserved)
{
   
    if (Reason == DLL_PROCESS_ATTACH) { /* init std host core */
        DisableThreadLibraryCalls((HMODULE)hInstance);    
    }

    if (Reason == DLL_PROCESS_DETACH) {
    }

    return TRUE;
} 

BOOL STREAM_PREFIX(PreDeinit)(DWORD DeviceContext)
{
    
    return TRUE;   
}

DWORD STREAM_PREFIX(Init)(DWORD InitContext)
{
    SDHCD_HW_DEVICE *pHWDevice = NULL;
     
    DBG_PRINT(SDDBG_TRACE,("_Init called  dc:0x%X \n",InitContext));
    
    do {
         
        if (!SDIO_LibraryInit()) {
            break;    
        }
        
        if (!SDIO_BusInit()) {
            SDIO_LibraryDeinit();
            DBG_PRINT(SDDBG_ERROR,("SDIO BusInit failed! \n"));
            break;  
        }
        
        pHWDevice = InitializeSPIHW((PTSTR)InitContext); 
        
        if (NULL == pHWDevice) {
            SDIO_BusDeinit();   
            SDIO_LibraryDeinit(); 
            break;
        }
        
            /* notify that SPI module is installed */
        SDIO_HandleHcdEvent(&pHWDevice->pDevice->Hcd, EVENT_HCD_ATTACH);
        
        SDIO_BusLoadClients();
        
    } while (FALSE);
        
    return (DWORD)pHWDevice;   
}

BOOL STREAM_PREFIX(Deinit)(DWORD DeviceContext)
{

    DBG_PRINT(SDDBG_TRACE,("_Deinit called  dc:0x%X \n",DeviceContext));
       
        /* unload any attached clients first */
    SDIO_BusUnloadClients();
        /* cleanup SPI HW layer, this de-registers the host controller */
    CleanupSPIHW((SDHCD_HW_DEVICE *)DeviceContext);
    SDIO_BusDeinit();
    SDIO_LibraryDeinit();
    return TRUE;
}    

BOOL STREAM_PREFIX(PreClose)(DWORD OpenContext)
{ 
    return TRUE;
}

DWORD STREAM_PREFIX(Open)(DWORD    DeviceContext,
               DWORD    AccessCode,
               DWORD    ShareMode)
{
    return 0;
}

BOOL STREAM_PREFIX(Close)(DWORD OpenContext)
{
   
    return FALSE;   
}

BOOL STREAM_PREFIX(IOControl)(
                   DWORD   OpenContext,
                   DWORD   dwCode,
                   PBYTE   pBufIn,
                   DWORD   dwLenIn,
                   PBYTE   pBufOut,
                   DWORD   dwLenOut,
                   PDWORD  pdwActualOut)
{   
    switch (dwCode) {
        
        case SPI_HOST_IOCTL_SLOT_OFF:
            /* TODO, reset module, poweroff slot */
            break;
            
        case SPI_HOST_IOCTL_SLOT_ON:
            /* TODO reset module poweroff slot */
            break; 
        default:
            return FALSE;    
    }
    
    return TRUE;
}

DWORD STREAM_PREFIX(Write)(DWORD   OpenContext,
                           LPCVOID pBuffer,
                           DWORD   Count)
{
   return -1;
}


DWORD STREAM_PREFIX(Read)(DWORD    OpenContext,
                          LPVOID   pBuffer,
                          DWORD    Count)
{
    return -1;   
}

DWORD STREAM_PREFIX(Seek)(DWORD    OpenContext,
                          LONG     Amount,
                          DWORD    Type)
{
    return -1;
}

VOID STREAM_PREFIX(PowerDown)(DWORD DeviceContext)
{

}

VOID STREAM_PREFIX(PowerUp)(DWORD DeviceContext)
{
	
}



