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

/*! \file samacro.h
 *  \brief The file defines macros used in LL sTSDK
 */

/*******************************************************************************/

#ifndef __SAMACRO_H__
#define __SAMACRO_H__

#if defined(SALLSDK_DEBUG)
#define MPI_IBQ_IOMB_LOG_ENABLE
#define MPI_OBQ_IOMB_LOG_ENABLE
#endif

/*! \def MIN(a,b)
* \brief MIN macro
*
* use to find MIN of two values
*/
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

/*! \def MAX(a,b)
* \brief MAX macro
*
* use to find MAX of two values
*/
#ifndef MAX
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#endif

/*************************************************************************************************
 *                      define Phy status macros                                                 *
 *************************************************************************************************/
/*! \def PHY_STATUS_SET(pPhy, value)
* \brief PHY_STATUS_SET macro
*
* use to set phy status
*/
#define PHY_STATUS_SET(pPhy, value)  ((pPhy)->status = (((pPhy)->status & 0xFFFF0000) | (value)))

/*! \def PHY_STATUS_CHECK(pPhy, value)
* \brief PHY_STATUS_CHECK macro
*
* use to check phy status
*/
#define PHY_STATUS_CHECK(pPhy, value)  ( ((pPhy)->status & 0x0000FFFF) == (value) )


/************************************************************************************
 *                        define CBUFFER operation macros                           *
 ************************************************************************************/
/*! \def AGSAMEM_ELEMENT_READ(pMem, index)
* \brief AGSAMEM_ELEMENT_READ macro
*
* use to read an element of a memory array
*/
#define AGSAMEM_ELEMENT_READ(pMem, index) (((bit8 *)(pMem)->virtPtr) + (pMem)->singleElementLength * (index))

/************************************************************************************
 *                        define Chip ID macro                                      *
 ************************************************************************************/

#define SA_TREAT_SFC_AS_SPC

#ifdef SA_TREAT_SFC_AS_SPC
#define SA_SFC_AS_SPC 1
#define SA_SFC_AS_SPCV 0
#else /* TREAT_SFC_AS_SPCv */
#define SA_SFC_AS_SPC 0
#define SA_SFC_AS_SPCV 1
#endif /* SA_TREAT_SFC_AS_SPC */

#define IS_SDKDATA(agr) (((agr)->sdkData != agNULL ) ? 1 : 0) /* returns true if sdkdata is available */

#define smIsCfgSpcREV_A(agr)    (8  ==( ossaHwRegReadConfig32((agr), 8 ) & 0xF) ? 1 : 0) /* returns true config space read is REVA */
#define smIsCfgSpcREV_B(agr)    (4  ==( ossaHwRegReadConfig32((agr), 8 ) & 0xF) ? 1 : 0) /* returns true config space read is REVB */
#define smIsCfgSpcREV_C(agr)    (5  ==( ossaHwRegReadConfig32((agr), 8 ) & 0xF) ? 1 : 0) /* returns true config space read is REVC */

#define smIsCfgVREV_A(agr)    (4  ==( ossaHwRegReadConfig32((agr), 8 ) & 0xF) ? 1 : 0) /* returns true config space read is REVA */
#define smIsCfgVREV_B(agr)    (5  ==( ossaHwRegReadConfig32((agr), 8 ) & 0xF) ? 1 : 0) /* returns true config space read is REVB */
#define smIsCfgVREV_C(agr)    (6  ==( ossaHwRegReadConfig32((agr), 8 ) & 0xF) ? 1 : 0) /* returns true config space read is REVC */

#define smIsCfg8001(agr)   (VEN_DEV_SPC   == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000)  ? 1 : 0) /* returns true config space read is SPC */
#define smIsCfg8081(agr)   (VEN_DEV_HIL   == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000 ) ? 1 : 0) /* returns true config space read is Hialeah */

