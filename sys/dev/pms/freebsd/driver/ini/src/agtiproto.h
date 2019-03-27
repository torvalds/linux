/*******************************************************************************
**Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
 *
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
*
*INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
*ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
*SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
*OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
*WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
*THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
**
* $FreeBSD$
**
*******************************************************************************/

//void agtiapi_cam_init(struct agtiapi_softc *sc);
//void agtiapi_cam_poll( struct cam_sim *asim );
int agtiapi_QueueCmnd_(struct agtiapi_softc *, union ccb * );
int agtiapi_alloc_ostimem(struct agtiapi_softc *);
int agtiapi_alloc_requests(struct agtiapi_softc *);
static int agtiapi_PrepareSGList(struct agtiapi_softc *, ccb_t *);
static void agtiapi_PrepareSGListCB( void *arg,
                                     bus_dma_segment_t *dm_segs,
                                     int nseg,
                                     int error );
static int agtiapi_PrepareSMPSGList(struct agtiapi_softc *pmcsc, ccb_t *);
static void agtiapi_PrepareSMPSGListCB( void *arg,
                                        bus_dma_segment_t *dm_segs,
                                        int nsegs,
                                        int error );
int agtiapi_eh_HostReset( struct agtiapi_softc *pmcsc, union ccb *cmnd );
STATIC void agtiapi_FreeCCB(struct agtiapi_softc *pmcsc, pccb_t pccb);
STATIC void agtiapi_FreeSMPCCB(struct agtiapi_softc *pmcsc, pccb_t pccb);
STATIC void agtiapi_FreeTMCCB(struct agtiapi_softc *pmcsc, pccb_t pccb);
STATIC pccb_t agtiapi_GetCCB(struct agtiapi_softc *pmcsc);
void agtiapi_SetLunField( ccb_t *pccb );
STATIC void agtiapi_QueueCCB( struct agtiapi_softc *pmcsc,
                              pccb_t *phead,
                              pccb_t *ptail, 
#ifdef AGTIAPI_LOCAL_LOCK
                              struct mtx *mutex,
#endif
                              ccb_t *pccb );
static int agtiapi_QueueSMP(struct agtiapi_softc *, union ccb * );
STATIC void agtiapi_StartIO(struct agtiapi_softc *pmcsc);
STATIC void agtiapi_StartSMP(struct agtiapi_softc *pmcsc);
STATIC void agtiapi_DumpCCB(ccb_t *pccb);
STATIC void agtiapi_Done(struct agtiapi_softc *pmcsc, ccb_t *pccb);
STATIC void agtiapi_SMPDone(struct agtiapi_softc *pmcsc, ccb_t *pccb);
// void agtiapi_LogEvent(ag_card_t *, U16, U16, U32 *, U08, S08 *, ...);
STATIC U32 agtiapi_CheckError(struct agtiapi_softc *pmcsc, ccb_t *pccb);
STATIC U32 agtiapi_CheckSMPError(struct agtiapi_softc *pmcsc, ccb_t *pccb);
STATIC void agtiapi_Retry(struct agtiapi_softc *pmcsc, ccb_t *pccb);
static void agtiapi_scan(struct agtiapi_softc *pmcsc);
STATIC int agtiapi_FindWWNListNext( ag_tgt_map_t  * pWWNList, int lstMax );
STATIC U32 agtiapi_GetDevHandle(struct agtiapi_softc *pmcsc, 
				ag_portal_info_t *pPortalInfo, 
				U32 eType, U32 eStatus);

int agtiapi_StartTM(struct agtiapi_softc *pCard, ccb_t *pccb);

STATIC void wwnprintk(unsigned char *name, int len);
STATIC int wwncpy(ag_device_t      *pDevice);

STATIC void agtiapi_DiscoverTgt(struct agtiapi_softc *pCard);
agBOOLEAN agtiapi_CheckCB( struct agtiapi_softc *pCard,
                           U32 milisec,
                           U32 flag,
                           volatile U32 *pStatus );
STATIC agBOOLEAN  agtiapi_DeQueueCCB( struct agtiapi_softc *,
                                      pccb_t *,
                                      pccb_t *,
#ifdef AGTIAPI_LOCAL_LOCK
                                      struct mtx *,
#endif
                                      ccb_t * );

void agtiapi_CheckAllVectors( struct agtiapi_softc *pCard, bit32 context );

STATIC U32 agtiapi_InitCCBs( struct agtiapi_softc *pCard,
                             int tgtCount,
                             int tid );
STATIC void agtiapi_PrepCCBs( struct agtiapi_softc *pCard,
                              ccb_hdr_t *hdr,
                              U32 size,
                              U32 max_ccb,
                              int tid );


#ifdef LINUX_PERBI_SUPPORT
void  agtiapi_GetWWNMappings( struct agtiapi_softc *, ag_mapping_t * );
//#ifndef HOTPLUG_SUPPORT
STATIC void agtiapi_MapWWNList( struct agtiapi_softc *pCard );
//#endif
#endif

STATIC void agtiapi_ReleaseCCBs( struct agtiapi_softc *pCard );
STATIC void agtiapi_clrRmScan(   struct agtiapi_softc *pCard );
STATIC void agtiapi_TITimer(    void *data );
STATIC void agtiapi_devRmCheck( void *data );

int agtiapi_ReleaseHBA( device_t dev );

void agtiapi_IntrHandler0(  void *arg );
void agtiapi_IntrHandler1(  void *arg );
void agtiapi_IntrHandler2(  void *arg );
void agtiapi_IntrHandler3(  void *arg );
void agtiapi_IntrHandler4(  void *arg );
void agtiapi_IntrHandler5(  void *arg );
void agtiapi_IntrHandler6(  void *arg );
void agtiapi_IntrHandler7(  void *arg );
void agtiapi_IntrHandler8(  void *arg );
void agtiapi_IntrHandler9(  void *arg );
void agtiapi_IntrHandler10( void *arg );
void agtiapi_IntrHandler11( void *arg );
void agtiapi_IntrHandler12( void *arg );
void agtiapi_IntrHandler13( void *arg );
void agtiapi_IntrHandler14( void *arg );
void agtiapi_IntrHandler15( void *arg );
void agtiapi_IntrHandlerx_( void *arg, int index );
STATIC agBOOLEAN agtiapi_InitCardSW( struct agtiapi_softc *pmsc );
STATIC agBOOLEAN agtiapi_InitCardHW( struct agtiapi_softc *pmsc );
STATIC void agtiapi_DumpCDB( const char *ptitle, ccb_t *pccb );
void agtiapi_hexdump( const char *ptitle, bit8 *pbuf, int len );
static void agtiapi_SglMemoryCB( void *arg,
                                 bus_dma_segment_t *dm_segs,
                                 int nseg,
                                 int error );
static void agtiapi_MemoryCB( void *arg,
                              bus_dma_segment_t *dm_segs,
                              int nseg,
                              int error );
U32 agtiapi_ResetCard( struct agtiapi_softc *pCard, unsigned long *flags );
int agtiapi_DoSoftReset( struct agtiapi_softc *pmcsc );

STATIC void agtiapi_FlushCCBs(   struct agtiapi_softc *pCard, U32 flag );

