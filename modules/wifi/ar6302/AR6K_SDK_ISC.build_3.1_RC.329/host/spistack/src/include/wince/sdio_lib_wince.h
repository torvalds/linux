//------------------------------------------------------------------------------
// <copyright file="sdio_lib_wince.h" company="Atheros">
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

 SDIO Library includes for Wince CE specific APIs

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef SDIO_LIB_WINCE_H_
#define SDIO_LIB_WINCE_H_

#include "sdlist.h"

    /* generic registry data fetch */
SDIO_STATUS SDLIB_GetRegistryKeyValue(HKEY hKey, 
                                      WCHAR *pKeyPath, 
                                      WCHAR *pValueName, 
                                      PUCHAR pValue, 
                                      ULONG BufferSize);
    /* get generic DWORD data from the registry */                                      
SDIO_STATUS SDLIB_GetRegistryKeyDWORD(HKEY   hKey, 
                                      WCHAR  *pKeyPath,
                                      WCHAR  *pValueName, 
                                      DWORD  *pValue);
                                  
#define SDGetDebugLevelFromPath(hkey,Path)              \
    SDLIB_GetRegistryKeyDWORD(hkey,                     \
                              (Path),                   \
                              TEXT("debuglevel"),       \
                              &DBG_GET_DEBUG_LEVEL())  
                                                                
typedef VOID (*PCT_WORKER_CALLBACK)(PVOID);

typedef struct _CT_WORKER_TASK {
    SDLIST              List;
    PCT_WORKER_CALLBACK pCallBack;
    PVOID               pContext;   
    BOOL                Queued;
}CT_WORKER_TASK, *PCT_WORKER_TASK;

#define SDLIB_InitializeWorkerTask(pT,pC,pCon)  { \
    memset((pT),0,sizeof(CT_WORKER_TASK));        \
    (pT)->pCallBack = (pC);                       \
    (pT)->pContext = (pCon);                      \
}

SDIO_STATUS SDLIB_QueueWorkTask(PVOID Worker, PCT_WORKER_TASK pTask);

VOID SDLIB_DestroyWorker(PVOID Worker);

PVOID SDLIB_CreateWorker(INT WorkerPriority);
                            
VOID SDLIB_FlushWorkTask(PVOID Worker, PCT_WORKER_TASK pTask);

BOOL SDIO_LibraryInit();

VOID SDIO_LibraryDeinit();

#endif /*SDIO_LIB_WINCE_H_*/
