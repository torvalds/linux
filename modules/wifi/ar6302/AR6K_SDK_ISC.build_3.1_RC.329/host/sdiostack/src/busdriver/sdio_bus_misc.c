// Copyright (c) 2004-2006 Atheros Communications Inc.
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
@file: sdio_bus_misc.c

@abstract: OS independent bus driver support

#notes: this file contains miscellaneous control functions
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define MODULE_NAME  SDBUSDRIVER
#include "../include/ctsystem.h"
#include "../include/sdio_busdriver.h"
#include "../include/sdio_lib.h"
#include "_busdriver.h"
#include "../include/_sdio_defs.h"
#include "../include/mmc_defs.h"
        
                              
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  IssueBusRequestBd - issue a bus request
  Input:  pHcd - HCD object
          Cmd - command to issue
          Argument - command argument
          Flags - request flags
        
  Output: pReqToUse - request to use (if caller wants response data)
  Return: SDIO Status
  Notes:  This function only issues 1 block data transfers
          This function issues the request synchronously
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _IssueBusRequestBd(PSDHCD           pHcd,
                               UINT8            Cmd,
                               UINT32           Argument,
                               SDREQUEST_FLAGS  Flags,
                               PSDREQUEST       pReqToUse,
                               PVOID            pData,
                               INT              Length)
{ 
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    PSDREQUEST  pReq;
    
    if (NULL == pReqToUse) {
            /* caller doesn't care about the response data, allocate locally */
        pReq = AllocateRequest();
        if (NULL == pReq) {
            return SDIO_STATUS_NO_RESOURCES;    
        }
    } else {
            /* use the caller's request buffer */
        pReq = pReqToUse;  
    }
    
    pReq->Argument = Argument;          
    pReq->Flags = Flags;              
    pReq->Command = Cmd; 
    if (pReq->Flags & SDREQ_FLAGS_DATA_TRANS) {
        pReq->pDataBuffer  = pData;
        pReq->BlockCount = 1;
        pReq->BlockLen = Length;
    }
        
    status = IssueRequestToHCD(pHcd,pReq);

    if (NULL == pReqToUse) {
        DBG_ASSERT(pReq != NULL);
        FreeRequest(pReq);   
    }
    return status;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  ConvertVoltageCapsToOCRMask - initialize card
  Input:  VoltageCaps - voltage cap to look up
  Return: 32 bit OCR mask
  Notes:  this function sets voltage for +- 10%
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static UINT32 ConvertVoltageCapsToOCRMask(SLOT_VOLTAGE_MASK VoltageCaps)
{
    UINT32 ocrMask;
    
    ocrMask = 0;
    
    if (VoltageCaps & SLOT_POWER_3_3V) {
        ocrMask |= SD_OCR_3_2_TO_3_3_VDD | SD_OCR_3_3_TO_3_4_VDD;   
    }
    if (VoltageCaps & SLOT_POWER_3_0V) {
        ocrMask |= SD_OCR_2_9_TO_3_0_VDD | SD_OCR_3_0_TO_3_1_VDD;
    } 
    if (VoltageCaps & SLOT_POWER_2_8V) {
        ocrMask |= SD_OCR_2_7_TO_2_8_VDD | SD_OCR_2_8_TO_2_9_VDD;      
    }
    if (VoltageCaps & SLOT_POWER_2_0V) {
        ocrMask |= SD_OCR_1_9_TO_2_0_VDD | SD_OCR_2_0_TO_2_1_VDD;      
    }
    if (VoltageCaps & SLOT_POWER_1_8V) {
        ocrMask |= SD_OCR_1_7_TO_1_8_VDD | SD_OCR_1_8_TO_1_9_VDD;        
    }
    if (VoltageCaps & SLOT_POWER_1_6V) {
        ocrMask |= SD_OCR_1_6_TO_1_7_VDD;   
    }
  
    return ocrMask;  
}

static UINT32 GetUsableOCRValue(UINT32 CardOCR, UINT32 SlotOCRMask) 
{
    INT    i;
    UINT32 mask = 0;
    
    for (i = 0; i < 32; i++) { 
        mask = 1 << i; 
        if ((SlotOCRMask & mask) && (CardOCR & mask)) {
            return mask;  
        }
    }
    
    return mask;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  GetPowerSetting - power up the SDIO card
  Input:  pHcd - HCD object
          pOCRvalue - OCR value of the card
  Output: pOCRvalue - OCR to actually use
  Return: power setting for HCD based on card's OCR, zero indicates unsupported
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static SLOT_VOLTAGE_MASK GetPowerSetting(PSDHCD pHcd, UINT32 *pOCRvalue)
{
    UINT32                      ocrMask;
    SLOT_VOLTAGE_MASK           hcdVoltage = 0;
    SLOT_VOLTAGE_MASK           hcdVMask;
    INT                         i;
   
        /* check preferred value */
    ocrMask = ConvertVoltageCapsToOCRMask(pHcd->SlotVoltagePreferred);    
    if (ocrMask & *pOCRvalue) {
            /* using preferred voltage */  
        *pOCRvalue = GetUsableOCRValue(*pOCRvalue, ocrMask);   
        hcdVoltage = pHcd->SlotVoltagePreferred;
    } else {
            /* walk through the slot voltage caps and find a match */
        for (i = 0; i < 8; i++) {
            hcdVMask = (1 << i); 
            if (hcdVMask & pHcd->SlotVoltageCaps) {
                ocrMask = ConvertVoltageCapsToOCRMask((SLOT_VOLTAGE_MASK)(pHcd->SlotVoltageCaps & hcdVMask));
                if (ocrMask & *pOCRvalue) {                    
                        /* found a match */
                    *pOCRvalue = GetUsableOCRValue(*pOCRvalue, ocrMask);  
                    hcdVoltage = pHcd->SlotVoltageCaps & hcdVMask;
                    break;  
                }   
            }     
        }   
    }
   
    return hcdVoltage;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  TestPresence - test the presence of a card/function
  Input:  pHcd - HCD object
          TestType - type of test to perform
  Output: pReq - Request to use (optional)
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS TestPresence(PSDHCD          pHcd, 
                         CARD_INFO_FLAGS TestType,
                         PSDREQUEST      pReq)
{
    SDIO_STATUS status = SDIO_STATUS_ERROR;
    
    switch (TestType) {
        case CARD_SDIO:   
                /* issue CMD5 */
            status = _IssueSimpleBusRequest(pHcd,CMD5,0,
                        SDREQ_FLAGS_RESP_SDIO_R4 | SDREQ_FLAGS_RESP_SKIP_SPI_FILT,pReq);
            break;
        case CARD_SD:
            if (IS_HCD_BUS_MODE_SPI(pHcd)) { 
                 /* ACMD41 just starts initialization when in SPI mode, argument is ignored
                 * Note: In SPI mode ACMD41 uses an R1 response */
                status = _IssueSimpleBusRequest(pHcd,ACMD41,0,
                                                SDREQ_FLAGS_APP_CMD | SDREQ_FLAGS_RESP_R1,pReq);        
     
            } else {
                /* issue ACMD41 with OCR value of zero */
                /* ACMD41 on SD uses an R3 response */
                status = _IssueSimpleBusRequest(pHcd,ACMD41,0,
                                                SDREQ_FLAGS_APP_CMD | SDREQ_FLAGS_RESP_R3,pReq);        
            }
            break;
        case CARD_MMC:
                 /* issue CMD1 */ 
            if (IS_HCD_BUS_MODE_SPI(pHcd)) { 
                    /* note: in SPI mode an R1 response is used */
                status = _IssueSimpleBusRequest(pHcd,CMD1,0,SDREQ_FLAGS_RESP_R1,pReq); 
            } else {
                status = _IssueSimpleBusRequest(pHcd,CMD1,0,SDREQ_FLAGS_RESP_R3,pReq);
            }
            break;
        default:
            DBG_ASSERT(FALSE);
            break;
    }          

    return status;
}
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  ReadOCR - read the OCR
  Input:  pHcd - HCD object
          ReadType - type of read to perform
          OCRValue - OCR value to use as an argument
  Output: pReq - Request to use
          pOCRValueRd - OCR value read back (can be NULL)
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static SDIO_STATUS ReadOCR(PSDHCD          pHcd, 
                           CARD_INFO_FLAGS ReadType, 
                           PSDREQUEST      pReq, 
                           UINT32          OCRValue, 
                           UINT32          *pOCRValueRd)
{
    SDIO_STATUS status = SDIO_STATUS_ERROR;
    
    switch (ReadType) {
        case CARD_SDIO:   
                /* CMD5 for SDIO cards */ 
            if (IS_HCD_BUS_MODE_SPI(pHcd)) {
                    /* skip the SPI filter, we will decode the response here  */
                status = _IssueSimpleBusRequest(pHcd,CMD5,
                                                OCRValue, 
                                                SDREQ_FLAGS_RESP_SDIO_R4 | 
                                                SDREQ_FLAGS_RESP_SKIP_SPI_FILT,
                                                pReq);   
            } else { 
                    /* native SD */               
                status = _IssueSimpleBusRequest(pHcd,CMD5,
                                                OCRValue, 
                                                SDREQ_FLAGS_RESP_SDIO_R4,
                                                pReq);   
            }         
            break;
        case CARD_SD:
            if (IS_HCD_BUS_MODE_SPI(pHcd)) {
                    /* CMD58 is used to read the OCR */
                status = _IssueSimpleBusRequest(pHcd,CMD58,
                                                0, /* argument ignored */
                                                (SDREQ_FLAGS_RESP_R3 | SDREQ_FLAGS_RESP_SKIP_SPI_FILT),
                                                pReq);     
            } else {
                    /* SD Native uses ACMD41 */
                status = _IssueSimpleBusRequest(pHcd,ACMD41,
                                                OCRValue, 
                                                SDREQ_FLAGS_APP_CMD | SDREQ_FLAGS_RESP_R3,
                                                pReq);  
            } 
            break;
        case CARD_MMC:
            if (IS_HCD_BUS_MODE_SPI(pHcd)) {
                    /* CMD58 is used to read the OCR  */
                status = _IssueSimpleBusRequest(pHcd,CMD58,
                                                0, /* argument ignored */
                                                (SDREQ_FLAGS_RESP_R3 | SDREQ_FLAGS_RESP_SKIP_SPI_FILT),
                                                pReq);     
            } else {
                    /* MMC Native uses CMD1 */
                status = _IssueSimpleBusRequest(pHcd,CMD1,
                                                OCRValue, SDREQ_FLAGS_RESP_R3,
                                                pReq); 
            }   
            break;
        default:
            DBG_ASSERT(FALSE);
            break;
    }

    if (SDIO_SUCCESS(status) && (pOCRValueRd != NULL)) {
        *pOCRValueRd = 0;
            /* someone wants the OCR read back */ 
        switch (ReadType) {
            case CARD_SDIO:
                if (IS_HCD_BUS_MODE_SPI(pHcd)) { 
                    *pOCRValueRd = SPI_SDIO_R4_GET_OCR(pReq->Response);                   
                } else {
                    *pOCRValueRd = SD_SDIO_R4_GET_OCR(pReq->Response);     
                } 
                break;
            case CARD_SD:
            case CARD_MMC:
                if (IS_HCD_BUS_MODE_SPI(pHcd)) {
                    *pOCRValueRd = SPI_R3_GET_OCR(pReq->Response); 
                } else {              
                    *pOCRValueRd = SD_R3_GET_OCR(pReq->Response);
                }                    
                break;
            default:
                DBG_ASSERT(FALSE);
                break;
        }          
    }
    return status;  
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  PollCardReady - poll card till it's ready
  Input:  pHcd - HCD object
          OCRValue - OCR value to poll with
          PollType - polling type (based on card type)
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS PollCardReady(PSDHCD pHcd, UINT32 OCRValue, CARD_INFO_FLAGS PollType) 
{
    INT             cardReadyRetry;
    SDIO_STATUS     status;    
    PSDREQUEST      pReq;
   
    if (!((PollType == CARD_SDIO) || (PollType == CARD_SD) || (PollType == CARD_MMC))) {
        DBG_ASSERT(FALSE);
        return SDIO_STATUS_INVALID_PARAMETER;  
    }
       
    pReq = AllocateRequest();    
    if (NULL == pReq) {
        return SDIO_STATUS_NO_RESOURCES;    
    }
    
    status = SDIO_STATUS_SUCCESS;   
    cardReadyRetry = pBusContext->CardReadyPollingRetry;
    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Polling card ready, Using OCR:0x%8.8X, Poll Type:0x%X\n",
                            OCRValue,PollType)); 
           
        /* now issue CMD with the actual OCR as an argument until the card is ready */
    while (cardReadyRetry) {
        if (IS_HCD_BUS_MODE_SPI(pHcd) && !(PollType == CARD_SDIO)) {            
            if (PollType == CARD_MMC) {
                /* under SPI mode for MMC cards, we need to issue CMD1 and
                 * check the response for the "in-idle" bit */
                status = _IssueSimpleBusRequest(pHcd,
                                                CMD1,
                                                0, 
                                                SDREQ_FLAGS_RESP_R1 | SDREQ_FLAGS_RESP_SKIP_SPI_FILT,
                                                pReq);  
            } else if (PollType == CARD_SD) {
                 /* under SPI mode for SD cards, we need to issue ACMD41 and
                 * check the response for the "in-idle" bit */
                 status = _IssueSimpleBusRequest(pHcd,
                                                 ACMD41,
                                                 0, 
                                                 SDREQ_FLAGS_RESP_R1 | 
                                                 SDREQ_FLAGS_APP_CMD |
                                                 SDREQ_FLAGS_RESP_SKIP_SPI_FILT,
                                                 pReq);     
            } else {
                DBG_ASSERT(FALSE);
            }
        } else {
                /* for SD/MMC in native mode and SDIO (all modes) we need to read the OCR register */
                /* read the OCR using the supplied OCR value as an argument, we don't care about the
                  * actual OCR read-back, but we are interested in the response */    
            status = ReadOCR(pHcd,PollType,pReq,OCRValue,NULL);   
        }     
        
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to issue CMD to poll ready \n"));
            break;
        }        
        if (PollType == CARD_SDIO)  { 
            if (IS_HCD_BUS_MODE_SPI(pHcd)) {
                if (SPI_SDIO_R4_IS_CARD_READY(pReq->Response)) {                               
                    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: SDIO Card Ready! (SPI) \n"));
                    break;  
                }       
            } else {
                if (SD_SDIO_R4_IS_CARD_READY(pReq->Response)) {                               
                    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: SDIO Card Ready! \n"));
                    break;  
                }
            } 
        } else if ((PollType == CARD_SD) || (PollType == CARD_MMC)) {
            if (IS_HCD_BUS_MODE_SPI(pHcd)) {
                    /* check response when MMC or SD cards operate in SPI mode */
                if (!(GET_SPI_R1_RESP_TOKEN(pReq->Response) & SPI_CS_STATE_IDLE)) {
                        /* card is no longer in idle */
                    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: SD/MMC Card (SPI mode) is ready! \n"));
                    break;
                }
            } else {
                    /* check the OCR busy bit */
                if (SD_R3_IS_CARD_READY(pReq->Response)) {                               
                    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: SD/MMC (Native Mode) Card Ready! \n"));
                    break;  
                }     
            }
        } else {
            DBG_ASSERT(FALSE);   
        }     
        cardReadyRetry--;
            /* delay */
        status = OSSleep(OCR_READY_CHECK_DELAY_MS);
        if (!SDIO_SUCCESS(status)){
            break;    
        }
    }    
     
    if (0 == cardReadyRetry) {            
        DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Card Ready timeout! \n"));
        status = SDIO_STATUS_DEVICE_ERROR;               
    }
    
    FreeRequest(pReq);
    
    return status;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  AdjustSlotPower - adjust slot power 
  Input:  pHcd - HCD object
  Output: pOCRvalue - ocr value to use
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/     
static SDIO_STATUS AdjustSlotPower(PSDHCD pHcd, UINT32 *pOCRvalue)   
{
    SDCONFIG_POWER_CTRL_DATA    pwrSetting;
    SDIO_STATUS                 status = SDIO_STATUS_SUCCESS;
    
    ZERO_OBJECT(pwrSetting);
    DBG_PRINT(SDDBG_TRACE, 
        ("SDIO Bus Driver: Adjusting Slot Power, Requesting adjustment for OCR:0x%8.8X \n", 
         *pOCRvalue));
    
    do {
        pwrSetting.SlotPowerEnable = TRUE;
            /* get optimal power setting */
        pwrSetting.SlotPowerVoltageMask = GetPowerSetting(pHcd, pOCRvalue);            
        if (0 == pwrSetting.SlotPowerVoltageMask) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: No matching voltage for OCR \n"));
            status = SDIO_STATUS_DEVICE_ERROR;
            break;
        } 
        
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Slot Pwr Mask 0x%X for OCR:0x%8.8X \n",
                                pwrSetting.SlotPowerVoltageMask,*pOCRvalue));
        status = _IssueConfig(pHcd,SDCONFIG_POWER_CTRL,&pwrSetting,sizeof(pwrSetting));
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to set power in hcd \n"));
            break; 
        }       
            /* delay for power to settle */
        OSSleep(pBusContext->PowerSettleDelay); 
            /* save off for drivers */
        pHcd->CardProperties.CardVoltage  = pwrSetting.SlotPowerVoltageMask;
        
    } while (FALSE);
    
    return status;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  ConvertEncodedTransSpeed - convert encoded TRANS_SPEED value to a clock rate
  Input:  TransSpeedValue - encoded transfer speed value
  Output: 
  Return: appropriate SD clock rate
  Notes: This function returns a rate of 0, if it could not be determined.
         This function can check tran speed values for SD,SDIO and MMC cards 
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/ 
static SD_BUSCLOCK_RATE ConvertEncodedTransSpeed(UINT8 TransSpeedValue)
{
    SD_BUSCLOCK_RATE transfMul = 0; 
    UINT8            timeVal = 0;
                
    switch (TransSpeedValue & TRANSFER_UNIT_MULTIPIER_MASK) {
        case 0:
            transfMul = 10000;
            break;
        case 1:
            transfMul = 100000;
            break;   
        case 2:
            transfMul = 1000000;
            break;
        case 3:
            transfMul = 10000000;
            break;
        default:
            transfMul = 0;
            DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: Card transfer multipler is wrong (val=0x%X)! \n",
                                   TransSpeedValue));
            break;
    } 
                
    switch ((TransSpeedValue & TIME_VALUE_MASK) >> TIME_VALUE_SHIFT) {
        case 1: timeVal = 10; break; 
        case 2: timeVal = 12; break; 
        case 3: timeVal = 13; break; 
        case 4: timeVal = 15; break; 
        case 5: timeVal = 20; break; 
        case 6: timeVal = 25; break; 
        case 7: timeVal = 30; break; 
        case 8: timeVal = 35; break; 
        case 9: timeVal = 40; break; 
        case 10: timeVal = 45; break; 
        case 11: timeVal = 50; break; 
        case 12: timeVal = 55; break; 
        case 13: timeVal = 60; break; 
        case 14: timeVal = 70; break; 
        case 15: timeVal = 80; break; 
        default: timeVal = 0; 
        DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: Card time value is wrong (val=0x%X)! \n",
                               TransSpeedValue));
        break;                   
    }  
                
    if ((transfMul != 0) && (timeVal != 0)) {
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Card Reported Max: %d Hz (0x%X) \n",
                                (timeVal*transfMul), TransSpeedValue)); 
        return timeVal*transfMul;
    }
    
    return 0;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SelectDeselectCard - Select or deselect a card
  Input:  pHcd - HCD object
          Select - select the card
  Output: 
  Return: status
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/ 
static SDIO_STATUS SelectDeselectCard(PSDHCD pHcd, BOOL Select)
{
    SDIO_STATUS status;
    
    if (IS_HCD_BUS_MODE_SPI(pHcd)) {
            /* SPI mode cards do not support selection */
        status = SDIO_STATUS_SUCCESS;  
    } else {
        if (!Select) {
                /* deselect, note that deselecting a card does not return a response */
            status = _IssueSimpleBusRequest(pHcd,
                                            CMD7,0,
                                            SDREQ_FLAGS_NO_RESP,NULL);  
        } else {
                /* select */
            status = _IssueSimpleBusRequest(pHcd,
                                            CMD7,(pHcd->CardProperties.RCA << 16),
                                            SDREQ_FLAGS_RESP_R1B,NULL);     
        }  
    }
    
    if (!SDIO_SUCCESS(status)) {
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Failed to %s card, RCA:0x%X Err:%d \n",
            (Select ? "Select":"Deselect"), pHcd->CardProperties.RCA, status));
    }
    return status;
}  

