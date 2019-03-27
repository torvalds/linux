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

********************************************************************************/

/*******************************************************************************/
/*! \file mpidebug.c
 *  \brief The file is a MPI Libraries to implement the MPI debug and trace functions
 *
 * The file implements the MPI functions.
 *
 */
/*******************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/RefTisa/sallsdk/spc/saglobal.h>
#ifdef MPI_DEBUG_TRACE_ENABLE /* enable with CCBUILD_MPI_TRACE*/

/*******************************************************************************/

#ifdef OSLAYER_USE_HI_RES_TIMER
unsigned __int64
GetHiResTimeStamp(void);
#endif /* OSLAYER_USE_HI_RES_TIMER */
/*******************************************************************************/
/*******************************************************************************/
/* FUNCTIONS                                                                   */
/*******************************************************************************/
mpiDebugObTrace_t obTraceData;
mpiDebugIbTrace_t ibTraceData;

void mpiTraceInit(void)
{

  SA_DBG1(("mpiTraceInit:obTraceData @ %p\n",&obTraceData ));
  SA_DBG1(("mpiTraceInit:ibTraceData @ %p\n",&ibTraceData ));
  SA_DBG1(("mpiTraceInit: num enties %d Ib Iomb size %d Ob Iomb size %d\n",
               MPI_DEBUG_TRACE_BUFFER_MAX,
               MPI_DEBUG_TRACE_IB_IOMB_SIZE,
               MPI_DEBUG_TRACE_OB_IOMB_SIZE ));

  si_memset(&obTraceData, 0, sizeof(obTraceData));
  si_memset(&ibTraceData, 0, sizeof(ibTraceData));
}

void mpiTraceAdd( bit32 q,bit32 pici,bit32 ib, void *iomb, bit32 numBytes)
{
  bit32                  curIdx;
  mpiDebugIbTraceEntry_t *curIbTrace;
  mpiDebugObTraceEntry_t *curObTrace;

  mpiDebugIbTrace_t * ibTrace = &ibTraceData;
  mpiDebugObTrace_t * obTrace = &obTraceData;

  if (ib)
  {
    if(ibTrace->Idx >= MPI_DEBUG_TRACE_BUFFER_MAX)
    {
      ibTrace->Idx = 0;
    }
    curIdx = ibTrace->Idx;

    curIbTrace = &ibTrace->Data[curIdx];
    curIbTrace->pEntry =  iomb;
    curIbTrace->QNum = q;
    curIbTrace->pici = pici;
#ifdef OSLAYER_USE_HI_RES_TIMER
#ifdef SA_64BIT_TIMESTAMP
  curIbTrace->Time = ossaTimeStamp64(agNULL);
#else /* SA_64BIT_TIMESTAMP */
  curIbTrace->Time = ossaTimeStamp(agNULL);
#endif /* SA_64BIT_TIMESTAMP */
#else /* OSLAYER_USE_HI_RES_TIMER */
  curIbTrace->Time = 0;
#endif
    si_memcpy(curIbTrace->Iomb, iomb, MIN(numBytes, MPI_DEBUG_TRACE_IB_IOMB_SIZE));
    ibTrace->Idx++;
  }
  else
  {
    if(obTrace->Idx >= MPI_DEBUG_TRACE_BUFFER_MAX )
    {
      obTrace->Idx = 0;
    }
    curIdx = obTrace->Idx;
    curObTrace = &obTrace->Data[curIdx];
    curObTrace->pEntry =  iomb;
    curObTrace->QNum = q;
    curObTrace->pici = pici;
#ifdef OSLAYER_USE_HI_RES_TIMER
#ifdef SA_64BIT_TIMESTAMP
    curObTrace->Time = ossaTimeStamp64(agNULL);
#else /* SA_64BIT_TIMESTAMP */
    curObTrace->Time = ossaTimeStamp(agNULL);
#endif /* SA_64BIT_TIMESTAMP */
#else /* OSLAYER_USE_HI_RES_TIMER */
    curObTrace->Time = 0;
#endif
    si_memcpy(curObTrace->Iomb, iomb, MIN(numBytes, MPI_DEBUG_TRACE_OB_IOMB_SIZE));
    obTrace->Idx++;
  }


  return;
}

