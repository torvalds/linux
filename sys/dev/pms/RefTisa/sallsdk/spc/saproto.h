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
/*! \file saproto.h
 *  \brief The file defines the function delcaration for internal used function
 *
 */
/******************************************************************************/

#ifndef  __SAPROTO_H__

#define __SAPROTO_H__

/* function declaration */
/*** SATIMER.C ***/
GLOBAL agsaTimerDesc_t *siTimerAdd(
                                  agsaRoot_t      *agRoot,
                                  bit32           timeout,
                                  agsaCallback_t  pfnTimeout,
                                  bit32           Event,
                                  void *          pParm
                                  );

GLOBAL void siTimerRemove(
                          agsaRoot_t      *agRoot,
                          agsaTimerDesc_t *pTimer
                          );

GLOBAL void siTimerRemoveAll(agsaRoot_t   *agRoot);

/*** SAINIT.C ***/
GLOBAL bit32 siConfiguration(agsaRoot_t    *agRoot,
                            mpiConfig_t    *mpiConfig,
                            agsaHwConfig_t *hwConfig,
                            agsaSwConfig_t *swConfig
                            );

GLOBAL bit32 mpiInitialize(agsaRoot_t  *agRoot,
                           mpiMemReq_t *memoryAllocated,
                           mpiConfig_t *config
                           );

GLOBAL bit32 mpiWaitForConfigTable(agsaRoot_t *agRoot,
                                   spc_configMainDescriptor_t *config
                                   );

GLOBAL void mpiUpdateIBQueueCfgTable(agsaRoot_t *agRoot,
                                     spc_inboundQueueDescriptor_t *inQueueCfg,
                                     bit32 QueueTableOffset,
                                     bit8 pcibar
                                     );

GLOBAL void mpiUpdateOBQueueCfgTable(agsaRoot_t *agRoot,
                                     spc_outboundQueueDescriptor_t *outQueueCfg,
                                     bit32 QueueTableOffset,
                                     bit8 pcibar
                                     );
GLOBAL void mpiUpdateFatalErrorTable(agsaRoot_t             *agRoot, 
                              bit32                         FerrTableOffset,
                              bit32                         lowerBaseAddress,
                              bit32                         upperBaseAddress,
                              bit32                         length,
                              bit8                          pcibar);

GLOBAL bit32 mpiGetPCIBarIndex(agsaRoot_t *agRoot,
                               bit32 pciBar
                               );

GLOBAL bit32 mpiUnInitConfigTable(agsaRoot_t *agRoot);

GLOBAL void mpiReadGSTable(agsaRoot_t             *agRoot,
                         spc_GSTableDescriptor_t  *mpiGSTable);

GLOBAL void siInitResources(agsaRoot_t              *agRoot,
                            agsaMemoryRequirement_t *memoryAllocated,
                            agsaHwConfig_t          *hwConfig,
                            agsaSwConfig_t          *swConfig,
                            bit32                   usecsPerTick);

GLOBAL void mpiReadCALTable(agsaRoot_t      *agRoot,
                            spc_SPASTable_t *mpiCALTable,
                            bit32           index);

GLOBAL void mpiWriteCALTable(agsaRoot_t     *agRoot,
                            spc_SPASTable_t *mpiCALTable,
                            bit32           index);

GLOBAL void mpiWriteCALAll(agsaRoot_t     *agRoot,
                           agsaPhyAnalogSetupTable_t *mpiCALTable);

GLOBAL void mpiWrIntVecTable(agsaRoot_t *agRoot,
                            mpiConfig_t* config
                            );

GLOBAL void mpiWrAnalogSetupTable(agsaRoot_t *agRoot,
                            mpiConfig_t      *config
                            );


GLOBAL void mpiWrPhyAttrbTable(agsaRoot_t *agRoot,
                            sasPhyAttribute_t *phyAttrib
                            );

/*** SAPHY.C ***/
GLOBAL bit32 siPhyStopCB(
                      agsaRoot_t    *agRoot,
                      bit32         phyId,
                      bit32         status,
                      agsaContext_t *agContext,
                      bit32         portId,
                      bit32         npipps
                      );

/*** SAPORT.C ***/
GLOBAL void siPortInvalid(
                          agsaRoot_t  *agRoot,
                          agsaPort_t  *pPort
                          );