/* reorder a buffer by swapping MSB with LSB */
static void ReorderBuffer(UINT8 *pBuffer, INT Bytes)
{
    UINT8 *pEnd; 
    UINT8 temp;
    
    DBG_ASSERT(!(Bytes & 1));  
        /* point to the end */
    pEnd = &pBuffer[Bytes - 1];
        /* divide in half */
    Bytes = Bytes >> 1;
    
    while (Bytes) {
        temp = *pBuffer;
            /* swap bytes */
        *pBuffer = *pEnd;
        *pEnd = temp;
        pBuffer++;
        pEnd--;
        Bytes--;    
    }
}

#define ADJUST_OPER_CLOCK(pBusMode,Clock) \
    (pBusMode)->ClockRate = min((SD_BUSCLOCK_RATE)(Clock),(pBusMode)->ClockRate)
#define ADJUST_OPER_BLOCK_LEN(pCaps,Length) \
    (pCaps)->OperBlockLenLimit = min((UINT16)(Length),(pCaps)->OperBlockLenLimit)
#define ADJUST_OPER_BLOCK_COUNT(pCaps,Count) \
    (pCaps)->OperBlockCountLimit = min((UINT16)(Count),(pCaps)->OperBlockCountLimit)      
             
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  GetBusParameters - Get bus parameters for a card
  Input:  pHcd - HCD object
          pBusMode - current bus mode on entry
  Output: pBusMode - new adjusted bus mode
  Return: status
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/ 
static SDIO_STATUS GetBusParameters(PSDHCD pHcd, PSDCONFIG_BUS_MODE_DATA pBusMode)
{
    SDIO_STATUS                        status = SDIO_STATUS_SUCCESS;
    UINT8                              temp;
    UINT32                             tplAddr;
    struct SDIO_FUNC_EXT_COMMON_TPL    func0ext;
    UINT8                              scrRegister[SD_SCR_BYTES]; 
    SD_BUSCLOCK_RATE                   cardReportedRate = 0;
    PSDREQUEST                         pReq = NULL;
    BOOL                               spiMode = FALSE;
    

    if (SDCONFIG_GET_BUSWIDTH(pBusMode->BusModeFlags) == SDCONFIG_BUS_WIDTH_SPI) {
        spiMode = TRUE;
    }
    
    if (!spiMode) {
            /* set highest bus mode bus driver is allowing (non-SPI), the code below will
               * adjust to lower or equal settings */
        pBusMode->BusModeFlags = pBusContext->DefaultBusMode;    
    }
        /* set operational parameters */
    pBusMode->ClockRate = pBusContext->DefaultOperClock;
    pHcd->CardProperties.OperBlockLenLimit = pBusContext->DefaultOperBlockLen;
    pHcd->CardProperties.OperBlockCountLimit = pBusContext->DefaultOperBlockCount;
    
        /* adjust operational block counts and length to match HCD */
    ADJUST_OPER_BLOCK_LEN(&pHcd->CardProperties,pHcd->MaxBytesPerBlock);
    ADJUST_OPER_BLOCK_COUNT(&pHcd->CardProperties,pHcd->MaxBlocksPerTrans);    
        /* limit operational clock to the max clock rate */
    ADJUST_OPER_CLOCK(pBusMode,pHcd->MaxClockRate);
    
    if (!spiMode) {
            /* check HCD bus mode */
        if (!(pHcd->Attributes & SDHCD_ATTRIB_BUS_4BIT) || 
            ((pHcd->CardProperties.Flags & CARD_SDIO) && 
             (pHcd->Attributes & SDHCD_ATTRIB_NO_4BIT_IRQ)) ) {
                
            if (pHcd->Attributes & SDHCD_ATTRIB_BUS_4BIT) {
                DBG_PRINT(SDDBG_WARN, 
                ("SDIO Card Detected, but host does not support IRQs in 4 bit mode - dropping to 1 bit. \n"));
            }
                /* force to 1 bit mode */
            SDCONFIG_SET_BUS_WIDTH(pBusMode->BusModeFlags, SDCONFIG_BUS_WIDTH_1_BIT);
        }       
    }
    
        /* now do various card inquiries to drop the bus mode or clock 
         * none of these checks can raise the bus mode or clock higher that what 
         * was initialized above */    
    do {     
        if (pHcd->CardProperties.Flags & (CARD_SD | CARD_MMC)) {
                /* allocate a request for response data we'll need */
            pReq = AllocateRequest();
            if (NULL == pReq) {
                status = SDIO_STATUS_NO_RESOURCES;    
                break;
            }   
        }
        
        if (!spiMode && (pHcd->CardProperties.Flags & CARD_MMC)) {
                /* MMC cards all run in 1 bit mode */
            SDCONFIG_SET_BUS_WIDTH(pBusMode->BusModeFlags, SDCONFIG_BUS_WIDTH_1_BIT);
        }
        
        if (pHcd->CardProperties.Flags & CARD_SD) {
            DBG_ASSERT(pReq != NULL); 
            DBG_PRINT(SDDBG_TRACE, ("Getting SCR from SD Card..\n"));
                /* read SCR (requires data transfer) to get supported modes */ 
            status = _IssueBusRequestBd(pHcd,ACMD51,0,
                                        SDREQ_FLAGS_RESP_R1 | SDREQ_FLAGS_APP_CMD | 
                                        SDREQ_FLAGS_DATA_TRANS,
                                        pReq,&scrRegister,SD_SCR_BYTES);  
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_WARN, ("SD card does not have SCR. \n"));
                if (!spiMode) {
                        /* switch it to 1 bit mode */
                    SDCONFIG_SET_BUS_WIDTH(pBusMode->BusModeFlags, SDCONFIG_BUS_WIDTH_1_BIT);   
                }
                status = SDIO_STATUS_SUCCESS;
            } else {  
                    /* we have to reorder this buffer since the SCR is sent MSB first on the data
                     * data bus */   
                ReorderBuffer(scrRegister,SD_SCR_BYTES);          
                    /* got the SCR */
                DBG_PRINT(SDDBG_TRACE, ("SD SCR StructRev:0x%X, Flags:0x%X \n",
                        GET_SD_SCR_STRUCT_VER(scrRegister), 
                        GET_SD_SCR_BUSWIDTHS_FLAGS(scrRegister)));
                    /* set the revision */
                switch (GET_SD_SCR_SDSPEC_VER(scrRegister)) {
                    case SCR_SD_SPEC_1_00:
                        DBG_PRINT(SDDBG_TRACE, ("SD Spec Revision 1.01 \n"));
                        pHcd->CardProperties.SD_MMC_Revision = SD_REVISION_1_01;    
                        break;
                    case SCR_SD_SPEC_1_10:
                        DBG_PRINT(SDDBG_TRACE, ("SD Spec Revision 1.10 \n"));
                        pHcd->CardProperties.SD_MMC_Revision = SD_REVISION_1_10;
                        break;
                    default:
                        DBG_PRINT(SDDBG_WARN, ("SD Spec Revision is greater than 1.10 \n"));
                        pHcd->CardProperties.SD_MMC_Revision = SD_REVISION_1_10;
                        break;
                }
                
                if (!(GET_SD_SCR_BUSWIDTHS(scrRegister) & SCR_BUS_SUPPORTS_4_BIT)) {  
                    if (!spiMode) {                        
                        DBG_PRINT(SDDBG_WARN, ("SD SCR reports 1bit only Mode \n"));
                            /* switch it to 1 bit mode */
                        SDCONFIG_SET_BUS_WIDTH(pBusMode->BusModeFlags, SDCONFIG_BUS_WIDTH_1_BIT);   
                    }
                }
            }
        }
            
        if (pHcd->CardProperties.Flags & (CARD_SD | CARD_MMC)) {
            DBG_ASSERT(pReq != NULL); 
                /* de-select the card in order to get the CSD */
            status = SelectDeselectCard(pHcd,FALSE);
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to deselect card before getting CSD \n"));
                break;   
            }
                /* Get CSD for SD or MMC cards */        
            if (spiMode) {
                    /* in SPI mode, getting the CSD requires a read data transfer */
                status = _IssueBusRequestBd(pHcd,CMD9,0,
                                            SDREQ_FLAGS_RESP_R1 | SDREQ_FLAGS_DATA_TRANS,
                                            pReq,
                                            pHcd->CardProperties.CardCSD, 
                                            MAX_CSD_CID_BYTES); 
                if (SDIO_SUCCESS(status)) {
                        /* when the CSD is sent over in SPI data mode, it comes to us in MSB first 
                         * and thus is not ordered correctly as defined in the SD spec */
                    ReorderBuffer(pHcd->CardProperties.CardCSD,MAX_CSD_CID_BYTES);                
                } 
            } else {
                status = _IssueSimpleBusRequest(pHcd,
                                                CMD9,
                                                (pHcd->CardProperties.RCA << 16), 
                                                SDREQ_FLAGS_RESP_R2,
                                                pReq); 
                if (SDIO_SUCCESS(status)) {
                        /* save the CSD */
                    memcpy(pHcd->CardProperties.CardCSD,pReq->Response,MAX_CARD_RESPONSE_BYTES);   
                }
            }
            
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to get CSD, Err:%d \n",
                                        status));
                break;      
            }   
                /* for MMC cards, the spec version is in the CSD */
            if (pHcd->CardProperties.Flags & CARD_MMC) {
                DBG_PRINT(SDDBG_TRACE, ("MMC Spec version : (0x%2.2X) \n",
                            GET_MMC_SPEC_VERSION(pHcd->CardProperties.CardCSD)));
                switch (GET_MMC_SPEC_VERSION(pHcd->CardProperties.CardCSD)) {
                    case MMC_SPEC_1_0_TO_1_2:
                    case MMC_SPEC_1_4:
                    case MMC_SPEC_2_0_TO_2_2:
                        DBG_PRINT(SDDBG_WARN, ("MMC Spec version less than 3.1 \n"));
                        pHcd->CardProperties.SD_MMC_Revision = MMC_REVISION_1_0_2_2;
                        break;
                    case MMC_SPEC_3_1:
                        DBG_PRINT(SDDBG_TRACE, ("MMC Spec version 3.1 \n")); 
                        pHcd->CardProperties.SD_MMC_Revision = MMC_REVISION_3_1; 
                        break;  
                    case MMC_SPEC_4_0_TO_4_1:
                        DBG_PRINT(SDDBG_TRACE, ("MMC Spec version 4.0-4.1 \n")); 
                        pHcd->CardProperties.SD_MMC_Revision = MMC_REVISION_4_0; 
                        break;  
                    default:
                        pHcd->CardProperties.SD_MMC_Revision = MMC_REVISION_3_1; 
                        DBG_PRINT(SDDBG_WARN, ("MMC Spec version greater than 4.1\n"));
                        break;
                }
            }                       
                /* re-select the card  */
            status = SelectDeselectCard(pHcd,TRUE);
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to re-select card after getting CSD \n"));
                break;   
            }
        } 
      
        if ((pHcd->CardProperties.Flags & CARD_SD) && 
            !(pHcd->CardProperties.Flags & CARD_SDIO) &&
             SDDEVICE_IS_SD_REV_GTEQ_1_10(pHcd->pPseudoDev) &&
             (pHcd->Attributes & SDHCD_ATTRIB_SD_HIGH_SPEED) && 
             !spiMode)  {
            UINT32 arg; 
            PUINT8 pSwitchStatusBlock = KernelAlloc(SD_SWITCH_FUNC_STATUS_BLOCK_BYTES);
            
            if (NULL == pSwitchStatusBlock) {
                status = SDIO_STATUS_NO_RESOURCES;  
                break; 
            }
            
            arg = SD_SWITCH_FUNC_ARG_GROUP_CHECK(SD_SWITCH_HIGH_SPEED_GROUP,
                                                 SD_SWITCH_HIGH_SPEED_FUNC_NO);
                                               
                /* for 1.10 SD cards, check if high speed mode is supported */      
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Checking SD Card for switchable functions (CMD6 arg:0x%X)\n",arg));                
               
                /* issue simple data transfer request to read the switch status */
            status = _IssueBusRequestBd(pHcd,
                                        CMD6,
                                        arg,
                                        SDREQ_FLAGS_RESP_R1 | SDREQ_FLAGS_DATA_TRANS,
                                        pReq,
                                        pSwitchStatusBlock, 
                                        SD_SWITCH_FUNC_STATUS_BLOCK_BYTES);
                                            
            if (SDIO_SUCCESS(status)) { 
                UINT16 switchGroupMask;
                    /* need to reorder this since cards send this MSB first */
                ReorderBuffer(pSwitchStatusBlock,SD_SWITCH_FUNC_STATUS_BLOCK_BYTES); 
                switchGroupMask = SD_SWITCH_FUNC_STATUS_GET_GRP_BIT_MASK(pSwitchStatusBlock,SD_SWITCH_HIGH_SPEED_GROUP);
                DBG_PRINT(SDDBG_TRACE, ("SD Card Switch Status Group1 Mask:0x%X Max Current:%d\n",
                        switchGroupMask, SD_SWITCH_FUNC_STATUS_GET_MAX_CURRENT(pSwitchStatusBlock) )); 
                if (SD_SWITCH_FUNC_STATUS_GET_MAX_CURRENT(pSwitchStatusBlock) == 0) {
                    DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: SD Switch Status block has zero max current \n"));
                    SDLIB_PrintBuffer(pSwitchStatusBlock,
                                      SD_SWITCH_FUNC_STATUS_BLOCK_BYTES, 
                                      "SDIO Bus Driver: SD Switch Status Block Error");      
                } else {              
                        /* check HS support */
                    if (switchGroupMask & (1 << SD_SWITCH_HIGH_SPEED_FUNC_NO)) {
                        DBG_PRINT(SDDBG_TRACE, ("SD Card Supports High Speed Mode\n"));
                            /* set the rate, this will override the CSD value */
                        cardReportedRate = SD_HS_MAX_BUS_CLOCK;                  
                        pBusMode->BusModeFlags |= SDCONFIG_BUS_MODE_SD_HS;
                    } 
                }             
            } else {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to get SD Switch Status block (%d)\n", status));
                    /* just fall through, we'll handle this like a normal SD card */               
                status = SDIO_STATUS_SUCCESS;      
            }        
            
            KernelFree(pSwitchStatusBlock); 
        }
               
        if ((pHcd->CardProperties.Flags & CARD_MMC) && 
             SDDEVICE_IS_MMC_REV_GTEQ_4_0(pHcd->pPseudoDev) &&
             (pHcd->Attributes & SDHCD_ATTRIB_MMC_HIGH_SPEED) &&
             !spiMode)  { 
                /* for MMC cards, get the Extended CSD to get the High speed and
                 * wide bus paramaters */
                  
            PUINT8 pExtData = KernelAlloc(MMC_EXT_CSD_SIZE);
            
            if (NULL == pExtData) {
                status = SDIO_STATUS_NO_RESOURCES;  
                break; 
            }
                /* issue simple data transfer request to read the extended CSD */
            status = _IssueBusRequestBd(pHcd,MMC_CMD8,0,
                                        SDREQ_FLAGS_RESP_R1 | SDREQ_FLAGS_DATA_TRANS,
                                        pReq,
                                        pExtData, 
                                        MMC_EXT_CSD_SIZE);    
            if (SDIO_SUCCESS(status)) {
                 DBG_PRINT(SDDBG_TRACE, ("MMC Ext CSD Version: 0x%X Card Type: 0x%X\n",
                        pExtData[MMC_EXT_VER_OFFSET],pExtData[MMC_EXT_CARD_TYPE_OFFSET]));                
                    /* check HS support */
                if (pExtData[MMC_EXT_CARD_TYPE_OFFSET] & MMC_EXT_CARD_TYPE_HS_52) {
                        /* try 52 Mhz */
                    cardReportedRate = 52000000;
                    pBusMode->BusModeFlags |= SDCONFIG_BUS_MODE_MMC_HS;
                } else if (pExtData[MMC_EXT_CARD_TYPE_OFFSET] & MMC_EXT_CARD_TYPE_HS_26) {
                        /* try 26MHZ */ 
                    cardReportedRate = 26000000; 
                    pBusMode->BusModeFlags |= SDCONFIG_BUS_MODE_MMC_HS;
                } else {
                        /* doesn't report high speed capable */
                    cardReportedRate = 0;   
                }               
                
                if (cardReportedRate && !spiMode) {
                        /* figure out the bus mode */
                    if (pHcd->Attributes & SDHCD_ATTRIB_BUS_MMC8BIT) {
                        SDCONFIG_SET_BUS_WIDTH(pBusMode->BusModeFlags, SDCONFIG_BUS_WIDTH_MMC8_BIT);
                    } else if (pHcd->Attributes & SDHCD_ATTRIB_BUS_4BIT) {
                        SDCONFIG_SET_BUS_WIDTH(pBusMode->BusModeFlags, SDCONFIG_BUS_WIDTH_4_BIT); 
                    } else { 
                        /* we leave it to default to 1 bit mode */   
                    }
                }      
            } else {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to get MMC Extended CSD \n"));
                    /* just fall through, we'll do without the extended information 
                     * and run it like a legacy MMC card */               
                status = SDIO_STATUS_SUCCESS;      
            }        
            
            KernelFree(pExtData);      
        }
        
        if (pHcd->CardProperties.Flags & (CARD_SD | CARD_MMC)) {
           
            if (0 == cardReportedRate) {
                    /* extract rate from CSD only if it was not set by earlier tests */
                cardReportedRate = ConvertEncodedTransSpeed(
                                GET_SD_CSD_TRANS_SPEED(pHcd->CardProperties.CardCSD)); 
                    /* fall through and test for zero again */
            }                  
        
            if (cardReportedRate != 0) {
                     /* adjust clock based on what the card can handle */
                ADJUST_OPER_CLOCK(pBusMode,cardReportedRate);
            } else { 
                    /* something is wrong with the CSD */ 
                if (DBG_GET_DEBUG_LEVEL() >= SDDBG_TRACE) { 
                    SDLIB_PrintBuffer(pHcd->CardProperties.CardCSD,
                                      MAX_CARD_RESPONSE_BYTES,
                                      "SDIO Bus Driver: CSD Dump");  
                }   
                    /* can't figure out the card rate, so set reasonable defaults */
                if (pHcd->CardProperties.Flags & CARD_SD) {
                    ADJUST_OPER_CLOCK(pBusMode,SD_MAX_BUS_CLOCK);           
                } else {
                    ADJUST_OPER_CLOCK(pBusMode,MMC_MAX_BUS_CLOCK);   
                } 
            } 
        }
                                                                 
            /* note, we do SDIO card "after" SD in case this is a combo card */       
        if (pHcd->CardProperties.Flags & CARD_SDIO) {                       
                /* read card capabilities */
            status = Cmd52ReadByteCommon(pHcd->pPseudoDev, 
                                         SDIO_CARD_CAPS_REG, 
                                         &pHcd->CardProperties.SDIOCaps); 
            if (!SDIO_SUCCESS(status)) {
                break;   
            } 
            DBG_PRINT(SDDBG_TRACE, ("SDIO Card Caps: 0x%X \n",pHcd->CardProperties.SDIOCaps));
            if (pHcd->CardProperties.SDIOCaps & SDIO_CAPS_LOW_SPEED) {
                    /* adjust max clock for LS device */
                ADJUST_OPER_CLOCK(pBusMode,SDIO_LOW_SPEED_MAX_BUS_CLOCK);  
                    /* adjust bus if LS device does not support 4 bit mode */      
                if (!(pHcd->CardProperties.SDIOCaps & SDIO_CAPS_4BIT_LS)) {
                    if (!spiMode) {
                            /* low speed device does not support 4 bit mode, force us to 1 bit */  
                        SDCONFIG_SET_BUS_WIDTH(pBusMode->BusModeFlags, 
                                               SDCONFIG_BUS_WIDTH_1_BIT);   
                    }
                }            
            } 
        
                /* check if 1.2 card supports high speed mode, checking HCD as well*/
            if (SDDEVICE_IS_SDIO_REV_GTEQ_1_20(pHcd->pPseudoDev) && 
                (pHcd->Attributes & SDHCD_ATTRIB_SD_HIGH_SPEED) &&
                !spiMode) {
                UCHAR hsControl = 0;
                
                status = Cmd52ReadByteCommon(pHcd->pPseudoDev, 
                                             SDIO_HS_CONTROL_REG, 
                                             &hsControl); 
                                            
                if (!SDIO_SUCCESS(status)) {
                    DBG_PRINT(SDDBG_TRACE, 
                        ("SDIO Failed to read high speed control (%d) \n",status)); 
                        /* reset status and continue */  
                    status = SDIO_STATUS_SUCCESS;
                } else {
                    if (hsControl & SDIO_HS_CONTROL_SHS) {
                        DBG_PRINT(SDDBG_TRACE, ("SDIO Card Supports High Speed Mode\n"));
                        pBusMode->BusModeFlags |= SDCONFIG_BUS_MODE_SD_HS;  
                        if (!(pBusContext->ConfigFlags & BD_CONFIG_SDIO_HS_NO_CLK_LMT)) {
                                /* check for a busdriver limit on the clock rate */
                            if (pBusMode->ClockRate < SD_HS_MAX_BUS_CLOCK) {
                                DBG_PRINT(SDDBG_TRACE, 
                                   ("SDIO bus clock limit is : %d hz, backing off HS SDIO mode \n", 
                                   pBusMode->ClockRate));
                                pBusMode->BusModeFlags &= ~SDCONFIG_BUS_MODE_SD_HS;    
                            }    
                        }
                    }                   
                } 
            
            } 
        
            cardReportedRate = 0;
            temp = sizeof(func0ext);
            tplAddr = pHcd->CardProperties.CommonCISPtr;                                  
                /* get the FUNCE tuple */
            status = SDLIB_FindTuple(pHcd->pPseudoDev,
                                     CISTPL_FUNCE,
                                     &tplAddr, 
                                     (PUINT8)&func0ext,
                                     &temp);
            if (!SDIO_SUCCESS(status) || (temp < sizeof(func0ext))) {
                DBG_PRINT(SDDBG_WARN, ("SDIO Function 0 Ext. Tuple Missing (Got size:%d) \n", temp)); 
                    /* reset status */
                status = SDIO_STATUS_SUCCESS;
            } else {
                    /* convert encoded value to rate */
                cardReportedRate = ConvertEncodedTransSpeed(func0ext.MaxTransSpeed);    
            }
            
            if (cardReportedRate != 0) {
                if (pBusMode->BusModeFlags & SDCONFIG_BUS_MODE_SD_HS) {
                    if (cardReportedRate <= SD_MAX_BUS_CLOCK) {
                        DBG_PRINT(SDDBG_WARN, 
                            ("SDIO Function tuple reports clock:%d Hz, with advertised High Speed support \n", 
                             cardReportedRate));                                  
                        if (!(pBusContext->ConfigFlags & BD_CONFIG_SDIO_HS_NO_CLK_LMT)) {
                                /* back off high speed support */
                            pBusMode->BusModeFlags &= ~SDCONFIG_BUS_MODE_SD_HS;
                        }
                    }
                } else {
                    if (cardReportedRate > SD_MAX_BUS_CLOCK) {
                        DBG_PRINT(SDDBG_WARN, 
                            ("SDIO Function tuple reports clock:%d Hz, without advertising High Speed support..using 25Mhz \n", cardReportedRate));      
                        cardReportedRate = SD_MAX_BUS_CLOCK;   
                    }     
                }
                    /* adjust clock based on what the card can handle */
                ADJUST_OPER_CLOCK(pBusMode,cardReportedRate);
                
            } else {
                    /* set a reasonable default */ 
                ADJUST_OPER_CLOCK(pBusMode,SD_MAX_BUS_CLOCK);
            }    
        }
    } while (FALSE); 

    if (pReq != NULL) {
        FreeRequest(pReq);    
    }
    return status;  
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SetOperationalBusMode - set operational bus mode
  Input:  pDevice - pDevice that is requesting the change
          pBusMode - operational bus mode
  Output: pBusMode - on return will have the actual clock rate set
  Return: status
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS SetOperationalBusMode(PSDDEVICE                pDevice, 
                                  PSDCONFIG_BUS_MODE_DATA  pBusMode) 
{
    SDIO_STATUS     status = SDIO_STATUS_SUCCESS;
    UCHAR           regData;
    UINT32          arg;
    UINT32          switcharg;
    PSDHCD          pHcd = pDevice->pHcd;   
    
        /* synchronize access for updating bus mode settings */                    
    status = SemaphorePendInterruptable(&pDevice->pHcd->ConfigureOpsSem);
    if (!SDIO_SUCCESS(status)) {
        return status;
    }
                            
    do { 
        
        if (!IS_CARD_PRESENT(pHcd)) {
                /* for an empty slot (a Pseudo dev was passed in) we still allow the 
                 * bus mode to be set for the card detect 
                 * polling */
            status = _IssueConfig(pHcd,SDCONFIG_BUS_MODE_CTRL,pBusMode,sizeof(SDCONFIG_BUS_MODE_DATA));
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to set bus mode in hcd : Err:%d \n",
                                        status));
            }              
                /* nothing more to do */
            break; 
        }       
        
        
        if ((pBusMode->BusModeFlags == SDDEVICE_GET_BUSMODE_FLAGS(pDevice)) &&
            (pBusMode->ClockRate == SDDEVICE_GET_OPER_CLOCK(pDevice))) {
            DBG_PRINT(SDDBG_TRACE, 
               ("SDIO Bus Driver: Bus mode already set, nothing to do\n"));
            pBusMode->ActualClockRate = SDDEVICE_GET_OPER_CLOCK(pDevice);
            break;
        }
            
        if (pBusMode->BusModeFlags & SDCONFIG_BUS_MODE_MMC_HS) {
            if (!(pHcd->Attributes & SDHCD_ATTRIB_MMC_HIGH_SPEED)) {
                status = SDIO_STATUS_INVALID_PARAMETER;
                DBG_PRINT(SDDBG_ERROR, 
                        ("SDIO Bus Driver: HCD does not support MMC High Speed\n"));
                break;    
            }  
        }
        
        if (pBusMode->BusModeFlags & SDCONFIG_BUS_MODE_SD_HS) {
            if (!(pHcd->Attributes & SDHCD_ATTRIB_SD_HIGH_SPEED)) {
                status = SDIO_STATUS_INVALID_PARAMETER;
                DBG_PRINT(SDDBG_ERROR, 
                        ("SDIO Bus Driver: HCD does not support SD High Speed\n"));
                break;    
            }     
        }
                
            /* before we set the operational clock and mode, configure the clock for high
             * speed mode on the card , if necessary */
        if ((pHcd->CardProperties.Flags & CARD_MMC) &&
            (pBusMode->BusModeFlags & SDCONFIG_BUS_MODE_MMC_HS) &&
            !(SDDEVICE_GET_BUSMODE_FLAGS(pDevice) & SDCONFIG_BUS_MODE_MMC_HS)) {
              
            switcharg = MMC_SWITCH_BUILD_ARG(MMC_SWITCH_CMD_SET0,
                                             MMC_SWITCH_WRITE_BYTE,
                                             MMC_EXT_HS_TIMING_OFFSET,
                                             MMC_EXT_HS_TIMING_ENABLE); 
            status = _IssueSimpleBusRequest(pHcd,
                                            MMC_CMD_SWITCH,
                                            switcharg,
                                            SDREQ_FLAGS_RESP_R1B,
                                            NULL);     
            if (!SDIO_SUCCESS(status)) { 
                DBG_PRINT(SDDBG_ERROR, 
                 ("SDIO Bus Driver: Failed to switch MMC High Speed Mode (arg:0x%X): %d \n",
                                        switcharg, status));
                break;       
            } 
            
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: High Speed MMC enabled (arg:0x%X)\n",
                switcharg));   
        }
        
            /* before setting bus mode and clock in the HCD, switch card to high speed mode 
             * if necessary */
        if ((pHcd->CardProperties.Flags & CARD_SD) &&
            (pBusMode->BusModeFlags & SDCONFIG_BUS_MODE_SD_HS) &&
            !(SDDEVICE_GET_BUSMODE_FLAGS(pDevice) & SDCONFIG_BUS_MODE_SD_HS)) {
            PUINT8     pSwitchStatusBlock;  
                        
            pSwitchStatusBlock = KernelAlloc(SD_SWITCH_FUNC_STATUS_BLOCK_BYTES);
            
            if (NULL == pSwitchStatusBlock) {
                status = SDIO_STATUS_NO_RESOURCES;  
                break; 
            }
            
                /* set high speed group */
            arg = SD_SWITCH_FUNC_ARG_GROUP_SET(SD_SWITCH_HIGH_SPEED_GROUP,
                                               SD_SWITCH_HIGH_SPEED_FUNC_NO);
                                                  
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Setting SD Card for High Speed mode (CMD6 arg:0x%X)\n",arg));                
               
                /* issue simple data transfer request to switch modes */
            status = _IssueBusRequestBd(pHcd,
                                        CMD6,
                                        arg,
                                        SDREQ_FLAGS_RESP_R1 | SDREQ_FLAGS_DATA_TRANS,
                                        NULL,
                                        pSwitchStatusBlock, 
                                        SD_SWITCH_FUNC_STATUS_BLOCK_BYTES);
                                            
            if (SDIO_SUCCESS(status)) {
                ReorderBuffer(pSwitchStatusBlock,SD_SWITCH_FUNC_STATUS_BLOCK_BYTES);  
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: SD High Speed Result, Got Max Current:%d mA, SwitchResult:0x%X \n",
                      SD_SWITCH_FUNC_STATUS_GET_MAX_CURRENT(pSwitchStatusBlock),
                      SDSwitchGetSwitchResult(pSwitchStatusBlock, SD_SWITCH_HIGH_SPEED_GROUP)));  
                if (SD_SWITCH_FUNC_STATUS_GET_MAX_CURRENT(pSwitchStatusBlock) == 0) {
                    DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Error in Status Block after High Speed Switch (current==0) \n"));    
                    status = SDIO_STATUS_DEVICE_ERROR;
                }                
                if (SDSwitchGetSwitchResult(pSwitchStatusBlock, SD_SWITCH_HIGH_SPEED_GROUP) !=
                    SD_SWITCH_HIGH_SPEED_FUNC_NO) {
                    DBG_PRINT(SDDBG_ERROR, 
                        ("SDIO Bus Driver: Error in Status Block after High Speed Switch (Group1 did not switch) \n"));    
                    status = SDIO_STATUS_DEVICE_ERROR;        
                }                
                if (SDIO_SUCCESS(status)) {
                    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: SD High Speed Mode Enabled \n"));   
                } else {
                    SDLIB_PrintBuffer(pSwitchStatusBlock,
                                      SD_SWITCH_FUNC_STATUS_BLOCK_BYTES, 
                                       "SDIO Bus Driver: SD Switch Status Block Error"); 
                } 
            } else {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to Set SD High Speed Mode (%d) \n",status));    
            }
            KernelFree(pSwitchStatusBlock);   
 
            if (!SDIO_SUCCESS(status)) {
                break;   
            }
        }
        
            /* enable/disable high speed mode for SDIO card */
        if (pHcd->CardProperties.Flags & CARD_SDIO) {
            BOOL doSet = TRUE;
            
            if ((pBusMode->BusModeFlags & SDCONFIG_BUS_MODE_SD_HS) &&
                !(SDDEVICE_GET_BUSMODE_FLAGS(pDevice) & SDCONFIG_BUS_MODE_SD_HS)) {                
                    /* enable */
                regData = SDIO_HS_CONTROL_EHS;
            } else if (!(pBusMode->BusModeFlags & SDCONFIG_BUS_MODE_SD_HS) &&
                       (SDDEVICE_GET_BUSMODE_FLAGS(pDevice) & SDCONFIG_BUS_MODE_SD_HS)) {
                    /* disable */
                regData = 0;            
            } else {
                    /* do nothing */
                doSet = FALSE;        
            }
            
            if (doSet) {
                status = Cmd52WriteByteCommon(pDevice,
                                              SDIO_HS_CONTROL_REG,
                                              &regData); 
                                                           
                if (!SDIO_SUCCESS(status)) {
                    DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to %s HS mode in SDIO card : Err:%d\n",
                                            (SDIO_HS_CONTROL_EHS == regData) ? "enable":"disable" , status));
                    break;
                } else {
                    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver:SDIO Card %s for High Speed mode \n",
                                    (SDIO_HS_CONTROL_EHS == regData) ? "enabled":"disabled" ));                
                } 
            }  
            
            if (pBusMode->BusModeFlags & SDCONFIG_BUS_MODE_SD_HS) {
                    /* check if we should bypass HS mode on the host, on certain prototyping systems
                     * we can switch the card-side to HS mode but keep the host side in normal mode,
                     * to change sample edge */
                if (pBusContext->ConfigFlags & BD_CONFIG_SDIO_HS_NO_HOST_CHNG) {
                    pBusMode->BusModeFlags &= ~SDCONFIG_BUS_MODE_SD_HS;
                    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Host Controller HS mode switch bypassed!\n"));    
                }        
            }
                
                
        }
            
            /* use synchronize-with-bus request version, this may have been requested by a
             * function driver */
        status = SDLIB_IssueConfig(pDevice,
                                   SDCONFIG_BUS_MODE_CTRL,
                                   pBusMode,
                                   sizeof(SDCONFIG_BUS_MODE_DATA));
       
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to set bus mode in hcd : Err:%d \n",
                                    status));
            break;  
        }
        
             /* check requested bus width against the current mode */
        if (SDCONFIG_GET_BUSWIDTH(pBusMode->BusModeFlags) == 
                SDCONFIG_GET_BUSWIDTH(pHcd->CardProperties.BusMode)) {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Bus mode set, no width change\n"));
            break;
        }
             
        if (SDCONFIG_GET_BUSWIDTH(pBusMode->BusModeFlags) == SDCONFIG_BUS_WIDTH_SPI) {
                /* nothing more to do for SPI */
            break; 
        }
        
            /* set the bus width for SD and combo cards */        
        if (pHcd->CardProperties.Flags & CARD_SD) {
            if (SDCONFIG_GET_BUSWIDTH(pBusMode->BusModeFlags) == SDCONFIG_BUS_WIDTH_4_BIT) { 
                    /* turn off card detect resistor */
                status = _IssueSimpleBusRequest(pHcd,
                                                ACMD42,
                                                0, /* disable CD */
                                                SDREQ_FLAGS_APP_CMD | SDREQ_FLAGS_RESP_R1,
                                                NULL); 
                if (!SDIO_SUCCESS(status)) {
                    DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: Failed to disable CD Res: %d \n",
                                           status)); /* this should be okay */
                }
                arg = SD_ACMD6_BUS_WIDTH_4_BIT;     
            } else {
                    /* don't need to turn off CD in 1 bit mode, just set mode */
                arg = SD_ACMD6_BUS_WIDTH_1_BIT;  
               
            }
                /* set the bus width */
            status = _IssueSimpleBusRequest(pHcd,
                                            ACMD6,
                                            arg, /* set bus mode */
                                            SDREQ_FLAGS_APP_CMD | SDREQ_FLAGS_RESP_R1,
                                            NULL);    
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to set bus width: %d \n",
                                        status));
                break;       
            }                                   
        }
            /* set bus width for SDIO cards */
        if (pHcd->CardProperties.Flags & CARD_SDIO) {
                /* default */
            regData = CARD_DETECT_DISABLE | SDIO_BUS_WIDTH_1_BIT;     
              
            if (SDCONFIG_GET_BUSWIDTH(pBusMode->BusModeFlags) == SDCONFIG_BUS_WIDTH_4_BIT) { 
                    /* turn off card detect resistor and set buswidth */
                regData = CARD_DETECT_DISABLE | SDIO_BUS_WIDTH_4_BIT;
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Enabling 4 bit mode on card \n"));
            } else {
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Enabling 1 bit mode on card \n"));
            }           
            status = Cmd52WriteByteCommon(pDevice, 
                                          SDIO_BUS_IF_REG, 
                                          &regData);              
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to set bus mode in Card : Err:%d\n",
                                        status));
                break;   
            }
            
                /* check for 4-bit interrupt detect mode */
            if ((SDCONFIG_GET_BUSWIDTH(pBusMode->BusModeFlags) == SDCONFIG_BUS_WIDTH_4_BIT) &&
                (pHcd->CardProperties.SDIOCaps & SDIO_CAPS_INT_MULTI_BLK) &&
                (pHcd->Attributes & SDHCD_ATTRIB_MULTI_BLK_IRQ)) { 
                    /* enable interrupts between blocks, this doesn't actually turn on interrupts
                     * it merely allows interrupts to be asserted in the inter-block gap */ 
                pHcd->CardProperties.SDIOCaps |= SDIO_CAPS_ENB_INT_MULTI_BLK;    
                
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: 4-Bit Multi-blk Interrupt support enabled\n"));   
            } else {
                    /* make sure this is disabled */
                pHcd->CardProperties.SDIOCaps &= ~SDIO_CAPS_ENB_INT_MULTI_BLK; 
            }     
                        
            status = Cmd52WriteByteCommon(pDevice, 
                                          SDIO_CARD_CAPS_REG, 
                                          &pHcd->CardProperties.SDIOCaps);              
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to update Card Caps register Err:%d\n",
                                        status));
                break;   
            }                       
        }
        
            /* set data bus width for MMC */
        if (pHcd->CardProperties.Flags & CARD_MMC) {
            UINT8  buswidth = 0;
            
            if (SDCONFIG_GET_BUSWIDTH(pBusMode->BusModeFlags) == SDCONFIG_BUS_WIDTH_4_BIT) {
                buswidth = MMC_EXT_BUS_WIDTH_4_BIT;   
            } else if (SDCONFIG_GET_BUSWIDTH(pBusMode->BusModeFlags) == SDCONFIG_BUS_WIDTH_MMC8_BIT) {
                buswidth = MMC_EXT_BUS_WIDTH_8_BIT;   
            } else {
                /* normal 1 bit mode .. nothing to do */ 
                break;  
            }            
                /* now set the bus mode on the card */
            switcharg = MMC_SWITCH_BUILD_ARG(MMC_SWITCH_CMD_SET0,
                                             MMC_SWITCH_WRITE_BYTE,
                                             MMC_EXT_BUS_WIDTH_OFFSET,
                                             buswidth);
            
            status = _IssueSimpleBusRequest(pHcd,
                                            MMC_CMD_SWITCH,
                                            switcharg,
                                            SDREQ_FLAGS_RESP_R1B,
                                            NULL);     
            if (!SDIO_SUCCESS(status)) { 
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to set MMC bus width (arg:0x%X): %d \n",
                                        switcharg, status));
                break;       
            } 
            
            if (SDCONFIG_GET_BUSWIDTH(pBusMode->BusModeFlags) == SDCONFIG_BUS_WIDTH_4_BIT) {
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: 4 bit MMC mode enabled (arg:0x%X) \n",
                      switcharg));   
            } else if (SDCONFIG_GET_BUSWIDTH(pBusMode->BusModeFlags) == SDCONFIG_BUS_WIDTH_MMC8_BIT) {  
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: 8-Bit MMC mode enabled (arg:0x%X) \n",
                      switcharg));  
            }     
        }
            
    } while (FALSE);      
    
    if (SDIO_SUCCESS(status)) {
            /* set the operating mode */
        pHcd->CardProperties.BusMode = pBusMode->BusModeFlags;   
            /* set the actual clock rate */
        pHcd->CardProperties.OperBusClock = pBusMode->ActualClockRate;
    }        
    
    SemaphorePost(&pDevice->pHcd->ConfigureOpsSem); 
                          
    return status;     
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CardInitSetup - setup host for card initialization
  Input:  pHcd - HCD object
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS CardInitSetup(PSDHCD pHcd) 
{
    SDCONFIG_INIT_CLOCKS_DATA   initClocks;  
    SDCONFIG_BUS_MODE_DATA      busMode;
    UINT32                      OCRvalue;
    SDIO_STATUS                 status = SDIO_STATUS_SUCCESS;
    
    ZERO_OBJECT(initClocks);
    ZERO_OBJECT(busMode);
        /* setup defaults */
    initClocks.NumberOfClocks = SDMMC_MIN_INIT_CLOCKS;  
    busMode.ClockRate = SD_INIT_BUS_CLOCK;
    
        /* check for SPI only */
    if (pHcd->Attributes & SDHCD_ATTRIB_BUS_SPI) {
            /* SPI cards startup in non-CRC mode with the exception of CMD0, the
             * HCDs must issue CMD0 with the correct CRC , the spec shows that a
             * CMD 0 sequence is 0x40,0x00,0x00,0x00,0x00,0x95 */
        busMode.BusModeFlags = SDCONFIG_BUS_WIDTH_SPI | SDCONFIG_BUS_MODE_SPI_NO_CRC;  
    }
        /* check if host supports 1 bit mode */  
        /* TODO : if host supports power switching, we can 
         * could initialize cards in SPI mode first */  
    if (pHcd->Attributes & SDHCD_ATTRIB_BUS_1BIT) {
        busMode.BusModeFlags = SDCONFIG_BUS_WIDTH_1_BIT; 
    }
    
        /* set initial VDD, starting at the highest allowable voltage and working
         * our way down */
    if (pHcd->SlotVoltageCaps & SLOT_POWER_3_3V) {
        OCRvalue = SD_OCR_3_2_TO_3_3_VDD;
    } else if (pHcd->SlotVoltageCaps & SLOT_POWER_3_0V) {
        OCRvalue = SD_OCR_2_9_TO_3_0_VDD;    
    } else if (pHcd->SlotVoltageCaps & SLOT_POWER_2_8V) {
        OCRvalue = SD_OCR_2_7_TO_2_8_VDD;    
    } else if (pHcd->SlotVoltageCaps & SLOT_POWER_2_0V) {
        OCRvalue = SD_OCR_1_9_TO_2_0_VDD;    
    } else if (pHcd->SlotVoltageCaps & SLOT_POWER_1_8V) {
        OCRvalue = SD_OCR_1_7_TO_1_8_VDD;    
    } else if (pHcd->SlotVoltageCaps & SLOT_POWER_1_6V) {
        OCRvalue = SD_OCR_1_6_TO_1_7_VDD;    
    } else {
        DBG_ASSERT(FALSE);
        OCRvalue = 0;  
    }
    
    do {            
            /* power up the card */
        status = AdjustSlotPower(pHcd, &OCRvalue);
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to adjust slot power \n"));
            break;  
        }  
        status = SetOperationalBusMode(pHcd->pPseudoDev,&busMode);
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to set bus mode \n"));
            break;  
        }
        status = _IssueConfig(pHcd,SDCONFIG_SEND_INIT_CLOCKS,&initClocks,sizeof(initClocks));
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to send init clocks in hcd \n"));
            break;  
        } 
        
    } while(FALSE);
   
    return status;       
}    
     
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SDInitializeCard - initialize card
  Input:  pHcd - HCD object
  Output: pProperties - card properties
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS SDInitializeCard(PSDHCD pHcd) 
{
    SDCONFIG_BUS_MODE_DATA      busMode;
    SDIO_STATUS                 status = SDIO_STATUS_SUCCESS;
    PSDREQUEST                  pReq = NULL;
    UINT32                      OCRvalue;
    UINT32                      tplAddr;
    UINT8                       temp;
    struct SDIO_MANFID_TPL      manfid;
    SDCONFIG_WP_VALUE           wpValue;
    UINT8                       cisBuffer[3];
                     
    OCRvalue = 0;
    
    do {
        if (IS_HCD_BUS_MODE_SPI(pHcd)) {  
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Initializing card in SPI mode \n"));
        } else {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Initializing card in MMC/SD mode \n"));
        }  
        
        pReq = AllocateRequest();    
        if (NULL == pReq) {
            status = SDIO_STATUS_NO_RESOURCES;
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: failed to allocate bus request \n"));
            break;   
        }
        memset(pReq, 0, sizeof(SDREQUEST));
                
        status = CardInitSetup(pHcd);
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to setup card \n")); 
            break;     
        }
        status = _IssueConfig(pHcd,SDCONFIG_GET_WP,&wpValue,sizeof(wpValue));
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: host doesn't support Write Protect \n"));
        } else {
            if (wpValue) {
                pHcd->CardProperties.Flags |= CARD_SD_WP; 
                DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: SD WP switch is on \n"));
            } 
        }   
        
        if (!(pHcd->Attributes & SDHCD_ATTRIB_SLOT_POLLING) && 
            IS_HCD_BUS_MODE_SPI(pHcd)) {
                /* for non-slot polling HCDs operating in SPI mode
                 * issue CMD0 to reset card state and to place the card
                 * in SPI mode.  If slot polling is used, the polling thread
                 * will have already issued a CMD0 to place the card in SPI mode*/
            if (IS_HCD_BUS_MODE_SPI(pHcd)) {
                INT ii = 256;
                status = SDIO_STATUS_ERROR;
                /* if the CMD0 fails, retry it. Some cards have a hard time getting into SPI mode.*/
                while ((!SDIO_SUCCESS(status)) && (ii-- >= 0)) {
                    status = _IssueSimpleBusRequest(pHcd,CMD0,0,SDREQ_FLAGS_RESP_R1,pReq);
                    OSSleep(20);
                }
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: cmd0 go SPI retries:(256) %d\n", ii)); 

            } else {
                status = _IssueSimpleBusRequest(pHcd,CMD0,0,SDREQ_FLAGS_NO_RESP,pReq);  
            }
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: go-idle failed! \n"));
                break;  
            } 
        } 
                
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Looking for SDIO.. \n"));
            /* check for SDIO card by trying to read it's OCR */
        status = ReadOCR(pHcd,CARD_SDIO,pReq,0,&OCRvalue);                                               
        if (SDIO_SUCCESS(status)) {
                /* we got a response, this is an SDIO card */ 
            if (IS_HCD_BUS_MODE_SPI(pHcd)) {                    
                    /* handle SPI */     
                pHcd->CardProperties.IOFnCount = SPI_SDIO_R4_GET_IO_FUNC_COUNT(pReq->Response); 
                if (SPI_SDIO_R4_IS_MEMORY_PRESENT(pReq->Response)) {
                        /* flag an SD function exists */
                    pHcd->CardProperties.Flags |= CARD_SD;      
                }        
            } else {
                    /* handle native SD */
                pHcd->CardProperties.IOFnCount = SD_SDIO_R4_GET_IO_FUNC_COUNT(pReq->Response); 
                if (SD_SDIO_R4_IS_MEMORY_PRESENT(pReq->Response)) {
                        /* flag an SD function exists */
                    pHcd->CardProperties.Flags |= CARD_SD;      
                }
                             
            }                        
            if (0 == pHcd->CardProperties.IOFnCount) {
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: SDIO Card reports no functions \n"));
                status = SDIO_STATUS_DEVICE_ERROR;
                pHcd->CardProperties.Flags = 0;
                break;  
            }
            pHcd->CardProperties.Flags |= CARD_SDIO; 
            
            DBG_PRINT(SDDBG_TRACE, 
                ("SDIO Bus Driver: SDIO Card, Functions: %d Card Info Flags:0x%X OCR:0x%8.8X\n",
                      pHcd->CardProperties.IOFnCount, pHcd->CardProperties.Flags, OCRvalue));
                /* adjust slot power for this SDIO card */          
            status = AdjustSlotPower(pHcd, &OCRvalue);
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to set power in hcd \n"));
                break;  
            }
                /* poll for SDIO card ready */          
            status = PollCardReady(pHcd,OCRvalue,CARD_SDIO);
            if (!SDIO_SUCCESS(status)) {
                break;   
            }
        } else if (status != SDIO_STATUS_BUS_RESP_TIMEOUT){
                /* major error in hcd, bail */
            break;   
        }
       
            /* check if this is an SDIO-only card before continuing  */    
        if (!(pHcd->CardProperties.Flags & CARD_SD) && (pHcd->CardProperties.Flags & CARD_SDIO)) {
                /* this is an SDIO card with no memory function */
            goto prepareCard;
        }
        
        if (!(pHcd->CardProperties.Flags & CARD_SDIO)) {
                /* issue go idle only if we did not find an SDIO function in our earlier test */
            if (IS_HCD_BUS_MODE_SPI(pHcd)) {
                status = _IssueSimpleBusRequest(pHcd,CMD0,0,SDREQ_FLAGS_RESP_R1,pReq);  
            } else {
                status = _IssueSimpleBusRequest(pHcd,CMD0,0,SDREQ_FLAGS_NO_RESP,pReq);  
            }
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: go-idle failed! \n"));
                break;  
            } 
        }
                        
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Looking for SD Memory.. \n"));
            /* SD Memory Card checking */       
            /* test for present of SD card (stand-alone or combo card) */
        status = TestPresence(pHcd, CARD_SD, pReq);        
        if (SDIO_SUCCESS(status)) {
                /* there is an SD Card present, could be part of a combo system */
            pHcd->CardProperties.Flags |= CARD_SD;            
            if (0 == OCRvalue) {
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: SD Memory card detected. \n"));
                    /* no OCR value on entry this is a stand-alone card, go and get it*/
                status = ReadOCR(pHcd,CARD_SD,pReq,0,&OCRvalue);       
                if (!SDIO_SUCCESS(status) || (OCRvalue == 0)) {
                    DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to get OCR (status:%d) \n",
                                            status));
                    break;    
                }
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: SD Card Reports OCR:0x%8.8X \n", OCRvalue));
                status = AdjustSlotPower(pHcd, &OCRvalue);
                if (!SDIO_SUCCESS(status)) {
                    DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to adjust power \n"));
                    break;  
                }            
            } else {
                 DBG_ASSERT((pHcd->CardProperties.Flags & (CARD_SD | CARD_SDIO)));
                 DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: SDIO Combo Card detected \n"));
            }          
                /* poll for SD card ready */          
            status = PollCardReady(pHcd,OCRvalue,CARD_SD);
            if (!SDIO_SUCCESS(status)) {
                    /* check if this card has an SDIO function */
                if (pHcd->CardProperties.Flags & CARD_SDIO) {
                    DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: Combo Detected but SD memory function failed \n"));
                        /* allow SDIO functions to load normally */
                    status = SDIO_STATUS_SUCCESS;
                        /* remove SD flag */
                    pHcd->CardProperties.Flags &= ~CARD_SD; 
                } else {
                    break;  
                } 
            } else {
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: SD Memory ready. \n"));
            }  
                /* we're done, no need to check for MMC */            
            goto prepareCard;                  
        } else if (status != SDIO_STATUS_BUS_RESP_TIMEOUT){
                /* major error in hcd, bail */
            break;   
        }
         
        /* MMC card checking */
        /* if we get here, these better not be set */
        DBG_ASSERT(!(pHcd->CardProperties.Flags & (CARD_SD | CARD_SDIO)));
           /* issue go idle */
        if (IS_HCD_BUS_MODE_SPI(pHcd)) {
            status = _IssueSimpleBusRequest(pHcd,CMD0,0,SDREQ_FLAGS_RESP_R1,pReq);  
        } else {
            status = _IssueSimpleBusRequest(pHcd,CMD0,0,SDREQ_FLAGS_NO_RESP,pReq);  
        }
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: go-idle failed! \n"));
            break;  
        } 
        
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Looking for MMC.. \n"));
        status = TestPresence(pHcd, CARD_MMC, pReq);
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: unknown card detected \n"));
            break;    
        }
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: MMC Card Detected \n"));
        pHcd->CardProperties.Flags |= CARD_MMC;         
            /* read the OCR value */        
        status = ReadOCR(pHcd,CARD_MMC,pReq,0,&OCRvalue);                       
        if (!SDIO_SUCCESS(status) || (OCRvalue == 0)) {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Failed to get OCR (status:%d)",
                                    status));
            break;   
        }
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: MMC Card Reports OCR:0x%8.8X \n", OCRvalue));
            /* adjust power */
        status = AdjustSlotPower(pHcd, &OCRvalue);
        if (!SDIO_SUCCESS(status)) {
             DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to adjust power \n"));
             break;  
        }
            /* poll for MMC card ready */          
        status = PollCardReady(pHcd,OCRvalue,CARD_MMC);
        if (!SDIO_SUCCESS(status)) {
            break;
        }
            /* fall through and prepare MMC card */
                      