#endif /* MPI_DEBUG_TRACE_ENABLE */



#ifdef SA_ENABLE_TRACE_FUNCTIONS

/**
 * fiEnableTracing
 *
 *    This fucntion is called to initialize tracing of FC layer.
 *
 */
void siEnableTracing (agsaRoot_t  *agRoot)
{

  agsaLLRoot_t      *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaSwConfig_t    *swC  = &saRoot->swConfig;
  bit32 count;

  OS_ASSERT(saRoot != NULL, "");

  if( saRoot->TraceBlockReInit != 0)
  {
    return;
  }


  /* Initialize tracing first */

  for (count = 0; count < 10; count++)
  {
      saRoot->traceBuffLookup[count] = (bit8)('0' + count);
  }
  for (count = 0; count < 6; count++)
  {
      saRoot->traceBuffLookup[(bitptr)count + 10] = (bit8)('a' + count);
  }


  saRoot->TraceDestination = swC->TraceDestination;
  saRoot->TraceMask = swC->TraceMask;
  saRoot->CurrentTraceIndexWrapCount = 0;
  saRoot->CurrentTraceIndex = 0;
  saRoot->TraceBlockReInit = 1;


  SA_DBG1(("siEnableTracing: \n" ));

  SA_DBG1 (("      length       = %08x\n", saRoot->TraceBufferLength ));
  SA_DBG1 (("      virt         = %p\n",   saRoot->TraceBuffer ));
  SA_DBG1 (("    traceMask        = %08x @ %p\n", saRoot->TraceMask, &saRoot->TraceMask));
  SA_DBG1 (("    last trace entry @ %p\n", &saRoot->CurrentTraceIndex));
  SA_DBG1 (("    TraceWrapAround  = %x\n", saRoot->TraceMask & hpDBG_TraceBufferWrapAround ? 1 : 0));
  SA_DBG1 (("    da %p l %x\n",saRoot->TraceBuffer ,saRoot->TraceBufferLength));

#ifdef SA_PRINTOUT_IN_WINDBG
#ifndef DBG
  DbgPrint("siTraceEnable: \n" );

  DbgPrint("      length       = %08x\n", saRoot->TraceBufferLength );
  DbgPrint("      virt         = %p\n",   saRoot->TraceBuffer );
  DbgPrint("    last trace entry @ %p\n", &saRoot->CurrentTraceIndex);
  DbgPrint("    traceMask      = %08x @ %p\n", saRoot->TraceMask, &saRoot->TraceMask);
  DbgPrint("    da %p l %x\n",saRoot->TraceBuffer ,saRoot->TraceBufferLength);
#endif /* DBG  */
#endif /* SA_PRINTOUT_IN_WINDBG  */
  /*
  ** Init trace buffer with all spaces
  */
  for (count = 0; count < saRoot->TraceBufferLength; count++)
  {
      saRoot->TraceBuffer[count] = (bit8)' ';
  }

}



/**
 * IF_DO_TRACE
 *
 * PURPOSE:     convenience macro for the "to output or not to output" logic
 *
 * PARAMETERS:
 *
 * CALLS:
 *
 * SIDE EFFECTS & CAVEATS:
 *
 * ALGORITHM:
 *
 *
 *
 *     MODIFICATION HISTORY     ***********************
 *
 * ENGINEER NAME      DATE     DESCRIPTION
 * -------------    --------   -----------
 */

#define IF_DO_TRACE                                     \
  if ( (saRoot != NULL) &&                              \
       (saRoot->TraceDestination & siTraceDestMask) &&  \
       (mask & saRoot->TraceMask) )                     \


/* #define TRACE_ENTER_LOCK  ossaSingleThreadedEnter(agRoot, LL_TRACE_LOCK); */
/* #define TRACE_LEAVE_LOCK  ossaSingleThreadedLeave(agRoot, LL_TRACE_LOCK); */
#define TRACE_ENTER_LOCK
#define TRACE_LEAVE_LOCK
/**
 * BUFFER_WRAP_CHECK
 *
 * PURPOSE: Checks if the tracing buffer tracing index is too high.  If it is,
 *          the buffer index gets reset to 0 or tracing stops..
 */