GLOBAL agsaDeviceDesc_t *siPortSASDeviceAdd(
                                    agsaRoot_t        *agRoot,
                                    agsaPort_t        *pPort,
                                    agsaSASIdentify_t sasIdentify,
                                    bit32             sasInitiator,
                                    bit32             smpTimeout,
                                    bit32             itNexusTimeout,
                                    bit32             firstBurstSize,
                                    bit8              dTypeSRate,
                                    bit32              flag
                                    );

GLOBAL void siPortDeviceRemove(
                              agsaRoot_t        *agRoot,
                              agsaPort_t        *pPort,
                              agsaDeviceDesc_t  *pDevice,
                              bit32             unmap
                              );

GLOBAL agsaDeviceDesc_t *siPortSATADeviceAdd(
                                              agsaRoot_t              *agRoot,
                                              agsaPort_t              *pPort,
                                              agsaDeviceDesc_t        *pSTPBridge,
                                              bit8                    *pSignature,
                                              bit8                    pm,
                                              bit8                    pmField,
                                              bit32                   smpReqTimeout,
                                              bit32                   itNexusTimeout,
                                              bit32                   firstBurstSize,
                                              bit8                    dTypeSRate,
                                              bit32                   flag
                                              );

GLOBAL void siPortDeviceListRemove(
                              agsaRoot_t        *agRoot,
                              agsaPort_t        *pPort,
                              agsaDeviceDesc_t  *pDevice
                              );

/*** SASATA.C ***/
GLOBAL void siSATASignatureCpy(
                                bit8  *pDstSignature,
                                bit8  *pSrcSignature
                                );

/*** SASSP.C ***/

/*** SAHW.C ***/
#ifdef SA_ENABLE_HDA_FUNCTIONS
GLOBAL bit32 siHDAMode(
                      agsaRoot_t  *agRoot,
                      bit32       HDAMode,
                      agsaFwImg_t *userFwImg
                      );

GLOBAL bit32 siHDAMode_V(
                      agsaRoot_t  *agRoot,
                      bit32       HDAMode,
                      agsaFwImg_t *userFwImg
                      );

#endif

GLOBAL bit32 siBar4Shift(
  agsaRoot_t  *agRoot,
  bit32       shiftValue
  );


GLOBAL bit32 siSoftReset(
                       agsaRoot_t  *agRoot,
                       bit32       signature
                       );

GLOBAL bit32 siSpcSoftReset(
                       agsaRoot_t  *agRoot,
                       bit32       signature
                       );

GLOBAL void siChipReset(
                       agsaRoot_t  *agRoot
                       );


GLOBAL bit32 siChipResetV(
                       agsaRoot_t  *agRoot,
                       bit32       signature
                       );

GLOBAL void siChipResetSpc(
                      agsaRoot_t  *agRoot
                      );


/*** SAUTIL.C ***/
GLOBAL void siPrintBuffer(
                          bit32                 debugLevel,
                          siPrintType           type,
                          char                  *header,
                          void                  *a,
                          bit32                 length
                          );
int siIsHexDigit(char a);
GLOBAL FORCEINLINE void* si_memcpy(void *dst, void *src, bit32 count);
GLOBAL FORCEINLINE void* si_memset(void *s, int c, bit32 n);

GLOBAL void siDumpActiveIORequests(
                          agsaRoot_t              *agRoot,
                          bit32                   count);


GLOBAL void siClearActiveIORequests(   agsaRoot_t  *agRoot);

GLOBAL void siCountActiveIORequestsOnDevice( agsaRoot_t *agRoot,  bit32      device );
GLOBAL void siClearActiveIORequestsOnDevice( agsaRoot_t *agRoot,  bit32      device );



/*** SAINT.C ***/
GLOBAL void siEventPhyUpRcvd(
                             agsaRoot_t  *agRoot,
                             bit32       phyId,
                             agsaSASIdentify_t *agSASIdentify,
                             bit32       portId,
                             bit32       npipps,
                             bit8        linkRate
                             );

GLOBAL void siEventSATASignatureRcvd(
                                    agsaRoot_t    *agRoot,
                                    bit32         phyId,
                                    void          *pMsg,
                                    bit32         portId,
                                    bit32         npipps,
                                    bit8          linkRate
                                    );

GLOBAL FORCEINLINE void siIODone(
                     agsaRoot_t          *agRoot,
                     agsaIORequestDesc_t *pRequest,
                     bit32               status,
                     bit32               sspTag
                     );