prepareCard:
            /* we're done figuring out what was inserted, and setting up
             * optimal slot voltage, now we need to prepare the card */  
        if (!IS_HCD_BUS_MODE_SPI(pHcd) &&
            (pHcd->CardProperties.Flags & (CARD_SD | CARD_MMC))) {
                /* non-SPI SD or MMC cards need to be moved to the "ident" state before we can get the
                 * RCA or select the card using the new RCA */  
            status = _IssueSimpleBusRequest(pHcd,CMD2,0,SDREQ_FLAGS_RESP_R2,pReq);    
            if (!SDIO_SUCCESS(status)){
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: failed to move SD/MMC card into ident state \n"));
                break;    
            }  
        }
         
        if (!IS_HCD_BUS_MODE_SPI(pHcd)) {  
                /* non-SPI mode cards need their RCA's setup */           
            if (pHcd->CardProperties.Flags & (CARD_SD | CARD_SDIO)) {
                    /* issue CMD3 to get RCA on SD/SDIO cards */
                status = _IssueSimpleBusRequest(pHcd,CMD3,0,SDREQ_FLAGS_RESP_R6,pReq);    
                if (!SDIO_SUCCESS(status)){
                    DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: failed to get RCA for SD/SDIO card \n"));
                    break;    
                }
                pHcd->CardProperties.RCA = SD_R6_GET_RCA(pReq->Response); 
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: SD/SDIO RCA:0x%X \n",
                                        pHcd->CardProperties.RCA));
            } else if (pHcd->CardProperties.Flags & CARD_MMC) {
                    /* for MMC cards, we have to assign a relative card address */  
                    /* just a non-zero number */
                pHcd->CardProperties.RCA = 1;   
                    /* issue CMD3 to set the RCA for MMC cards */
                status = _IssueSimpleBusRequest(pHcd,
                                                CMD3,(pHcd->CardProperties.RCA << 16),
                                                SDREQ_FLAGS_RESP_R1,pReq);    
                if (!SDIO_SUCCESS(status)){
                    DBG_PRINT(SDDBG_ERROR, 
                            ("SDIO Bus Driver: failed to set RCA for MMC card! (err=%d) \n",status));
                    break;    
                }              
            } else { 
                DBG_ASSERT(FALSE);   
            }
        }        
            /* select the card in order to get the rest of the card info, applies 
             * to SDIO/SD/MMC cards*/
        status = SelectDeselectCard(pHcd, TRUE);
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: failed to select card! \n"));
            break;    
        }  
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver, Card now Selected.. \n"));
        
        if (pHcd->CardProperties.Flags & CARD_SDIO) {
                /* read SDIO revision register */ 
            status = Cmd52ReadByteCommon(pHcd->pPseudoDev, CCCR_SDIO_REVISION_REG, &temp); 
            if (!SDIO_SUCCESS(status)) {
                break;   
            }             
            DBG_PRINT(SDDBG_TRACE, ("SDIO Revision Reg: 0x%X \n", temp));
            switch (temp & SDIO_REV_MASK) {
                case SDIO_REV_1_00:
                    DBG_PRINT(SDDBG_TRACE, ("SDIO Spec Revision 1.00 \n"));
                    pHcd->CardProperties.SDIORevision = SDIO_REVISION_1_00;
                    break;
                case SDIO_REV_1_10:
                    DBG_PRINT(SDDBG_TRACE, ("SDIO Spec Revision 1.10 \n"));
                    pHcd->CardProperties.SDIORevision = SDIO_REVISION_1_10;
                    break;   
                case SDIO_REV_1_20: 
                    DBG_PRINT(SDDBG_TRACE, ("SDIO Spec Revision 1.20 \n"));
                    pHcd->CardProperties.SDIORevision = SDIO_REVISION_1_20;
                    break;    
                case SDIO_REV_2_00:
                    DBG_PRINT(SDDBG_TRACE, ("SDIO Spec Revision 2.00 \n"));
                    pHcd->CardProperties.SDIORevision = SDIO_REVISION_2_00;
                    break; 
                
                default:
                    DBG_PRINT(SDDBG_WARN, ("SDIO Warning: unknown SDIO revision, treating like 1.0 device \n")); 
                    pHcd->CardProperties.SDIORevision = SDIO_REVISION_1_00;
                    break;
            }            
                /* get the common CIS ptr */
            status = Cmd52ReadMultipleCommon(pHcd->pPseudoDev,
                                             SDIO_CMN_CIS_PTR_LOW_REG,
                                             cisBuffer,
                                             3);                            
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to get CIS ptr, Err:%d", status));
                break;   
            }
                /* this is endian-safe*/
            pHcd->CardProperties.CommonCISPtr = ((UINT32)cisBuffer[0]) | 
                                                (((UINT32)cisBuffer[1]) << 8) |
                                                (((UINT32)cisBuffer[2]) << 16);
                                                  
            DBG_PRINT(SDDBG_TRACE, ("SDIO Card CIS Ptr: 0x%X \n", pHcd->CardProperties.CommonCISPtr));
            temp = sizeof(manfid);
            tplAddr = pHcd->CardProperties.CommonCISPtr;      
                /* get the MANFID tuple */
            status = SDLIB_FindTuple(pHcd->pPseudoDev,
                                     CISTPL_MANFID,
                                     &tplAddr, 
                                     (PUINT8)&manfid,
                                     &temp); 
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: Failed to get MANFID tuple err:%d \n", status));
                status = SDIO_STATUS_SUCCESS;   
            } else {  
                    /* save this off so that it can be copied into each SDIO Func's SDDEVICE structure */          
                pHcd->CardProperties.SDIO_ManufacturerCode = 
                                        CT_LE16_TO_CPU_ENDIAN(manfid.ManufacturerCode);
                pHcd->CardProperties.SDIO_ManufacturerID = 
                                        CT_LE16_TO_CPU_ENDIAN(manfid.ManufacturerInfo);
                DBG_PRINT(SDDBG_TRACE, ("SDIO MANFID:0x%X, MANFINFO:0x%X \n",
                                        pHcd->CardProperties.SDIO_ManufacturerID,
                                        pHcd->CardProperties.SDIO_ManufacturerCode));
            }
            
            if (pHcd->CardProperties.SDIORevision >= SDIO_REVISION_1_10) { 
                    /* read power control */
                status = Cmd52ReadByteCommon(pHcd->pPseudoDev, SDIO_POWER_CONTROL_REG, &temp); 
                if (SDIO_SUCCESS(status)) {
                        /* check for power control support which indicates the card may use more
                         * than 200 mA */
                    if (temp & SDIO_POWER_CONTROL_SMPC) {
                            /* check that the host can support this. */
                        if (pHcd->MaxSlotCurrent >= SDIO_EMPC_CURRENT_THRESHOLD) {
                            temp = SDIO_POWER_CONTROL_EMPC; 
                                /* enable power control on the card */ 
                            status = Cmd52WriteByteCommon(pHcd->pPseudoDev, SDIO_POWER_CONTROL_REG, &temp); 
                            if (!SDIO_SUCCESS(status)) {
                                DBG_PRINT(SDDBG_ERROR,
                                        ("SDIO Busdriver: failed to enable power control (%d) \n",status));
                                break;   
                            }   
                                /* mark that the card is high power */                      
                            pHcd->CardProperties.Flags |= CARD_HIPWR; 
                                                        
                            DBG_PRINT(SDDBG_TRACE,
                               ("SDIO Busdriver: Power Control Enabled on SDIO (1.10 or greater) card \n"));      
                        } else {
                            DBG_PRINT(SDDBG_WARN,
                               ("SDIO Busdriver: Card can operate higher than 200mA, host cannot (max:%d) \n",
                               pHcd->MaxSlotCurrent));  
                            /* this is not fatal, the card should operate at a reduced rate */ 
                        } 
                    } else {
                        DBG_PRINT(SDDBG_TRACE,
                            ("SDIO Busdriver: SDIO 1.10 (or greater) card draws less than 200mA \n"));    
                    }
                } else {                 
                    DBG_PRINT(SDDBG_WARN,
                            ("SDIO Busdriver: failed to get POWER CONTROL REG (%d) \n",status)); 
                    /* fall through and continue on at reduced mode */
                }
            } 
        }      
            /* get the current bus parameters */
        busMode.BusModeFlags = pHcd->CardProperties.BusMode;
        busMode.ClockRate =  pHcd->CardProperties.OperBusClock;
            /* get the rest of the bus parameters like clock and supported bus width */
        status = GetBusParameters(pHcd,&busMode);
        if (!SDIO_SUCCESS(status)) {
            break;   
        }
        
        if (IS_HCD_BUS_MODE_SPI(pHcd)) {
                /* check HCD if it wants to run without SPI CRC */
            if (pHcd->Attributes & SDHCD_ATTRIB_NO_SPI_CRC) {
                    /* hcd would rather not run with CRC we don't need to tell the card since SPI mode
                     * cards power up with CRC initially disabled */
                busMode.BusModeFlags |= SDCONFIG_BUS_MODE_SPI_NO_CRC; 
            } else {
                    /* first enable SPI CRC checking if the HCD can handle it */
                status = SDSPIModeEnableDisableCRC(pHcd->pPseudoDev, TRUE);
                if (!SDIO_SUCCESS(status)) {
                    DBG_PRINT(SDDBG_ERROR, 
                                ("SDIO Bus Driver: Failed to set Enable SPI CRC on card \n"));   
                    break;   
                }
            }            
        }
        
        status = SetOperationalBusMode(pHcd->pPseudoDev, &busMode);
        
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to set operational bus mode\n"));
            break;  
        } 
        
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Oper. Mode: Clock:%d, Bus:0x%X \n", 
                                pHcd->CardProperties.OperBusClock,pHcd->CardProperties.BusMode));
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Card in TRANS state, Ready: CardInfo Flags 0x%X \n", 
                                pHcd->CardProperties.Flags));
                                                  
    } while (FALSE);
    
    if (pReq != NULL) {
        FreeRequest(pReq);   
    } 
    
    return status; 
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SDQuerySDMMCInfo - query MMC card info
  Input:  pDevice - device
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS SDQuerySDMMCInfo(PSDDEVICE pDevice) 
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    PSDREQUEST  pReq = NULL;
    UINT8       CID[MAX_CSD_CID_BYTES];
    
    do {
        pReq = AllocateRequest();
        if (NULL == pReq) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;    
        }
            /* de-select the card */
        status = SelectDeselectCard(pDevice->pHcd,FALSE);
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to deselect card before getting CID \n"));
            break;   
        }
        
        if (SDDEVICE_IS_BUSMODE_SPI(pDevice)) {
                /* in SPI mode, getting the CSD requires a data transfer */
            status = _IssueBusRequestBd(pDevice->pHcd,CMD10,0,
                                        SDREQ_FLAGS_RESP_R1 | SDREQ_FLAGS_DATA_TRANS,
                                        pReq,
                                        CID, 
                                        MAX_CSD_CID_BYTES);
            if (SDIO_SUCCESS(status)) {
                    /* in SPI mode we need to reorder to the CID since SPI data comes in MSB first*/
                ReorderBuffer(CID,MAX_CSD_CID_BYTES); 
            }
        } else {                                             
                /* get the CID */
            status = _IssueSimpleBusRequest(pDevice->pHcd,
                                            CMD10,
                                            (SDDEVICE_GET_CARD_RCA(pDevice) << 16),
                                            SDREQ_FLAGS_RESP_R2,
                                            pReq); 
            if (SDIO_SUCCESS(status)) {
                    /* extract it from the reponse */
                memcpy(CID,pReq->Response,MAX_CSD_CID_BYTES);
            }
        }
        
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_WARN, ("SDQuerySDMMCInfo: failed to get CID. \n"));
            status = SDIO_STATUS_SUCCESS;    
        } else {
            pDevice->pId[0].SDMMC_ManfacturerID = GET_SD_CID_MANFID(CID);
            pDevice->pId[0].SDMMC_OEMApplicationID = GET_SD_CID_OEMID(CID);
#ifdef DEBUG
            {  
                char pBuf[7];
                 
                pBuf[0] = GET_SD_CID_PN_1(CID);
                pBuf[1] = GET_SD_CID_PN_2(CID);
                pBuf[2] = GET_SD_CID_PN_3(CID);
                pBuf[3] = GET_SD_CID_PN_4(CID);
                pBuf[4] = GET_SD_CID_PN_5(CID);
                if (pDevice->pHcd->CardProperties.Flags & CARD_MMC) {
                    pBuf[5] = GET_SD_CID_PN_6(CID); 
                    pBuf[6] = 0;   
                } else {
                    pBuf[5] = 0;     
                }
                DBG_PRINT(SDDBG_TRACE, ("SDQuerySDMMCInfo: Product String: %s\n", pBuf));
            }
#endif            
            DBG_PRINT(SDDBG_TRACE, ("SDQuerySDMMCInfo: ManfID: 0x%X, OEMID:0x%X \n",
                       pDevice->pId[0].SDMMC_ManfacturerID, pDevice->pId[0].SDMMC_OEMApplicationID));
        }                                                              
            /* re-select card */         
        status = SelectDeselectCard(pDevice->pHcd,TRUE);
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to re-select card after getting CID \n"));   
            break;    
        }  
    } while (FALSE);
           
    if (pReq != NULL) {
        FreeRequest(pReq);    
    }
    
    return status; 
}
 
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SDQuerySDIOInfo - query SDIO card info
  Input:  pDevice - the device
  Output:  
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS SDQuerySDIOInfo(PSDDEVICE pDevice) 
{
    SDIO_STATUS     status = SDIO_STATUS_SUCCESS;
    UINT32          faddress;
    UINT8           fInfo;
    UINT32          nextTpl;
    UINT8           tplLength;
    UINT8           cisPtrBuffer[3];
    struct SDIO_FUNC_EXT_FUNCTION_TPL_1_1 funcTuple;
        
        /* use the card-wide SDIO manufacturer code and ID previously read.*/
    pDevice->pId[0].SDIO_ManufacturerCode = pDevice->pHcd->CardProperties.SDIO_ManufacturerCode; 
    pDevice->pId[0].SDIO_ManufacturerID = pDevice->pHcd->CardProperties.SDIO_ManufacturerID; 
        
        /* calculate function base address */    
    faddress = CalculateFBROffset(SDDEVICE_GET_SDIO_FUNCNO(pDevice));  
    DBG_ASSERT(faddress != 0);
       
    do {
        status = Cmd52ReadByteCommon(pDevice,
                                     FBR_FUNC_INFO_REG_OFFSET(faddress),
                                     &fInfo);                                  
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to get function info, Err:%d , using Class:UNKNOWN\n", status));
            fInfo = 0;
            pDevice->pId[0].SDIO_FunctionClass = 0;
            status = SDIO_STATUS_SUCCESS;
        } else {
            pDevice->pId[0].SDIO_FunctionClass = fInfo & FUNC_INFO_DEVICE_CODE_MASK; 
        }    
          
        if ((FUNC_INFO_DEVICE_CODE_LAST == pDevice->pId[0].SDIO_FunctionClass) &&
            SDDEVICE_IS_SDIO_REV_GTEQ_1_10(pDevice)) { 
                /* if the device code is the last one, check for 1.1 revision and get the
                 * extended code */ 
            status = Cmd52ReadByteCommon(pDevice,
                                         FBR_FUNC_EXT_DEVICE_CODE_OFFSET(faddress),
                                         &(pDevice->pId[0].SDIO_FunctionClass));                                  
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to get 1.1 extended DC, Err:%d\n",
                                        status));
                break;   
            }  
        }  
                                                               
            /* get the function CIS ptr */
        status = Cmd52ReadMultipleCommon(pDevice,
                                         FBR_FUNC_CIS_LOW_OFFSET(faddress),
                                         cisPtrBuffer,
                                         3); 
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to get FN CIS ptr, Err:%d\n", status));
            break;   
        }
            /* endian safe */
        pDevice->DeviceInfo.AsSDIOInfo.FunctionCISPtr = ((UINT32)cisPtrBuffer[0]) | 
                                                        (((UINT32)cisPtrBuffer[1]) << 8) |
                                                        (((UINT32)cisPtrBuffer[2]) << 16);
                                                        
        DBG_PRINT(SDDBG_TRACE, ("SDIO Function:%d, Class:%d FnCISPtr:0x%X \n",
                  SDDEVICE_GET_SDIO_FUNCNO(pDevice),
                  pDevice->pId[0].SDIO_FunctionClass,pDevice->DeviceInfo.AsSDIOInfo.FunctionCISPtr));
        
        if (fInfo & FUNC_INFO_SUPPORTS_CSA_MASK) {
               /* get the function CSA ptr */
            status = Cmd52ReadMultipleCommon(pDevice,
                                             FBR_FUNC_CSA_LOW_OFFSET(faddress),
                                             cisPtrBuffer,
                                             3); 
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to get FN CSA ptr, Err:%d \n", status));
                break;   
            }
                /* endian safe */ 
            pDevice->DeviceInfo.AsSDIOInfo.FunctionCSAPtr = ((UINT32)cisPtrBuffer[0]) | 
                                                            (((UINT32)cisPtrBuffer[1]) << 8) |
                                                            (((UINT32)cisPtrBuffer[2]) << 16);
                                                      
        } 
        
        nextTpl = SDDEVICE_GET_SDIO_FUNC_CISPTR(pDevice);
            /* look for the funce TPL */
        tplLength = sizeof(funcTuple); 
            /* go get the func CE tuple */
        status = SDLIB_FindTuple(pDevice,
                                 CISTPL_FUNCE,
                                 &nextTpl,
                                 (PUINT8)&funcTuple,
                                 &tplLength);
        
        if (!SDIO_SUCCESS(status)){
            /* handles case of bad CIS or missing tupple, allow function driver to handle */
            DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: Failed to get FuncCE Tuple: %d \n", status));
            status = SDIO_STATUS_SUCCESS;
            break;    
        }
            /* set the max block size */
        pDevice->DeviceInfo.AsSDIOInfo.FunctionMaxBlockSize = 
                                CT_LE16_TO_CPU_ENDIAN(funcTuple.CommonInfo.MaxBlockSize);
                                                                   
        DBG_PRINT(SDDBG_TRACE, ("SDIO Function:%d, MaxBlocks:%d \n",
                  SDDEVICE_GET_SDIO_FUNCNO(pDevice),
                  pDevice->DeviceInfo.AsSDIOInfo.FunctionMaxBlockSize));
                  
            /* check for MANFID function tuple (SDIO 1.1 or greater) */
        if (SDDEVICE_IS_SDIO_REV_GTEQ_1_10(pDevice)) {
            struct SDIO_MANFID_TPL      manfid;
            nextTpl = SDDEVICE_GET_SDIO_FUNC_CISPTR(pDevice);   
            tplLength = sizeof(manfid);            
                /* get the MANFID tuple */
            status = SDLIB_FindTuple(pDevice,
                                     CISTPL_MANFID,
                                     &nextTpl, 
                                     (PUINT8)&manfid,
                                     &tplLength); 
            if (SDIO_SUCCESS(status)) {
                    /* this function has a MANFID tuple */          
                pDevice->pId[0].SDIO_ManufacturerCode = 
                                        CT_LE16_TO_CPU_ENDIAN(manfid.ManufacturerCode);
                pDevice->pId[0].SDIO_ManufacturerID = 
                                        CT_LE16_TO_CPU_ENDIAN(manfid.ManufacturerInfo);
                DBG_PRINT(SDDBG_TRACE, ("SDIO 1.1 (Function Specific) MANFID:0x%X, MANFINFO:0x%X \n",
                                        pDevice->pId[0].SDIO_ManufacturerID,
                                        pDevice->pId[0].SDIO_ManufacturerCode));
            } else {
                DBG_PRINT(SDDBG_WARN, ("SDIO 1.1, No CISTPL_MANFID Tuple in FUNC CIS \n"));       
                status = SDIO_STATUS_SUCCESS;
            }
        }    
    } while (FALSE);
    
    return status; 
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SDEnableFunction - enable function
  Input:  pDevice - the device/function
          pEnData - enable data;
  Output: 
  Return: status
  Notes: Note, this performs synchronous calls
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS SDEnableFunction(PSDDEVICE pDevice, PSDCONFIG_FUNC_ENABLE_DISABLE_DATA pEnData)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    UINT8       registerValue;
    UINT8       mask;
    FUNC_ENABLE_TIMEOUT  retry;
   
        /* take the configure op lock to make this atomic */
    status = SemaphorePendInterruptable(&pDevice->pHcd->ConfigureOpsSem);
    if (!SDIO_SUCCESS(status)) {
        return status;   
    } 
    
    status = SDIO_STATUS_INVALID_PARAMETER;       
    do { 
        if (!(pDevice->pHcd->CardProperties.Flags & CARD_SDIO)){
                /* nothing to do if it's not an SDIO card */
            break;  
        }
                
        if (!((SDDEVICE_GET_SDIO_FUNCNO(pDevice) >= SDIO_FIRST_FUNCTION_NUMBER) && 
              (SDDEVICE_GET_SDIO_FUNCNO(pDevice) <= SDIO_LAST_FUNCTION_NUMBER))){
            DBG_ASSERT(FALSE);
            break;
        }         
            /* make sure there is a timeout value */ 
        if (0 == pEnData->TimeOut) {
            break;  
        } 
                   
        mask = 1 << SDDEVICE_GET_SDIO_FUNCNO(pDevice);
            /* read the enable register */
        status = Cmd52ReadByteCommon(pDevice, SDIO_ENABLE_REG, &registerValue);
        if (!SDIO_SUCCESS(status)){
            break;    
        }        
        if (pEnData->EnableFlags & SDCONFIG_ENABLE_FUNC) {                   
                /* set the enable register bit */     
            registerValue |= mask;    
        } else {
               /* clear the bit */
            registerValue &= ~mask;           
        }
        
        DBG_PRINT(SDDBG_TRACE,
                ("SDIO Bus Driver %s Function, Mask:0x%X Enable Reg Value:0x%2.2X\n",
                 (pEnData->EnableFlags & SDCONFIG_ENABLE_FUNC) ? "Enabling":"Disabling",
                 mask,
                 registerValue));
  
            /* write it back out */
        status = Cmd52WriteByteCommon(pDevice, SDIO_ENABLE_REG, &registerValue);
        if (!SDIO_SUCCESS(status)){
            break;    
        } 
            /* now poll the ready bit until it sets or clears */
        retry = pEnData->TimeOut;
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Function Enable/Disable Polling: %d retries \n", 
                                retry));
        while (retry) {
            status = Cmd52ReadByteCommon(pDevice, SDIO_READY_REG, &registerValue);
            if (!SDIO_SUCCESS(status)){
                break;    
            }
            if (pEnData->EnableFlags & SDCONFIG_ENABLE_FUNC) { 
                    /* if the bit is set, the device is ready */
                if (registerValue & mask) {
                        /* device ready */
                    break; 
                }
            } else {
                if (!(registerValue & mask)) {
                        /* device is no longer ready */
                    break; 
                }       
            } 
                /* sleep before trying again */    
            status = OSSleep(1);   
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("OSSleep Failed! \n"));
                break;   
            }
            retry--;
        } 
        
        if (0 == retry) {
            status = SDIO_STATUS_FUNC_ENABLE_TIMEOUT; 
            break;
        }         
                                                  
    } while (FALSE);
        
    SemaphorePost(&pDevice->pHcd->ConfigureOpsSem);            
    return status;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SDAllocFreeSlotCurrent - allocate or free slot current
  Input:  pDevice - the device/function
          Allocate - Allocate current, else free
          pData - slotcurrent data (non-NULL if Allocate is TRUE)
  Output: 
  Return: status
  Notes:  if the function returns SDIO_STATUS_NO_RESOURCES, the pData->SlotCurrent field is
          updated with the available current
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS SDAllocFreeSlotCurrent(PSDDEVICE pDevice, BOOL Allocate, PSDCONFIG_FUNC_SLOT_CURRENT_DATA pData)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
     
    DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: SDAllocFreeSlotCurrent\n"));

        /* take the configure op lock to make this atomic */
    status = SemaphorePendInterruptable(&pDevice->pHcd->ConfigureOpsSem);
    if (!SDIO_SUCCESS(status)) {
        return status;   
    } 
    
    status = SDIO_STATUS_INVALID_PARAMETER;      
    do {
            /* check the current budget and allocate */
        if (Allocate) {
            if (0 == pData->SlotCurrent) {
                /* caller must specify current requirement for the power mode */
                break;    
            }   
            if (pDevice->SlotCurrentAlloc != 0) {
               /* slot current has already been allocated, caller needs to free
                * first */
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Slot Current Already allocated! \n"));
                break;  
            }     
            if (((UINT32)pDevice->pHcd->SlotCurrentAllocated + (UINT32)pData->SlotCurrent) > 
                (UINT32)pDevice->pHcd->MaxSlotCurrent) {  
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Slot Current Budget exceeded, Requesting: %d, Allocated already: %d, Max: %d \n", 
                            pData->SlotCurrent, pDevice->pHcd->SlotCurrentAllocated,
                            pDevice->pHcd->MaxSlotCurrent));
                status = SDIO_STATUS_NO_RESOURCES;  
                    /* return remaining */                  
                pData->SlotCurrent = pDevice->pHcd->MaxSlotCurrent - 
                                     pDevice->pHcd->SlotCurrentAllocated;
                break;   
            }        
                /* bump up allocation */   
            pDevice->pHcd->SlotCurrentAllocated += pData->SlotCurrent;
                /* save this off for the call to free slot current */  
            pDevice->SlotCurrentAlloc = pData->SlotCurrent;
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Slot Current Requested: %d, New Total: %d, Max: %d \n", 
                            pData->SlotCurrent, pDevice->pHcd->SlotCurrentAllocated,
                            pDevice->pHcd->MaxSlotCurrent));
            
        } else {
            if (0 == pDevice->SlotCurrentAlloc) {
                    /* no allocation */
                break;
            }
                /* return the allocation back */
            if (pDevice->SlotCurrentAlloc <= pDevice->pHcd->SlotCurrentAllocated) {
                pDevice->pHcd->SlotCurrentAllocated -= pDevice->SlotCurrentAlloc;  
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Slot Current Freed: %d, New Total: %d, Max: %d \n", 
                            pDevice->SlotCurrentAlloc, pDevice->pHcd->SlotCurrentAllocated,
                            pDevice->pHcd->MaxSlotCurrent));
            } else {
                DBG_ASSERT(FALSE);   
            }
           
                /* make sure this is zeroed */
            pDevice->SlotCurrentAlloc = 0;          
        }                                   
        
        status = SDIO_STATUS_SUCCESS;
                                             
    } while (FALSE);
        
    SemaphorePost(&pDevice->pHcd->ConfigureOpsSem);            
    DBG_PRINT(SDDBG_TRACE, ("-SDIO Bus Driver: SDAllocFreeSlotCurrent, %d\n", status));
    return status;
}

