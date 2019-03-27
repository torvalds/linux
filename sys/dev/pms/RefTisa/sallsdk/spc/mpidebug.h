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
/*! \file mpidebug.h
 *  \brief The file defines the debug constants and structures
 *
 */
/*******************************************************************************/

#ifndef __MPIDEBUG_H__
#define __MPIDEBUG_H__

/*******************************************************************************/
#define MPI_DEBUG_TRACE_BUFFER_MAX  1024
#define MPI_DEBUG_TRACE_OB_IOMB_SIZE   128 /* 64 */
#define MPI_DEBUG_TRACE_IB_IOMB_SIZE   128 /* 64 */
#define MPI_DEBUG_TRACE_IBQ   1
#define MPI_DEBUG_TRACE_OBQ   0
#define MPI_DEBUG_TRACE_QNUM_ERROR   100 /* Added to Qnumber to indicate error */

typedef struct mpiObDebugTraceEntry_s
{
  bit64       Time;
  bit32       QNum;
  bit32       pici;
  void *      pEntry;
  bit32       Iomb[MPI_DEBUG_TRACE_OB_IOMB_SIZE/4];
} mpiDebugObTraceEntry_t;

typedef struct mpiIbDebugTraceEntry_s
{
  bit64       Time;
  bit32       QNum;
  bit32       pici;
  void *      pEntry;
  bit32       Iomb[MPI_DEBUG_TRACE_IB_IOMB_SIZE/4];
} mpiDebugIbTraceEntry_t;

typedef struct mpiIbDebugTrace_s
{
  bit32                 Idx;
  bit32                 Pad;
  mpiDebugIbTraceEntry_t  Data[MPI_DEBUG_TRACE_BUFFER_MAX];
} mpiDebugIbTrace_t;

typedef struct mpiObDebugTrace_s
{
  bit32                 Idx;
  bit32                 Pad;
  mpiDebugObTraceEntry_t  Data[MPI_DEBUG_TRACE_BUFFER_MAX];
} mpiDebugObTrace_t;

void mpiTraceInit(void);
void mpiTraceAdd(bit32 q,bit32 pici,bit32 ib, void *iomb, bit32 numBytes);

#endif /* __MPIDEBUG_H__ */




/********************************************************************
**  File that contains debug-specific APIs ( driver tracing etc )
*********************************************************************/

#ifndef __SPCDEBUG_H__
#define __SPCDEBUG_H__


/*
** console and trace levels
*/

#define  hpDBG_ALWAYS     0x0000ffff
#define  hpDBG_IOMB       0x00000040
#define  hpDBG_REGISTERS  0x00000020
#define  hpDBG_TICK_INT   0x00000010
#define  hpDBG_SCREAM     0x00000008
#define  hpDBG_VERY_LOUD  0x00000004
#define  hpDBG_LOUD       0x00000002
#define  hpDBG_ERROR      0x00000001
#define  hpDBG_NEVER      0x00000000

#define smTraceDestBuffer    0x00000001
#define smTraceDestRegister  0x00000002
#define smTraceDestDebugger  0x00000004


#define siTraceDestMask     (smTraceDestBuffer    |  \
                             smTraceDestRegister  |  \
                             smTraceDestDebugger)

/* Trace buffer will continuously  */
/* trace and wrap-around on itself */
/* when it reaches the end         */
#define hpDBG_TraceBufferWrapAround   0x80000000
/* This features enables logging of trace time       */
/* stamps.  Only certain key routines use this       */
/* feature because it tends to clog up the trace     */
/* buffer.                                           */
#define hpDBG_TraceBufferUseTimeStamp 0x40000000
/* This features enables logging of trace sequential */
/* stamps.  Only certain key routines use this       */
/* feature because it tends to clog up the trace     */
/* buffer.                                           */
#define hpDBG_TraceBufferUseSequenceStamp 0x20000000

/* Trace IDs of various state machines */
#define fiTraceSmChip   'C'
#define fiTraceSmPort   'P'
#define fiTraceSmLogin  'L'
#define fiTraceSmXchg   'X'
#define fiTraceSmFabr   'F'
#define fiTraceDiscFab  'D'
#define fiTraceDiscLoop 'M'
#define fiTraceFc2      'A'
#define fiTraceTgtState 'S'
#define fiTraceIniState 'I'

/* Trace IDs of various queues  */
#define fiSfsFreeList   'Z'
#define fiSestFreeList  'W'
#define fiOsSfsFreeList 'G'
#define fiLgnFreeList   'K'
#define fiPortalFreeList  'l'
#define fiBusyList      'N'
#define fiOsSfsAllocList     'B'
#define fiTimerList         'V'
#define fiSfsWaitForRspList 'I'
#define fiLgnBusyList   'J'
#define fiPortalBusyList  'g'
#define fiWait4ErqList  'o'
#define fiXchgAbortList   'U'
#define fiXchgWaitList 'b'

/* not used right now */
#define fiSfsDeferFreeList  'q'
#define fiDeferBusyList     'm'
#define fiInvalidList   'X'
#define fiInvalidatedList   'a'
#define fiTmpXchList    'n'

#define TMP_TRACE_BUFF_SIZE  32
#define FC_TRACE_LINE_SIZE   70
/******************************************************************************/
/* Macro Conventions:  we are assuming that the macros will be called inside  */
/*                     a function that already has a workable saRoot variable */
/******************************************************************************/

/******************************************************************************/
/* fiTraceState : ==>        _!n_        _ss: XXXXXXXX       _se: XXXXXXXX    */
/*              statemachine --^     currentstate--^    triggerevent--^       */
/*              NOTE: shorthand forms available as macros below.              */
/******************************************************************************/
#ifdef SA_ENABLE_TRACE_FUNCTIONS


