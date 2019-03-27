/*******************************************************************************
*
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
#ifndef __DMPROTO_H__
#define __DMPROTO_H__

#include <dev/pms/RefTisa/discovery/dm/dmtypes.h>

/***************** util ****************************************/
osGLOBAL void 
*dm_memset(void *s, int c, bit32 n);

osGLOBAL void 
*dm_memcpy(void *dst, void *src, bit32 count);

osGLOBAL void 
dmhexdump(const char *ptitle, bit8 *pbuf, int len);


/* timer related */
osGLOBAL void
dmInitTimers(
             dmRoot_t *dmRoot 
            );
osGLOBAL void
dmInitTimerRequest(
                   dmRoot_t                *dmRoot, 
                   dmTimerRequest_t        *timerRequest
                   );
		   		   
osGLOBAL void
dmSetTimerRequest(
                  dmRoot_t            *dmRoot,
                  dmTimerRequest_t    *timerRequest,
                  bit32               timeout,
                  dmTimerCBFunc_t     CBFunc,
                  void                *timerData1,
                  void                *timerData2,
                  void                *timerData3
                  );
		  
osGLOBAL void
dmAddTimer(
           dmRoot_t            *dmRoot,
           dmList_t            *timerListHdr, 
           dmTimerRequest_t    *timerRequest
          );
	  
osGLOBAL void
dmKillTimer(
            dmRoot_t            *dmRoot,
            dmTimerRequest_t    *timerRequest
           );
	   
osGLOBAL void 
dmProcessTimers(
                dmRoot_t *dmRoot
                );
	   
	  

osGLOBAL void
dmPortContextInit(
                  dmRoot_t *dmRoot 
                 );
		    
osGLOBAL void
dmPortContextReInit(
                    dmRoot_t		  *dmRoot,
                    dmIntPortContext_t    *onePortContext		     
                    );

osGLOBAL void
dmDeviceDataInit(
                 dmRoot_t *dmRoot 
                );

osGLOBAL void
dmDeviceDataReInit(
                   dmRoot_t		  *dmRoot,
                   dmDeviceData_t         *oneDeviceData		     
                  );
		  
osGLOBAL void
dmExpanderDeviceDataInit(
                         dmRoot_t *dmRoot,
                         bit32    max_exp			  
                        );
		   
osGLOBAL void
dmExpanderDeviceDataReInit(
                           dmRoot_t 	    *dmRoot, 
                           dmExpander_t     *oneExpander
                          );


osGLOBAL void
dmSMPInit(
          dmRoot_t *dmRoot 
         );


osGLOBAL bit32
dmDiscoverCheck(
                dmRoot_t 	    	*dmRoot, 
                dmIntPortContext_t      *onePortContext	
                );
osGLOBAL void
dmDiscoverAbort(
                dmRoot_t 	    	*dmRoot, 
                dmIntPortContext_t      *onePortContext	
                );


osGLOBAL bit32
dmFullDiscover(
               dmRoot_t 	    	*dmRoot, 
               dmIntPortContext_t       *onePortContext	
              );

osGLOBAL bit32
dmIncrementalDiscover(
                      dmRoot_t 	    	      *dmRoot, 
                      dmIntPortContext_t      *onePortContext,
		      bit32                   flag	
                     );

osGLOBAL dmExpander_t *
dmDiscoveringExpanderAlloc(
                           dmRoot_t                 *dmRoot,
                           dmIntPortContext_t       *onePortContext,
                           dmDeviceData_t           *oneDeviceData
                          );
osGLOBAL void
dmDiscoveringExpanderAdd(
                         dmRoot_t                 *dmRoot,
                         dmIntPortContext_t       *onePortContext,
                         dmExpander_t             *oneExpander
                        );

osGLOBAL void
dmDiscoveringExpanderRemove(
                            dmRoot_t                 *dmRoot,
                            dmIntPortContext_t       *onePortContext,
                            dmExpander_t             *oneExpander
                           );

osGLOBAL dmExpander_t *
dmExpFind(
          dmRoot_t            *dmRoot,
          dmIntPortContext_t  *onePortContext,
          bit32               sasAddrHi,
          bit32               sasAddrLo
         );