static void RawHcdIrqControl(PSDHCD pHcd, BOOL Enable)
{
    SDIO_STATUS status;
    SDCONFIG_SDIO_INT_CTRL_DATA irqData;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    ZERO_OBJECT(irqData);  
    
    status = _AcquireHcdLock(pHcd);
    if (!SDIO_SUCCESS(status)) { 
        return;
    }
          
    do {
            /* for raw devices, we simply enable/disable in the HCD only */
        if (Enable) {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver (RAW) Unmasking Int \n"));
            irqData.IRQDetectMode = IRQ_DETECT_RAW;
            irqData.SlotIRQEnable = TRUE; 
        } else {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver (RAW) Masking Int \n"));
            irqData.SlotIRQEnable = FALSE; 
        }
        
        status = _IssueConfig(pHcd,SDCONFIG_SDIO_INT_CTRL,
                              (PVOID)&irqData, sizeof(irqData));
                              
        if (!SDIO_SUCCESS(status)){
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver failed to enable/disable IRQ in (RAW) hcd :%d\n", 
                                    status)); 
        }       
        
    } while (FALSE);
  
    status = _ReleaseHcdLock(pHcd); 
}

static void RawHcdEnableIrqPseudoComplete(PSDREQUEST pReq)
{
    if (SDIO_SUCCESS(pReq->Status)) {
        RawHcdIrqControl((PSDHCD)pReq->pCompleteContext, TRUE);  
    }
    FreeRequest(pReq);
}