GLOBAL void siAbnormal(
                       agsaRoot_t          *agRoot,
                       agsaIORequestDesc_t *pRequest,
                       bit32               status,
                       bit32               param,
                       bit32               sspTag
                       );

GLOBAL void siDifAbnormal(
                         agsaRoot_t          *agRoot,
                         agsaIORequestDesc_t *pRequest,
                         bit32               status,
                         bit32               param,
                         bit32               sspTag,
                         bit32               *pMsg1
                         );

GLOBAL void siEventSSPResponseWtDataRcvd(
                                        agsaRoot_t                *agRoot,
                                        agsaIORequestDesc_t       *pRequest,
                                        agsaSSPResponseInfoUnit_t *pRespIU,
                                        bit32                     param,
                                        bit32                     sspTag
                                        );

GLOBAL void siSMPRespRcvd(
                          agsaRoot_t              *agRoot,
                          agsaSMPCompletionRsp_t  *pIomb,
                          bit32                   payloadSize,
                          bit32                   tag
                          );

GLOBAL void siEventSATAResponseWtDataRcvd(
                                          agsaRoot_t          *agRoot,
                                          agsaIORequestDesc_t *pRequest,
                                          bit32               *agFirstDword,
                                          bit32               *pResp,
                                          bit32               lengthResp
                                          );

/*** SADISC.C ***/
GLOBAL bit32 siRemoveDevHandle(
                              agsaRoot_t      *agRoot,
                              agsaDevHandle_t *agDevHandle
                              );

/*** SAMPIRSP.C ***/
GLOBAL FORCEINLINE bit32 mpiParseOBIomb(
                            agsaRoot_t            *agRoot,
                            bit32                 *pMsg1,
                            mpiMsgCategory_t      category,
                            bit16                 opcode
                            );

GLOBAL bit32 mpiEchoRsp(
                        agsaRoot_t          *agRoot,
                        agsaEchoRsp_t       *pIomb
                        );

GLOBAL bit32 mpiGetNVMDataRsp(
  agsaRoot_t          *agRoot,
  agsaGetNVMDataRsp_t *pIomb
  );

GLOBAL bit32 mpiHWevent(
  agsaRoot_t        *agRoot,
  agsaHWEvent_SPC_OUB_t  *pIomb
  );

GLOBAL bit32 mpiPhyStartEvent(
  agsaRoot_t        *agRoot,
  agsaHWEvent_Phy_OUB_t  *pIomb
  );

GLOBAL bit32 mpiPhyStopEvent(
  agsaRoot_t        *agRoot,
  agsaHWEvent_Phy_OUB_t  *pIomb
  );

GLOBAL bit32 mpiSMPCompletion(
  agsaRoot_t             *agRoot,
  agsaSMPCompletionRsp_t *pIomb
  );

GLOBAL bit32 mpiGetDevInfoRspSpc(
  agsaRoot_t          *agRoot,
  agsaGetDevInfoRsp_t *pIomb
  );

GLOBAL bit32 mpiGetPhyProfileRsp(
  agsaRoot_t             *agRoot,
  agsaGetPhyProfileRspV_t *pIomb
  );

GLOBAL bit32 mpiSetPhyProfileRsp(
  agsaRoot_t             *agRoot,
  agsaSetPhyProfileRspV_t *pIomb
  );

GLOBAL bit32 mpiGetDevInfoRsp(
  agsaRoot_t          *agRoot,
  agsaGetDevInfoRspV_t *pIomb
  );

GLOBAL bit32 mpiGetDevHandleRsp(
  agsaRoot_t            *agRoot,
  agsaGetDevHandleRsp_t *pIomb
  );

GLOBAL bit32 mpiPhyCntrlRsp(
  agsaRoot_t             *agRoot,
  agsaLocalPhyCntrlRsp_t *pIomb
  );

GLOBAL bit32 mpiDeviceRegRsp(
  agsaRoot_t                  *agRoot,
  agsaDeviceRegistrationRsp_t *pIomb
  );

GLOBAL bit32 mpiDeregDevHandleRsp(
  agsaRoot_t              *agRoot,
  agsaDeregDevHandleRsp_t *pIomb
  );

GLOBAL FORCEINLINE bit32 mpiSSPCompletion(
  agsaRoot_t        *agRoot,
  bit32             *pIomb
  );

GLOBAL FORCEINLINE bit32 mpiSATACompletion(
  agsaRoot_t        *agRoot,
  bit32             *pIomb
  );