#define BUFFER_WRAP_CHECK                                           \
    if( (saRoot->CurrentTraceIndex + TMP_TRACE_BUFF_SIZE)               \
                           >= saRoot->TraceBufferLength )               \
    {                                                                   \
        /* Trace wrap-Around is enabled.  */                            \
        if( saRoot->TraceMask & hpDBG_TraceBufferWrapAround )           \
        {                                                               \
            /* Fill the end of the buffer with spaces */                \
            for( i = saRoot->CurrentTraceIndex;                         \
                     i < saRoot->TraceBufferLength; i++ )               \
            {                                                           \
                saRoot->TraceBuffer[i] = (bit8)' ';                     \
            }                                                           \
            /* Wrap the current trace index back to 0.. */              \
            saRoot->CurrentTraceIndex = 0;                              \
            saRoot->CurrentTraceIndexWrapCount++;                       \
        }                                                               \
        else                                                            \
        {                                                               \
            /* Don't do anything -- trace buffer is filled up */        \
            return;                                                     \
        }                                                               \
    }

/**
 * LOCAL_OS_LOG_DEBUG_STRING
 *
 * PURPOSE:     protects against a change in the api for this function
 *
 * PARAMETERS:
 *
 * CALLS:
 *
 * SIDE EFFECTS & CAVEATS:
 *
 * ALGORITHM:
 *
 *
 *
 *     MODIFICATION HISTORY     ***********************
 *
 * ENGINEER NAME      DATE     DESCRIPTION
 * -------------    --------   -----------
 * Laurent Chavey   03/09/00   - changed cast of 3rd parameter to (char *)
 */
    #define LOCAL_OS_LOG_DEBUG_STRING(H,S)  \
            osLogDebugString(H,hpDBG_ALWAYS,(char *)(S))

/******************************************************************************
*******************************************************************************
**
** copyHex
**
** PURPOSE:  Copies a hex version of a bit32 into a bit8 buffer
**
*******************************************************************************
******************************************************************************/
#define copyHex(bit32Val, bitSize)                                     \
{                                                                      \
  bit32 nibbleLen = bitSize / 4;                                       \
  bit32 scratch = 0;                                                   \
  for( i = 0; i < nibbleLen; i++ )                                     \
  {                                                                    \
    bPtr[pos++] =                                                      \
        saRoot->traceBuffLookup[0xf & (bit32Val >> ((bitSize - 4) - (i << 2)))];  \
    i++;                                                               \
    bPtr[pos++] =                                                      \
    saRoot->traceBuffLookup[0xf & (bit32Val >> ((bitSize - 4) - (i << 2)))]; \
    /* Skip leading 0-s to save memory buffer space */                 \
    if( !scratch                                                       \
          && (bPtr[pos-2] == '0')                                      \
          && (bPtr[pos-1] == '0') )                                    \
    {                                                                  \
      pos -= 2;                                                        \
      continue;                                                        \
    }                                                                  \
    else                                                               \
    {                                                                  \
      scratch = 1;                                                     \
    }                                                                  \
  }                                                                    \
  if( scratch == 0 )                                                   \
  {                                                                    \
    /* The value is 0 and nothing got put in the buffer.  Do       */  \
    /* print at least two zeros.                                   */  \
    bPtr[pos++] = '0';                                                 \
    bPtr[pos++] = '0';                                                 \
  }                                                                    \
}


/**
 * TRACE_OTHER_DEST
 *
 * PURPOSE:  Check if any other destinations are enabled.  If yes, use them
 *           for debug log.
 */
