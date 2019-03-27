/*******************************************************************************
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
/*******************************************************************************/
/** \file
 *
 * function definitions used in SAS/SATA TD layer
 *
 */

#ifndef __TDPROTO_H__
#define __TDPROTO_H__

#ifdef FDS_DM
#include <dev/pms/RefTisa/discovery/api/dm.h>
#endif

/*****************************************************************************
*
* SA SHARED PROTOTYPES
*
*****************************************************************************/

osGLOBAL void 
tdsaQueueConfigInit(
             tiRoot_t *tiRoot
             );

osGLOBAL void 
tdsaEsglInit(
             tiRoot_t *tiRoot
             );

osGLOBAL void
tdsaResetComMemFlags(
                   tiRoot_t *tiRoot
                   );


osGLOBAL void
tdsaInitTimerRequest(
                     tiRoot_t                *tiRoot,
                     tdsaTimerRequest_t      *timerRequest
                     );
                     
osGLOBAL void
tdsaSetTimerRequest(
                  tiRoot_t            *tiRoot,
                  tdsaTimerRequest_t  *timerRequest,
                  bit32               timeout,
                  tdsaTimerCBFunc_t   CBFunc,
                  void                *timerData1,
                  void                *timerData2,
                  void                *timerData3
                  );
                  
osGLOBAL void
tdsaAddTimer (
              tiRoot_t            *tiRoot,
              tdList_t            *timerListHdr, 
              tdsaTimerRequest_t  *timerRequest
             );
             
osGLOBAL void
tdsaKillTimer(
              tiRoot_t            *tiRoot,
              tdsaTimerRequest_t  *timerRequest
              );
              
              
             
                  

osGLOBAL void
tdsaLoLevelGetResource (
                        tiRoot_t              * tiRoot, 
                        tiLoLevelResource_t   * loResource
                        );

osGLOBAL void
tdsaSharedMemCalculate (
                        tiRoot_t              * tiRoot,
                        tiLoLevelResource_t   * loResource,
                        tiTdSharedMem_t       * tdSharedMem
                        );

osGLOBAL void 
tdsaGetEsglPagesInfo(
                     tiRoot_t *tiRoot, 
                     bit32    *PageSize,
                     bit32    *NumPages
                     );

osGLOBAL void 
tdsaGetPortParams(
                  tiRoot_t *tiRoot
                  );



osGLOBAL void 
tdsaGetSwConfigParams(
                      tiRoot_t *tiRoot
                      );

osGLOBAL void 
tdsaGetHwConfigParams(
                      tiRoot_t *tiRoot
                      );

osGLOBAL void 
tdsaGetCardPhyParams(
                       tiRoot_t *tiRoot
                       );


osGLOBAL void 
tdsaGetGlobalPhyParams(
                       tiRoot_t *tiRoot
                       );

osGLOBAL bit32 
tdsaGetCardIDString(
                    tiRoot_t *tiRoot
                    );
                                  
osGLOBAL void
tdsaParseLinkRateMode(
                      tiRoot_t *tiRoot,
                      bit32 index,
                      bit32 LinkRateRead,
                      bit32 ModeRead,
                      bit32 OpticalModeRead,
                      bit32 LinkRate, 
                      bit32 Mode,
                      bit32 OpticalMode
                      );

osGLOBAL void
tdsaInitTimers(
               tiRoot_t *tiRoot 
               );

osGLOBAL void 
tdsaProcessTimers(
                  tiRoot_t *tiRoot
                  );

osGLOBAL void 
tdsaInitTimerHandler(
                     tiRoot_t  *tiRoot,
                     void      *timerData
                     );

osGLOBAL void
tdsaGetEsglPages(
                 tiRoot_t *tiRoot,
                 tdList_t *EsglListHdr,
                 tiSgl_t  *ptiSgl,
                 tiSgl_t  *virtSgl
                 );

osGLOBAL void
tdsaFreeEsglPages(
                  tiRoot_t *tiRoot,
                  tdList_t *EsglListHdr
                  );

osGLOBAL void 
tdssGetMaxTargetsParams(
                      tiRoot_t                *tiRoot, 
                      bit32                   *pMaxTargets
                      );

osGLOBAL void 
tdssGetSATAOnlyModeParams(
                      tiRoot_t                *tiRoot, 
                      bit32                   *pMaxTargets
                      );
                      
osGLOBAL bit32 
tdipFWControlIoctl(
                   tiRoot_t            *tiRoot,
                   tiIOCTLPayload_t    *agIOCTLPayload,
                   void                *agParam1,
                   void                *agParam2,
                   void                *agParam3
                   );

osGLOBAL bit32 
tdsaVPDGetIoctl(
                tiRoot_t            *tiRoot,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2,
                void                *agParam3
                );

osGLOBAL bit32 
tdsaVPDSetIoctl(
                tiRoot_t            *tiRoot,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2,
                void                *agParam3
                );

osGLOBAL void
ostiCOMMgntVPDSetIOCTLRsp(
                          tiRoot_t            *tiRoot,
                          bit32               status
                          );

osGLOBAL void 
tdsaFreeCardID(tiRoot_t *tiRoot,
               bit32    CardID
               );


osGLOBAL bit32
tdsaAbortAll( 
             tiRoot_t                   *tiRoot,
             agsaRoot_t                 *agRoot,
             tdsaDeviceData_t           *oneDeviceData
             );
                
osGLOBAL bit32
tdsaFindLocalMCN( 
                 tiRoot_t                   *tiRoot,
                 tdsaPortContext_t          *onePortContext
                );
	     
osGLOBAL bit32 
tdsaRegDumpGetIoctl(
                tiRoot_t            *tiRoot,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2,
                void                *agParam3
                );

osGLOBAL bit32 
tdsaNVMDSetIoctl(
                tiRoot_t            *tiRoot,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2,
                void                *agParam3
                );

osGLOBAL bit32 
tdsaNVMDGetIoctl(
                tiRoot_t            *tiRoot,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2,
                void                *agParam3
                );

osGLOBAL void ostiGetGpioIOCTLRsp(
		tiRoot_t	       *tiRoot,
		bit32		       status,
		bit32		       gpioReadValue,
		agsaGpioPinSetupInfo_t *gpioPinSetupInfo,
		agsaGpioEventSetupInfo_t *gpioEventSetupInfo
		);

osGLOBAL bit32
tdsaGpioSetup(
		tiRoot_t		*tiRoot,
		agsaContext_t		*agContext,
		tiIOCTLPayload_t	*agIOCTLPayload,
		void			*agParam1,
		void			*agParam2
		);


osGLOBAL bit32
tdsaSGpioIoctlSetup(
                    tiRoot_t            *tiRoot,
                    agsaContext_t       *agContext,
                    tiIOCTLPayload_t    *agIOCTLPayload,
                    void                *agParam1,
                    void                *agParam2
                    );

osGLOBAL void ostiSgpioIoctlRsp(
                                tiRoot_t                *tiRoot,
                                agsaSGpioReqResponse_t  *pSgpioResponse
                                );
osGLOBAL bit32
tdsaDeviceInfoGetIoctl(
                tiRoot_t            *tiRoot,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2,
                void                *agParam3
                );

osGLOBAL bit32
tdsaIoErrorStatisticGetIoctl(
                 tiRoot_t            *tiRoot,
                 tiIOCTLPayload_t    *agIOCTLPayload,
                 void                *agParam1,
                 void                *agParam2,
                 void                *agParam3
                 );

osGLOBAL bit32
tdsaIoEventStatisticGetIoctl(
                 tiRoot_t            *tiRoot,
                 tiIOCTLPayload_t    *agIOCTLPayload,
                 void                *agParam1,
                 void                *agParam2,
                 void                *agParam3
                 );