GLOBAL bit32 mpiSSPEvent(
  agsaRoot_t        *agRoot,
  agsaSSPEventRsp_t *pIomb
  );

GLOBAL bit32 mpiSATAEvent(
  agsaRoot_t         *agRoot,
  agsaSATAEventRsp_t *pIomb
  );

GLOBAL bit32 mpiFwFlashUpdateRsp(
  agsaRoot_t             *agRoot,
  agsaFwFlashUpdateRsp_t *payload
  );


GLOBAL bit32 mpiFwExtFlashUpdateRsp(
  agsaRoot_t             *agRoot,
  agsaFwFlashOpExtRsp_t *payload
  );

#ifdef SPC_ENABLE_PROFILE
GLOBAL bit32 mpiFwProfileRsp(
  agsaRoot_t             *agRoot,
  agsaFwProfileRsp_t *payload
  );
#endif
GLOBAL bit32 mpiSetNVMDataRsp(
  agsaRoot_t          *agRoot,
  agsaSetNVMDataRsp_t *pIomb
  );

GLOBAL bit32 mpiSSPAbortRsp(
  agsaRoot_t         *agRoot,
  agsaSSPAbortRsp_t  *pIomb
  );

GLOBAL bit32 mpiSATAAbortRsp(
  agsaRoot_t         *agRoot,
  agsaSATAAbortRsp_t *pIomb
  );

GLOBAL bit32 mpiGPIORsp(
  agsaRoot_t          *agRoot,
  agsaGPIORsp_t       *pIomb
  );

GLOBAL bit32 mpiGPIOEventRsp(
  agsaRoot_t          *agRoot,
  agsaGPIOEvent_t     *pIomb
  );

GLOBAL bit32 mpiSASDiagStartEndRsp(
  agsaRoot_t               *agRoot,
  agsaSASDiagStartEndRsp_t *pIomb
  );

GLOBAL bit32 mpiSASDiagExecuteRsp(
  agsaRoot_t               *agRoot,
  agsaSASDiagExecuteRsp_t  *pIomb
  );

GLOBAL bit32 mpiGeneralEventRsp(
  agsaRoot_t               *agRoot,
  agsaGeneralEventRsp_t    *pIomb
  );

GLOBAL bit32 mpiSSPReqReceivedNotify(
  agsaRoot_t *agRoot,
  agsaSSPReqReceivedNotify_t *pMsg1
  );

GLOBAL bit32 mpiDeviceHandleArrived(
  agsaRoot_t *agRoot,
  agsaDeviceHandleArrivedNotify_t *pMsg1
  );

GLOBAL bit32 mpiGetTimeStampRsp(
  agsaRoot_t               *agRoot,
  agsaGetTimeStampRsp_t    *pIomb
  );

GLOBAL bit32 mpiSASHwEventAckRsp(
  agsaRoot_t               *agRoot,
  agsaSASHwEventAckRsp_t   *pIomb
  );

GLOBAL bit32 mpiSetDevInfoRsp(
  agsaRoot_t             *agRoot,
  agsaSetDeviceInfoRsp_t *pIomb
  );

GLOBAL bit32 mpiSetDeviceStateRsp(
  agsaRoot_t              *agRoot,
  agsaSetDeviceStateRsp_t *pIomb
  );

GLOBAL bit32 mpiGetDeviceStateRsp(
  agsaRoot_t             *agRoot,
  agsaGetDeviceStateRsp_t *pIomb
  );

GLOBAL bit32 mpiSasReInitializeRsp(
  agsaRoot_t               *agRoot,
  agsaSasReInitializeRsp_t *pIomb
  );

GLOBAL bit32 mpiSetControllerConfigRsp(
  agsaRoot_t               *agRoot,
  agsaSetControllerConfigRsp_t *pIomb
  );

GLOBAL bit32 mpiGetControllerConfigRsp(
  agsaRoot_t                  *agRoot,
  agsaGetControllerConfigRsp_t *pIomb
  );

GLOBAL bit32  mpiKekManagementRsp(
    agsaRoot_t               *agRoot,
    agsaKekManagementRsp_t   *pIomb
  );

GLOBAL bit32  mpiDekManagementRsp(
    agsaRoot_t               *agRoot,
    agsaDekManagementRsp_t   *pIomb
  );

GLOBAL bit32 mpiOperatorManagementRsp(
  agsaRoot_t               *agRoot,
  agsaOperatorMangmenRsp_t *pIomb
  );