#define TRACE_OTHER_DEST                                                \
    {                                                                   \
    bit32 bitptrscratch;                                                \
    if( saRoot->TraceDestination & smTraceDestDebugger )                \
    {                                                                   \
        bPtr[pos++] = (bit8)'\n';                                       \
        bPtr[pos++] = (bit8)0;                                          \
        LOCAL_OS_LOG_DEBUG_STRING(hpRoot, (char *)bPtr);                \
    }                                                                   \
    if( saRoot->TraceDestination & smTraceDestRegister )                \
    {                                                                   \
        while( (pos & 0x3) != 0x3 )                                     \
        {                                                               \
            bPtr[pos++] = (bit8)' ';                                    \
        }                                                               \
        bPtr[pos] = ' ';                                                \
        for( i = 0; i < pos; i = i + 4 )                                \
        {                                                               \
            bitptrscratch =  bPtr[i+0];                                 \
            bitptrscratch <<= 8;                                        \
            bitptrscratch |= bPtr[i+1];                                 \
            bitptrscratch <<= 8;                                        \
            bitptrscratch |= bPtr[i+2];                                 \
            bitptrscratch <<= 8;                                        \
            bitptrscratch |= bPtr[i+3];                                 \
            osChipRegWrite(hpRoot,                                      \
                 FC_rFMReceivedALPA, (bit32)bitptrscratch );            \
        }                                                               \
    }                                                                   \
    }



/**
 * siGetCurrentTraceIndex()
 *
 * PURPOSE:     Returns the current tracing index ( if tracing buffer is
 *              used ).
 *
 * PARAMETERS:
 *
 * CALLS:
 *
 * SIDE EFFECTS & CAVEATS:
 *
 * ALGORITHM:
 *
 *
 *     MODIFICATION HISTORY     ***********************
 *
 * ENGINEER NAME      DATE     DESCRIPTION
 * -------------    --------   -----------
 * Tom Nalepa       02/27/03
 *
 * @param hpRoot
 *
 * @return
 */
GLOBAL bit32 siGetCurrentTraceIndex(agsaRoot_t  *agRoot)
{
    agsaLLRoot_t      *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
    return(saRoot->CurrentTraceIndex);
}




/**
 * siResetTraceBuffer
 *
 * PURPOSE:     Sets saRoot->CurrentTraceIndex to 0.
 *
 * @param hpRoot
 *
 * @return
 */
GLOBAL void siResetTraceBuffer(agsaRoot_t  *agRoot)
{
    bit32 count;
    agsaLLRoot_t      *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
    saRoot->CurrentTraceIndex = 0;

    for ( count = 0; count < saRoot->TraceBufferLength; count++ )
    {
        saRoot->TraceBuffer[count] = (bit8)' ';
    }
}


/**
 * siTraceFuncEnter
 *
 * PURPOSE:     Format a function entry trace and post it to the appropriate
 *              destination.
 *
 * PARAMETERS:
 *
 * CALLS:
 *
 * SIDE EFFECTS & CAVEATS:
 *
 * ALGORITHM:
 *
 * siTraceFuncEnter  :    _[Xxxxx_
 *                 fileid---^  ^------funcid
 *
 *
 *     MODIFICATION HISTORY     ***********************
 *
 * ENGINEER NAME      DATE     DESCRIPTION
 * -------------    --------   -----------
 *
 * @param hpRoot
 * @param mask
 * @param fileid
 * @param funcid
 *
 * @return
 */

#define TMP_TRACE_BUFF_SIZE 32


GLOBAL void siTraceFuncEnter( agsaRoot_t  *agRoot,
                             bit32        mask,
                             bit32        fileid,
                             char       * funcid)
{
agsaLLRoot_t      *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
    bitptr         i;
    bit8           tmpB[TMP_TRACE_BUFF_SIZE];
    bit8          *bPtr;
    bit8           pos = 0;

    IF_DO_TRACE
    {
      TRACE_ENTER_LOCK
      if ( saRoot->TraceDestination & smTraceDestBuffer )
      {
        BUFFER_WRAP_CHECK
        bPtr = &saRoot->TraceBuffer[saRoot->CurrentTraceIndex];
      }
      else
      {
        bPtr = tmpB;
      }
      bPtr[pos++] = (bit8)'[';

#ifndef FC_DO_NOT_INCLUDE_FILE_NAME_TAGS_IN_ENTER_EXIT_TRACE
        bPtr[pos++] = (bit8)fileid;
#endif

        for ( i=0; i<4; i++ )
        {
            if ( funcid[i] == 0 )
            {
                break;
            }
            bPtr[pos++] = (bit8)funcid[i];
        }
        bPtr[pos++] = ' ';
        if ( saRoot->traceLineFeedCnt > FC_TRACE_LINE_SIZE )
        {
            bPtr[pos++] = '\r';
            bPtr[pos++] = '\n';
            saRoot->traceLineFeedCnt = 0;
        }
        saRoot->CurrentTraceIndex += pos;
//        TRACE_OTHER_DEST
    TRACE_LEAVE_LOCK

    }
    return;
}