#define smIsCfg_V8025(agr) (VEN_DEV_SFC   == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000)  ? 1 : 0) /* returns true config space read is SFC  */

#define smIsCfg_V8008(agr) (VEN_DEV_SPCV  == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000)  ? 1 : 0) /* returns true config space read is SPCv */
#define smIsCfg_V8009(agr) (VEN_DEV_SPCVE == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000)  ? 1 : 0) /* returns true config space read is SPCv */
#define smIsCfg_V8018(agr) (VEN_DEV_SPCVP == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000)  ? 1 : 0) /* returns true config space read is SPCv */
#define smIsCfg_V8019(agr) (VEN_DEV_SPCVEP== (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000)  ? 1 : 0) /* returns true config space read is SPCv */

#define smIsCfg_V8088(agr) (VEN_DEV_ADAPVP == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCv */
#define smIsCfg_V8089(agr) (VEN_DEV_ADAPVEP== (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPCv */

#define smIsCfg_V8070(agr) (VEN_DEV_SPC12V  == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC12v */
#define smIsCfg_V8071(agr) (VEN_DEV_SPC12VE == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC12v */
#define smIsCfg_V8072(agr) (VEN_DEV_SPC12VP == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC12v */
#define smIsCfg_V8073(agr) (VEN_DEV_SPC12VEP== (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC12v */

#define smIsCfg_V8074(agr) (VEN_DEV_SPC12ADP   == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is Adaptec SPC12v */
#define smIsCfg_V8075(agr) (VEN_DEV_SPC12ADPE  == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is Adaptec SPC12v */
#define smIsCfg_V8076(agr) (VEN_DEV_SPC12ADPP  == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is Adaptec SPC12v */
#define smIsCfg_V8077(agr) (VEN_DEV_SPC12ADPEP == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is Adaptec SPC12v */
#define smIsCfg_V8006(agr) (VEN_DEV_SPC12SATA  == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is Adaptec SPC12v */
#define smIsCfg_V9015(agr) (VEN_DEV_9015 == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC12v */
#define smIsCfg_V9060(agr) (VEN_DEV_9060 == (ossaHwRegReadConfig32((agr),0 ) & 0xFFFF0000) ? 1 : 0) /* returns true config space read is SPC12v */

#define smIsCfg_SPC_ANY(agr) ((smIsCfg8001((agr))    == 1) ? 1 : \
                              (smIsCfg8081((agr))    == 1) ? 1 : \
                              (smIsCfg_V8025((agr)) == 1) ? SA_SFC_AS_SPC : 0)

#define smIS_SPCV8008(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPCV )  ? 1 : 0) : smIsCfg_V8008((agr)))
#define smIS_SPCV8009(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPCVE)  ? 1 : 0) : smIsCfg_V8009((agr)))
#define smIS_SPCV8018(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPCVP)  ? 1 : 0) : smIsCfg_V8018((agr)))
#define smIS_SPCV8019(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPCVEP) ? 1 : 0) : smIsCfg_V8019((agr)))
#define smIS_ADAP8088(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_ADAPVP) ? 1 : 0) : smIsCfg_V8088((agr)))
#define smIS_ADAP8089(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_ADAPVEP)? 1 : 0): smIsCfg_V8089((agr)))

#define smIS_SPCV8070(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPC12V ) ? 1 : 0) : smIsCfg_V8070((agr)))
#define smIS_SPCV8071(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPC12VE) ? 1 : 0) : smIsCfg_V8071((agr)))
#define smIS_SPCV8072(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPC12VP) ? 1 : 0) : smIsCfg_V8072((agr)))
#define smIS_SPCV8073(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPC12VEP)? 1 : 0) : smIsCfg_V8073((agr)))