GLOBAL bit32 mpiBistRsp(
  agsaRoot_t           *agRoot,
  agsaEncryptBistRsp_t *pIomb
  );

GLOBAL bit32 mpiSetOperatorRsp(
  agsaRoot_t               *agRoot,
  agsaSetOperatorRsp_t    *pIomb
  );

GLOBAL bit32 mpiGetOperatorRsp(
  agsaRoot_t               *agRoot,
  agsaGetOperatorRsp_t    *pIomb
  );

GLOBAL bit32 mpiDifEncOffloadRsp(
  agsaRoot_t               *agRoot,
  agsaDifEncOffloadRspV_t  *pIomb
  );

GLOBAL bit32 mpiGetVHistRsp(
   agsaRoot_t          *agRoot,
   agsaGetVHistCapRsp_t *pIomb
  );


/*** SAMPICMD.C ***/
GLOBAL bit32 mpiBuildCmd(
  agsaRoot_t        *agRoot,
  bit32             *payload,
  mpiMsgCategory_t  category,
  bit16             opcode,
  bit16             size,
  bit32             queueNum
  );


GLOBAL bit32 mpiVHistCapCmd(
  agsaRoot_t    *agRoot,
  agsaContext_t *agContext,
  bit32         queueNum,
  bit32         Channel,
  bit32         NumBitLo,
  bit32         NumBitHi,
  bit32         PcieAddrLo,
  bit32         PcieAddrHi,
  bit32         ByteCount );

GLOBAL bit32 mpiEchoCmd(
  agsaRoot_t          *agRoot,
  bit32               queueNum,
  agsaContext_t       *agContext,
  void                *echoPayload
  );

GLOBAL bit32 mpiGetPhyProfileCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32                Operation,
  bit32                PhyId,
  void                *agCB
  );

GLOBAL bit32 mpiSetPhyProfileCmd(
  agsaRoot_t    *agRoot,
  agsaContext_t *agContext,
  bit32         Operation,
  bit32         PhyId,
  bit32         length,
  void *        buffer
  );

GLOBAL bit32 mpiPhyStartCmd(
  agsaRoot_t          *agRoot,
  bit32               tag,
  bit32               phyId,
  agsaPhyConfig_t     *agPhyConfig,
  agsaSASIdentify_t   *agSASIdentify,
  bit32               queueNum
  );

GLOBAL bit32 mpiPhyStopCmd(
  agsaRoot_t          *agRoot,
  bit32               tag,
  bit32               phyId,
  bit32               queueNum
  );

GLOBAL bit32 mpiSMPCmd(
  agsaRoot_t             *agRoot,
  void                   *pIomb,
  bit16                  opcode,
  agsaSMPCmd_t           *payload,
  bit8                   inq,
  bit8                   outq
  );

GLOBAL bit32 mpiDeregDevHandleCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  agsaDeviceDesc_t    *pDevice,
  bit32               deviceId,
  bit32               portId,
  bit32               queueNum
  );

GLOBAL bit32 mpiGetDeviceHandleCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               portId,
  bit32               flags,
  bit32               maxDevs,
  bit32               queueNum,
  bit32               skipCount
  );

GLOBAL bit32 mpiLocalPhyControlCmd(
  agsaRoot_t          *agRoot,
  bit32               tag,
  bit32               phyId,
  bit32               operation,
  bit32               queueNum
  );

GLOBAL bit32 mpiGetDeviceInfoCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               deviceid,
  bit32               option,
  bit32               queueNum
  );

GLOBAL bit32 mpiDevHandleAcceptCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               ctag,
  bit32               deviceId,
  bit32               action,
  bit32               flag,
  bit32               itlnx,
  bit32               queueNum
  );

GLOBAL bit32 mpiPortControlRsp(
  agsaRoot_t           *agRoot,
  agsaPortControlRsp_t *pIomb
  );

GLOBAL bit32 mpiSMPAbortRsp(
  agsaRoot_t         *agRoot,
  agsaSMPAbortRsp_t  *pIomb
  );

GLOBAL bit32 siGetRegisterDumpGSM(
  agsaRoot_t        *agRoot,
  void              *destinationAddress,
  bit32             regDumpNum,
  bit32             regDumpOffset,
  bit32             len
  );

GLOBAL bit32 mpiNVMReadRegDumpCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               queueNum,
  bit32               cpuId,
  bit32               cOffset,
  bit32               addrHi,
  bit32               addrLo,
  bit32               len
  );

