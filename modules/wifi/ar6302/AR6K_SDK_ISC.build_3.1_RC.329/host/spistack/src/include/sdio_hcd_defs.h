//------------------------------------------------------------------------------
// <copyright file="sdio_hcd_defs.h" company="Atheros">
//    Copyright (c) 2007-2008 Atheros Corporation.  All rights reserved.
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
#ifndef __SDIO_HCD_DEFS_H___
#define __SDIO_HCD_DEFS_H___

#define SDCONFIG_SDIO_INT_CTRL     (SDCONFIG_FLAGS_HC_CONFIG | SDCONFIG_FLAGS_DATA_PUT  | 2)
#define SDCONFIG_SDIO_REARM_INT    (SDCONFIG_FLAGS_HC_CONFIG | SDCONFIG_FLAGS_DATA_NONE | 3)
#define SDCONFIG_BUS_MODE_CTRL     (SDCONFIG_FLAGS_HC_CONFIG | SDCONFIG_FLAGS_DATA_BOTH | 4)

typedef UINT8 SDIO_IRQ_MODE_FLAGS;
/* SDIO Interrupt control */
typedef struct _SDCONFIG_SDIO_INT_CTRL_DATA  {
    BOOL                  SlotIRQEnable;      /* turn on/off Slot IRQ detection */
}SDCONFIG_SDIO_INT_CTRL_DATA, *PSDCONFIG_SDIO_INT_CTRL_DATA;

/* card insert */
#define EVENT_HCD_ATTACH               1
/* card remove */
#define EVENT_HCD_DETACH               2
/* card slot interrupt */
#define EVENT_HCD_SDIO_IRQ_PENDING     3
/* transfer done */
#define EVENT_HCD_TRANSFER_DONE        4
/* (internal use only) */
#define EVENT_HCD_CD_POLLING           5
/* NOP */
#define EVENT_HCD_NOP                  0

/* attrib_flags */
#define SDHCD_ATTRIB_SUPPORTS_POWER   0x0001  /* host controller driver supports power managment */
#define SDHCD_ATTRIB_RAW_MODE         0x1000  /* host controller is a raw mode hcd*/

#define IS_CARD_PRESENT(pHcd)         ((pHcd)->CardProperties.Flags & CARD_TYPE_MASK)
#define SET_CURRENT_REQUEST(pHcd,Req) (pHcd)->pCurrentRequest = (Req)
#define IS_HCD_RAW(pHcd)              ((pHcd)->Attributes & SDHCD_ATTRIB_RAW_MODE)
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get a pointer to the current bus request for a host controller

  @function name: GET_CURRENT_REQUEST
  @prototype: PSDREQUEST GET_CURRENT_REQUEST (PSDHCD pHcd) 
  @category: HD_Reference
 
  @input:  pHcd - host structure
           
  @return: current SD/SDIO bus request being worked on
 
  @notes: Implemented as a macro. This macro returns the current SD request that is
          being worked on.
           
  @example: getting the current request: 
          pReq = GET_CURRENT_REQUEST(&pHct->Hcd);
        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define GET_CURRENT_REQUEST(pHcd)     (pHcd)->pCurrentRequest 

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get host controller's current operational bus clock 
  
  @function name: SDHCD_GET_OPER_CLOCK
  @prototype: SD_BUSCLOCK_RATE SDHCD_GET_OPER_CLOCK(PSDHCD pHcd)
  @category: HD_Reference
  
  @input:  pHcd   - the registered host structure
 
  @output: none

  @return: clock rate
 
  @notes: Implemented as a macro. Returns the current bus clock rate. 
         
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDHCD_GET_OPER_CLOCK(pHcd)      (pHcd)->CardProperties.OperBusClock
 
/* prototypes */
/* for HCD use */
SDIO_STATUS SDIO_RegisterHostController(PSDHCD pHcd);
SDIO_STATUS SDIO_UnregisterHostController(PSDHCD pHcd);
SDIO_STATUS SDIO_HandleHcdEvent(PSDHCD pHcd, HCD_EVENT Event);
SDIO_STATUS SDIO_BusAddOSDevice(PSDDMA_DESCRIPTION pDma, POS_PNPDRIVER pDriver, POS_PNPDEVICE pDevice);
void SDIO_BusRemoveOSDevice(POS_PNPDRIVER pDriver, POS_PNPDEVICE pDevice);
 
#endif /* __SDIO_BUSDRIVER_H___ */
