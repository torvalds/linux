// Copyright (c) 2006 Atheros Communications Inc.
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

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@file: sdio_std_hcd_linux_lib.h

@abstract: include file for linux std host core APIs
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifndef SDIO_STD_HCD_LINUX_LIB_H_
#define SDIO_STD_HCD_LINUX_LIB_H_

typedef struct _SDHCD_CORE_CONTEXT {
    SDLIST       List;
    PVOID        pBusContext;        /* bus context this one belongs to */
    SDLIST       SlotList;         /* the list of current slots handled by this driver */
    spinlock_t   SlotListLock;     /* protection for the slot List */
    UINT         SlotCount;        /* number of slots currently installed */  
    /* everything below this line is reserved for the user of this library */
    UINT32       CoreReserved1; 
    UINT32       CoreReserved2; 
}SDHCD_CORE_CONTEXT, *PSDHCD_CORE_CONTEXT;

void  InitStdHostLib(void);
void  DeinitStdHostLib(void);
PSDHCD_CORE_CONTEXT CreateStdHostCore(PVOID pBusContext);
void  DeleteStdHostCore(PSDHCD_CORE_CONTEXT pStdCore);
PSDHCD_CORE_CONTEXT GetStdHostCore(PVOID pBusContext);

INT GetCurrentHcdInstanceCount(PSDHCD_CORE_CONTEXT pStdCore);
PSDHCD_INSTANCE CreateStdHcdInstance(POS_DEVICE pOSDevice, 
                                     UINT       SlotNumber, 
                                     PTEXT      pName);
void DeleteStdHcdInstance(PSDHCD_INSTANCE pHcInstance);
#define START_HCD_FLAGS_FORCE_NO_DMA  0x01  /* don't use DMA even though capabilities indicate it can */
#define START_HCD_FLAGS_FORCE_SDMA    0x02  /* force SDMA even though the capabilities show advance DMA support */
#define START_HCD_FLAGS_ALLOW_CBDMA   0x04  /* allow CBDMA */


typedef SDIO_STATUS (*PPLAT_OVERRIDE_CALLBACK)(PSDHCD_INSTANCE);
SDIO_STATUS AddStdHcdInstance(PSDHCD_CORE_CONTEXT pStdCore, 
                              PSDHCD_INSTANCE pHcInstance, 
                              UINT  Flags, 
                              PPLAT_OVERRIDE_CALLBACK pCallBack,                                
                              SDDMA_DESCRIPTION       *pSDMADescrip,
                              SDDMA_DESCRIPTION       *pADMADescrip);                              
SDIO_STATUS StartStdHostCore(PSDHCD_CORE_CONTEXT pStdCore);  
PSDHCD_INSTANCE RemoveStdHcdInstance(PSDHCD_CORE_CONTEXT pStdCore);
BOOL HandleSharedStdHostInterrupt(PSDHCD_CORE_CONTEXT pStdCore);
#endif /*SDIO_STD_HCD_LINUX_LIB_H_*/