osGLOBAL bit32
tdsaForensicDataGetIoctl(
                 tiRoot_t            *tiRoot,
                 tiIOCTLPayload_t    *agIOCTLPayload,
                 void                *agParam1,
                 void                *agParam2,
                 void                *agParam3
                 );

osGLOBAL bit32
tdsaSendSMPIoctl(
                tiRoot_t            *tiRoot,
                tiIOCTLPayload_t    *agIOCTLPayload,
                void                *agParam1,
                void                *agParam2,
                void                *agParam3
                );

osGLOBAL bit32
tdsaSendBISTIoctl(
                 tiRoot_t            *tiRoot,
                 tiIOCTLPayload_t    *agIOCTLPayload,
                 void                *agParam1,
                 void                *agParam2,
                 void                *agParam3
                 );

osGLOBAL bit32
tdsaSendTMFIoctl(
		tiRoot_t	*tiRoot,
		tiIOCTLPayload_t *agIOCTLPayload,
		void		*agParam1,
		void		*agParam2,
		unsigned long	resetType
	       );


osGLOBAL bit32
tdsaRegisterIoctl(
                 tiRoot_t            *tiRoot,
                 tiIOCTLPayload_t    *agIOCTLPayload,
                 void                *agParam1,
                 void                *agParam2,
                 void                *agParam3
                 );

osGLOBAL bit32
tdsaGetPhyGeneralStatusIoctl(
	                tiRoot_t                  *tiRoot,
	                agsaPhyGeneralState_t     *PhyData
	                );
	
osGLOBAL void ostiGetPhyGeneralStatusRsp(
									tiRoot_t				      *tiRoot,
                                    agsaSASPhyGeneralStatusPage_t *GenStatus,
                                    bit32                          phyID
									);


osGLOBAL bit32
tdsaPhyProfileIoctl(
                 tiRoot_t            *tiRoot,
                 tiIOCTLPayload_t    *agIOCTLPayload,
                 void                *agParam1,
                 void                *agParam2,
                 void                *agParam3
                 );

osGLOBAL void 
tdsaDeregisterDevicesInPort(
                tiRoot_t             *tiRoot,
                tdsaPortContext_t    *onePortContext
               );

#ifdef VPD_TESTING
osGLOBAL bit32 
tdsaVPDGet(
                tiRoot_t            *tiRoot
                );

osGLOBAL bit32 
tdsaVPDSet(
                tiRoot_t            *tiRoot
                );
                
#endif                
 
/*****************************************************************************
*
* SAS SHARED PROTOTYPES
*
*****************************************************************************/
osGLOBAL void
tdsaJumpTableInit(
                  tiRoot_t *tiRoot
                  );

osGLOBAL void
tdsaPortContextInit(
                    tiRoot_t *tiRoot
                    );
            
osGLOBAL void
tdsaPortContextReInit(
                      tiRoot_t             *tiRoot,
                      tdsaPortContext_t    *onePortContext           
                    );

osGLOBAL void
tdsaDeviceDataInit(
                   tiRoot_t *tiRoot
                   );
           
osGLOBAL void
tdsaDeviceDataReInit(
                   tiRoot_t             *tiRoot, 
                   tdsaDeviceData_t     *oneDeviceData
                   );

#ifdef TD_INT_COALESCE
osGLOBAL void
tdsaIntCoalCxtInit(
                    tiRoot_t *tiRoot 
                    );
#endif

osGLOBAL FORCEINLINE bit32
tdsaRotateQnumber(tiRoot_t                *tiRoot,
                  tdsaDeviceData_t        *oneDeviceData);

osGLOBAL bit32
tdsaRotateQnumber1(tiRoot_t                *tiRoot,
                  tdsaDeviceData_t        *oneDeviceData );
osGLOBAL void
tdssRemoveSASSATAFromSharedcontext(
                          agsaRoot_t           *agRoot,
                          tdsaPortContext_t    *PortContext_Instance
                          );
osGLOBAL void
tdssRemoveSASSATAFromSharedcontextByReset(
                          agsaRoot_t           *agRoot
                          );
osGLOBAL bit32
tdssSASFindDiscoveringExpander(
                          tiRoot_t                 *tiRoot,
                          tdsaPortContext_t        *onePortContext,
                          tdsaExpander_t           *oneExpander
                          );

osGLOBAL void
tdssAddSASToSharedcontext(
                          tdsaPortContext_t    *tdsaPortContext_Instance,
                          agsaRoot_t           *agRoot,
                          agsaDevHandle_t      *agDevHandle,
                          tdsaSASSubID_t       *agSASSubID,
                          bit32                registered,
                          bit8                 phyID,
                          bit32                flag
                          );

osGLOBAL void
tdssRemoveSASFromSharedcontext(
                               tdsaPortContext_t *tdsaPortContext_Ins,
                               tdsaDeviceData_t  *tdsaDeviceData_ins,
                               agsaRoot_t        *agRoot
                               );

osGLOBAL void
tdssRemoveAllDevicelistFromPortcontext(
                                       tdsaPortContext_t *PortContext_Ins,
                                       agsaRoot_t        *agRoot
                                       );
                                                                             
osGLOBAL void
tdssAddSATAToSharedcontext( tdsaPortContext_t    *tdsaPortContext_Instance,
                            agsaRoot_t           *agRoot,
                            agsaDevHandle_t      *agDevHandle,
                            agsaSATADeviceInfo_t *agSATADeviceInfo,
                            bit32                 registered,
                            bit8                  phyID
                            );
                                                                             
osGLOBAL void
tdssSubAddSATAToSharedcontext( tiRoot_t             *tiRoot,
                               tdsaDeviceData_t     *oneDeviceData
                              );
                                                                             
osGLOBAL void
tdssRetrySATAID( tiRoot_t             *tiRoot,
                 tdsaDeviceData_t     *oneDeviceData
               );

osGLOBAL void 
tdssInitSASPortStartInfo(
                         tiRoot_t *tiRoot
                         );
#ifndef ossaDeviceRegistrationCB 
osGLOBAL void
ossaDeviceRegistrationCB(
                         agsaRoot_t        *agRoot,
                         agsaContext_t     *agContext,
                         bit32             status,
                         agsaDevHandle_t   *agDevHandle,
                         bit32                   deviceID);
#endif

#ifndef ossaDeregisterDeviceHandleCB
osGLOBAL void
ossaDeregisterDeviceHandleCB(
                             agsaRoot_t          *agRoot,
                             agsaContext_t       *agContext, 
                             agsaDevHandle_t     *agDevHandle,
                             bit32               status
                             );
#endif

#ifdef INITIATOR_DRIVER
/*****************************************************************************
*
* SAS Initiator only PROTOTYPES
*
*****************************************************************************/
osGLOBAL bit32
itdssInit(
          tiRoot_t              *tiRoot,
          tiInitiatorResource_t *initiatorResource,
          tiTdSharedMem_t       *tdSharedMem
          );

osGLOBAL void 
itdssInitTimers ( 
                 tiRoot_t *tiRoot 
                 );

osGLOBAL FORCEINLINE void
itdssIOCompleted(
                 agsaRoot_t             *agRoot,
                 agsaIORequest_t        *agIORequest,
                 bit32                  agIOStatus, 
                 bit32                  agIOInfoLen,
                 void                   *agParam,
                 bit32                  agOtherInfo
                 );

osGLOBAL void
itdssTaskCompleted(
                   agsaRoot_t             *agRoot,
                   agsaIORequest_t        *agIORequest,
                   bit32                  agIOStatus, 
                   bit32                  agIOInfoLen,
                   void                   *agParam,
                   bit32                  agOtherInfo
                   );

