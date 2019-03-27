/*******************************************************************************
**
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*
* $FreeBSD$
*
********************************************************************************/
/********************************************************************************
**    
*   dm.h 
*
*   Abstract:   This module defines the contants, enum and #define definition used
*               by Discovery Moduled (DM).
*     
********************************************************************************/

#ifndef DM_H

#define DM_H

/*************************************************
 *   constants for type field in agsaMem_t
 *************************************************/
#define DM_CACHED_MEM                             0x00     /**< CACHED memory type */
#define DM_DMA_MEM                                0x01     /**< DMA memory type */
#define DM_CACHED_DMA_MEM                         0x02     /**< CACHED DMA memory type */

/*************************************************
 *   constants for API return values
 *************************************************/
#define DM_RC_SUCCESS                             0x00     /**< Successful function return value */
#define DM_RC_FAILURE                             0x01     /**< Failed function return value */
#define DM_RC_BUSY                                0x02     /**< Busy function return value */
#define DM_RC_VERSION_INCOMPATIBLE                0x03     /**< Version miss match */
#define DM_RC_VERSION_UNTESTED                    0x04     /**< Version not tested */



/*************************************************
 *   Discovery option
 *************************************************/
#define DM_DISCOVERY_OPTION_FULL_START			0x00     /**< Full discovery */
#define DM_DISCOVERY_OPTION_INCREMENTAL_START		0x01     /**< Incremental discovery */
#define DM_DISCOVERY_OPTION_ABORT			0x02     /**< Discovery abort */


/*************************************************
 *   Discovery status
 *************************************************/
enum dmDiscoveryState_e
{
  dmDiscCompleted  = 0,
  dmDiscFailed,
  dmDiscAborted,
  dmDiscAbortFailed,
  dmDiscInProgress,
  dmDiscAbortInvalid, /* no discovery to abort */   
  dmDiscAbortInProgress, /* abort in progress */   

};

/*************************************************
 *   Device status
 *************************************************/
enum dmDeviceState_e
{
  dmDeviceNoChange = 0,
  dmDeviceArrival,
  dmDeviceRemoval,
  dmDeviceMCNChange,
  dmDeviceRateChange,
};

typedef struct  dmContext_s {
		void		*tdData;
		void		*dmData;
} dmContext_t;

typedef struct{
        bit16	smpTimeout;
        bit16	it_NexusTimeout;
        bit16	firstBurstSize;
        bit8	 flag;
        bit8	 devType_S_Rate;
        bit8 	sasAddressHi[4]; 
        bit8 	sasAddressLo[4];
        bit8    initiator_ssp_stp_smp;
        bit8    target_ssp_stp_smp;
        /* bit8 - bit14 are set by the user of DM such as TDM for directly attached expander
           0 - 7; PhyID 
           8: non SMP or not
           9 - 10: types of expander, valid only when bit8 is set
                   10b (2): edge expander
                   11b (3): fanout expander
           11 - 14: MCN
        */
        bit16   ext;
        bit8    sataDeviceType;
        bit8    reserved;
} dmDeviceInfo_t;


typedef struct{
	void 	*virtPtr;
	void 	*osHandle;
	bit32 	physAddrUpper;
	bit32 	physAddrLower;
	bit32 	totalLength;
	bit32 	numElements;
	bit32 	singleElementLength;
	bit32 	alignment;
	bit32 	type;
	bit32 	reserved;
} dmMem_t;

#define DM_NUM_MEM_CHUNKS 8

typedef struct{
	bit32 		count;
	dmMem_t 	dmMemory[DM_NUM_MEM_CHUNKS];
} dmMemoryRequirement_t;

typedef    dmContext_t    dmPortContext_t;

typedef    dmContext_t    dmRoot_t;

typedef struct{
 bit32   numDevHandles;
 bit32   tbd1;
 bit32   tbd2;
#ifdef DM_DEBUG
 bit32   DMDebugLevel;
#endif
 bit32   itNexusTimeout;
} dmSwConfig_t;

typedef struct{
               bit8  	sasRemoteAddressHi[4]; 
               bit8  	sasRemoteAddressLo[4]; 
               bit8  	sasLocalAddressHi[4]; 
               bit8  	sasLocalAddressLo[4]; 
               bit32    flag;
} dmPortInfo_t;


#endif  /* DM_H */