/**
 * siTraceFuncExit
 *
 * PURPOSE:     Format a function exit trace and post it to the appropriate
 *              destination.
 *
 * PARAMETERS:
 *
 * CALLS:
 *
 * SIDE EFFECTS & CAVEATS:
 *
 * ALGORITHM:
 *
 * siTraceFuncExit         _Xxxxx]_
 *                 fileid---^  ^------funcid
 *
 *
 *     MODIFICATION HISTORY     ***********************
 *
 * ENGINEER NAME      DATE     DESCRIPTION
 * -------------    --------   -----------
 *
 * @param hpRoot
 * @param mask
 * @param fileid
 * @param funcid
 * @param exitId
 *
 * @return
 */
GLOBAL void siTraceFuncExit(   agsaRoot_t  *agRoot,  bit32   mask, char  fileid, char  * funcid, char  exitId )
{
    bitptr         i;
    bit8           tmpB[TMP_TRACE_BUFF_SIZE];
    bit8          *bPtr;
    bit8           pos = 0;

    agsaLLRoot_t      *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);

    IF_DO_TRACE
    {
      TRACE_ENTER_LOCK
      if ( saRoot->TraceDestination & smTraceDestBuffer )
      {
        BUFFER_WRAP_CHECK
        bPtr = &saRoot->TraceBuffer[saRoot->CurrentTraceIndex];
      }
      else
      {
        bPtr = tmpB;
      }

#ifndef FC_DO_NOT_INCLUDE_FILE_NAME_TAGS_IN_ENTER_EXIT_TRACE
        bPtr[pos++] = (bit8)fileid;
#endif

        for ( i=0; i<4; i++ )
        {
            if ( funcid[i] == 0 )
            {
                break;
            }
            bPtr[pos++] = (bit8)funcid[i];
        }
        bPtr[pos++] = (bit8)exitId;
        bPtr[pos++] = (bit8)']';
        bPtr[pos++] = (bit8)' ';
        if ( saRoot->traceLineFeedCnt > FC_TRACE_LINE_SIZE )
        {
            bPtr[pos++] = '\r';
            bPtr[pos++] = '\n';
            saRoot->traceLineFeedCnt = 0;
        }
        saRoot->CurrentTraceIndex += pos;
//        TRACE_OTHER_DEST
    TRACE_LEAVE_LOCK
    }
    return;
}

/**
 * siTraceListRemove
 *
 * PURPOSE:     Adds a trace tag for an exchange that is removed from a list
 *
 * PARAMETERS:
 *
 * CALLS:
 *
 * SIDE EFFECTS & CAVEATS:
 *
 * ALGORITHM:
 *
 *
 *     MODIFICATION HISTORY     ***********************
 *
 * ENGINEER NAME      DATE     DESCRIPTION
 * -------------    --------   -----------
 * Tom Nalepa       12/16/02   Initial Developmet
 *
 * @param hpRoot
 * @param mask
 * @param listId
 * @param exchangeId
 *
 * @return
 */