osGLOBAL void
itdssQueryTaskCompleted(
                        agsaRoot_t             *agRoot,
                        agsaIORequest_t        *agIORequest,
                        bit32                  agIOStatus, 
                        bit32                  agIOInfoLen,
                        void                   *agParam,
                        bit32                  agOtherInfo
                        );

osGLOBAL void
itdssSMPCompleted (
                   agsaRoot_t            *agRoot,
                   agsaIORequest_t       *agIORequest,
                   bit32                 agIOStatus,
                   bit32                 agIOInfoLen,
                   agsaFrameHandle_t     agFrameHandle                   
                   );

osGLOBAL void 
ossaSMPCAMCompleted(
                    agsaRoot_t            *agRoot,
                    agsaIORequest_t       *agIORequest,
                    bit32                 agIOStatus,
                    bit32                 agIOInfoLen,
                    agsaFrameHandle_t     agFrameHandle
                   );

osGLOBAL void 
itdssIOSuccessHandler(
                      agsaRoot_t           *agRoot, 
                      agsaIORequest_t      *agIORequest, 
                      bit32                agIOStatus,  
                      bit32                agIOInfoLen,
                      void                 *agParam,
                      bit32                 agOtherInfo
                      );
osGLOBAL void 
itdssIOAbortedHandler(
                      agsaRoot_t           *agRoot, 
                      agsaIORequest_t      *agIORequest, 
                      bit32                agIOStatus,  
                      bit32                agIOInfoLen,
                      void                 *agParam,
                      bit32                agOtherInfo
                      );

#ifdef REMOVED
osGLOBAL void 
itdssIOOverFlowHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                 agIOStatus,  
                       bit32                 agIOInfoLen,
                       void                 *agParam
                       );
#endif
               
osGLOBAL void 
itdssIOUnderFlowHandler(
                        agsaRoot_t           *agRoot, 
                        agsaIORequest_t      *agIORequest, 
                        bit32                agIOStatus,  
                        bit32                agIOInfoLen,
                        void                 *agParam,
                        bit32                agOtherInfo
                        );

osGLOBAL void 
itdssIOFailedHandler(
                     agsaRoot_t           *agRoot, 
                     agsaIORequest_t      *agIORequest, 
                     bit32                agIOStatus,  
                     bit32                agIOInfoLen,
                     void                 *agParam,
                     bit32                agOtherInfo
                     );

osGLOBAL void 
itdssIOAbortResetHandler(
                         agsaRoot_t           *agRoot, 
                         agsaIORequest_t      *agIORequest, 
                         bit32                agIOStatus,  
                         bit32                agIOInfoLen,
                         void                 *agParam,
                         bit32                agOtherInfo
                         );
osGLOBAL void 
itdssIONotValidHandler(
                       agsaRoot_t               *agRoot, 
                       agsaIORequest_t          *agIORequest, 
                       bit32                    agIOStatus,  
                       bit32                    agIOInfoLen,
                       void                     *agParam,
                       bit32                    agOtherInfo
                       );

osGLOBAL void 
itdssIONoDeviceHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );

#ifdef REMOVED /* removed from spec */
osGLOBAL void 
itdssIllegalParameterHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam
                       );
#endif                       

osGLOBAL void 
itdssLinkFailureHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssProgErrorHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorBreakHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorPhyNotReadyHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssOpenCnxErrorProtocolNotSupprotedHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssOpenCnxErrorZoneViolationHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssOpenCnxErrorBreakHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssOpenCnxErrorITNexusLossHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssOpenCnxErrorBadDestinationHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssOpenCnxErrorConnectionRateNotSupportedHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );

osGLOBAL void 
itdssOpenCnxErrorSTPResourceBusyHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssOpenCnxErrorWrongDestinationHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssOpenCnxErrorUnknownErrorHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorNAKReceivedHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorACKNAKTimeoutHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorPeerAbortedHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorRxFrameHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorDMAHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorCreditTimeoutHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );

osGLOBAL void 
itdssXferErrorCMDIssueACKNAKTimeoutHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorCMDIssueBreakBeforeACKNAKHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorCMDIssuePhyDownBeforeACKNAKHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorDisruptedPhyDownHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorOffsetMismatchHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorXferZeroDataLenHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );

osGLOBAL void 
itdssXferOpenRetryTimeoutHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );

osGLOBAL void 
itdssPortInResetHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );

osGLOBAL void 
itdssDsNonOperationalHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssDsInRecoveryHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssTmTagNotFoundHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssSSPExtIUZeroLenHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void 
itdssXferErrorUnexpectedPhaseHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );

#ifdef REMOVED             
osGLOBAL void 
itdssIOUnderFlowWithChkConditionHandler(
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam
                       );
#endif

osGLOBAL void 
itdssEncryptionHandler (
                       agsaRoot_t              *agRoot, 
                       agsaIORequest_t         *agIORequest, 
                       bit32                   agIOStatus,  
                       bit32                   agIOInfoLen,
                       void                    *agParam,
                       bit32                   agOtherInfo
                       );

osGLOBAL void 
itdssXferOpenRetryBackoffThresholdReachedHandler(
                                                 agsaRoot_t           *agRoot, 
                                                 agsaIORequest_t      *agIORequest, 
                                                 bit32                agIOStatus,  
                                                 bit32                agIOInfoLen,
                                                 void                 *agParam,
                                                 bit32                agOtherInfo
                                                );

osGLOBAL void 
itdssOpenCnxErrorItNexusLossOpenTmoHandler(
                                           agsaRoot_t           *agRoot, 
                                           agsaIORequest_t      *agIORequest, 
                                           bit32                agIOStatus,  
                                           bit32                agIOInfoLen,
                                           void                 *agParam,
                                           bit32                agOtherInfo
                                          );
osGLOBAL void 
itdssOpenCnxErrorItNexusLossNoDestHandler(
                                          agsaRoot_t           *agRoot, 
                                          agsaIORequest_t      *agIORequest, 
                                          bit32                agIOStatus,  
                                          bit32                agIOInfoLen,
                                          void                 *agParam,
                                          bit32                agOtherInfo
                                         );
osGLOBAL void 
itdssOpenCnxErrorItNexusLossOpenCollideHandler(
                                               agsaRoot_t           *agRoot, 
                                               agsaIORequest_t      *agIORequest, 
                                               bit32                agIOStatus,  
                                               bit32                agIOInfoLen,
                                               void                 *agParam,
                                               bit32                agOtherInfo
                                              );
osGLOBAL void 
itdssOpenCnxErrorItNexusLossOpenPathwayBlockedHandler(
                                                      agsaRoot_t           *agRoot, 
                                                      agsaIORequest_t      *agIORequest, 
                                                      bit32                agIOStatus,  
                                                      bit32                agIOInfoLen,
                                                      void                 *agParam,
                                                      bit32                agOtherInfo
                                                     );
osGLOBAL void 
itdssDifHandler(
                agsaRoot_t           *agRoot, 
                agsaIORequest_t      *agIORequest, 
                bit32                agIOStatus,  
                bit32                agIOInfoLen,
                void                 *agParam,
                bit32                agOtherInfo
               );
	       
osGLOBAL void 
itdssIOResourceUnavailableHandler(
                                  agsaRoot_t              *agRoot, 
                                  agsaIORequest_t         *agIORequest, 
                                  bit32                   agIOStatus,  
                                  bit32                   agIOInfoLen,
                                  void                    *agParam,
                                  bit32                   agOtherInfo
                                 );