#define smIS_SPCV8074(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPC12ADP ) ? 1 : 0) : smIsCfg_V8074((agr)))
#define smIS_SPCV8075(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPC12ADPE) ? 1 : 0) : smIsCfg_V8075((agr)))
#define smIS_SPCV8076(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPC12ADPP) ? 1 : 0) : smIsCfg_V8076((agr)))
#define smIS_SPCV8077(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPC12ADPEP)? 1 : 0) : smIsCfg_V8077((agr)))
#define smIS_SPCV8006(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPC12SATA) ? 1 : 0) : smIsCfg_V8006((agr)))
#define smIS_SPCV9015(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_9015)    ? 1 : 0) : smIsCfg_V9015((agr)))
#define smIS_SPCV9060(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_9060)    ? 1 : 0) : smIsCfg_V9060((agr)))

#define smIS_SPCV8025(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SFC  ) ? 1 : 0) : smIsCfg_V8025((agr)))

#define smIS_SFC(agr)      (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SFC  ) ? 1 : 0) : smIsCfg_V8025((agr)))
#define smIS_spc8001(agr)  (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_SPC  ) ? 1 : 0) : smIsCfg8001((agr)))
#define smIS_spc8081(agr)  (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->ChipId == VEN_DEV_HIL  ) ? 1 : 0) : smIsCfg8081((agr)))



#define smIS_SFC_AS_SPC(agr) ((smIS_SFC((agr)) == 1) ? SA_SFC_AS_SPC : 0 )

#define smIS_SFC_AS_V(agr)   ((smIS_SFC((agr)) == 1 )? SA_SFC_AS_SPCV : 0 )

/* Use 64 bit interrupts for SPCv, before getting saroot. Once saroot available only use 64bit when needed */
#define smIS64bInt(agr) (IS_SDKDATA((agr)) ? ( (((agsaLLRoot_t *)((agr)->sdkData))->Use64bit) ? 1 : 0)  : smIS_SPCV(agr))

#define WHATTABLE(agr)                                                         \
(                                                                               \
IS_SDKDATA((agr)) ?                                                               \
  (smIS_SPC((agr))  ? &SPCTable[0]  : (smIS_SPCV((agr)) ? &SPC_V_Table[0] : agNULL ) )  \
:                                                                               \
  (smIsCfg_SPC_ANY((agr)) ? &SPCTable[0] : (smIsCfg_V_ANY((agr)) ? &SPC_V_Table[0] : agNULL ) ) \
) \

#if defined(SALLSDK_DEBUG)
/*
* for debugging purposes.
*/
extern bit32 gLLDebugLevel;

#define SA_DBG0(format) ossaLogDebugString(gLLDebugLevel, 0, format)
#define SA_DBG1(format) ossaLogDebugString(gLLDebugLevel, 1, format)
#define SA_DBG2(format) ossaLogDebugString(gLLDebugLevel, 2, format)
#define SA_DBG3(format) ossaLogDebugString(gLLDebugLevel, 3, format)
#define SA_DBG4(format) ossaLogDebugString(gLLDebugLevel, 4, format)
#define SA_DBG5(format) ossaLogDebugString(gLLDebugLevel, 5, format)
#define SA_DBG6(format) ossaLogDebugString(gLLDebugLevel, 6, format)

#else

#define SA_DBG0(format)
#define SA_DBG1(format)
#define SA_DBG2(format)
#define SA_DBG3(format)
#define SA_DBG4(format)
#define SA_DBG5(format)
#define SA_DBG6(format)

#endif

#define SA_ASSERT OS_ASSERT

typedef enum siPrintType_e
{
  SA_8,
  SA_16,
  SA_32
} siPrintType;

#if defined(SALLSDK_DEBUG)
#define SA_PRINTBUF(lDebugLevel,lWidth,pHeader,pBuffer,lLength) siPrintBuffer(lDebugLevel,lWidth,pHeader,pBuffer,lLength)
#else
#define SA_PRINTBUF(lDebugLevel,lWidth,pHeader,pBuffer,lLength)
#endif