osGLOBAL dmExpander_t *
dmExpMainListFind(
                  dmRoot_t            *dmRoot,
                  dmIntPortContext_t  *onePortContext,
                  bit32               sasAddrHi,
                  bit32               sasAddrLo
                 );
	 
osGLOBAL dmDeviceData_t *
dmDeviceFind(
             dmRoot_t            *dmRoot,
             dmIntPortContext_t  *onePortContext,
             bit32               sasAddrHi,
             bit32               sasAddrLo
            );
	 
osGLOBAL void
dmUpStreamDiscoverStart(
                        dmRoot_t             *dmRoot,
                        dmIntPortContext_t   *onePortContext
                       );
		      
osGLOBAL void
dmUpStreamDiscovering(
                      dmRoot_t              *dmRoot,
                      dmIntPortContext_t    *onePortContext,
                      dmDeviceData_t        *oneDeviceData
                     );

osGLOBAL void
dmDownStreamDiscovering(
                        dmRoot_t              *dmRoot,
                        dmIntPortContext_t    *onePortContext,
                        dmDeviceData_t        *oneDeviceData
                       );

osGLOBAL void
dmDownStreamDiscoverStart(
                          dmRoot_t              *dmRoot,
                          dmIntPortContext_t    *onePortContext,
                          dmDeviceData_t        *oneDeviceData
                         );
		     
osGLOBAL void
dmCleanAllExp(
              dmRoot_t                 *dmRoot,
              dmIntPortContext_t       *onePortContext
             );

osGLOBAL void
dmInternalRemovals(
                   dmRoot_t                 *dmRoot,
                   dmIntPortContext_t       *onePortContext
                   );
osGLOBAL void
dmDiscoveryResetProcessed(
                          dmRoot_t                 *dmRoot,
                          dmIntPortContext_t       *onePortContext
                         );
		   
osGLOBAL void
dmDiscoverDone(
               dmRoot_t                 *dmRoot,
               dmIntPortContext_t       *onePortContext,
               bit32                    flag
              );

osGLOBAL void
dmUpStreamDiscoverExpanderPhy(
                              dmRoot_t              *dmRoot,
                              dmIntPortContext_t    *onePortContext,
                              dmExpander_t          *oneExpander,
                              smpRespDiscover_t     *pDiscoverResp
                             );

osGLOBAL void
dmUpStreamDiscover2ExpanderPhy(
                              dmRoot_t              *dmRoot,
                              dmIntPortContext_t    *onePortContext,
                              dmExpander_t          *oneExpander,
                              smpRespDiscover2_t    *pDiscoverResp
                             );

osGLOBAL void
dmDownStreamDiscoverExpanderPhy(
                                dmRoot_t              *dmRoot,
                                dmIntPortContext_t    *onePortContext,
                                dmExpander_t          *oneExpander,
                                smpRespDiscover_t     *pDiscoverResp
                               );
osGLOBAL void
dmDownStreamDiscover2ExpanderPhy(
                                dmRoot_t              *dmRoot,
                                dmIntPortContext_t    *onePortContext,
                                dmExpander_t          *oneExpander,
                                smpRespDiscover2_t     *pDiscoverResp
                               );

osGLOBAL void
dmUpStreamDiscoverExpanderPhySkip(
                                   dmRoot_t              *dmRoot,
                                   dmIntPortContext_t    *onePortContext,
                                   dmExpander_t          *oneExpander
                                   );

osGLOBAL void
dmUpStreamDiscover2ExpanderPhySkip(
                                   dmRoot_t              *dmRoot,
                                   dmIntPortContext_t    *onePortContext,
                                   dmExpander_t          *oneExpander
                                   );

osGLOBAL void
dmDownStreamDiscoverExpanderPhySkip(
                                     dmRoot_t              *dmRoot,
                                     dmIntPortContext_t    *onePortContext,
                                     dmExpander_t          *oneExpander
                                     );
osGLOBAL void
dmDownStreamDiscover2ExpanderPhySkip(
                                     dmRoot_t              *dmRoot,
                                     dmIntPortContext_t    *onePortContext,
                                     dmExpander_t          *oneExpander
                                     );

osGLOBAL void
dmDiscoveringUndoAdd(
                     dmRoot_t                 *dmRoot,
                     dmIntPortContext_t       *onePortContext,
                     dmExpander_t             *oneExpander
                    );