osGLOBAL void 
itdssIORQEBusyFullHandler(
                                  agsaRoot_t              *agRoot, 
                                  agsaIORequest_t         *agIORequest, 
                                  bit32                   agIOStatus,  
                                  bit32                   agIOInfoLen,
                                  void                    *agParam,
                                  bit32                   agOtherInfo
                                 );

osGLOBAL void 
itdssXferErrorInvalidSSPRspFrameHandler(
                                  agsaRoot_t              *agRoot, 
                                  agsaIORequest_t         *agIORequest, 
                                  bit32                   agIOStatus,  
                                  bit32                   agIOInfoLen,
                                  void                    *agParam,
                                  bit32                   agOtherInfo
                                 );

osGLOBAL void 
itdssXferErrorEOBDataOverrunHandler(
                                  agsaRoot_t              *agRoot, 
                                  agsaIORequest_t         *agIORequest, 
                                  bit32                   agIOStatus,  
                                  bit32                   agIOInfoLen,
                                  void                    *agParam,
                                  bit32                   agOtherInfo
                                 );

osGLOBAL void 
itdssOpenCnxErrorOpenPreemptedHandler(
                                  agsaRoot_t              *agRoot, 
                                  agsaIORequest_t         *agIORequest, 
                                  bit32                   agIOStatus,  
                                  bit32                   agIOInfoLen,
                                  void                    *agParam,
                                  bit32                   agOtherInfo
                                 );
				 
/* default handler */
osGLOBAL void 
itdssIODefaultHandler (
                       agsaRoot_t           *agRoot, 
                       agsaIORequest_t      *agIORequest, 
                       bit32                agIOStatus,  
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       );
osGLOBAL void
itdssIOForDebugging1Completed(
                 agsaRoot_t             *agRoot,
                 agsaIORequest_t        *agIORequest,
                 bit32                  agIOStatus, 
                 bit32                  agIOInfoLen,
                 void                   *agParam,
                 bit32                  agOtherInfo
                 );

osGLOBAL void
itdssIOForDebugging2Completed(
                 agsaRoot_t             *agRoot,
                 agsaIORequest_t        *agIORequest,
                 bit32                  agIOStatus, 
                 bit32                  agIOInfoLen,
                 void                   *agParam,
                 bit32                  agOtherInfo
                 );

osGLOBAL void
itdssIOForDebugging3Completed(
                 agsaRoot_t             *agRoot,
                 agsaIORequest_t        *agIORequest,
                 bit32                  agIOStatus, 
                 bit32                  agIOInfoLen,
                 void                   *agParam,
                 bit32                  agOtherInfo
                 );

osGLOBAL void 
itdssInitDiscoveryModule (
                          tiRoot_t *tiRoot
                          );

osGLOBAL void
itdssGetResource (
                  tiRoot_t              *tiRoot,
                  tiInitiatorResource_t *initiatorResource
                  );


osGLOBAL void 
itdssGetOperatingOptionParams(
                              tiRoot_t              *tiRoot, 
                              itdssOperatingOption_t  *OperatingOption
                              );


osGLOBAL FORCEINLINE bit32
itdssIOPrepareSGL(
                  tiRoot_t            *tiRoot,
                  tdIORequestBody_t   *IORequestBody,
                  tiSgl_t             *tiSgl1,
                  void                *sglVirtualAddr
                  );

#ifdef FDS_SM
osGLOBAL void	
smReportRemoval(
                 tiRoot_t             *tiRoot,
                 agsaRoot_t           *agRoot,
                 tdsaDeviceData_t     *oneDeviceData,
                 tdsaPortContext_t    *onePortContext
	       );
osGLOBAL void	
smReportRemovalDirect(
                       tiRoot_t             *tiRoot,
                       agsaRoot_t           *agRoot,
                       tdsaDeviceData_t     *oneDeviceData
		     );
osGLOBAL void	
smHandleDirect(
                tiRoot_t             *tiRoot,
                agsaRoot_t           *agRoot,
                tdsaDeviceData_t     *oneDeviceData,
                void                 *IDdata
	      );
	      
osGLOBAL void 
ossaSATAIDAbortCB(
                  agsaRoot_t               *agRoot,
                  agsaIORequest_t          *agIORequest,
                  bit32                    flag,
                  bit32                    status
                 );

osGLOBAL void 
ossaIniSetDeviceInfoCB(
                        agsaRoot_t        *agRoot,
                        agsaContext_t     *agContext, 
                        agsaDevHandle_t   *agDevHandle,
                        bit32             status,
                        bit32             option,
                        bit32             param
                      );

#endif /* FDS_SM */

#endif /* INITIATOR_DRIVER */

#ifdef TARGET_DRIVER
/*****************************************************************************
*
* SAS Target only PROTOTYPES (ttdproto.h)
*
*****************************************************************************/
osGLOBAL bit32
ttdssInit(
          tiRoot_t              *tiRoot,
          tiTargetResource_t    *targetResource,
          tiTdSharedMem_t       *tdSharedMem
          );

osGLOBAL void
ttdssGetResource (
                  tiRoot_t              *tiRoot,
                  tiTargetResource_t    *targetResource
                  );

osGLOBAL void 
ttdssGetTargetParams(
                     tiRoot_t          *tiRoot
                     );

osGLOBAL void 
ttdssGetOperatingOptionParams(
                              tiRoot_t                *tiRoot, 
                              ttdssOperatingOption_t  *OperatingOption
                              );

osGLOBAL agBOOLEAN
ttdsaXchgInit(
              tiRoot_t           *tiRoot,
              ttdsaXchgData_t    *ttdsaXchgData,
              tiTargetMem_t      *tgtMem,
              bit32              maxNumXchgs
              );

osGLOBAL void
ttdsaXchgLinkInit(
                   tiRoot_t           *tiRoot,
                   ttdsaXchg_t        *ttdsaXchg
                   );


osGLOBAL void
ttdsaXchgFreeStruct(
                   tiRoot_t           *tiRoot,
                   ttdsaXchg_t        *ttdsaXchg
                   );
osGLOBAL void
ttdsaSSPReqReceived(
                   agsaRoot_t           *agRoot,
                   agsaDevHandle_t      *agDevHandle,
                   agsaFrameHandle_t    agFrameHandle,
                   bit32                agInitiatorTag,
                   bit32                parameter,      
                   bit32                agFrameLen                                            
                   );

osGLOBAL ttdsaXchg_t
*ttdsaXchgGetStruct(
                    agsaRoot_t *agRoot
                    );
osGLOBAL void
ttdsaDumpallXchg(tiRoot_t           *tiRoot);

osGLOBAL void
tdsaProcessCDB(
               agsaSSPCmdInfoUnit_t      *cmdIU,
               ttdsaXchg_t               *ttdsaXchg
               );

osGLOBAL bit32
ttdssIOPrepareSGL(
                  tiRoot_t                 *tiRoot,
                  tdIORequestBody_t        *tdIORequestBody,
                  tiSgl_t                  *tiSgl1,
                  tiSgl_t                  *tiSgl2,
                  void                     *sglVirtualAddr);

osGLOBAL void
ttdsaIOCompleted(
                 agsaRoot_t             *agRoot,
                 agsaIORequest_t        *agIORequest,
                 bit32                  agIOStatus, 
                 bit32                  agIOInfoLen,
                 agsaFrameHandle_t      agFrameHandle,
                 bit32                  agOtherInfo
                 );

osGLOBAL void
ttdsaTMProcess(
               tiRoot_t    *tiRoot,
               ttdsaXchg_t *ttdsaXchg
               );