GLOBAL void siTraceListRemove(agsaRoot_t  *agRoot,
                              bit32        mask,
                              char         listId,
                              bitptr       exchangeId)
{
    agsaLLRoot_t      *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
    bitptr         i;
    bit8           tmpB[TMP_TRACE_BUFF_SIZE];
    bit8          *bPtr;
    bit8           pos = 0;

    IF_DO_TRACE
    {
     TRACE_ENTER_LOCK
        if ( saRoot->TraceDestination & smTraceDestBuffer )
        {
            BUFFER_WRAP_CHECK
            bPtr = &saRoot->TraceBuffer[saRoot->CurrentTraceIndex];
        }
        else
        {
            bPtr = tmpB;
        }
        bPtr[pos++] = (bit8)'<';
        bPtr[pos++] = (bit8)listId;
        copyHex(exchangeId, 32);
        bPtr[pos++] = (bit8)' ';
        if ( saRoot->traceLineFeedCnt > FC_TRACE_LINE_SIZE )
        {
            bPtr[pos++] = '\r';
            bPtr[pos++] = '\n';
            saRoot->traceLineFeedCnt = 0;
        }
        saRoot->CurrentTraceIndex += pos;
//        TRACE_OTHER_DEST
    TRACE_LEAVE_LOCK
    }
    return;
}

/**
 * siTraceListAdd
 *
 * PURPOSE:     Adds a trace tag for an exchange that is added to a list
 *
 * PARAMETERS:
 *
 * CALLS:
 *
 * SIDE EFFECTS & CAVEATS:
 *
 * ALGORITHM:
 *
 *
 *     MODIFICATION HISTORY     ***********************
 *
 * ENGINEER NAME      DATE     DESCRIPTION
 * -------------    --------   -----------
 * Tom Nalepa       12/16/02   Initial Developmet
 *
 * @param hpRoot
 * @param mask
 * @param listId
 * @param exchangeId
 *
 * @return
 */
GLOBAL void siTraceListAdd(agsaRoot_t      *agRoot,
                           bit32        mask,
                           char         listId,
                           bitptr       exchangeId)
{

  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);

    bitptr         i;
    bit8           tmpB[TMP_TRACE_BUFF_SIZE];
    bit8          *bPtr;
    bit8           pos = 0;

    IF_DO_TRACE
    {
        if ( saRoot->TraceDestination & smTraceDestBuffer )
        {
            BUFFER_WRAP_CHECK
            bPtr = &saRoot->TraceBuffer[saRoot->CurrentTraceIndex];
        }
        else
        {
            bPtr = tmpB;
        }
        bPtr[pos++] = (bit8)'>';
        bPtr[pos++] = (bit8)listId;
        copyHex(exchangeId, 32);
        bPtr[pos++] = (bit8)' ';
        if ( saRoot->traceLineFeedCnt > FC_TRACE_LINE_SIZE )
        {
            bPtr[pos++] = '\r';
            bPtr[pos++] = '\n';
            saRoot->traceLineFeedCnt = 0;
        }
        saRoot->CurrentTraceIndex += pos;
//        TRACE_OTHER_DEST
    }
    return;
}

/**
 * siTrace64
 *
 * PURPOSE:     Format a function parameter trace and post it to the appropriate
 *              destination.
 *
 * PARAMETERS:
 *
 * CALLS:
 *
 * SIDE EFFECTS & CAVEATS:
 *
 * ALGORITHM:
 *
 * siTrace : index is 0 for return value, 1 for first parm after "("
 *           produces:   _nn" XXXXXXXXXX
 *           index-----^    value--^
 *
 *
 *     MODIFICATION HISTORY     ***********************
 *
 * ENGINEER NAME      DATE     DESCRIPTION
 * -------------    --------   -----------
 *
 * @param hpRoot
 * @param mask
 * @param uId
 * @param value
 *
 * @return
 */
GLOBAL void siTrace64(agsaRoot_t      *agRoot,
                      bit32        mask,
                      char       * uId,
                      bit64        value,
                      bit32        dataSizeInBits)
{

    agsaLLRoot_t  *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
    bitptr         i;
    bit8           tmpB[TMP_TRACE_BUFF_SIZE];
    bit8          *bPtr;
    bit8           pos = 0;

    IF_DO_TRACE
    {
        if ( saRoot->TraceDestination & smTraceDestBuffer )
        {
            BUFFER_WRAP_CHECK
            bPtr = &saRoot->TraceBuffer[saRoot->CurrentTraceIndex];
        }
        else
        {
            bPtr = tmpB;
        }
        bPtr[pos++] = (bit8)'"';
        bPtr[pos++] = (bit8)uId[0];
        bPtr[pos++] = (bit8)uId[1];
        bPtr[pos++] = (bit8)':';
        copyHex(value, dataSizeInBits);
        bPtr[pos++] = (bit8)' ';
        if ( saRoot->traceLineFeedCnt > FC_TRACE_LINE_SIZE )
        {
            bPtr[pos++] = '\r';
            bPtr[pos++] = '\n';
            saRoot->traceLineFeedCnt = 0;
        }
        saRoot->CurrentTraceIndex += pos;
//        TRACE_OTHER_DEST
    }
    return;
}