osGLOBAL void
dmExpanderUpStreamPhyAdd(
                         dmRoot_t              *dmRoot,
                         dmExpander_t          *oneExpander,
                         bit8                  phyId
                         );

osGLOBAL void
dmExpanderDownStreamPhyAdd(
                           dmRoot_t              *dmRoot,
                           dmExpander_t          *oneExpander,
                           bit8                  phyId
                          );

osGLOBAL dmDeviceData_t *
dmPortSASDeviceFind(
                    dmRoot_t            *dmRoot,
                    dmIntPortContext_t  *onePortContext,
                    bit32               sasAddrLo,
                    bit32               sasAddrHi,
                    dmDeviceData_t      *CurrentDeviceData		    
                    );  
bit32
dmNewEXPorNot(
              dmRoot_t              *dmRoot,
              dmIntPortContext_t    *onePortContext,
              dmSASSubID_t          *dmSASSubID
             );

bit32
dmNewSASorNot(
              dmRoot_t              *dmRoot,
              dmIntPortContext_t    *onePortContext,
              dmSASSubID_t          *dmSASSubID
             );

osGLOBAL dmDeviceData_t *
dmPortSASDeviceAdd(
                   dmRoot_t            *dmRoot,
                   dmIntPortContext_t  *onePortContext,
                   agsaSASIdentify_t   sasIdentify,
                   bit32               sasInitiator,
                   bit8                connectionRate,
                   bit32               itNexusTimeout,
                   bit32               firstBurstSize,
                   bit32               deviceType,
                   dmDeviceData_t      *oneDeviceData,
                   dmExpander_t        *dmExpander,
                   bit8                phyID
                  );


osGLOBAL dmDeviceData_t *
dmFindRegNValid(
                dmRoot_t             *dmRoot,
                dmIntPortContext_t   *onePortContext,
                dmSASSubID_t         *dmSASSubID
               );								

osGLOBAL dmExpander_t *
dmFindConfigurableExp(
                      dmRoot_t                  *dmRoot,
                      dmIntPortContext_t        *onePortContext,
                      dmExpander_t              *oneExpander
                     );

osGLOBAL bit32
dmDuplicateConfigSASAddr(
                         dmRoot_t                 *dmRoot,
                         dmExpander_t             *oneExpander,
                         bit32                    configSASAddressHi,
                         bit32                    configSASAddressLo
                        );


osGLOBAL bit16
dmFindCurrentDownStreamPhyIndex(
                                dmRoot_t          *dmRoot,
                                dmExpander_t      *oneExpander
                                );


osGLOBAL bit32
dmFindDiscoveringExpander(
                          dmRoot_t                  *dmRoot,
                          dmIntPortContext_t        *onePortContext,
                          dmExpander_t              *oneExpander
                         );

osGLOBAL void
dmDumpAllExp(
             dmRoot_t                  *dmRoot,
             dmIntPortContext_t        *onePortContext,
             dmExpander_t              *oneExpander
            );


osGLOBAL void
dmDumpAllUpExp(
               dmRoot_t                  *dmRoot,
               dmIntPortContext_t        *onePortContext,
               dmExpander_t              *oneExpander
              );

osGLOBAL void
dmDumpAllFreeExp(
                 dmRoot_t                  *dmRoot
                );

osGLOBAL void
dmDumpAllMainExp(
                 dmRoot_t                 *dmRoot,
                 dmIntPortContext_t       *onePortContext
                );

osGLOBAL void
dmDumpAllMainDevice(
                   dmRoot_t                 *dmRoot,
                   dmIntPortContext_t       *onePortContext
                   );
		
osGLOBAL void
dmSubReportChanges(
                   dmRoot_t                  *dmRoot,
                   dmIntPortContext_t        *onePortContext,
		   dmDeviceData_t            *oneDeviceData,
                   bit32                     flag
                  );
osGLOBAL void
dmSubReportRemovals(
                   dmRoot_t                  *dmRoot,
                   dmIntPortContext_t        *onePortContext,
                   dmDeviceData_t            *oneDeviceData,
                   bit32                     flag
                  );
		  