osGLOBAL void
ttdsaSMPReqReceived(
                    agsaRoot_t            *agRoot,
                    agsaDevHandle_t       *agDevHandle,
                    agsaSMPFrameHeader_t  *agFrameHeader,
                    agsaFrameHandle_t     agFrameHandle,
                    bit32                 agFrameLength,
                    bit32                 phyId
                    );
osGLOBAL void
ttdsaSMPCompleted(
                  agsaRoot_t            *agRoot,
                  agsaIORequest_t       *agIORequest,
                  bit32                 agIOStatus,
                  //agsaSMPFrameHeader_t  *agFrameHeader,   (TP)
                  bit32                 agIOInfoLen,
                  agsaFrameHandle_t     agFrameHandle
                  );
osGLOBAL bit32
ttdsaSendResp(
              agsaRoot_t            *agRoot,
              ttdsaXchg_t           *ttdsaXchg
              );
              
osGLOBAL void
ttdssReportRemovals(
                    agsaRoot_t           *agRoot,
                    tdsaPortContext_t    *onePortContext,
                    bit32                flag
                    );

              
osGLOBAL void
ttdsaAbortAll( 
             tiRoot_t                   *tiRoot,
             agsaRoot_t                 *agRoot,
             tdsaDeviceData_t           *oneDeviceData
             );
             
osGLOBAL void 
ttdssIOAbortedHandler(
                      agsaRoot_t           *agRoot, 
                      agsaIORequest_t      *agIORequest, 
                      bit32                agIOStatus,  
                      bit32                agIOInfoLen,
                      void                 *agParam,
                      bit32                agOtherInfo
                      );

#endif /* TARGET_DRIVER */



/*****************************************************************************
*
* For debugging only 
*
*****************************************************************************/
osGLOBAL void
tdsaPrintSwConfig(
                agsaSwConfig_t *SwConfig
                );

osGLOBAL void
tdsaPrintHwConfig(
                agsaHwConfig_t *HwConfig
                );
osGLOBAL void
tdssPrintSASIdentify(
                     agsaSASIdentify_t *id
                     );
osGLOBAL void
print_tdlist_flink(tdList_t *hdr, int type, int flag);

osGLOBAL void
print_tdlist_blink(tdList_t *hdr, int flag);

osGLOBAL void
tdhexdump(const char *ptitle, bit8 *pbuf, int len);


/*****************************************************************************
*
* SAT only PROTOTYPE
*
*****************************************************************************/

#ifdef  SATA_ENABLE

/*****************************************************************************
 *! \brief  satIOStart
 *
 *   This routine is called to initiate a new SCSI request to SATL.
 * 
 *  \param   tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param   tiIORequest:      Pointer to TISA I/O request context for this I/O.
 *  \param   tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param   tiScsiRequest:    Pointer to TISA SCSI I/O request and SGL list. 
 *  \param   satIOContext_t:   Pointer to the SAT IO Context
 *
 *  \return: 
 *       
 *  \e tiSuccess:     I/O request successfully initiated. 
 *  \e tiBusy:        No resources available, try again later.
 *  \e tiIONoDevice:  Invalid device handle.
 *  \e tiError:       Other errors that prevent the I/O request to be started.
 * 
 *
 *****************************************************************************/

GLOBAL bit32  satIOStart(
                   tiRoot_t                  *tiRoot, 
                   tiIORequest_t             *tiIORequest,
                   tiDeviceHandle_t          *tiDeviceHandle,
                   tiScsiInitiatorRequest_t *tiScsiRequest,
                   satIOContext_t            *satIOContext
                  );

/*****************************************************************************
 *! \brief  satIOAbort
 *
 *   This routine is called to initiate a I/O abort to SATL.
 *   This routine is independent of HW/LL API.
 * 
 *  \param  tiRoot:     Pointer to TISA initiator driver/port instance.
 *  \param  taskTag:    Pointer to TISA I/O request context/tag to be aborted.
 *
 *  \return: 
 *       
 *  \e tiSuccess:     I/O request successfully initiated. 
 *  \e tiBusy:        No resources available, try again later.
 *  \e tiError:       Other errors that prevent the I/O request to be started.
 * 
 *
 *****************************************************************************/

GLOBAL bit32 satIOAbort(
                          tiRoot_t      *tiRoot,
                          tiIORequest_t *taskTag );


/*****************************************************************************
 *! \brief  satTM
 *
 *   This routine is called to initiate a TM request to SATL.
 *   This routine is independent of HW/LL API.
 * 
 *  \param  tiRoot:           Pointer to TISA initiator driver/port instance.
 *  \param  tiDeviceHandle:   Pointer to TISA device handle for this I/O.
 *  \param  task:             SAM-3 task management request.
 *  \param  lun:              Pointer to LUN.
 *  \param  taskTag:          Pointer to the associated task where the TM
 *                            command is to be applied. 
 *  \param  currentTaskTag:   Pointer to tag/context for this TM request. 
 *
 *  \return: 
 *       
 *  \e tiSuccess:     I/O request successfully initiated. 
 *  \e tiBusy:        No resources available, try again later.
 *  \e tiIONoDevice:  Invalid device handle.
 *  \e tiError:       Other errors that prevent the I/O request to be started.
 * 
 *
 *****************************************************************************/

osGLOBAL bit32 satTM(
                        tiRoot_t          *tiRoot,
                        tiDeviceHandle_t  *tiDeviceHandle,
                        bit32             task,
                        tiLUN_t           *lun,
                        tiIORequest_t     *taskTag,
                        tiIORequest_t     *currentTaskTag,
                        tdIORequestBody_t *tiRequestBody,
                        bit32              NotifyOS
                        );


#endif  /* SAT only */

#ifdef INITIATOR_DRIVER
#ifdef TD_DISCOVER

osGLOBAL void
tdsaExpanderInit(
                 tiRoot_t *tiRoot 
                 );

osGLOBAL bit32
tdsaDiscover(
             tiRoot_t          *tiRoot,
             tdsaPortContext_t *onePortContext,
             bit32             type,
             bit32             option
             );

osGLOBAL bit32
tdsaSASFullDiscover(
                    tiRoot_t          *tiRoot,
                    tdsaPortContext_t *onePortContext
                    );

osGLOBAL bit32
tdsaSATAFullDiscover(
                     tiRoot_t          *tiRoot,
                     tdsaPortContext_t *onePortContext
                     );
osGLOBAL bit32
tdsaSASIncrementalDiscover(
                    tiRoot_t          *tiRoot,
                    tdsaPortContext_t *onePortContext
                    );

osGLOBAL bit32
tdsaSATAIncrementalDiscover(
                     tiRoot_t          *tiRoot,
                     tdsaPortContext_t *onePortContext
                     );
                     
osGLOBAL void
tdsaSASUpStreamDiscoverStart(
                             tiRoot_t             *tiRoot,
                             tdsaPortContext_t    *onePortContext,
                             tdsaDeviceData_t     *oneDeviceData
                             );

osGLOBAL void
tdsaSASUpStreamDiscovering(
                           tiRoot_t             *tiRoot,
                           tdsaPortContext_t    *onePortContext,
                           tdsaDeviceData_t     *oneDeviceData
                           );


osGLOBAL void
tdsaSASDownStreamDiscoverStart(
                               tiRoot_t             *tiRoot,
                               tdsaPortContext_t    *onePortContext,
                               tdsaDeviceData_t     *oneDeviceData
                               );

osGLOBAL void
tdsaSASDownStreamDiscovering(
                             tiRoot_t             *tiRoot,
                             tdsaPortContext_t    *onePortContext,
                             tdsaDeviceData_t     *oneDeviceData
                             );