static void RawHcdDisableIrqPseudoComplete(PSDREQUEST pReq)
{
    RawHcdIrqControl((PSDHCD)pReq->pCompleteContext, FALSE);  
    FreeRequest(pReq);
}

static void HcdIrqControl(PSDHCD pHcd, BOOL Enable)
{
    SDIO_STATUS                 status;
    SDCONFIG_SDIO_INT_CTRL_DATA irqData;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    ZERO_OBJECT(irqData);  
    
    status = _AcquireHcdLock(pHcd);
    if (!SDIO_SUCCESS(status)) { 
        return;
    }
    
    do {
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: HcdIrqControl (%s), IrqsEnabled:0x%X \n", 
                        Enable ? "Enable":"Disable",pHcd->IrqsEnabled ));
                        
        if (Enable) {
            irqData.SlotIRQEnable = TRUE;
        } else {
            irqData.SlotIRQEnable = FALSE;
        }
                /* setup HCD to enable/disable it's detection hardware */
        if (irqData.SlotIRQEnable) {
                /* set the IRQ detection mode */
            switch (SDCONFIG_GET_BUSWIDTH(pHcd->CardProperties.BusMode)) {                
                case SDCONFIG_BUS_WIDTH_SPI:
                    irqData.IRQDetectMode = IRQ_DETECT_SPI;
                    break;
                case SDCONFIG_BUS_WIDTH_1_BIT:
                    irqData.IRQDetectMode = IRQ_DETECT_1_BIT;
                    break;
                case SDCONFIG_BUS_WIDTH_4_BIT:  
                    irqData.IRQDetectMode = IRQ_DETECT_4_BIT;
                        /* check card and HCD for 4bit multi-block interrupt support */
                    if ((pHcd->CardProperties.SDIOCaps & SDIO_CAPS_INT_MULTI_BLK) &&
                        (pHcd->Attributes & SDHCD_ATTRIB_MULTI_BLK_IRQ)) {                           
                            /* note: during initialization of the card, the mult-blk IRQ support
                             * is enabled in card caps register */
                        irqData.IRQDetectMode |= IRQ_DETECT_MULTI_BLK;  
                        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver enabling IRQ in multi-block mode:\n"));
                    }  
                    break;
                default:
                    DBG_ASSERT(FALSE);
                    break;
            }
            
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver enabling IRQ in HCD Mode:0x%X\n", 
                                     irqData.IRQDetectMode));
        }  
             
        status = _IssueConfig(pHcd,SDCONFIG_SDIO_INT_CTRL,
                                (PVOID)&irqData, sizeof(irqData));
        if (!SDIO_SUCCESS(status)){
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver failed to enable/disable IRQ in hcd %d\n", 
                                    status));
        }
  
    } while (FALSE);
    
    status = _ReleaseHcdLock(pHcd); 
}