osGLOBAL void
dmReportChanges(
                dmRoot_t                  *dmRoot,
                dmIntPortContext_t        *onePortContext
               );

osGLOBAL void
dmReportRemovals(
                 dmRoot_t                  *dmRoot,
                 dmIntPortContext_t        *onePortContext,
                 bit32                     flag
                );

osGLOBAL void
dmDiscoveryDeviceCleanUp(
                         dmRoot_t                  *dmRoot,
                         dmIntPortContext_t        *onePortContext
                        );
osGLOBAL void
dmDiscoveryExpanderCleanUp(
                         dmRoot_t                  *dmRoot,
                         dmIntPortContext_t        *onePortContext
                        );

osGLOBAL void
dmResetReported(
                dmRoot_t                  *dmRoot,
                dmIntPortContext_t        *onePortContext
               );

osGLOBAL void
dmDiscoveryErrorRemovals(
                         dmRoot_t                  *dmRoot,
                         dmIntPortContext_t        *onePortContext
                        );
osGLOBAL void
dmDiscoveryInvalidateDevices(
                             dmRoot_t                  *dmRoot,
                             dmIntPortContext_t        *onePortContext
                            );

osGLOBAL dmDeviceData_t *
dmAddSASToSharedcontext(
                         dmRoot_t              *dmRoot,
                         dmIntPortContext_t    *onePortContext,
                         dmSASSubID_t          *dmSASSubID,
                         dmDeviceData_t        *oneExpDeviceData,
                         bit8                  phyID
                        );
osGLOBAL bit32
dmSAS2SAS11ErrorCheck(
                      dmRoot_t              *dmRoot,
                      dmIntPortContext_t    *onePortContext,
                      dmExpander_t          *topExpander,
                      dmExpander_t          *bottomExpander,
                      dmExpander_t          *currentExpander
                     );

osGLOBAL void
dmUpdateMCN(
            dmRoot_t            *dmRoot,
            dmIntPortContext_t  *onePortContext,
            dmDeviceData_t      *AdjacentDeviceData, /* adjacent expander */ 		    
            dmDeviceData_t      *oneDeviceData /* current one */
           );

osGLOBAL void
dmUpdateAllAdjacent(
                    dmRoot_t            *dmRoot,
                    dmIntPortContext_t  *onePortContext,
                    dmDeviceData_t      *oneDeviceData /* current one */
                   );
osGLOBAL void
dmDiscoveryResetMCN(
                    dmRoot_t                 *dmRoot,
                    dmIntPortContext_t       *onePortContext
                   );

osGLOBAL void
dmDiscoveryDumpMCN(
                    dmRoot_t                 *dmRoot,
                    dmIntPortContext_t       *onePortContext
                   );

osGLOBAL void
dmDiscoveryReportMCN(
                    dmRoot_t                 *dmRoot,
                    dmIntPortContext_t       *onePortContext
                   );

GLOBAL void dmSetDeviceInfoCB(
                                agsaRoot_t        *agRoot,
                                agsaContext_t     *agContext, 
                                agsaDevHandle_t   *agDevHandle,
                                bit32             status,
                                bit32             option,
                                bit32             param
                                );

/*********************************** SMP-related *******************************************************/
osGLOBAL void 
dmsaSMPCompleted( 
                 agsaRoot_t            *agRoot,
                 agsaIORequest_t       *agIORequest,
                 bit32                 agIOStatus,
                 bit32                 agIOInfoLen,
                 agsaFrameHandle_t     agFrameHandle
                 );
		 
osGLOBAL bit32
dmSMPStart(
           dmRoot_t              *dmRoot,
           agsaRoot_t            *agRoot,
           dmDeviceData_t        *oneDeviceData,
           bit32                 functionCode,
           bit8                  *pSmpBody,
           bit32                 smpBodySize,
           bit32                 agRequestType
           );

osGLOBAL void
dmReportGeneralSend(
                    dmRoot_t             *dmRoot,
                    dmDeviceData_t       *oneDeviceData
                    );

osGLOBAL void
dmReportGeneralRespRcvd(
                        dmRoot_t              *dmRoot,
                        agsaRoot_t            *agRoot,
                        agsaIORequest_t       *agIORequest,
                        dmDeviceData_t        *oneDeviceData,
                        dmSMPFrameHeader_t    *frameHeader,
                        agsaFrameHandle_t     frameHandle
                        );