osGLOBAL void
tdsaSASDiscoverDone(
                    tiRoot_t             *tiRoot,
                    tdsaPortContext_t    *onePortContext,
                    bit32                 flag
                    );

osGLOBAL void
tdsaSATADiscoverDone(
                    tiRoot_t             *tiRoot,
                    tdsaPortContext_t    *onePortContext,
                    bit32                flag
                    );
            
osGLOBAL void
tdsaAckBC(
                    tiRoot_t             *tiRoot,
                    tdsaPortContext_t    *onePortContext
                    );

osGLOBAL void
tdsaDiscoveryResetProcessed(
                    tiRoot_t             *tiRoot,
                    tdsaPortContext_t    *onePortContext
                    );



osGLOBAL void
tdsaSASUpStreamDiscoverExpanderPhy(
                                   tiRoot_t              *tiRoot,
                                   tdsaPortContext_t     *onePortContext,
                                   tdsaExpander_t        *oneExpander,
                                   smpRespDiscover_t     *pDiscoverResp
                                   );
osGLOBAL tdsaExpander_t *
tdsaFindUpStreamConfigurableExp(tiRoot_t              *tiRoot,
                                tdsaExpander_t        *oneExpander);

osGLOBAL void
tdsaSASDownStreamDiscoverExpanderPhy(
                                     tiRoot_t              *tiRoot,
                                     tdsaPortContext_t     *onePortContext,
                                     tdsaExpander_t        *oneExpander,
                                     smpRespDiscover_t     *pDiscoverResp
                                     );
osGLOBAL void
tdsaSASUpStreamDiscoverExpanderPhySkip(
                                   tiRoot_t              *tiRoot,
                                   tdsaPortContext_t     *onePortContext,
                                   tdsaExpander_t        *oneExpander
                                   );
osGLOBAL tdsaExpander_t *
tdsaFindDownStreamConfigurableExp(tiRoot_t              *tiRoot,
                                  tdsaExpander_t        *oneExpander);

osGLOBAL void
tdsaSASDownStreamDiscoverExpanderPhySkip(
                                     tiRoot_t              *tiRoot,
                                     tdsaPortContext_t     *onePortContext,
                                     tdsaExpander_t        *oneExpander
                                     );
osGLOBAL void
tdsaDiscoveringStpSATADevice(
                             tiRoot_t              *tiRoot,
                             tdsaPortContext_t     *onePortContext,
                             tdsaDeviceData_t      *oneDeviceData
                             );


osGLOBAL void
tdsaSASExpanderUpStreamPhyAdd(
                              tiRoot_t          *tiRoot,
                              tdsaExpander_t    *oneExpander,
                              bit8              phyId
                              );

osGLOBAL void
tdsaSASExpanderDownStreamPhyAdd(
                              tiRoot_t          *tiRoot,
                              tdsaExpander_t    *oneExpander,
                              bit8              phyId
                              );
osGLOBAL bit16
tdsaFindCurrentDownStreamPhyIndex(
                              tiRoot_t          *tiRoot,
                              tdsaExpander_t    *oneExpander
                              );

osGLOBAL tdsaDeviceData_t *
tdsaPortSASDeviceFind(
                      tiRoot_t           *tiRoot,
                      tdsaPortContext_t  *onePortContext,
                      bit32              sasAddrLo,
                      bit32              sasAddrHi
                      );  

GLOBAL tdsaDeviceData_t *
tdsaPortSASDeviceAdd(
                     tiRoot_t            *tiRoot,
                     tdsaPortContext_t   *onePortContext,
                     agsaSASIdentify_t   sasIdentify,
                     bit32               sasInitiator,
                     bit8                connectionRate,
                     bit32               itNexusTimeout,
                     bit32               firstBurstSize,
                     bit32               deviceType,
                     tdsaDeviceData_t    *oneExpDeviceData,
                     bit8                phyID
                     );





/* in tdport.c */
osGLOBAL tdsaDeviceData_t *
tdssNewAddSASToSharedcontext(
                                 agsaRoot_t           *agRoot,
                                 tdsaPortContext_t    *onePortContext,
                                 tdsaSASSubID_t       *agSASSubID,
                                 tdsaDeviceData_t     *oneExpDeviceData,
                                 bit8                 phyID
                                 );
osGLOBAL void
tdsaResetValidDeviceData(
                                 agsaRoot_t           *agRoot,
                                 tdsaPortContext_t    *onePortContext
                                 );


osGLOBAL void
tdssReportChanges(
                                 agsaRoot_t           *agRoot,
                                 tdsaPortContext_t    *onePortContext                                                 );

osGLOBAL void
tdssReportRemovals(
                                 agsaRoot_t           *agRoot,
                                 tdsaPortContext_t    *onePortContext,
                                 bit32                flag
                                 );
osGLOBAL void
tdssInternalRemovals(
                                 agsaRoot_t           *agRoot,
                                 tdsaPortContext_t    *onePortContext
                                 );
osGLOBAL void
tdssDiscoveryErrorRemovals(
                                 agsaRoot_t           *agRoot,
                                 tdsaPortContext_t    *onePortContext
                                 );
                                 
osGLOBAL void
tdsaSASDiscoverAbort(
                    tiRoot_t             *tiRoot,
                    tdsaPortContext_t    *onePortContext
                    );
                                 

osGLOBAL tdsaDeviceData_t *
tdsaFindRegNValid(
                                 agsaRoot_t           *agRoot,
                                 tdsaPortContext_t    *onePortContext,
                                 tdsaSASSubID_t       *agSASSubID
                  );                                                                 
bit32 
tdssNewSASorNot(
                                 agsaRoot_t           *agRoot,
                                 tdsaPortContext_t    *onePortContext,
                                 tdsaSASSubID_t       *agSASSubID
                                 );
                                                                 

osGLOBAL tdsaExpander_t *
tdssSASDiscoveringExpanderAlloc(
                                tiRoot_t                 *tiRoot,
                                tdsaPortContext_t        *onePortContext,
                                tdsaDeviceData_t         *oneDeviceData
                                );

osGLOBAL void
tdssSASDiscoveringExpanderAdd(
                              tiRoot_t                 *tiRoot,
                              tdsaPortContext_t        *onePortContext,
                              tdsaExpander_t           *oneExpander
                              );

osGLOBAL void
tdssSASDiscoveringExpanderRemove(
                                 tiRoot_t                 *tiRoot,
                                 tdsaPortContext_t        *onePortContext,
                                 tdsaExpander_t           *oneExpander
                                 );

GLOBAL bit32
tdssSATADeviceTypeDecode(
                         bit8  *pSignature
                         );


GLOBAL tdsaDeviceData_t *
tdsaPortSATADeviceAdd(
                      tiRoot_t                *tiRoot,
                      tdsaPortContext_t       *onePortContext,
                      tdsaDeviceData_t        *oneSTPBridge,
                      bit8                    *Signature,
                      bit8                    pm,
                      bit8                    pmField,
                      bit8                    connectionRate,  
                      tdsaDeviceData_t        *oneExpDeviceData,
                      bit8                    phyID
                      );

/* in tdport.c */
osGLOBAL tdsaDeviceData_t *
tdssNewAddSATAToSharedcontext(tiRoot_t             *tiRoot,
                              agsaRoot_t           *agRoot,
                              tdsaPortContext_t    *onePortContext,
                              agsaSATADeviceInfo_t *agSATADeviceInfo,
                              bit8                    *Signature,
                              bit8                    pm,
                              bit8                    pmField,
                              bit32                   connectionRate, 
                              tdsaDeviceData_t        *oneExpDeviceData,
                              bit8                    phyID
                              );