/**
 * siTrace
 *
 * PURPOSE:     Format a function parameter trace and post it to the appropriate
 *              destination.
 *
 * PARAMETERS:
 *
 * CALLS:
 *
 * SIDE EFFECTS & CAVEATS:
 *
 * ALGORITHM:
 *
 * fiTrace : index is 0 for return value, 1 for first parm after "("
 *           produces:   _nn" XXXXXXXXXX
 *           index-----^    value--^
 *
 *
 *     MODIFICATION HISTORY     ***********************
 *
 * ENGINEER NAME      DATE     DESCRIPTION
 * -------------    --------   -----------
 *
 * @param hpRoot
 * @param mask
 * @param uId
 * @param value
 *
 * @return
 */
GLOBAL void siTrace( agsaRoot_t      *agRoot,
                    bit32        mask,
                    char       * uId,
                    bit32        value,
                    bit32        dataSizeInBits)
{

   agsaLLRoot_t   *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);

    bitptr         i;
    bit8           tmpB[TMP_TRACE_BUFF_SIZE];
    bit8          *bPtr;
    bit8           pos = 0;

    IF_DO_TRACE
    {
        if ( saRoot->TraceDestination & smTraceDestBuffer )
        {
            BUFFER_WRAP_CHECK
            bPtr = &saRoot->TraceBuffer[saRoot->CurrentTraceIndex];
        }
        else
        {
            bPtr = tmpB;
        }
        bPtr[pos++] = (bit8)'"';
        bPtr[pos++] = (bit8)uId[0];
        bPtr[pos++] = (bit8)uId[1];
        bPtr[pos++] = (bit8)':';
        copyHex(value, dataSizeInBits);
        bPtr[pos++] = (bit8)' ';
        if ( saRoot->traceLineFeedCnt > FC_TRACE_LINE_SIZE )
        {
            bPtr[pos++] = '\r';
            bPtr[pos++] = '\n';
            saRoot->traceLineFeedCnt = 0;
        }
        saRoot->CurrentTraceIndex += pos;
//        TRACE_OTHER_DEST
    }
    return;
}


/*Set Wrap 0 for Wrapping non zero stops when full  */


GLOBAL void siTraceGetInfo(agsaRoot_t  *agRoot, hpTraceBufferParms_t * pBParms)
{
    agsaLLRoot_t  *saRoot = (agsaLLRoot_t *)agRoot->sdkData;

    pBParms->TraceCompiled  =  TRUE;

    pBParms->TraceWrap                  = saRoot->TraceMask & 0x80000000;
    pBParms->CurrentTraceIndexWrapCount = saRoot->CurrentTraceIndexWrapCount;
    pBParms->BufferSize                 = saRoot->TraceBufferLength;
    pBParms->CurrentIndex               = saRoot->CurrentTraceIndex;
    pBParms->pTrace                     = saRoot->TraceBuffer;
    pBParms->pTraceIndexWrapCount       = &saRoot->CurrentTraceIndexWrapCount;
    pBParms->pTraceMask                 = &saRoot->TraceMask;
    pBParms->pCurrentTraceIndex         = &saRoot->CurrentTraceIndex;
}
/**/

GLOBAL void siTraceSetMask(agsaRoot_t  *agRoot, bit32 TraceMask  )
{
    agsaLLRoot_t  *saRoot = (agsaLLRoot_t *)agRoot->sdkData;
    saRoot->TraceMask = TraceMask;
}



#endif