static BOOL CheckWriteIntEnableSuccess(PSDREQUEST pReq)
{
    if (!SDIO_SUCCESS(pReq->Status)){
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to get write INT Enable register Err:%d\n",
                                     pReq->Status));
        return FALSE;
    } 
                   
    if (SD_R5_GET_RESP_FLAGS(pReq->Response) & SD_R5_ERRORS) {
       DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: WriteIntEnableComplete CMD52 resp error: 0x%X \n", 
                  SD_R5_GET_RESP_FLAGS(pReq->Response)));
        return FALSE;
    }  
    
    return TRUE;
}

static void HcdIrqEnableComplete(PSDREQUEST pReq)
{
    if (CheckWriteIntEnableSuccess(pReq)) {
            /* configure HCD */
        HcdIrqControl((PSDHCD)pReq->pCompleteContext, TRUE);  
    }
    FreeRequest(pReq);
}

static void HcdIrqDisableComplete(PSDREQUEST pReq)
{
    CheckWriteIntEnableSuccess(pReq);
    HcdIrqControl((PSDHCD)pReq->pCompleteContext, FALSE);  
    FreeRequest(pReq);
}

static void WriteIntEnableComplete(PSDREQUEST pReq) 
{  
   if (CheckWriteIntEnableSuccess(pReq)) {
       DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Wrote INT Enable value:0x%X \n", 
                    (INT)pReq->pCompleteContext));
   }
   FreeRequest(pReq); 
}