osGLOBAL tdsaDeviceData_t  *
tdsaFindRightDevice(
                   tiRoot_t               *tiRoot,
                   tdsaPortContext_t      *onePortContext,
                   tdsaDeviceData_t       *tdsaDeviceData
                   );
GLOBAL void
ossaIDCDiscoverCompleted(
                  agsaRoot_t        *agRoot,
                  agsaIORequest_t   *agIORequest,
                  bit32             agIOStatus,
                  void              *agFirstDword,
                  bit32             agIOInfoLen,
                  agsaFrameHandle_t agFrameHandle
                  );
                  
osGLOBAL bit8
tdsaFindLocalLinkRate(
                      tiRoot_t                  *tiRoot,
                      tdsaPortStartInfo_t       *tdsaPortStartInfo
                      );
                  
/* SMP related */

osGLOBAL bit32
tdSMPStart(
           tiRoot_t              *tiRoot,
           agsaRoot_t            *agRoot,
           tdsaDeviceData_t      *oneDeviceData,
           bit32                 functionCode,
           bit8                  *pSmpBody,
           bit32                 smpBodySize,
           bit32                 agRequestType,
           tiIORequest_t         *CurrentTaskTag,  
           bit32                 queueNumber		   
           );
//temp for testing
osGLOBAL void
tdsaReportManInfoSend(
                      tiRoot_t             *tiRoot,
                      tdsaDeviceData_t     *oneDeviceData
                      );
                      
osGLOBAL void
tdsaReportManInfoRespRcvd(
                          tiRoot_t              *tiRoot,
                          agsaRoot_t            *agRoot,
                          tdsaDeviceData_t      *oneDeviceData,
                          tdssSMPFrameHeader_t  *frameHeader,
                          agsaFrameHandle_t     frameHandle
                          );

//end temp for testing

osGLOBAL void
tdsaReportGeneralSend(
                      tiRoot_t             *tiRoot,
                      tdsaDeviceData_t     *oneDeviceData
                      );

osGLOBAL void
tdsaReportGeneralRespRcvd(
                          tiRoot_t              *tiRoot,
                          agsaRoot_t            *agRoot,
                          agsaIORequest_t       *agIORequest,
                          tdsaDeviceData_t      *oneDeviceData,
                          tdssSMPFrameHeader_t  *frameHeader,
                          agsaFrameHandle_t     frameHandle
              );
osGLOBAL void
tdsaDiscoverSend(
                 tiRoot_t             *tiRoot,
                 tdsaDeviceData_t     *oneDeviceData
                 );

osGLOBAL void
tdsaDiscoverRespRcvd(
                     tiRoot_t              *tiRoot,
                     agsaRoot_t            *agRoot,
                     agsaIORequest_t       *agIORequest,
                     tdsaDeviceData_t      *oneDeviceData,
                     tdssSMPFrameHeader_t  *frameHeader,
                     agsaFrameHandle_t     frameHandle
                     );
                     

osGLOBAL void
tdsaReportPhySataSend(
                      tiRoot_t             *tiRoot,
                      tdsaDeviceData_t     *oneDeviceData,
                      bit8                 phyId
                      );



osGLOBAL void
tdsaReportPhySataRcvd(
                      tiRoot_t              *tiRoot,
                      agsaRoot_t            *agRoot,
                      agsaIORequest_t       *agIORequest,
                      tdsaDeviceData_t      *oneDeviceData,
                      tdssSMPFrameHeader_t  *frameHeader,
                      agsaFrameHandle_t     frameHandle
                      );
                      
osGLOBAL bit32
tdsaSASRoutingEntryAdd(
                       tiRoot_t          *tiRoot,
                       tdsaExpander_t    *oneExpander,
                       bit32             phyId,  
                       bit32             configSASAddressHi,
                       bit32             configSASAddressLo
                       );

                     
osGLOBAL void
tdsaConfigRoutingInfoRespRcvd(
                              tiRoot_t              *tiRoot,
                              agsaRoot_t            *agRoot,
                              agsaIORequest_t       *agIORequest,
                              tdsaDeviceData_t      *oneDeviceData,
                              tdssSMPFrameHeader_t  *frameHeader,
                              agsaFrameHandle_t     frameHandle
                              );

osGLOBAL bit32
tdsaPhyControlSend(
                   tiRoot_t             *tiRoot,
                   tdsaDeviceData_t     *oneDeviceData,
                   bit8                 phyOp,
                   tiIORequest_t        *CurrentTaskTag,
                   bit32                queueNumber		   
                   );

osGLOBAL void
tdsaPhyControlRespRcvd(
                       tiRoot_t              *tiRoot,
                       agsaRoot_t            *agRoot,
                       agsaIORequest_t       *agIORequest,
                       tdsaDeviceData_t      *oneDeviceData,
                       tdssSMPFrameHeader_t  *frameHeader,
                       agsaFrameHandle_t     frameHandle,
                       tiIORequest_t         *CurrentTaskTag
                       );

osGLOBAL void
tdsaPhyControlFailureRespRcvd(
                              tiRoot_t              *tiRoot,
                              agsaRoot_t            *agRoot,
                              tdsaDeviceData_t      *oneDeviceData,
                              tdssSMPFrameHeader_t  *frameHeader,
                              agsaFrameHandle_t     frameHandle,
                              tiIORequest_t         *CurrentTaskTag
                             );


osGLOBAL void
tdsaDumpAllExp(
                              tiRoot_t                 *tiRoot,
                              tdsaPortContext_t        *onePortContext,
                              tdsaExpander_t           *oneExpander
                              );
osGLOBAL void
tdsaDumpAllUpExp(
                              tiRoot_t                 *tiRoot,
                              tdsaPortContext_t        *onePortContext,
                              tdsaExpander_t           *oneExpander
                              );
osGLOBAL void
tdsaCleanAllExp(
                              tiRoot_t                 *tiRoot,
                              tdsaPortContext_t        *onePortContext
                              );
osGLOBAL void
tdsaFreeAllExp(
                              tiRoot_t                 *tiRoot,
                              tdsaPortContext_t        *onePortContext
                              );
osGLOBAL void
tdsaDumpAllFreeExp(
                              tiRoot_t                 *tiRoot
                              );
                              
osGLOBAL void                          
tdsaDiscoveryTimer(tiRoot_t                 *tiRoot,
                   tdsaPortContext_t        *onePortContext,
                   tdsaDeviceData_t         *oneDeviceData
                   );
                              
osGLOBAL void
tdsaDiscoveryTimerCB(
                       tiRoot_t    * tiRoot, 
                       void        * timerData1,
                       void        * timerData2,
                       void        * timerData3
                       );

osGLOBAL void                          
tdsaConfigureRouteTimer(tiRoot_t                 *tiRoot,
                        tdsaPortContext_t        *onePortContext,
                        tdsaExpander_t           *oneExpander,
                        smpRespDiscover_t        *ptdSMPDiscoverResp
                       );
                              
osGLOBAL void
tdsaConfigureRouteTimerCB(
                          tiRoot_t    * tiRoot, 
                          void        * timerData1,
                          void        * timerData2,
                          void        * timerData3
                         );

osGLOBAL void                          
tdsaDeviceRegistrationTimer(tiRoot_t                 *tiRoot,
                            tdsaPortContext_t        *onePortContext,
                            tdsaDeviceData_t         *oneDeviceData
                            );
                              
osGLOBAL void
tdsaDeviceRegistrationTimerCB(
                              tiRoot_t    * tiRoot, 
                              void        * timerData1,
                              void        * timerData2,
                              void        * timerData3
                             );
                 