#ifdef SALLSDK_DEBUG

#define DBG_DUMP_SSPSTART_CMDIU(agDevHandle,agRequestType,agRequestBody) siDumpSSPStartIu(agDevHandle,agRequestType,agRequestBody)

#else

#define DBG_DUMP_SSPSTART_CMDIU(agDevHandle,agRequestType,agRequestBody)

#endif

#ifdef MPI_DEBUG_TRACE_ENABLE
#define MPI_DEBUG_TRACE_ENTER_LOCK  ossaSingleThreadedEnter(agRoot, LL_IOMB_TRACE_LOCK);
#define MPI_DEBUG_TRACE_LEAVE_LOCK  ossaSingleThreadedLeave(agRoot, LL_IOMB_TRACE_LOCK);

#define MPI_DEBUG_TRACE( queue, pici, ib,iomb,count) \
  MPI_DEBUG_TRACE_ENTER_LOCK \
 mpiTraceAdd( (queue), (pici),(ib), (iomb), (count)); \
  MPI_DEBUG_TRACE_LEAVE_LOCK
#else
#define MPI_DEBUG_TRACE( queue, pici, ib,iomb,count)
#endif /* MPI_DEBUG_TRACE_ENABLE */

#ifdef MPI_IBQ_IOMB_LOG_ENABLE
#define MPI_IBQ_IOMB_LOG(qNumber, msgHeader, msgLength) \
do \
{ \
  bit32 i; \
  SA_DBG3(("\n")); \
  SA_DBG3(("mpiMsgProduce: IBQ %d\n", (qNumber))); \
  for (i = 0; i < msgLength/16; i++) \
  { \
    SA_DBG3(("Inb: DW %02d 0x%08x 0x%08x 0x%08x 0x%08x\n", i*4, *((bit32 *)msgHeader+(i*4)), \
           *((bit32 *)msgHeader+(i*4)+1), *((bit32 *)msgHeader+(i*4)+2), \
           *((bit32 *)msgHeader+(i*4)+3))); \
  } \
} while(0)
#endif
#ifdef MPI_OBQ_IOMB_LOG_ENABLE
#define MPI_OBQ_IOMB_LOG(qNumber, msgHeader, msgLength) \
do \
{ \
  bit32 i; \
  SA_DBG3(("\n")); \
  SA_DBG3(("mpiMsgConsume: OBQ %d\n", qNumber)); \
  for (i = 0; i < msgLength/16; i++) \
  { \
    SA_DBG3(("Out: DW %02d 0x%08x 0x%08x 0x%08x 0x%08x\n", i*4, *((bit32 *)msgHeader+(i*4)), \
           *((bit32 *)msgHeader+(i*4)+1), *((bit32 *)msgHeader+(i*4)+2), \
           *((bit32 *)msgHeader+(i*4)+3))); \
  } \
} while(0)
#endif


/************************************************************************************
 *                        Wait X Second                                             *
 ************************************************************************************/

#define WAIT_SECONDS(x) ((x) * 1000 * 1000 )
#define ONE_HUNDRED_MILLISECS (100 * 1000)   /* 100,000 microseconds  */

#define WAIT_INCREMENT_DEFAULT  1000
#define WAIT_INCREMENT  (IS_SDKDATA(agRoot) ? ( ((agsaLLRoot_t *)(agRoot->sdkData))->minStallusecs ) : WAIT_INCREMENT_DEFAULT )
// (((agsaLLRoot_t *)(agRoot->sdkData))->minStallusecs)


#define MAKE_MODULO(a,b)  (((a) % (b)) ? ((a) - ((a) % (b))) : (a))


#define HDA_STEP_2  1
#define HDA_STEP_3  1
#define HDA_STEP_4  1
#define HDA_STEP_5  1
#define HDA_STEP_6  1
#define HDA_STEP_7  1
#define HDA_STEP_8  1

#endif /* __SAMACRO_H__ */
