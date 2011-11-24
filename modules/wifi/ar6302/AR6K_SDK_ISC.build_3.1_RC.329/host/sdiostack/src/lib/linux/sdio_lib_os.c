/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@file: sdio_function_os.c

@abstract: Linux implementation module for SDIO library

#notes: includes module load and unload functions

@notice: Copyright (c) 2004 Atheros Communications Inc.


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
// Portions of this code were developed with information supplied from the 
// SD Card Association Simplified Specifications. The following conditions and disclaimers may apply:
//
//  The following conditions apply to the release of the SD simplified specification (“Simplified
//  Specification”) by the SD Card Association. The Simplified Specification is a subset of the complete 
//  SD Specification which is owned by the SD Card Association. This Simplified Specification is provided 
//  on a non-confidential basis subject to the disclaimers below. Any implementation of the Simplified 
//  Specification may require a license from the SD Card Association or other third parties.
//  Disclaimers:
//  The information contained in the Simplified Specification is presented only as a standard 
//  specification for SD Cards and SD Host/Ancillary products and is provided "AS-IS" without any 
//  representations or warranties of any kind. No responsibility is assumed by the SD Card Association for 
//  any damages, any infringements of patents or other right of the SD Card Association or any third 
//  parties, which may result from its use. No license is granted by implication, estoppel or otherwise 
//  under any patent or other rights of the SD Card Association or any third party. Nothing herein shall 
//  be construed as an obligation by the SD Card Association to disclose or distribute any technical 
//  information, know-how or other confidential information to any third party.
//
//
// The initial developers of the original code are Seung Yi and Paul Lever
//
// sdio@atheros.com
//
//
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* debug level for this module*/
#define DBG_DECLARE 4;
#include "../../include/ctsystem.h"
 
#include <linux/module.h>
#include <linux/init.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)   
#include <linux/kthread.h>
#endif

#include "../../include/sdio_busdriver.h"
#include "../../include/sdio_lib.h"
#include "../_sdio_lib.h"

#define DESCRIPTION "SDIO Kernel Library"
#define AUTHOR "Atheros Communications, Inc."

/* debug print parameter */

CT_DECLARE_MODULE_PARAM_INTEGER(debuglevel);
MODULE_PARM_DESC(debuglevel, "debuglevel 0-7, controls debug prints");



/* proxies */
SDIO_STATUS SDLIB_IssueCMD52(PSDDEVICE     pDevice,
                            UINT8         FuncNo,
                            UINT32        Address,
                            PUINT8        pData,
                            INT           ByteCount,
                            BOOL          Write)
{
    return _SDLIB_IssueCMD52(pDevice,FuncNo,Address,pData,ByteCount,Write);
}

SDIO_STATUS SDLIB_FindTuple(PSDDEVICE  pDevice,
                         UINT8      Tuple,
                         UINT32     *pTupleScanAddress,
                         PUINT8     pBuffer,
                         UINT8      *pLength)
{
    return _SDLIB_FindTuple(pDevice,Tuple,pTupleScanAddress,pBuffer,pLength);
}  

SDIO_STATUS SDLIB_IssueConfig(PSDDEVICE        pDevice,
                              SDCONFIG_COMMAND Command,
                              PVOID            pData,
                              INT              Length)
{
    return _SDLIB_IssueConfig(pDevice,Command,pData,Length);
}   
  
void SDLIB_PrintBuffer(PUCHAR pBuffer,INT Length,PTEXT pDescription)
{
    _SDLIB_PrintBuffer(pBuffer,Length,pDescription);   
} 

SDIO_STATUS SDLIB_SetFunctionBlockSize(PSDDEVICE        pDevice,
                                       UINT16           BlockSize)   
{
    return _SDLIB_SetFunctionBlockSize(pDevice,BlockSize);  
} 
 
void SDLIB_SetupCMD52Request(UINT8         FuncNo,
                             UINT32        Address,
                             BOOL          Write,
                             UINT8         WriteData,                                    
                             PSDREQUEST    pRequest)
{
    _SDLIB_SetupCMD52Request(FuncNo,Address,Write,WriteData,pRequest);  
}        
      
SDIO_STATUS SDLIB_GetDefaultOpCurrent(PSDDEVICE  pDevice, SD_SLOT_CURRENT *pOpCurrent) 
{
    return _SDLIB_GetDefaultOpCurrent(pDevice,pOpCurrent);  
}   

/* helper function launcher */
INT HelperLaunch(PVOID pContext)
{
    INT exit;
        /* call function */
    exit = ((POSKERNEL_HELPER)pContext)->pHelperFunc((POSKERNEL_HELPER)pContext);
    complete_and_exit(&((POSKERNEL_HELPER)pContext)->Completion, exit);    
    return exit;
}