static void HcdAckComplete(PSDREQUEST pReq)
{
    SDIO_STATUS status;
    DBG_PRINT(SDIODBG_FUNC_IRQ, ("SDIO Bus Driver: Hcd (0x%X) Irq Ack \n", 
                    (INT)pReq->pCompleteContext));
        /* re-arm the HCD */
    status = _IssueConfig((PSDHCD)pReq->pCompleteContext,SDCONFIG_SDIO_REARM_INT,NULL,0);  
  
    if (!SDIO_SUCCESS(status)) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: HCD Re-Arm failed : %d\n", 
                    status));       
    }
    FreeRequest(pReq);
}
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SDFunctionAckInterrupt - handle device interrupt acknowledgement
  Input:  pDevice - the device 
  Output: 
  Return: 
  Notes: 
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS SDFunctionAckInterrupt(PSDDEVICE pDevice)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    UCHAR       mask;
    PSDREQUEST  pReq = NULL;
    BOOL        setHcd = FALSE;
    SDIO_STATUS status2;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    pReq = AllocateRequest();        
    if (NULL == pReq) {
        return SDIO_STATUS_NO_RESOURCES;
    }
    
    status = _AcquireHcdLock(pDevice->pHcd);
    
    if (!SDIO_SUCCESS(status)) {
        FreeRequest(pReq);
        return status; 
    } 
    
    do { 
        if (!((SDDEVICE_GET_SDIO_FUNCNO(pDevice) >= SDIO_FIRST_FUNCTION_NUMBER) && 
              (SDDEVICE_GET_SDIO_FUNCNO(pDevice) <= SDIO_LAST_FUNCTION_NUMBER))){
            status = SDIO_STATUS_INVALID_PARAMETER;
            DBG_ASSERT(FALSE);
            break;
        } 
        mask = 1 << SDDEVICE_GET_SDIO_FUNCNO(pDevice);
        if (pDevice->pHcd->PendingIrqAcks & mask) { 
                /* clear the ack bit in question */
            pDevice->pHcd->PendingIrqAcks &= ~mask;      
            if (0 == pDevice->pHcd->PendingIrqAcks) {
                pDevice->pHcd->IrqProcState = SDHCD_IDLE;
                    /* no pending acks, so re-arm if irqs are stilled enabled */
                if (pDevice->pHcd->IrqsEnabled) {
                    setHcd = TRUE;
                        /* issue pseudo request to sync this with bus requests */
                    pReq->Status = SDIO_STATUS_SUCCESS;
                    pReq->pCompletion = HcdAckComplete;
                    pReq->pCompleteContext = pDevice->pHcd;  
                    pReq->Flags = SD_PSEUDO_REQ_FLAGS;
                }     
            } 
        } else {
            DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: AckInterrupt: no IRQ pending on Function :%d, \n", 
                        SDDEVICE_GET_SDIO_FUNCNO(pDevice)));
        }     
    } while (FALSE); 
        
    status2 = ReleaseHcdLock(pDevice);
    
    if (pReq != NULL) {
        if (SDIO_SUCCESS(status) && (setHcd)) {
                /* issue request */
            IssueRequestToHCD(pDevice->pHcd,pReq);       
        } else {
            FreeRequest(pReq);   
        }  
    }  
    
    return status;  
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SDMaskUnmaskFunctionIRQ - mask/unmask function IRQ
  Input:  pDevice - the device/function
          MaskInt - mask interrupt
  Output: 
  Return: status
  Notes:  Note, this function can be called from an ISR or completion context
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS SDMaskUnmaskFunctionIRQ(PSDDEVICE pDevice, BOOL MaskInt)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    UINT8       mask;
    UINT8       controlVal;
    BOOL        setHcd;
    PSDREQUEST  pReq = NULL;
    SDIO_STATUS status2;
    
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    setHcd = FALSE;
    
    pReq = AllocateRequest();        
    if (NULL == pReq) {
        return SDIO_STATUS_NO_RESOURCES;
    }
    
    status = _AcquireHcdLock(pDevice->pHcd);
    
    if (!SDIO_SUCCESS(status)) {
        FreeRequest(pReq);
        return status; 
    } 
    
    do { 
          
        if (pDevice->pHcd->CardProperties.Flags & CARD_RAW) {
            if (!MaskInt) {
                if (!pDevice->pHcd->IrqsEnabled) {
                    pReq->pCompletion = RawHcdEnableIrqPseudoComplete; 
                    setHcd = TRUE; 
                    pDevice->pHcd->IrqsEnabled = 1 << 1;   
                } 
            } else {
                if (pDevice->pHcd->IrqsEnabled) {
                    pReq->pCompletion = RawHcdDisableIrqPseudoComplete; 
                    setHcd = TRUE; 
                    pDevice->pHcd->IrqsEnabled = 0;   
                } 
            }                       
            
            if (setHcd) {
                    /* hcd IRQ control requests must be synched with outstanding 
                     * bus requests so we issue a pseudo bus request  */
                pReq->pCompleteContext = pDevice->pHcd;  
                pReq->Flags = SD_PSEUDO_REQ_FLAGS;
                pReq->Status = SDIO_STATUS_SUCCESS;
            } else {
                    /* no request to submit, just free it */
                FreeRequest(pReq);   
                pReq = NULL;  
            }
                /* we're done, submit the bus request if any */
            break;  
        } 
        
        if (!(pDevice->pHcd->CardProperties.Flags & CARD_SDIO)){
                /* nothing to do if it's not an SDIO card */
            DBG_ASSERT(FALSE);
            status = SDIO_STATUS_INVALID_PARAMETER;
            break;  
        } 
        
        if (!((SDDEVICE_GET_SDIO_FUNCNO(pDevice) >= SDIO_FIRST_FUNCTION_NUMBER) && 
              (SDDEVICE_GET_SDIO_FUNCNO(pDevice) <= SDIO_LAST_FUNCTION_NUMBER))){
            status = SDIO_STATUS_INVALID_PARAMETER;
            DBG_ASSERT(FALSE);
            break;
        }   
               
        mask = 1 << SDDEVICE_GET_SDIO_FUNCNO(pDevice);      
        if (!MaskInt) {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver Unmasking Int, Mask:0x%X\n", mask));
                /* check interrupts that were enabled on entry */
            if (0 == pDevice->pHcd->IrqsEnabled) {
                    /* need to turn on interrupts in HCD */
                setHcd = TRUE;   
                    /* use this completion routine */
                pReq->pCompletion = HcdIrqEnableComplete;   
            } 
                /* set the enable bit, in the shadow register */     
            pDevice->pHcd->IrqsEnabled |= mask;
                /* make sure control value includes the master enable */
            controlVal = pDevice->pHcd->IrqsEnabled | SDIO_INT_MASTER_ENABLE;
        } else {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver Masking Int, Mask:0x%X\n", mask));
                /* clear the bit */
            pDevice->pHcd->IrqsEnabled &= ~mask;  
                /* check and see if this clears all the bits */
            if (0 == pDevice->pHcd->IrqsEnabled){
                    /* if none of the functions are enabled, clear this register */
                controlVal = 0;                 
                    /* disable in host */
                setHcd = TRUE;
                    /* use this completion routine */
                pReq->pCompletion = HcdIrqDisableComplete; 
            } else {
                    /* set control value making sure master enable is left on */
                controlVal = pDevice->pHcd->IrqsEnabled | SDIO_INT_MASTER_ENABLE;
            }
        }
        
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver INT_ENABLE_REG value:0x%X\n", controlVal));
            /* setup bus request to update the mask register */
        SDIO_SET_CMD52_WRITE_ARG(pReq->Argument,0,SDIO_INT_ENABLE_REG,controlVal);    
        pReq->Command = CMD52;
        pReq->Flags = SDREQ_FLAGS_TRANS_ASYNC | SDREQ_FLAGS_RESP_SDIO_R5;
            
        if (setHcd) {
                /* make this a barrier request and set context*/
            pReq->Flags |= SDREQ_FLAGS_BARRIER;
            pReq->pCompleteContext = pDevice->pHcd;     
        } else {
                /* does not require an update to the HCD  */
            pReq->pCompleteContext = (PVOID)(UINT32)controlVal;
            pReq->pCompletion = WriteIntEnableComplete; 
        }
                  
    } while (FALSE);
    
    status2 = _ReleaseHcdLock(pDevice->pHcd);
        
    if (pReq != NULL) {
        if (SDIO_SUCCESS(status)) {
                /* issue request */
            IssueRequestToHCD(pDevice->pHcd,pReq);       
        } else {
            FreeRequest(pReq);   
        }  
    }  
            
    return status;
}