osGLOBAL void
dmReportGeneral2RespRcvd(
                        dmRoot_t              *dmRoot,
                        agsaRoot_t            *agRoot,
                        agsaIORequest_t       *agIORequest,
                        dmDeviceData_t        *oneDeviceData,
                        dmSMPFrameHeader_t    *frameHeader,
                        agsaFrameHandle_t     frameHandle
                        );

osGLOBAL void
dmDiscoverSend(
               dmRoot_t             *dmRoot,
               dmDeviceData_t       *oneDeviceData	       
              );

osGLOBAL void
dmDiscoverRespRcvd(
                   dmRoot_t              *dmRoot,
                   agsaRoot_t            *agRoot,
                   agsaIORequest_t       *agIORequest,
                   dmDeviceData_t        *oneDeviceData,
                   dmSMPFrameHeader_t    *frameHeader,
                   agsaFrameHandle_t     frameHandle
                  );

osGLOBAL void
dmDiscover2RespRcvd(
                   dmRoot_t              *dmRoot,
                   agsaRoot_t            *agRoot,
                   agsaIORequest_t       *agIORequest,
                   dmDeviceData_t        *oneDeviceData,
                   dmSMPFrameHeader_t    *frameHeader,
                   agsaFrameHandle_t     frameHandle
                  );

#ifdef NOT_YET
osGLOBAL void
dmDiscoverList2Send(
                    dmRoot_t             *dmRoot,
                    dmDeviceData_t       *oneDeviceData
                   );

osGLOBAL void
dmDiscoverList2RespRcvd(
                        dmRoot_t              *dmRoot,
                        agsaRoot_t            *agRoot,
                        dmDeviceData_t        *oneDeviceData,
                        dmSMPFrameHeader_t    *frameHeader,
                        agsaFrameHandle_t     frameHandle
                       );
#endif		     

osGLOBAL void
dmReportPhySataSend(
                    dmRoot_t             *dmRoot,
                    dmDeviceData_t       *oneDeviceData,
                    bit8                 phyId
                    );

osGLOBAL void
dmReportPhySataRcvd(
                    dmRoot_t              *dmRoot,
                    agsaRoot_t            *agRoot,
                    agsaIORequest_t       *agIORequest,
                    dmDeviceData_t        *oneDeviceData,
                    dmSMPFrameHeader_t    *frameHeader,
                    agsaFrameHandle_t     frameHandle
                   );

osGLOBAL void
dmReportPhySata2Rcvd(
                    dmRoot_t              *dmRoot,
                    agsaRoot_t            *agRoot,
                    agsaIORequest_t       *agIORequest,
                    dmDeviceData_t        *oneDeviceData,
                    dmSMPFrameHeader_t    *frameHeader,
                    agsaFrameHandle_t     frameHandle
                   );

osGLOBAL bit32
dmRoutingEntryAdd(
                  dmRoot_t          *dmRoot,
                  dmExpander_t      *oneExpander,
                  bit32             phyId,  
                  bit32             configSASAddressHi,
                  bit32             configSASAddressLo
                 );

osGLOBAL void
dmConfigRoutingInfoRespRcvd(
                            dmRoot_t              *dmRoot,
                            agsaRoot_t            *agRoot,
                            agsaIORequest_t       *agIORequest,
                            dmDeviceData_t        *oneDeviceData,
                            dmSMPFrameHeader_t    *frameHeader,
                            agsaFrameHandle_t     frameHandle
                           );
						
osGLOBAL void
dmConfigRoutingInfo2RespRcvd(
                            dmRoot_t              *dmRoot,
                            agsaRoot_t            *agRoot,
                            agsaIORequest_t       *agIORequest,
                            dmDeviceData_t        *oneDeviceData,
                            dmSMPFrameHeader_t    *frameHeader,
                            agsaFrameHandle_t     frameHandle
                           );
						
osGLOBAL bit32
dmPhyControlSend(
                 dmRoot_t             *dmRoot,
                 dmDeviceData_t       *oneDeviceData,
                 bit8                 phyOp,
                 bit8                 phyID
                 );