GLOBAL bit32 mpiDeviceHandleRemoval(
  agsaRoot_t                *agRoot,
  agsaDeviceHandleRemoval_t *pMsg1);

GLOBAL bit32 mpiGetNVMDCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  agsaNVMDData_t      *NVMDInfo,
  bit32               queueNum
  );

GLOBAL bit32 mpiSetNVMDCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  agsaNVMDData_t      *NVMDInfo,
  bit32               queueNum
  );

GLOBAL bit32 mpiSetDeviceInfoCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               deviceid,
  bit32               option,
  bit32               queueNum,
  bit32               param,
  ossaSetDeviceInfoCB_t   agCB
  );

GLOBAL bit32 mpiSetDeviceStateCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               deviceId,
  bit32               nds,
  bit32               queueNum
  );

GLOBAL bit32 mpiGetDeviceStateCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               deviceId,
  bit32               queueNum
  );

GLOBAL bit32 mpiSasReinitializeCmd(
  agsaRoot_t        *agRoot,
  agsaContext_t     *agContext,
  agsaSASReconfig_t *agSASConfig,
  bit32             queueNum
  );

GLOBAL bit32 mpiSGpioRsp(
  agsaRoot_t       *agRoot,
  agsaSGpioRsp_t   *pInIomb
  );

GLOBAL bit32 mpiPCIeDiagExecuteRsp(
  agsaRoot_t    *agRoot,
  void          *pInIomb
  );

GLOBAL bit32 mpiGetDFEDataRsp(
  agsaRoot_t    *agRoot,
  void          *pInIomb
  );

GLOBAL bit32 mpiGetVisDataRsp(
  agsaRoot_t    *agRoot,
  void          *pIomb
  );

GLOBAL bit32 mpiSetControllerConfigCmd(
  agsaRoot_t        *agRoot,
  agsaContext_t     *agContext,
  agsaSetControllerConfigCmd_t *agControllerConfig,
  bit32             queueNum,
  bit8              modePageContext
  );

GLOBAL bit32 mpiGetControllerConfigCmd(
   agsaRoot_t        *agRoot,
   agsaContext_t     *agContext,
   agsaGetControllerConfigCmd_t *agControllerConfig,
   bit32             queueNum
   );

GLOBAL bit32 mpiKekManagementCmd(
   agsaRoot_t        *agRoot,
   agsaContext_t     *agContext,
   agsaKekManagementCmd_t *agKekMgmt,
   bit32             queueNum
   );

GLOBAL bit32 mpiDekManagementCmd(
   agsaRoot_t        *agRoot,
   agsaContext_t     *agContext,
   agsaDekManagementCmd_t *agDekMgmt,
   bit32             queueNum
   );

GLOBAL bit32 mpiOperatorManagementCmd(
  agsaRoot_t                *agRoot,
  bit32                     queueNum,
  agsaContext_t             *agContext,
  agsaOperatorMangmentCmd_t *operatorcode );

GLOBAL bit32 mpiEncryptBistCmd(
  agsaRoot_t        *agRoot,
  bit32              queueNum,
  agsaContext_t     *agContext,
  agsaEncryptBist_t *bist );

GLOBAL bit32 mpiSetOperatorCmd(
  agsaRoot_t                *agRoot,
  bit32                      queueNum,
  agsaContext_t             *agContext,
  agsaSetOperatorCmd_t      *operatorcode
  );

GLOBAL bit32 mpiGetOperatorCmd(
  agsaRoot_t                *agRoot,
  bit32                      queueNum,
  agsaContext_t             *agContext,
  agsaGetOperatorCmd_t      *operatorcode
  );

GLOBAL bit32 mpiDIFEncryptionOffloadCmd(
   agsaRoot_t                *agRoot,
   agsaContext_t             *agContext,
   bit32                     queueNum,
   bit32                     op,
   agsaDifEncPayload_t      *agDifEncOffload,
   ossaDIFEncryptionOffloadStartCB_t agCB
   );

bit32 siOurMSIXInterrupt(agsaRoot_t *agRoot,bit32 interruptVectorIndex);
void siDisableMSIXInterrupts(agsaRoot_t *agRoot,bit32 interruptVectorIndex);
void siReenableMSIXInterrupts(agsaRoot_t *agRoot,bit32 interruptVectorIndex);