/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SDSPIModeEnableDisableCRC - Enable/Disable SPI Mode CRC checking
  Input:  pDevice - the device/function
          Enable - Enable CRC
  Output: 
  Return: status
  Notes:  Note, this function can be called from an ISR or completion context
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS SDSPIModeEnableDisableCRC(PSDDEVICE pDevice,BOOL Enable)
{
    SDCONFIG_BUS_MODE_DATA busMode;
    SDIO_STATUS            status = SDIO_STATUS_SUCCESS;
    UINT32                 cmdARG = 0; 
    
    if (!SDDEVICE_IS_BUSMODE_SPI(pDevice)) {
        return SDIO_STATUS_INVALID_PARAMETER;   
    }   
       //??we should make these atomic using a barrier
   
        /* get the current mode and clock */ 
    busMode.BusModeFlags = pDevice->pHcd->CardProperties.BusMode;
    busMode.ClockRate = pDevice->pHcd->CardProperties.OperBusClock;    
    
    if (Enable) {
            /* clear the no-CRC flag */
        busMode.BusModeFlags &= ~SDCONFIG_BUS_MODE_SPI_NO_CRC;   
        cmdARG = SD_CMD59_CRC_ON;
    } else {
        busMode.BusModeFlags |= SDCONFIG_BUS_MODE_SPI_NO_CRC; 
        cmdARG = SD_CMD59_CRC_OFF;      
    } 
       
    do { 
            /* issue CMD59 to turn on/off CRC */
        status = _IssueSimpleBusRequest(pDevice->pHcd,
                                        CMD59,
                                        cmdARG,
                                        SDREQ_FLAGS_RESP_R1,
                                        NULL); 
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed issue CMD59 (arg=0x%X) Err:%d \n",
                                    cmdARG, status));
            break;   
        }
        if (Enable) {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: CRC Enabled in SPI mode \n"));
        } else {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: CRC Disabled in SPI mode \n"));   
        }
        status = SetOperationalBusMode(pDevice,&busMode);
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to set SPI NO CRC mode in hcd : Err:%d \n",
                                    status));
            break;  
        }
    } while (FALSE);
    
    return status; 
}


static UINT32 ConvertSPIStatusToSDCardStatus(UINT8 SpiR1, UINT8 SpiR2) 
{
    UINT32 cardStatus = 0;
    
    if (SpiR1 != 0) {
            /* convert the error */
        if (SpiR1 & SPI_CS_ERASE_RESET) {
            cardStatus |= SD_CS_ERASE_RESET;
        }            
        if (SpiR1 & SPI_CS_ILLEGAL_CMD) {
            cardStatus |= SD_CS_ILLEGAL_CMD_ERR;
        }            
        if (SpiR1 & SPI_CS_CMD_CRC_ERR) {
            cardStatus |= SD_CS_PREV_CMD_CRC_ERR;
        }
        if (SpiR1 & SPI_CS_ERASE_SEQ_ERR) {
            cardStatus |= SD_CS_ERASE_SEQ_ERR;
        }
        if (SpiR1 & SPI_CS_ADDRESS_ERR) {
            cardStatus |= SD_CS_ADDRESS_ERR;
        }
        if (SpiR1 & SPI_CS_PARAM_ERR) {
            cardStatus |= SD_CS_CMD_OUT_OF_RANGE;
        }   
    }
    
    if (SpiR2 != 0) {
            /* convert the error */
        if (SpiR2 & SPI_CS_CARD_IS_LOCKED) {
            cardStatus |= SD_CS_CARD_LOCKED;
        }            
        if (SpiR2 & SPI_CS_LOCK_UNLOCK_FAILED) {
                /* this bit is shared, just set both */
            cardStatus |= (SD_CS_LK_UNLK_FAILED | SD_CS_WP_ERASE_SKIP);
        }            
        if (SpiR2 & SPI_CS_ERROR) {
            cardStatus |= SD_CS_GENERAL_ERR;
        }
        if (SpiR2 & SPI_CS_INTERNAL_ERROR) {
            cardStatus |= SD_CS_CARD_INTERNAL_ERR;
        }
        if (SpiR2 & SPI_CS_ECC_FAILED) {
            cardStatus |= SD_CS_ECC_FAILED;
        }
        if (SpiR2 & SPI_CS_WP_VIOLATION) {
            cardStatus |= SD_CS_WP_ERR;
        }  
        if (SpiR2 & SPI_CS_ERASE_PARAM_ERR) {
            cardStatus |= SD_CS_ERASE_PARAM_ERR;
        }  
        if (SpiR2 & SPI_CS_OUT_OF_RANGE) {
            cardStatus |= SD_CS_CMD_OUT_OF_RANGE;
        }    
    }
    
    return cardStatus;
}
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  ConvertSPI_Response - filter the SPI response and convert it to an SD Response
  Input:  pReq - request
  Output: pReq - modified response, if pRespBuffer is not NULL
          pRespBuffer - converted response (optional)
  Return: 
  Notes:  This function converts a SPI response into an SD response.  A caller
          can supply a buffer instead.
          For SPI bus operation the HCD must send the SPI response as 
          a stream of bytes, the highest byte contains the first received byte from the
          card.  This function only filters simple responses (R1 primarily). 
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void ConvertSPI_Response(PSDREQUEST pReq, UINT8 *pRespBuffer)
{

    UINT32  cardStatus;
    
    if (pReq->Flags & SDREQ_FLAGS_RESP_SPI_CONVERTED) {
            /* already converted */
        return;    
    }  
    if (NULL == pRespBuffer) {
        pRespBuffer = pReq->Response; 
    }    
       
    switch (GET_SDREQ_RESP_TYPE(pReq->Flags)) {  
        case SDREQ_FLAGS_RESP_R1:
        case SDREQ_FLAGS_RESP_R1B:
            cardStatus = ConvertSPIStatusToSDCardStatus(GET_SPI_R1_RESP_TOKEN(pReq->Response),
                                                        0);            
            if (CMD55 == pReq->Command) {
                    /* we emulate this since SPI does not have such a bit */
                cardStatus |= SD_CS_APP_CMD;     
            }
                /* stuff the SD card status */
            SD_R1_SET_CMD_STATUS(pRespBuffer,cardStatus);
                /* stuff the command */
            SD_R1_SET_CMD(pRespBuffer,pReq->Command);  
            pReq->Flags |= SDREQ_FLAGS_RESP_SPI_CONVERTED;                                            
            break;
        case SDREQ_FLAGS_RESP_SDIO_R5:
            {
                UINT8 respFlags;
                UINT8 readData;
                
                readData = GET_SPI_SDIO_R5_RESPONSE_RDATA(pReq->Response);
                respFlags = GET_SPI_SDIO_R5_RESP_TOKEN(pReq->Response);
                
                pRespBuffer[SD_R5_RESP_FLAGS_OFFSET] = 0;            
                if (respFlags != 0) {
                    if (respFlags & SPI_R5_ILLEGAL_CMD) {
                        pRespBuffer[SD_R5_RESP_FLAGS_OFFSET] |= SD_R5_ILLEGAL_CMD;
                    }
                    if (respFlags & SPI_R5_CMD_CRC) {
                        pRespBuffer[SD_R5_RESP_FLAGS_OFFSET] |= SD_R5_RESP_CMD_ERR;
                    }
                    if (respFlags & SPI_R5_FUNC_ERR) {
                        pRespBuffer[SD_R5_RESP_FLAGS_OFFSET] |= SD_R5_INVALID_FUNC;
                    }
                    if (respFlags & SPI_R5_PARAM_ERR) {
                        pRespBuffer[SD_R5_RESP_FLAGS_OFFSET] |= SD_R5_ARG_RANGE_ERR;
                    }
                }
                    /* stuff read data */
                pRespBuffer[SD_SDIO_R5_READ_DATA_OFFSET] = readData;
                    /* stuff the command */
                SD_R5_SET_CMD(pRespBuffer,pReq->Command);
            } 
            pReq->Flags |= SDREQ_FLAGS_RESP_SPI_CONVERTED; 
            break;       
        case SDREQ_FLAGS_RESP_R2:
                /* for CMD13 and ACMD13 , SPI uses it's own R2 response format (2 bytes) */
                /* the issue of CMD13 needs to change the response flag to R2 */            
            if (CMD13 == pReq->Command) {    
                cardStatus = ConvertSPIStatusToSDCardStatus(
                                    GET_SPI_R2_RESP_TOKEN(pReq->Response),
                                    GET_SPI_R2_STATUS_TOKEN(pReq->Response));    
                    /* stuff the SD card status */
                SD_R1_SET_CMD_STATUS(pRespBuffer,cardStatus);
                    /* stuff the command */
                SD_R1_SET_CMD(pRespBuffer,pReq->Command);
                pReq->Flags |= SDREQ_FLAGS_RESP_SPI_CONVERTED; 
                break; 
            }
                /* no other commands should be using R2 when using SPI, if they are
                 * they should be bypassing the filter  */
            DBG_ASSERT(FALSE);
            break;        
        default: 
                /* for all others:
                 * 
                 * SDREQ_FLAGS_RESP_R6 - SPI mode does not use RCA  
                 * SDREQ_FLAGS_RESP_R3 - bus driver handles this internally
                 * SDREQ_FLAGS_RESP_SDIO_R4 - bus driver handles this internally
                 *
                 */   
            DBG_PRINT(SDDBG_ERROR, ("ConvertSPI_Response - invalid response type:0x%2.2X",
                                    GET_SDREQ_RESP_TYPE(pReq->Flags)));
            DBG_ASSERT(FALSE);
            break;
    }   
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Check an SD/MMC/SDIO response.

  @function name: SDIO_CheckResponse  
  @prototype: SDIO_STATUS SDIO_CheckResponse(PSDHCD pHcd, PSDREQUEST pReq, SDHCD_RESPONSE_CHECK_MODE CheckMode)
  @category: HD_Reference
  
  @input:  pHcd - the host controller definition structure.
  @input:  pReq - request containing the response
  @input:  CheckMode - mode

  @return: SDIO_STATUS 
 
  @notes: Host controller drivers must call into this function to validate various command 
          responses before continuing with data transfers or for decoding received SPI tokens.
          The CheckMode option determines the type of validation to perform.  
          if (CheckMode == SDHCD_CHECK_DATA_TRANS_OK) : 
             The host controller must check the card response to determine whether it
          is safe to perform a data transfer.  This API only checks commands that
          involve data transfers and checks various status fields in the command response.
          If the card cannot accept data, this function will return a non-successful status that 
          should be treated as a request failure.  The host driver should complete the request with the
          returned status. Host controller should only call this function in preparation for a
          data transfer.    
          if (CheckMode == SDHCD_CHECK_SPI_TOKEN) : 
             This API checks the SPI token and returns a timeout status if the illegal command bit is
          set.  This simulates the behavior of SD 1/4 bit operation where illegal commands result in 
          a command timeout.  A driver that supports SPI mode should pass every response to this 
          function to determine the appropriate error status to complete the request with.  If the 
          API returns success, the response indicates that the card accepted the command. 
 
  @example: Checking the response before starting the data transfer :
        if (SDIO_SUCCESS(status) && (pReq->Flags & SDREQ_FLAGS_DATA_TRANS)) {
                // check the response to see if we should continue with data
            status = SDIO_CheckResponse(pHcd, pReq, SDHCD_CHECK_DATA_TRANS_OK);
            if (SDIO_SUCCESS(status)) {
                .... start data transfer phase 
            } else {
               ... card response indicates that the card cannot handle data
                  // set completion status
               pRequest->Status = status;               
            }
        }
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  _SDIO_CheckResponse - check response on behalf of the host controller
  Input:  pHcd - host controller
          pReq - request containing the response
          CheckMode - mode
  Output: 
  Return: status
  Notes: 
  
    CheckMode == SDHCD_CHECK_DATA_TRANS_OK : 
    The host controller requests a check on the response to determine whether it
    is okay to perform a data transfer.  This function only filters on commands that
    involve data.  Host controller should only call this function in preparation for a
    data transfer.
    
    CheckMode == SDHCD_CHECK_SPI_TOKEN : 
    The bus driver checks the SPI token and returns a timeout status if the illegal command bit is
    set.  This simulates the behavior of SD native operation. 
    
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _SDIO_CheckResponse(PSDHCD pHcd, PSDREQUEST pReq, SDHCD_RESPONSE_CHECK_MODE CheckMode)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
     
    if (CheckMode == SDHCD_CHECK_DATA_TRANS_OK) {
        UINT32      cardStatus;
        UINT8       *pResponse;
        UINT8       convertedResponse[MAX_CARD_RESPONSE_BYTES];
        
        if (!(pReq->Flags & SDREQ_FLAGS_DATA_TRANS) ||
             (pReq->Flags & SDREQ_FLAGS_DATA_SKIP_RESP_CHK) ||
             (GET_SDREQ_RESP_TYPE(pReq->Flags) ==  SDREQ_FLAGS_NO_RESP)) {
            return SDIO_STATUS_SUCCESS;    
        } 
        pResponse = pReq->Response;
            /* check SPI mode */
        if (IS_HCD_BUS_MODE_SPI(pHcd)) {
            if (!(pReq->Flags & SDREQ_FLAGS_RESP_SKIP_SPI_FILT)) {
                    /* apply conversion */
                ConvertSPI_Response(pReq, NULL);     
            } else {
                    /* temporarily convert the response, without altering the original */
                ConvertSPI_Response(pReq, convertedResponse); 
                    /* point to the converted one */  
                pResponse = convertedResponse;
            } 
        }
        
        switch (GET_SDREQ_RESP_TYPE(pReq->Flags)) {    
            case SDREQ_FLAGS_RESP_R1:
            case SDREQ_FLAGS_RESP_R1B:
                cardStatus = SD_R1_GET_CARD_STATUS(pResponse);
                if (!(cardStatus & 
                     (SD_CS_ILLEGAL_CMD_ERR | SD_CS_CARD_INTERNAL_ERR | SD_CS_GENERAL_ERR))) {
                        /* okay for data */
                    break; 
                }
                    /* figure out what it was */
                if (cardStatus & SD_CS_ILLEGAL_CMD_ERR) {       
                    status = SDIO_STATUS_DATA_STATE_INVALID;
                } else {
                    status = SDIO_STATUS_DATA_ERROR_UNKNOWN;  
                }
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Check Response Error. R1 CardStatus:0x%X \n",
                                        cardStatus));
                break;
            case SDREQ_FLAGS_RESP_SDIO_R5:
                cardStatus = SD_R5_GET_RESP_FLAGS(pResponse);
                if (!(cardStatus & SD_R5_CURRENT_CMD_ERRORS)){
                        /* all okay */
                    break;   
                }
               
                status = ConvertCMD52ResponseToSDIOStatus((UINT8)cardStatus);
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Check Response Error. R5 CardStatus:0x%X \n",
                                        cardStatus));
                break;
            default:
                break;
        }
        
        return status;   
    }
    
    {
        UINT8       spiToken;
         
            /* handle SPI token validation */
        switch (GET_SDREQ_RESP_TYPE(pReq->Flags)) {
            case SDREQ_FLAGS_RESP_R2: 
                spiToken = GET_SPI_R2_RESP_TOKEN(pReq->Response);
                break;
            case SDREQ_FLAGS_RESP_SDIO_R5: 
                spiToken = GET_SPI_SDIO_R5_RESP_TOKEN(pReq->Response);
                break;
            case SDREQ_FLAGS_RESP_R3:
                spiToken = GET_SPI_R3_RESP_TOKEN(pReq->Response);
                break;
            case SDREQ_FLAGS_RESP_SDIO_R4:
                spiToken = GET_SPI_SDIO_R4_RESP_TOKEN(pReq->Response);
                break;
            default:
                    /* all other tokesn are SPI R1 type */
                spiToken = GET_SPI_R1_RESP_TOKEN(pReq->Response);    
                break;    
        } 
        
        if ((GET_SDREQ_RESP_TYPE(pReq->Flags) == SDREQ_FLAGS_RESP_SDIO_R5) ||
            (GET_SDREQ_RESP_TYPE(pReq->Flags) == SDREQ_FLAGS_RESP_SDIO_R4)) {
                /* handle SDIO status tokens */ 
            if ((spiToken & SPI_R5_ILLEGAL_CMD) ||
                (spiToken & SPI_R5_CMD_CRC)) {
                status = SDIO_STATUS_BUS_RESP_TIMEOUT; 
            }    
        } else {
                /* handle all other status tokens */ 
            if ((spiToken & SPI_CS_ILLEGAL_CMD) ||
                (spiToken & SPI_CS_CMD_CRC_ERR)) {
                status = SDIO_STATUS_BUS_RESP_TIMEOUT; 
            }
        }
    }
            
    return status;  
}