/*
 * OSCreateHelper - create a worker kernel thread
*/
SDIO_STATUS SDLIB_OSCreateHelper(POSKERNEL_HELPER pHelper,
                           PHELPER_FUNCTION pFunction, 
                           PVOID            pContext)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    
    memset(pHelper,0,sizeof(OSKERNEL_HELPER));  
    
    do {
        pHelper->pContext = pContext;
        pHelper->pHelperFunc = pFunction;

#ifndef CT_NO_HELPER_WAKE_SIGNAL    
        status = SignalInitialize(&pHelper->WakeSignal);
        if (!SDIO_SUCCESS(status)) {
            break; 
        }    
#endif
        
        init_completion(&pHelper->Completion);
        
        spin_lock_init(&pHelper->WakeLock);
        pHelper->WakeState = FALSE;
        
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        pHelper->pTask = kthread_create(HelperLaunch,
                                       (PVOID)pHelper,
                                       "SDIO Helper");
        if (NULL == pHelper->pTask) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;  
        }
        wake_up_process(pHelper->pTask);
#else 
    /* 2.4 */       
        pHelper->pTask = kernel_thread(HelperLaunch,
                                       (PVOID)pHelper,
                                       (CLONE_FS | CLONE_FILES | SIGCHLD));
        if (pHelper->pTask < 0) {
            DBG_PRINT(SDDBG_TRACE, 
                ("SDIO BusDriver - OSCreateHelper, failed to create thread\n"));
        }        
#endif

    } while (FALSE);
    
    if (!SDIO_SUCCESS(status)) {
        SDLIB_OSDeleteHelper(pHelper);   
    }
    return status;
}
                           
/*
 * OSDeleteHelper - delete thread created with OSCreateHelper
*/
void SDLIB_OSDeleteHelper(POSKERNEL_HELPER pHelper)
{
 
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    if (pHelper->pTask != NULL) {
#else 
    /* 2.4 */       
    if (pHelper->pTask >= 0) {
#endif        
        pHelper->ShutDown = TRUE;       

#ifdef CT_NO_HELPER_WAKE_SIGNAL   
        if (!pHelper->Completion.done) {
                /* helper is still running, wake it */
            SD_WAKE_OS_HELPER(pHelper);
        }
#else    
        SignalSet(&pHelper->WakeSignal); 
#endif        
            /* wait for thread to exit */
        wait_for_completion(&pHelper->Completion);  
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        pHelper->pTask = NULL;
#else 
    /* 2.4 */       
        pHelper->pTask = 0;
#endif        
    }  

#ifndef CT_NO_HELPER_WAKE_SIGNAL
    SignalDelete(&pHelper->WakeSignal);
#endif
}
                          
/*
 * module init
*/
static int __init sdio_lib_init(void) {
    REL_PRINT(SDDBG_TRACE, ("SDIO Library load\n"));
    return 0;
}

/*
 * module cleanup
*/
static void __exit sdio_lib_cleanup(void) {
    REL_PRINT(SDDBG_TRACE, ("SDIO Library unload\n"));
}

PSDMESSAGE_QUEUE SDLIB_CreateMessageQueue(INT MaxMessages, UINT MaxMessageLength)
{
    return _CreateMessageQueue(MaxMessages,MaxMessageLength);
  
}
void SDLIB_DeleteMessageQueue(PSDMESSAGE_QUEUE pQueue)
{
    _DeleteMessageQueue(pQueue);
}

SDIO_STATUS SDLIB_PostMessage(PSDMESSAGE_QUEUE pQueue, PVOID pMessage, UINT MessageLength)
{
    return _PostMessage(pQueue,pMessage,MessageLength);
}

SDIO_STATUS SDLIB_GetMessage(PSDMESSAGE_QUEUE pQueue, PVOID pData, UINT *pBufferLength)
{
    return _GetMessage(pQueue,pData,pBufferLength);
}

// 
//MODULE_LICENSE("Dual BSD/GPL");
//
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_AUTHOR(AUTHOR);
module_init(sdio_lib_init);
module_exit(sdio_lib_cleanup);
EXPORT_SYMBOL(SDLIB_IssueCMD52);
EXPORT_SYMBOL(SDLIB_FindTuple);
EXPORT_SYMBOL(SDLIB_IssueConfig);
EXPORT_SYMBOL(SDLIB_PrintBuffer);
EXPORT_SYMBOL(SDLIB_SetFunctionBlockSize);
EXPORT_SYMBOL(SDLIB_SetupCMD52Request);
EXPORT_SYMBOL(SDLIB_GetDefaultOpCurrent);
EXPORT_SYMBOL(SDLIB_OSCreateHelper);
EXPORT_SYMBOL(SDLIB_OSDeleteHelper);
EXPORT_SYMBOL(SDLIB_CreateMessageQueue);
EXPORT_SYMBOL(SDLIB_DeleteMessageQueue);
EXPORT_SYMBOL(SDLIB_PostMessage);
EXPORT_SYMBOL(SDLIB_GetMessage);