void siResetTraceBuffer(agsaRoot_t  *agRoot);
void siTraceFuncEnter(agsaRoot_t  *agRoot, bit32 mask, bit32 fileid, char *funcid);


GLOBAL void siTraceFuncExit(   agsaRoot_t  *agRoot,  bit32   mask, char  fileid, char  * funcid, char  exitId );


void siTrace(agsaRoot_t  *agRoot, bit32 mask, char *uId, bit32 value, bit32 dataSizeInBits);
void siTrace64(agsaRoot_t  *agRoot, bit32 mask, char *uId, bit64 value, bit32 dataSizeInBits);
bit32 siGetCurrentTraceIndex(agsaRoot_t  *agRoot);
void siTraceListRemove(agsaRoot_t  *agRoot, bit32 mask, char listId, bitptr exchangeId);
void siTraceListAdd(agsaRoot_t  *agRoot, bit32 mask, char listId, bitptr exchangeId);
void siTraceState(agsaRoot_t  *agRoot, bit32 mask, bit32 statemachine, bit32 currentstate, bit32 triggerevent);

#define smTraceState(L,S,C,T)     siTraceState(agRoot,L,S,C,T)
#define smTraceChipState(L,C,T)   siTraceState(agRoot,L,fiTraceSmChip,C,T)
#define smTraceFabricState(L,C,T) siTraceState(agRoot,L,fiTraceSmFabr,C,T)
#define smTracePortState(L,C,T)   siTraceState(agRoot,L,fiTraceSmPort,C,T)
#define smTraceLoginState(L,C,T)  siTraceState(agRoot,L,fiTraceSmLogin,C,T)
#define smTraceXchgState(L,C,T)   siTraceState(agRoot,L,fiTraceSmXchg,C,T)
#define smTraceDiscFabState(L,C,T)    siTraceState(agRoot,L,fiTraceDiscFab,C,T)
#define smTraceDiscLoopState(L,C,T)   siTraceState(agRoot,L,fiTraceDiscLoop,C,T)
#define smTraceFc2State(L,C,T)    siTraceState(agRoot,L,fiTraceFc2,C,T)
#define smTraceScsiTgtState(L,C,T)    siTraceState(agRoot,L,fiTraceTgtState,C,T)
#define smTraceScsiIniState(L,C,T)    siTraceState(agRoot,L,fiTraceIniState,C,T)

#define smResetTraceBuffer(L)   siResetTraceBuffer(L)
#define smTraceFuncEnter(L,I)  siTraceFuncEnter(agRoot,L,siTraceFileID,I)
#define smTraceFuncExit(L,S,I)  siTraceFuncExit(agRoot,L,siTraceFileID,I,S)
#define smGetCurrentTraceIndex(L)   siGetCurrentTraceIndex(L)
#define smTraceListRemove(R,L,I,V)   siTraceListRemove(R,L,I,V)
#define smTraceListAdd(R,L,I,V)   siTraceListAdd(R,L,I,V)

#define smTrace(L,I,V)                                        \
    /*lint -e506 */                                           \
    /*lint -e774 */                                           \
    if (sizeof(V) == 8) {siTrace64(agRoot,L,I,(bit64)V,64);}  \
    else {siTrace(agRoot,L,I,(bit32)V,32);}                   \
    /*lint +e506 */                                           \
    /*lint +e774 */


#else

#define siTraceState(agRoot,L,fiTraceSmXchg,C,T)

#define smTraceState(L,S,C,T)
#define smTraceChipState(L,C,T)
#define smTraceFabricState(L,C,T)
#define smTracePortState(L,C,T)
#define smTraceLoginState(L,C,T)
#define smTraceXchgState(L,C,T)
#define smTraceDiscFabState(L,C,T)
#define smTraceDiscLoopState(L,C,T)
#define smTraceFc2State(L,C,T)
#define smTraceScsiTgtState(L,C,T)
#define smTraceScsiIniState(L,C,T)

#define smResetTraceBuffer(agRoot)
#define smTraceFuncEnter(L,I)
#define smTraceFuncExit(L,S,I)
#define smGetCurrentTraceIndex(L)
#define smTraceListRemove(L,I,V)
#define smTraceListAdd(L,I,V)

#define smTrace(L,I,V)

#endif

struct hpTraceBufferParms_s {
  bit32 TraceCompiled;
  bit32 BufferSize;
  bit32 CurrentTraceIndexWrapCount;
  bit32 CurrentIndex;
  bit32 TraceWrap;
  bit8  * pTrace;
  bit32 * pCurrentTraceIndex;
  bit32 * pTraceIndexWrapCount;
  bit32 * pTraceMask;
};
typedef struct hpTraceBufferParms_s
               hpTraceBufferParms_t;

#ifdef SA_ENABLE_TRACE_FUNCTIONS

GLOBAL void siTraceGetInfo(agsaRoot_t  *agRoot, hpTraceBufferParms_t * pBParms);

#define smTraceGetInfo(R,P)  siTraceGetInfo(R,P)
#else
#define smTraceGetInfo(R,P)
#endif


void siEnableTracing ( agsaRoot_t  *agRoot );
#ifdef SA_ENABLE_TRACE_FUNCTIONS

GLOBAL void siTraceSetMask(agsaRoot_t  *agRoot, bit32 TraceMask  );

#define smTraceSetMask(R,P)  siTraceSetMask(R,P)
#else
#define smTraceSetMask(R,P)
#endif /* SA_ENABLE_TRACE_FUNCTIONS */

#endif /* #ifndef __SPCDEBUG_H__ */

