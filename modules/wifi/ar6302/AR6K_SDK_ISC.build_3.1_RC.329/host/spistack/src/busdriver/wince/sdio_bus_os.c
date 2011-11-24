//------------------------------------------------------------------------------
// <copyright file="sdio_bus_os.c" company="Atheros">
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
// SPI stack WINCE OS - layer
//
// Author(s): ="Atheros"
//==============================================================================
#define MODULE_NAME  SDBUSDRIVER
/* debug level for this module*/
#define DBG_DECLARE 7;
#include <ctsystem.h>
#include <sdio_busdriver.h>
#include <sdio_lib.h>
#include "../_busdriver.h"


#define SDIO_BUSDRIVER_REG_PATH (SDIO_STACK_REG_BASE TEXT("\\BusDriver"))

#define SPI_CLIENT_REGPATH _T("\\Drivers\\ATHSPI\\CLIENT")

#define GetBusDefaultDWORDValue(pKey,pDword)         \
    SDLIB_GetRegistryKeyDWORD(SDIO_STACK_BASE_HKEY,  \
                              SDIO_BUSDRIVER_REG_PATH, \
                              (pKey),                \
                              (pDword))
                              
static BOOL     g_BusDriverRdy = FALSE;
static HANDLE   g_hClientDriver = NULL;
static HANDLE   g_hClientUnloadThread = NULL;
static HANDLE   g_hDoUnloadEvent = NULL;
static HANDLE   g_hUnloadCompleteEvent = NULL;

/* 
 * SDIO_RegisterHostController - register a host controller bus driver
*/
SDIO_STATUS SDIO_RegisterHostController(PSDHCD pHcd) {
    if (!g_BusDriverRdy) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO BusDriver - Not Ready \n"));
        return SDIO_STATUS_ERROR;    
    }
    /* we are the exported version, call the internal version */
    return _SDIO_RegisterHostController(pHcd);
}

/* 
 * SDIO_UnregisterHostController - unregister a host controller bus driver
*/
SDIO_STATUS SDIO_UnregisterHostController(PSDHCD pHcd) {
    if (!g_BusDriverRdy) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO BusDriver - Not Ready \n"));
        return SDIO_STATUS_ERROR;    
    }
    /* we are the exported version, call the internal verison */
    return _SDIO_UnregisterHostController(pHcd);
}

/* 
 * SDIO_RegisterFunction - register a function driver
*/
static SDIO_STATUS SDIO_RegisterFunction(PSDFUNCTION pFunction) {
    SDIO_STATUS status;

    DBG_PRINT(SDDBG_TRACE, ("SDIO BusDriver - SDIO_RegisterFunction\n"));
    
    if (!g_BusDriverRdy) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO BusDriver - Not Ready \n"));
        return SDIO_STATUS_ERROR;    
    }
    
        /* since we do PnP registration first, we need to check the version */
    if (!CHECK_FUNCTION_DRIVER_VERSION(pFunction)) {
        DBG_PRINT(SDDBG_ERROR, 
           ("SDIO Bus Driver: Function Major Version Mismatch (hcd = %d, bus driver = %d)\n",
           GET_SDIO_STACK_VERSION_MAJOR(pFunction), CT_SDIO_STACK_VERSION_MAJOR(g_Version)));
        return SDIO_STATUS_INVALID_PARAMETER;       
    }
    
   
    status = _SDIO_RegisterFunction(pFunction);
       
    return status;
}

/* 
 * SDIO_UnregisterFunction - unregister a function driver
*/
static SDIO_STATUS SDIO_UnregisterFunction(PSDFUNCTION pFunction) {
    if (!g_BusDriverRdy) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO BusDriver - Not Ready \n"));
        return SDIO_STATUS_ERROR;    
    }
    /* we are the exported version, call the internal verison */
    return _SDIO_UnregisterFunction(pFunction);
}