bit32 siOurMSIInterrupt(agsaRoot_t *agRoot,bit32 interruptVectorIndex);
void siDisableMSIInterrupts(agsaRoot_t *agRoot,bit32 interruptVectorIndex);
void siReenableMSIInterrupts(agsaRoot_t *agRoot,bit32 interruptVectorIndex);


bit32 siOurLegacyInterrupt(agsaRoot_t *agRoot,bit32 interruptVectorIndex);
void siDisableLegacyInterrupts(agsaRoot_t *agRoot,bit32 interruptVectorIndex);
void siReenableLegacyInterrupts(agsaRoot_t *agRoot,bit32 interruptVectorIndex);

bit32 siOurMSIX_V_Interrupt(agsaRoot_t *agRoot,bit32 interruptVectorIndex);
bit32 siOurMSI_V_Interrupt(agsaRoot_t *agRoot,bit32 interruptVectorIndex);
bit32 siOurLegacy_V_Interrupt(agsaRoot_t *agRoot,bit32 interruptVectorIndex);

void siDisableMSIX_V_Interrupts(agsaRoot_t *agRoot,bit32 interruptVectorIndex);
void siDisableMSI_V_Interrupts(agsaRoot_t *agRoot,bit32 interruptVectorIndex);
void siDisableLegacy_V_Interrupts(agsaRoot_t *agRoot,bit32 interruptVectorIndex);

void siReenableMSIX_V_Interrupts(agsaRoot_t *agRoot,bit32 interruptVectorIndex);
void siReenableMSI_V_Interrupts(agsaRoot_t *agRoot,bit32 interruptVectorIndex);
void siReenableLegacy_V_Interrupts(agsaRoot_t *agRoot,bit32 interruptVectorIndex);


GLOBAL void siUpdateBarOffsetTable(agsaRoot_t     *agRoot, bit32   Spc_Type);

GLOBAL void siPciCpyMem(agsaRoot_t *agRoot,
                       bit32 soffset,
                       const void *dst,
                       bit32 DWcount,
                       bit32 busBaseNumber
                       );

GLOBAL void siHalRegWriteExt(
                             agsaRoot_t  *agRoot,
                             bit32       generic,
                             bit32       regOffset,
                             bit32       regValue
                             );

GLOBAL bit32 siHalRegReadExt( agsaRoot_t  *agRoot,
                             bit32       generic,
                             bit32       regOffset
                             );

#ifdef SA_FW_TIMER_READS_STATUS
bit32 siReadControllerStatus(
                                  agsaRoot_t      *agRoot,
                                  bit32           Event,
                                  void *          pParm
                                  );
#endif /* SA_FW_TIMER_READS_STATUS */


#if defined(SALLSDK_DEBUG)
void sidump_hwConfig(agsaHwConfig_t  *hwConfig);
void sidump_swConfig(agsaSwConfig_t  *swConfig);
void sidump_Q_config( agsaQueueConfig_t *queueConfig );
#endif
GLOBAL bit32 siGetTableOffset(
              agsaRoot_t *agRoot,
              bit32  TableOffsetInTable
              );

GLOBAL bit32 siGetPciBar(
              agsaRoot_t *agRoot
              );

GLOBAL bit32 siScratchDump(agsaRoot_t *agRoot);

void si_macro_check(agsaRoot_t *agRoot);

GLOBAL bit32 si_check_V_HDA(agsaRoot_t *agRoot);
GLOBAL bit32 si_check_V_Ready(agsaRoot_t *agRoot);

GLOBAL void siPCITriger(agsaRoot_t *agRoot);

GLOBAL void siCheckQs(agsaRoot_t *agRoot);


GLOBAL bit32 smIsCfg_V_ANY( agsaRoot_t *agRoot);
GLOBAL bit32 smIS_SPC( agsaRoot_t *agRoot);
GLOBAL bit32 smIS_HIL( agsaRoot_t *agRoot);
GLOBAL bit32 smIS_SPC6V( agsaRoot_t *agRoot);
GLOBAL bit32 smIS_SPC12V( agsaRoot_t *agRoot);
GLOBAL bit32 smIS_SPCV( agsaRoot_t *agRoot);
GLOBAL bit32 smIS_ENCRYPT( agsaRoot_t *agRoot);
GLOBAL bit32 smIS_SPCV_2_IOP( agsaRoot_t *agRoot);
#endif  /*__SAPROTO_H__ */