osGLOBAL void                          
tdsaSMPBusyTimer(tiRoot_t                 *tiRoot,
                 tdsaPortContext_t        *onePortContext,
                 tdsaDeviceData_t         *oneDeviceData,
                 tdssSMPRequestBody_t     *tdSMPRequestBody
                 );
                              
osGLOBAL void
tdsaSMPBusyTimerCB(
                       tiRoot_t    * tiRoot, 
                       void        * timerData1,
                       void        * timerData2,
                       void        * timerData3
                       );

osGLOBAL void                          
tdsaSATAIDDeviceTimer(tiRoot_t                 *tiRoot,
                      tdsaDeviceData_t         *oneDeviceData
                     );
#ifdef FDS_SM
osGLOBAL void                          
tdIDStartTimer(tiRoot_t                 *tiRoot,
               smIORequest_t            *smIORequest,
               tdsaDeviceData_t         *oneDeviceData
               );
osGLOBAL void
tdIDStartTimerCB(
                  tiRoot_t    * tiRoot, 
                  void        * timerData1,
                  void        * timerData2,
                  void        * timerData3
                );
#endif			                                    
osGLOBAL void                          
tdsaBCTimer(tiRoot_t                 *tiRoot,
            tdsaPortContext_t        *onePortContext
           );
       
osGLOBAL void
tdsaBCTimerCB(
              tiRoot_t    * tiRoot, 
              void        * timerData1,
              void        * timerData2,
              void        * timerData3
              );
          
osGLOBAL void
tdsaSATAIDDeviceTimerCB(
                       tiRoot_t    * tiRoot, 
                       void        * timerData1,
                       void        * timerData2,
                       void        * timerData3
                       );
                           
osGLOBAL void                          
tdsaDiscoverySMPTimer(tiRoot_t                 *tiRoot,
                      tdsaPortContext_t        *onePortContext,
                      bit32                    functionCode,
                      tdssSMPRequestBody_t     *tdSMPRequestBody
                     );
                              
osGLOBAL void
tdsaDiscoverySMPTimerCB(
                        tiRoot_t    * tiRoot, 
                        void        * timerData1,
                        void        * timerData2,
                        void        * timerData3
                       );
          
osGLOBAL void
dumpRoutingAttributes(
                      tiRoot_t                 *tiRoot,
                      tdsaExpander_t           *oneExpander,
                      bit8                     phyID
                      );

osGLOBAL bit32
tdsaDuplicateConfigSASAddr(
                      tiRoot_t                 *tiRoot,
                      tdsaExpander_t           *oneExpander,
                      bit32                    configSASAddressHi,
                      bit32                    configSASAddressLo
                      );
                      
osGLOBAL tdsaExpander_t *
tdsaFindConfigurableExp(
                         tiRoot_t                 *tiRoot,
                         tdsaPortContext_t        *onePortContext,
                         tdsaExpander_t           *oneExpander
                        );
                      
GLOBAL bit32  
tdsaDiscoveryStartIDDev(
                        tiRoot_t                  *tiRoot, 
                        tiIORequest_t             *tiIORequest,
                        tiDeviceHandle_t          *tiDeviceHandle,
                        tiScsiInitiatorRequest_t *tiScsiRequest,
                        tdsaDeviceData_t          *oneDeviceData 
                        );

GLOBAL void  satFreeIntIoResource(
                    tiRoot_t              *tiRoot,
                    satDeviceData_t       *satDevData,
                    satInternalIo_t       *satIntIo);
osGLOBAL void 
tddmDeregisterDevicesInPort(
                tiRoot_t             *tiRoot,
                tdsaPortContext_t    *onePortContext
               );

#ifdef AGTIAPI_CTL
osGLOBAL void
tdsaCTLSet(
           tiRoot_t          *tiRoot,
           tdsaPortContext_t *onePortContext,
           tiIntrEventType_t eventType,
           bit32             eventStatus);

STATIC void
tdsaCTLNextDevice(
                  tiRoot_t          *tiRoot,
                  tdsaPortContext_t *onePortContext,
                  tdIORequest_t     *tdIORequest,
                  tdList_t          *DeviceList);

STATIC int
tdsaCTLModeSelect(
                  tiRoot_t                  *tiRoot,
                  tiDeviceHandle_t          *tiDeviceHandle,
                  tdIORequest_t             *tdIORequest);

STATIC void
tdsaCTLIOCompleted(
                   agsaRoot_t      *agRoot,
                   agsaIORequest_t *agIORequest,
                   bit32           agIOStatus,
                   bit32           agIOInfoLen,
                   void            *agParam,
                   bit16           sspTag,
                   bit32           agOtherInfo);
#endif /* AGTIAPI_CTL */

#endif /* TD_DISCOVER */
#endif /* INITIATOR_DRIVER */

#ifdef FDS_DM
/**********		For DM		*******/
osGLOBAL tdsaDeviceData_t *
tddmPortDeviceAdd(
                     tiRoot_t            *tiRoot,
                     tdsaPortContext_t   *onePortContext,
                     dmDeviceInfo_t      *dmDeviceInfo,
                     tdsaDeviceData_t    *oneExpDeviceData
                     );

osGLOBAL void 
tddmInvalidateDevicesInPort(
                tiRoot_t             *tiRoot,
                tdsaPortContext_t    *onePortContext
               );

osGLOBAL bit32 
tddmNewSASorNot(
                                 tiRoot_t             *tiRoot,
                                 tdsaPortContext_t    *onePortContext,
                                 tdsaSASSubID_t       *agSASSubID
                                 );

osGLOBAL tdsaDeviceData_t *
tddmPortSASDeviceFind(
                      tiRoot_t           *tiRoot,
                      tdsaPortContext_t  *onePortContext,
                      bit32              sasAddrLo,
                      bit32              sasAddrHi
                      );
		      
osGLOBAL tdsaDeviceData_t *
tddmAddToSharedcontext(
                       agsaRoot_t           *agRoot,
                       tdsaPortContext_t    *onePortContext,
                       tdsaSASSubID_t       *agSASSubID,
                       tdsaDeviceData_t     *oneExpDeviceData,
                       bit8                 phyID
                      );

osGLOBAL void
tdsaUpdateMCN(
              dmRoot_t 	           *dmRoot,
              tdsaPortContext_t    *onePortContext
             );
#endif

GLOBAL void
tdsaSingleThreadedEnter(tiRoot_t *ptiRoot, bit32 queueId);

GLOBAL void
tdsaSingleThreadedLeave(tiRoot_t *ptiRoot, bit32 queueId);

#ifdef PERF_COUNT
GLOBAL void
tdsaEnter(tiRoot_t *ptiRoot, int io);

GLOBAL void
tdsaLeave(tiRoot_t *ptiRoot, int io);

#define TDSA_INP_ENTER(root) tdsaEnter(root, 0)
#define TDSA_INP_LEAVE(root) tdsaLeave(root, 0)
#define TDSA_OUT_ENTER(root) tdsaEnter(root, 1)
#define TDSA_OUT_LEAVE(root) tdsaLeave(root, 1)
#else
#define TDSA_INP_ENTER(root)
#define TDSA_INP_LEAVE(root)
#define TDSA_OUT_ENTER(root)
#define TDSA_OUT_LEAVE(root)
#endif

#if defined(FDS_DM) && defined(FDS_SM)
GLOBAL void 
tdIDStart(
           tiRoot_t             *tiRoot,
           agsaRoot_t           *agRoot,	   
           smRoot_t             *smRoot,
           tdsaDeviceData_t     *oneDeviceData,
           tdsaPortContext_t    *onePortContext
          );
#endif

void t_MacroCheck(  agsaRoot_t       *agRoot);

#endif                          /* __TDPROTO_H__ */