/* 
 * SDIO_HandleHcdEvent - tell core an event occurred
*/
SDIO_STATUS SDIO_HandleHcdEvent(PSDHCD pHcd, HCD_EVENT Event) {
    /* we are the exported version, call the internal verison */
    DBG_PRINT(SDIODBG_HCD_EVENTS, ("SDIO Bus Driver: SDIO_HandleHcdEvent, event type 0x%X, HCD:0x%X\n", 
                         Event, (UINT)pHcd));
    return _SDIO_HandleHcdEvent(pHcd, Event);
}	
                                     
/* get default settings */
SDIO_STATUS _SDIO_BusGetDefaultSettings(PBDCONTEXT pBdc)
{    
    GetBusDefaultDWORDValue(TEXT("RequestRetries"), 
                            &pBdc->RequestRetries);
                                                            
    GetBusDefaultDWORDValue(TEXT("RequestListSize"), 
                            &pBdc->RequestListSize);
                                  
    GetBusDefaultDWORDValue(TEXT("SignalSemListSize"), 
                            &pBdc->SignalSemListSize);
                                  
    GetBusDefaultDWORDValue(TEXT("MaxHcdRecursion"), 
                            &pBdc->MaxHcdRecursion);
             
    return SDIO_STATUS_SUCCESS;  
}

/*
 * OS_IncHcdReference - increment host controller driver reference count
*/
SDIO_STATUS Do_OS_IncHcdReference(PSDHCD pHcd)
{
    if (pHcd->pModule != NULL) {
        InterlockedIncrement(&pHcd->pModule->ReferenceCount);       
    } else {
        DBG_PRINT(SDDBG_WARN,("SDIO BusDriver - HCD: %s should set module info! \n",
                               (pHcd->pName != NULL) ? pHcd->pName : "Unknown"));    
    }        
    return SDIO_STATUS_SUCCESS;
}

/*
 * OS_DecHcdReference - decrement host controller driver reference count
*/
SDIO_STATUS Do_OS_DecHcdReference(PSDHCD pHcd)
{
    if (pHcd->pModule != NULL) {
        InterlockedDecrement(&pHcd->pModule->ReferenceCount);     
    }
    return SDIO_STATUS_SUCCESS;
}



/*
 * OS_InitializeDevice - initialize device that will be registered
*/
SDIO_STATUS OS_InitializeDevice(PSDDEVICE pDevice, PSDFUNCTION pFunction) 
{
    
    return SDIO_STATUS_SUCCESS;
}

/*
 * OS_AddDevice - must be pre-initialized with OS_InitializeDevice
*/
SDIO_STATUS OS_AddDevice(PSDDEVICE pDevice, PSDFUNCTION pFunction) 
{
   
    DBG_PRINT(SDDBG_TRACE, ("SDIO BusDriver - OS_AddDevice adding function: %s\n",
                               pFunction->pName));
    
    return SDIO_STATUS_SUCCESS;
}

/*
 * OS_RemoveDevice - unregister device with driver and bus
*/
void OS_RemoveDevice(PSDDEVICE pDevice) 
{
    DBG_PRINT(SDDBG_TRACE, ("SDIO BusDriver - OS_RemoveDevice \n"));
}

static DWORD ClientUnloadThread(LPVOID pContext) 
{
    
    DEBUGMSG(1,(TEXT("*** SPI Bus driver client unload thread handle: 0x%X  \r\n"), g_hClientUnloadThread ));
        
    WaitForSingleObject(g_hDoUnloadEvent, INFINITE);
    
    if (g_hClientDriver != NULL) {
            /* unload client driver */
        DeactivateDevice(g_hClientDriver);
        g_hClientDriver = NULL;  
    }  
    
    SetEvent(g_hUnloadCompleteEvent);
    
    return 0;    
}