osGLOBAL void
dmPhyControlRespRcvd(
                     dmRoot_t              *dmRoot,
                     agsaRoot_t            *agRoot,
                     agsaIORequest_t       *agIORequest,
                     dmDeviceData_t        *oneDeviceData,
                     dmSMPFrameHeader_t    *frameHeader,
                     agsaFrameHandle_t     frameHandle
                    );

osGLOBAL void
dmPhyControl2RespRcvd(
                     dmRoot_t              *dmRoot,
                     agsaRoot_t            *agRoot,
                     agsaIORequest_t       *agIORequest,
                     dmDeviceData_t        *oneDeviceData,
                     dmSMPFrameHeader_t    *frameHeader,
                     agsaFrameHandle_t     frameHandle
                    );

osGLOBAL void
dmPhyControlFailureRespRcvd(
                            dmRoot_t              *dmRoot,
                            agsaRoot_t            *agRoot,
                            dmDeviceData_t        *oneDeviceData,
                            dmSMPFrameHeader_t    *frameHeader,
                            agsaFrameHandle_t     frameHandle
                           );

osGLOBAL void
dmHandleZoneViolation(
                      dmRoot_t              *dmRoot,
                      agsaRoot_t            *agRoot,
                      agsaIORequest_t       *agIORequest,
                      dmDeviceData_t        *oneDeviceData,
                      dmSMPFrameHeader_t    *frameHeader,
                      agsaFrameHandle_t     frameHandle
                     );

osGLOBAL void
dmSMPCompleted(
               agsaRoot_t            *agRoot,
               agsaIORequest_t       *agIORequest,
               bit32                 agIOStatus,
               bit32                 agIOInfoLen,
               agsaFrameHandle_t     agFrameHandle                   
              );

osGLOBAL void
dmSMPAbortCB(
             agsaRoot_t           *agRoot,
             agsaIORequest_t      *agIORequest,
             bit32                flag,
             bit32                status
             );
	     
osGLOBAL void                          
dmBCTimer(
          dmRoot_t                 *dmRoot,
          dmIntPortContext_t       *onePortContext
         );

osGLOBAL void
dmBCTimerCB(
              dmRoot_t    * dmRoot_t, 
              void        * timerData1,
              void        * timerData2,
              void        * timerData3
              );

/*********************************** SMP-related *******************************************************/
osGLOBAL void
dmDiscoverySMPTimer(dmRoot_t                 *dmRoot,
                    dmIntPortContext_t       *onePortContext,
                    bit32                    functionCode,
                    dmSMPRequestBody_t       *dmSMPRequestBody
                   );

osGLOBAL void
dmDiscoverySMPTimerCB(
                      dmRoot_t    * dmRoot, 
                      void        * timerData1,
                      void        * timerData2,
                      void        * timerData3
                     );

osGLOBAL void                          
dmDiscoveryConfiguringTimer(dmRoot_t                 *dmRoot,
                            dmIntPortContext_t       *onePortContext,
                            dmDeviceData_t           *oneDeviceData
                           );


osGLOBAL void
dmDiscoveryConfiguringTimerCB(
                              dmRoot_t    * dmRoot, 
                              void        * timerData1,
                              void        * timerData2,
                              void        * timerData3
                             );

osGLOBAL void                          
dmSMPBusyTimer(dmRoot_t             *dmRoot,
               dmIntPortContext_t   *onePortContext,
               dmDeviceData_t       *oneDeviceData,
               dmSMPRequestBody_t   *dmSMPRequestBody
              );

osGLOBAL void
dmSMPBusyTimerCB(
                 dmRoot_t    * dmRoot, 
                 void        * timerData1,
                 void        * timerData2,
                 void        * timerData3
                );

osGLOBAL void                          
dmConfigureRouteTimer(dmRoot_t                 *dmRoot,
                      dmIntPortContext_t       *onePortContext,
                      dmExpander_t             *oneExpander,
                      smpRespDiscover_t        *pdmSMPDiscoverResp,
                      smpRespDiscover2_t       *pdmSMPDiscover2Resp
                     );

osGLOBAL void
dmConfigureRouteTimerCB(
                        dmRoot_t    * dmRoot, 
                        void        * timerData1,
                        void        * timerData2,
                        void        * timerData3
                       );

#endif                          /* __DMPROTO_H__ */