static void Cleanup()
{
    if (g_hDoUnloadEvent != NULL) {
        SetEvent(g_hDoUnloadEvent);    
    }
    
    if (g_hClientUnloadThread != NULL) {
        WaitForSingleObject(g_hClientUnloadThread, INFINITE);
        CloseHandle(g_hClientUnloadThread);   
        g_hClientUnloadThread = NULL; 
    }
    
    if (g_hDoUnloadEvent != NULL) {
        CloseHandle(g_hDoUnloadEvent);  
        g_hDoUnloadEvent = NULL;  
    }
    
    if (g_hUnloadCompleteEvent != NULL) {
        CloseHandle(g_hUnloadCompleteEvent);  
        g_hUnloadCompleteEvent = NULL;  
    }
    
}

/* main entry point for HCD layer to initialize bus driver layer */
BOOL SDIO_BusInit()
{ 
    DWORD threadId;
    
        /* get debug level */
    SDGetDebugLevelFromPath(SDIO_STACK_BASE_HKEY,SDIO_BUSDRIVER_REG_PATH);

    do {        
    
        if (!SDIO_SUCCESS(_SDIO_BusDriverInitialize())) {
            break;
        }
    
        g_hDoUnloadEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        
        if (NULL == g_hDoUnloadEvent) {
            break;    
        }
        
        g_hUnloadCompleteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        
        if (NULL == g_hUnloadCompleteEvent) {
            break;    
        }
        
        g_hClientUnloadThread = CreateThread(0, 
                                             0, 
                                             ClientUnloadThread, 
                                             NULL, 
                                             0, 
                                             &threadId);
        
        if (NULL == g_hClientUnloadThread) {
            break;    
        }
        
        g_BusDriverRdy = TRUE;
      
    } while (FALSE);
    
    if (!g_BusDriverRdy) {
        Cleanup();    
    }
    
    return g_BusDriverRdy;
}

VOID SDIO_BusUnloadClients()
{
    if (g_hClientDriver != NULL) {
        LONG waitStatus;
        
            /* wake thread to unload the client driver because the unload could have been
             * called by an applicaton thus the runtime stack will have the wrong read/write permissions.
             * We use a second thread to make sure the stack permissions are correct */
        SetEvent(g_hDoUnloadEvent);

        waitStatus = WaitForSingleObject(g_hUnloadCompleteEvent, 20000);

        if (waitStatus != WAIT_OBJECT_0) {
            RETAILMSG(1,(TEXT("*** SPI client driver unload thread is STUCK!!! (%d) (hThread=0x%X) \r\n"),
                    waitStatus,g_hClientUnloadThread));            
#ifdef DEBUG
            DebugBreak();
#endif            
        }
        
    }  
}

/* entry point for HCD layer to shutdown bus driver layer */
VOID SDIO_BusDeinit()
{
    if (g_BusDriverRdy) {
        SDIO_BusUnloadClients();
        Cleanup();
        _SDIO_BusDriverCleanup(); 
        g_BusDriverRdy = FALSE;  
    } 
} 

/* indicate to bus driver to load clients */
SDIO_STATUS SDIO_BusLoadClients()
{
    SDIO_STATUS                 status = SDIO_STATUS_ERROR;
    SDIO_CLIENT_INIT_CONTEXT    clientContext;
    
    ZERO_OBJECT(clientContext);
    
    clientContext.pRegisterFunction = SDIO_RegisterFunction;
    clientContext.pUnregisterFunction = SDIO_UnregisterFunction;
    clientContext.Magic = SDIO_CLIENT_INIT_MAGIC;
    do {
        
        if (!g_BusDriverRdy) {  
            break;
        }
            /* client drivers are streams drivers */
        g_hClientDriver = ActivateDevice(SPI_CLIENT_REGPATH, (DWORD)&clientContext);
        
        if (NULL == g_hClientDriver) {
            break;    
        }
        
        status = SDIO_STATUS_SUCCESS;  
        
    } while (FALSE);
    
    return status;
}


