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
/*! \file sautil.c
 *  \brief The file contains general helper routines.
 *
 *
 */
/******************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/RefTisa/sallsdk/spc/saglobal.h>
#ifdef SA_TESTBASE_EXTRA
#include <string.h>
#endif /*  SA_TESTBASE_EXTRA */


#ifdef SA_ENABLE_TRACE_FUNCTIONS
#ifdef siTraceFileID
#undef siTraceFileID
#endif
#define siTraceFileID 'S'
#endif

/******************************************************************************/
/*! \brief Check for Hex digit
 *
 *
 *  \param char value
 *
 *  \return -void-
 *
 */
/*******************************************************************************/
int siIsHexDigit(char a)
{
  return (  (((a) >= 'a') && ((a) <= 'z')) ||
            (((a) >= 'A') && ((a) <= 'Z')) ||
            (((a) >= '0') && ((a) <= '9')) ||
            ( (a) == '*'));
}

/******************************************************************************/
/*! \brief memcopy
 *
 *
 *  \param char value
 *
 *  \return -void-
 *
 */
/*******************************************************************************/
FORCEINLINE
void*
si_memcpy(void *dst,  void *src, bit32 count)
{
/*
  bit32 x;
  unsigned char *dst1 = (unsigned char *)dst;
  unsigned char *src1 = (unsigned char *)src;

  for (x=0; x < count; x++)
    dst1[x] = src1[x];

  return dst;
*/
 return memcpy(dst, src, count);
}


/******************************************************************************/
/*! \brief memset
 *
 *
 *  \param char value
 *
 *  \return -void-
 *
 */
/*******************************************************************************/
FORCEINLINE
void*
si_memset(void *s, int c, bit32 n)
{
/*
  bit32   i;
  char *dst = (char *)s;
  for (i=0; i < n; i++)
  {
    dst[i] = (char) c;
  }
  return (void *)(&dst[i-n]);
*/
  return memset(s, c, n);
}


/******************************************************************************/
/*! \brief siDumpActiveIORequests
 *
 *
 *  \param char value
 *
 *  \return -void-
 *
 */
/*******************************************************************************/
GLOBAL void
siDumpActiveIORequests(
  agsaRoot_t              *agRoot,
  bit32                   count)
{
  bit32                 j, num_found = 0;
  agsaIORequestDesc_t   *pRequestDesc = agNULL;
  agsaLLRoot_t          *saRoot = agNULL;
  bit32 i;
  mpiOCQueue_t          *circularQ;

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");


  saCountActiveIORequests(agRoot);
  // return;


  if(smIS_SPCV(agRoot))
  {
    bit32 sp1;
    sp1= ossaHwRegRead(agRoot,V_Scratchpad_1_Register );

    if(SCRATCH_PAD1_V_ERROR_STATE(sp1))
    {
      SA_DBG1(("siDumpActiveIORequests: SCRATCH_PAD1_V_ERROR_STAT 0x%x\n",sp1 ));
    }
    SA_DBG1(("siDumpActiveIORequests: SCRATCH_PAD0 value = 0x%x\n", ossaHwRegRead(agRoot, V_Scratchpad_0_Register)));
    SA_DBG1(("siDumpActiveIORequests: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegRead(agRoot, V_Scratchpad_1_Register)));
    SA_DBG1(("siDumpActiveIORequests: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegRead(agRoot, V_Scratchpad_2_Register)));
    SA_DBG1(("siDumpActiveIORequests: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegRead(agRoot, V_Scratchpad_3_Register)));
  }

  for ( i = 0; i < saRoot->QueueConfig.numOutboundQueues; i++ )
  {
    circularQ = &saRoot->outboundQueue[i];
    OSSA_READ_LE_32(circularQ->agRoot, &circularQ->producerIdx, circularQ->piPointer, 0);
    if(circularQ->producerIdx != circularQ->consumerIdx)
    {
      SA_DBG1(("siDumpActiveIORequests:OBQ%d PI 0x%03x CI 0x%03x\n", i,circularQ->producerIdx, circularQ->consumerIdx  ));
    }
  }

  pRequestDesc = (agsaIORequestDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->IORequestMem), 0);
  SA_DBG1(("siDumpActiveIORequests: Current Time: %d ticks (usecpertick=%d)\n",
    saRoot->timeTick, saRoot->usecsPerTick));

  for ( j = 0; j < count; j ++ )
  {
    pRequestDesc = (agsaIORequestDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->IORequestMem), j);

    if (pRequestDesc->valid == agTRUE)
    {
      num_found++;
      SA_DBG1(("siDumpActiveIORequests: IO #%4d: %p Tag=%03X  Type=%08X Device 0x%X Pending for %d seconds\n",
        j,
        pRequestDesc->pIORequestContext,
        pRequestDesc->HTag,
        pRequestDesc->requestType,
        pRequestDesc->pDevice ? pRequestDesc->pDevice->DeviceMapIndex : 0,
        ((saRoot->timeTick - pRequestDesc->startTick)*saRoot->usecsPerTick)/1000000 ));

    }
  }
  if(count)
  {
    SA_DBG1(("siDumpActiveIORequests: %d found active\n",num_found));
  }

}

/******************************************************************************/
/*! \brief saCountActiveIORequests
 *
 *
 *  \param char value
 *
 *  \return -void-
 *
 */
/*******************************************************************************/
GLOBAL void
siClearActiveIORequests(
  agsaRoot_t              *agRoot)
{
  bit32                 j;
  bit32                 num_found = 0;
  agsaIORequestDesc_t   *pRequestDesc = agNULL;
  agsaLLRoot_t          *saRoot = agNULL;

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");

  if(saRoot)
  {
    pRequestDesc = (agsaIORequestDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->IORequestMem), 0);

    for ( j = 0; j < saRoot->swConfig.maxActiveIOs; j++ )
    {
      pRequestDesc = (agsaIORequestDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->IORequestMem), j);

      if (pRequestDesc->valid == agTRUE)
      {
        num_found++;
        pRequestDesc->valid =  agFALSE;
      }
    }
    if(num_found)
    {
      SA_DBG1(("siClearActiveIORequests %d found active\n",num_found));
    }
  }
  else
  {
     SA_DBG1(("siClearActiveIORequests saroot NULL\n"));
  }

}

/******************************************************************************/
/*! \brief siCountActiveIORequestsOnDevice
 *   count all active IO's
 *
 *  \param char value
 *
 *  \return -void-
 *
 */
/*******************************************************************************/
GLOBAL void
siClearActiveIORequestsOnDevice(
  agsaRoot_t *agRoot,
  bit32      device )
{
  bit32                 j, num_found = 0;
  agsaIORequestDesc_t   *pRequestDesc = agNULL;
  agsaLLRoot_t          *saRoot = agNULL;

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");

  pRequestDesc = (agsaIORequestDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->IORequestMem), 0);

  for ( j = 0; j < saRoot->swConfig.maxActiveIOs; j++ )
  {
    pRequestDesc = (agsaIORequestDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->IORequestMem), j);

    if (pRequestDesc->valid == agTRUE)
    {
      if (pRequestDesc->pDevice)
      {
        if (pRequestDesc->pDevice->DeviceMapIndex == device)
        {
          num_found++;
          pRequestDesc->valid = agFALSE;
        }
      }
    }
  }
  if(num_found)
  {
    SA_DBG1(("siClearActiveIORequestsOnDevice 0x%x %d cleared\n",device,num_found));
  }

}



/******************************************************************************/
/*! \brief siCountActiveIORequestsOnDevice
 *   count all active IO's
 *
 *  \param char value
 *
 *  \return -void-
 *
 */
/*******************************************************************************/
GLOBAL void
siCountActiveIORequestsOnDevice(
  agsaRoot_t *agRoot,
  bit32      device )
{
  bit32                 j, num_found = 0;
  agsaIORequestDesc_t   *pRequestDesc = agNULL;
  agsaLLRoot_t          *saRoot = agNULL;

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");

  pRequestDesc = (agsaIORequestDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->IORequestMem), 0);

  for ( j = 0; j < saRoot->swConfig.maxActiveIOs; j++ )
  {
    pRequestDesc = (agsaIORequestDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->IORequestMem), j);

    if (pRequestDesc->valid == agTRUE)
    {
      if (pRequestDesc->pDevice)
      {
        if (pRequestDesc->pDevice->DeviceMapIndex == device)
        {
          num_found++;
          if(saRoot->ResetStartTick > pRequestDesc->startTick)
          {
            SA_DBG2(("siCountActiveIORequestsOnDevice: saRoot->ResetStartTick %d pRequestDesc->startTick %d\n",
                    saRoot->ResetStartTick, pRequestDesc->startTick));
          }
        }
      }
    }
  }
  if(num_found)
  {
    SA_DBG1(("siCountActiveIORequestsOnDevice 0x%x %d found active\n",device,num_found));
  }

}



/******************************************************************************/
/*! \brief saCountActiveIORequests
 *   count all active IO's
 *
 *  \param char value
 *
 *  \return -void-
 *
 */
/*******************************************************************************/
GLOBAL void
saCountActiveIORequests(
  agsaRoot_t              *agRoot)
{
  bit32                 j, num_found = 0;
  agsaIORequestDesc_t   *pRequestDesc = agNULL;
  agsaLLRoot_t          *saRoot = agNULL;

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  if( agRoot == agNULL)
  {
    return;
  }
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  if( saRoot == agNULL)
  {
    return;
  }
  pRequestDesc = (agsaIORequestDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->IORequestMem), 0);

  for ( j = 0; j < saRoot->swConfig.maxActiveIOs; j++ )
  {
    pRequestDesc = (agsaIORequestDesc_t *) AGSAMEM_ELEMENT_READ(&(saRoot->IORequestMem), j);

    if (pRequestDesc->valid == agTRUE)
    {
      num_found++;
      if(saRoot->ResetStartTick > pRequestDesc->startTick)
      {
        SA_DBG2(("saCountActiveIORequests: saRoot->ResetStartTick %d pRequestDesc->startTick %d\n",
                saRoot->ResetStartTick, pRequestDesc->startTick));
      }
    }
  }
  if(num_found)
  {
    SA_DBG1(("saCountActiveIORequests %d found active\n",num_found));
  }

}


GLOBAL bit32 smIsCfg_V_ANY( agsaRoot_t *agRoot)
{

  if(smIsCfg_V8008(agRoot) == 1) return 1;
  if(smIsCfg_V8009(agRoot) == 1) return 1;
  if(smIsCfg_V8018(agRoot) == 1) return 1;
  if(smIsCfg_V8019(agRoot) == 1) return 1;
  if(smIsCfg_V8088(agRoot) == 1) return 1;
  if(smIsCfg_V8089(agRoot) == 1) return 1;
  if(smIsCfg_V8070(agRoot) == 1) return 1;
  if(smIsCfg_V8071(agRoot) == 1) return 1;
  if(smIsCfg_V8072(agRoot) == 1) return 1;
  if(smIsCfg_V8073(agRoot) == 1) return 1;
  if(smIS_SPCV8074(agRoot) == 1) return 1;
  if(smIS_SPCV8075(agRoot) == 1) return 1;
  if(smIS_SPCV8076(agRoot) == 1) return 1;
  if(smIS_SPCV8077(agRoot) == 1) return 1;
  if(smIsCfg_V8025(agRoot) == 1) return 1;
  if(smIsCfg_V9015(agRoot) == 1) return 1;
  if(smIsCfg_V9060(agRoot) == 1) return 1;
  if(smIsCfg_V8006(agRoot) == 1) return 1;

  return 0;
}

GLOBAL bit32 smIS_SPC( agsaRoot_t *agRoot) 
{
  if(smIS_spc8001(agRoot)    == 1) return 1;
  if(smIS_spc8081(agRoot)    == 1) return 1;
  if(smIS_SFC_AS_SPC(agRoot) == 1) return 1;
  return 0;
}


GLOBAL bit32 smIS_HIL( agsaRoot_t *agRoot) /* or delray */ 
{
  if(smIS_spc8081(agRoot)  == 1) return 1;
  if(smIS_ADAP8088(agRoot) == 1) return 1;
  if(smIS_ADAP8089(agRoot) == 1) return 1;
  if(smIS_SPCV8074(agRoot) == 1) return 1;
  if(smIS_SPCV8075(agRoot) == 1) return 1;
  if(smIS_SPCV8076(agRoot) == 1) return 1;
  if(smIS_SPCV8077(agRoot) == 1) return 1;
  return 0;

}

GLOBAL bit32 smIS_SPC6V( agsaRoot_t *agRoot)
{
  if(smIS_SPCV8008(agRoot) == 1) return 1;
  if(smIS_SPCV8009(agRoot) == 1) return 1;
  if(smIS_SPCV8018(agRoot) == 1) return 1;
  if(smIS_SPCV8019(agRoot) == 1) return 1;
  if(smIS_ADAP8088(agRoot) == 1) return 1;
  if(smIS_ADAP8089(agRoot) == 1) return 1;
  return 0;
}

GLOBAL bit32 smIS_SPC12V( agsaRoot_t *agRoot) 
{
  if(smIS_SPCV8070(agRoot) == 1) return 1;
  if(smIS_SPCV8071(agRoot) == 1) return 1;
  if(smIS_SPCV8072(agRoot) == 1) return 1;
  if(smIS_SPCV8073(agRoot) == 1) return 1;
  if(smIS_SPCV8074(agRoot) == 1) return 1;
  if(smIS_SPCV8075(agRoot) == 1) return 1;
  if(smIS_SPCV8076(agRoot) == 1) return 1;
  if(smIS_SPCV8077(agRoot) == 1) return 1;
  if(smIS_SPCV9015(agRoot) == 1) return 1;
  if(smIS_SPCV9060(agRoot) == 1) return 1;
  if(smIS_SPCV8006(agRoot) == 1) return 1;
  return 0;
}

GLOBAL bit32 smIS_SPCV_2_IOP( agsaRoot_t *agRoot)
{
  if(smIS_SPCV8009(agRoot) == 1) return 1;
  if(smIS_SPCV8018(agRoot) == 1) return 1;
  if(smIS_SPCV8019(agRoot) == 1) return 1;
  if(smIS_SPCV8071(agRoot) == 1) return 1;
  if(smIS_SPCV8072(agRoot) == 1) return 1;
  if(smIS_SPCV8073(agRoot) == 1) return 1;
  if(smIS_SPCV8076(agRoot) == 1) return 1;
  if(smIS_SPCV8077(agRoot) == 1) return 1;
  if(smIS_ADAP8088(agRoot) == 1) return 1;
  if(smIS_ADAP8089(agRoot) == 1) return 1;
  if(smIS_SPCV8006(agRoot) == 1) return 1;
  return 0;
}

GLOBAL bit32 smIS_SPCV( agsaRoot_t *agRoot)
{
  if(smIS_SPC6V(agRoot)    == 1) return 1;
  if(smIS_SPC12V(agRoot)   == 1) return 1;
  if(smIS_SFC_AS_V(agRoot) == 1 ) return 1;
  return 0;
}

GLOBAL bit32 smIS_ENCRYPT( agsaRoot_t *agRoot)
{
  if(smIS_SPCV8009(agRoot) == 1) return 1;
  if(smIS_ADAP8088(agRoot) == 1) return 1;
  if(smIS_SPCV8019(agRoot) == 1) return 1;
  if(smIS_SPCV8071(agRoot) == 1) return 1;
  if(smIS_SPCV8073(agRoot) == 1) return 1;
  if(smIS_SPCV8077(agRoot) == 1) return 1;
  if(smIS_SPCV9015(agRoot) == 1) return 1;
  if(smIS_SPCV9060(agRoot) == 1) return 1;
  return 0;
}



#if defined(SALLSDK_DEBUG)

/******************************************************************************/
/*! \brief Routine print buffer
 *
 *
 *  \param debugLevel     verbosity level
 *  \param header         header to print
 *  \param buffer         buffer to print
 *  \param  length        length of buffer in bytes
 *
 *  \return -void-
 *
 */
/*******************************************************************************/
GLOBAL void siPrintBuffer(
  bit32                 debugLevel,
  siPrintType           type,
  char                  *header,
  void                  *a,
  bit32                 length
  )
{
  bit32 x, rem;
  bit8 *buffer = (bit8 *)a;
  bit32 *lPtr;
  bit8 temp[16];

  ossaLogDebugString(gLLDebugLevel, debugLevel, ("%s\n", header));

  if (type == SA_8)
  {
    for (x=0; x < length/16; x++)
    {
      ossaLogDebugString(gLLDebugLevel, debugLevel,
        ("%02x %02x %02x %02x %02x %02x %02x %02x - %02x %02x %02x %02x %02x %02x %02x %02x  == "
         "%c%c%c%c%c%c%c%c - %c%c%c%c%c%c%c%c\n",
        *(buffer),
        *(buffer+1),
        *(buffer+2),
        *(buffer+3),
        *(buffer+4),
        *(buffer+5),
        *(buffer+6),
        *(buffer+7),
        *(buffer+8),
        *(buffer+9),
        *(buffer+10),
        *(buffer+11),
        *(buffer+12),
        *(buffer+13),
        *(buffer+14),
        *(buffer+15),
        siIsHexDigit(*(buffer)) ? *(buffer) : ' ',
        siIsHexDigit(*(buffer+1)) ? *(buffer+1) : ' ',
        siIsHexDigit(*(buffer+2)) ? *(buffer+2) : ' ',
        siIsHexDigit(*(buffer+3)) ? *(buffer+3) : ' ',
        siIsHexDigit(*(buffer+4)) ? *(buffer+4) : ' ',
        siIsHexDigit(*(buffer+5)) ? *(buffer+5) : ' ',
        siIsHexDigit(*(buffer+6)) ? *(buffer+6) : ' ',
        siIsHexDigit(*(buffer+7)) ? *(buffer+7) : ' ',
        siIsHexDigit(*(buffer+8)) ? *(buffer+8) : ' ',
        siIsHexDigit(*(buffer+9)) ? *(buffer+9) : ' ',
        siIsHexDigit(*(buffer+10)) ? *(buffer+10) : ' ',
        siIsHexDigit(*(buffer+11)) ? *(buffer+11) : ' ',
        siIsHexDigit(*(buffer+12)) ? *(buffer+12) : ' ',
        siIsHexDigit(*(buffer+13)) ? *(buffer+13) : ' ',
        siIsHexDigit(*(buffer+14)) ? *(buffer+14) : ' ',
        siIsHexDigit(*(buffer+15)) ? *(buffer+15) : ' ')
        );

      buffer += 16;
    }

    rem = length%16;
    if (rem)
    {
      for (x = 0; x < 16; x++)
      {
        temp[x] = ' ';
      }

      for (x = 0; x < rem; x++)
      {
        temp[x] = *(buffer+x);
      }

      buffer = temp;

      ossaLogDebugString(gLLDebugLevel, debugLevel,
        ("%02x %02x %02x %02x %02x %02x %02x %02x - %02x %02x %02x %02x %02x %02x %02x %02x  == "
         "%c%c%c%c%c%c%c%c - %c%c%c%c%c%c%c%c\n",
        *(buffer),
        *(buffer+1),
        *(buffer+2),
        *(buffer+3),
        *(buffer+4),
        *(buffer+5),
        *(buffer+6),
        *(buffer+7),
        *(buffer+8),
        *(buffer+9),
        *(buffer+10),
        *(buffer+11),
        *(buffer+12),
        *(buffer+13),
        *(buffer+14),
        *(buffer+15),
        siIsHexDigit(*(buffer)) ? *(buffer) : ' ',
        siIsHexDigit(*(buffer+1)) ? *(buffer+1) : ' ',
        siIsHexDigit(*(buffer+2)) ? *(buffer+2) : ' ',
        siIsHexDigit(*(buffer+3)) ? *(buffer+3) : ' ',
        siIsHexDigit(*(buffer+4)) ? *(buffer+4) : ' ',
        siIsHexDigit(*(buffer+5)) ? *(buffer+5) : ' ',
        siIsHexDigit(*(buffer+6)) ? *(buffer+6) : ' ',
        siIsHexDigit(*(buffer+7)) ? *(buffer+7) : ' ',
        siIsHexDigit(*(buffer+8)) ? *(buffer+8) : ' ',
        siIsHexDigit(*(buffer+9)) ? *(buffer+9) : ' ',
        siIsHexDigit(*(buffer+10)) ? *(buffer+10) : ' ',
        siIsHexDigit(*(buffer+11)) ? *(buffer+11) : ' ',
        siIsHexDigit(*(buffer+12)) ? *(buffer+12) : ' ',
        siIsHexDigit(*(buffer+13)) ? *(buffer+13) : ' ',
        siIsHexDigit(*(buffer+14)) ? *(buffer+14) : ' ',
        siIsHexDigit(*(buffer+15)) ? *(buffer+15) : ' ')
        );
    }
  }
  else
  {
    bit32 *ltemp = (bit32 *)temp;
    lPtr = (bit32 *) a;

    for (x=0; x < length/4; x++)
    {
      ossaLogDebugString(gLLDebugLevel, debugLevel,
        ("%08x %08x %08x %08x\n",
        *(lPtr),
        *(lPtr+1),
        *(lPtr+2),
        *(lPtr+3))
        );

      lPtr += 4;
    }

    rem = length%4;
    if (rem)
    {
      for (x = 0; x < 4; x++)
      {
        ltemp[x] = 0;
      }

      for (x = 0; x < rem; x++)
      {
        ltemp[x] = lPtr[x];
      }

      lPtr = ltemp;

      ossaLogDebugString(gLLDebugLevel, debugLevel,
        ("%08x %08x %08x %08x\n",
        *(lPtr),
        *(lPtr+1),
        *(lPtr+2),
        *(lPtr+3))
        );
    }
  }

}



void sidump_hwConfig(agsaHwConfig_t  *hwConfig)
{
  SA_DBG2(("sidump_hwConfig:hwConfig->hwInterruptCoalescingTimer                             0x%x\n",hwConfig->hwInterruptCoalescingTimer                            ));
  SA_DBG2(("sidump_hwConfig:hwConfig->hwInterruptCoalescingControl                           0x%x\n",hwConfig->hwInterruptCoalescingControl                          ));
  SA_DBG2(("sidump_hwConfig:hwConfig->intReassertionOption                                   0x%x\n",hwConfig->intReassertionOption                                  ));
  SA_DBG2(("sidump_hwConfig:hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister0  0x%x\n",hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister0 ));
  SA_DBG2(("sidump_hwConfig:hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister1  0x%x\n",hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister1 ));
  SA_DBG2(("sidump_hwConfig:hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister2  0x%x\n",hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister2 ));
  SA_DBG2(("sidump_hwConfig:hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister3  0x%x\n",hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister3 ));
  SA_DBG2(("sidump_hwConfig:hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister4  0x%x\n",hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister4 ));
  SA_DBG2(("sidump_hwConfig:hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister5  0x%x\n",hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister5 ));
  SA_DBG2(("sidump_hwConfig:hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister6  0x%x\n",hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister6 ));
  SA_DBG2(("sidump_hwConfig:hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister7  0x%x\n",hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister7 ));
  SA_DBG2(("sidump_hwConfig:hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister8  0x%x\n",hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister8 ));
  SA_DBG2(("sidump_hwConfig:hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister9  0x%x\n",hwConfig->phyAnalogConfig.phyAnalogSetupRegisters->spaRegister9 ));
  SA_DBG2(("sidump_hwConfig:hwConfig->hwOption                                               0x%x\n",hwConfig->hwOption                                              ));
}

void sidump_swConfig(agsaSwConfig_t  *swConfig)
{
  SA_DBG2(("sidump_swConfig:swConfig->maxActiveIOs               0x%x\n",swConfig->maxActiveIOs              ));
  SA_DBG2(("sidump_swConfig:swConfig->numDevHandles              0x%x\n",swConfig->numDevHandles             ));
  SA_DBG2(("sidump_swConfig:swConfig->smpReqTimeout              0x%x\n",swConfig->smpReqTimeout             ));
  SA_DBG2(("sidump_swConfig:swConfig->numberOfEventRegClients    0x%x\n",swConfig->numberOfEventRegClients   ));
  SA_DBG2(("sidump_swConfig:swConfig->sizefEventLog1             0x%x\n",swConfig->sizefEventLog1            ));
  SA_DBG2(("sidump_swConfig:swConfig->sizefEventLog2             0x%x\n",swConfig->sizefEventLog2            ));
  SA_DBG2(("sidump_swConfig:swConfig->eventLog1Option            0x%x\n",swConfig->eventLog1Option           ));
  SA_DBG2(("sidump_swConfig:swConfig->eventLog2Option            0x%x\n",swConfig->eventLog2Option           ));
  SA_DBG2(("sidump_swConfig:swConfig->fatalErrorInterruptEnable  0x%x\n",swConfig->fatalErrorInterruptEnable ));
  SA_DBG2(("sidump_swConfig:swConfig->fatalErrorInterruptVector  0x%x\n",swConfig->fatalErrorInterruptVector ));
  SA_DBG2(("sidump_swConfig:swConfig->max_MSI_InterruptVectors   0x%x\n",swConfig->max_MSI_InterruptVectors  ));
  SA_DBG2(("sidump_swConfig:swConfig->max_MSIX_InterruptVectors  0x%x\n",swConfig->max_MSIX_InterruptVectors ));
  SA_DBG2(("sidump_swConfig:swConfig->legacyInt_X                0x%x\n",swConfig->legacyInt_X               ));
  SA_DBG2(("sidump_swConfig:swConfig->hostDirectAccessSupport    0x%x\n",swConfig->hostDirectAccessSupport   ));
  SA_DBG2(("sidump_swConfig:swConfig->hostDirectAccessMode       0x%x\n",swConfig->hostDirectAccessMode      ));
  SA_DBG2(("sidump_swConfig:swConfig->param1                     0x%x\n",swConfig->param1                    ));
  SA_DBG2(("sidump_swConfig:swConfig->param2                     0x%x\n",swConfig->param2                    ));
  SA_DBG2(("sidump_swConfig:swConfig->param3                     %p\n",swConfig->param3                    ));
  SA_DBG2(("sidump_swConfig:swConfig->param4                     %p\n",swConfig->param4                    ));

}


void sidump_Q_config( agsaQueueConfig_t         *queueConfig )
{
  bit32 x;

  SA_DBG2(("sidump_Q_config: queueConfig->generalEventQueue                0x%x\n", queueConfig->generalEventQueue                ));
  SA_DBG2(("sidump_Q_config: queueConfig->numInboundQueues                 0x%x\n", queueConfig->numInboundQueues                 ));
  SA_DBG2(("sidump_Q_config: queueConfig->numOutboundQueues                0x%x\n", queueConfig->numOutboundQueues                ));
  SA_DBG2(("sidump_Q_config: queueConfig->iqHighPriorityProcessingDepth    0x%x\n", queueConfig->iqHighPriorityProcessingDepth    ));
  SA_DBG2(("sidump_Q_config: queueConfig->iqNormalPriorityProcessingDepth  0x%x\n", queueConfig->iqNormalPriorityProcessingDepth  ));
  SA_DBG2(("sidump_Q_config: queueConfig->queueOption                      0x%x\n", queueConfig->queueOption                      ));
  SA_DBG2(("sidump_Q_config: queueConfig->tgtDeviceRemovedEventQueue       0x%x\n", queueConfig->tgtDeviceRemovedEventQueue       ));

  for(x=0;x < queueConfig->numInboundQueues;x++)
  {
    SA_DBG2(("sidump_Q_config: queueConfig->inboundQueues[%d].elementCount  0x%x\n",x,queueConfig->inboundQueues[x].elementCount  ));
    SA_DBG2(("sidump_Q_config: queueConfig->inboundQueues[%d].elementSize   0x%x\n",x,queueConfig->inboundQueues[x].elementSize   ));
  }

  for(x=0;x < queueConfig->numOutboundQueues;x++)
  {

    SA_DBG2(("sidump_Q_config: queueConfig->outboundQueues[%d].elementCount 0x%x\n",x,queueConfig->outboundQueues[x].elementCount ));
    SA_DBG2(("sidump_Q_config: queueConfig->outboundQueues[%d].elementSize  0x%x\n",x,queueConfig->outboundQueues[x].elementSize  ));
  }

}
#endif

#ifdef SALL_API_TEST
/******************************************************************************/
/*! \brief Get Performance IO counters
 *
 *  Start/Abort SAS/SATA discovery
 *
 *  \param agRoot         Handles for this instance of SAS/SATA hardware
 *  \param counters       bit map of the counters
 *  \param LLCountInfo    pointer to the LLCounters
 *
 *  \return
 *          - \e AGSA_RC_SUCCESS
 *
 */
/*******************************************************************************/
GLOBAL bit32 saGetLLCounters(
                      agsaRoot_t          *agRoot,
                      bit32               counters,
                      agsaLLCountInfo_t   *LLCountInfo
                      )
{
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  bit32 i;

  for (i = 0; i < LL_COUNTERS; i++)
  {
    if (counters & (1 << i))
      LLCountInfo->arrayIOCounter[i] = saRoot->LLCounters.arrayIOCounter[i];
  }

  return AGSA_RC_SUCCESS;
}

/******************************************************************************/
/*! \brief Function for target to remove stale initiator device handle
 *
 *  function is called to ask the LL layer to remove all LL layer and SPC firmware
 *  internal resources associated with a device handle
 *
 *  \param agRoot       Handles for this instance of SAS/SATA hardware
 *  \param counters     Bit map of the IO counters
 *
 *  \return
 *          - \e AGSA_RC_SUCCESS
 *
 */
/*******************************************************************************/
GLOBAL bit32 saResetLLCounters(
                      agsaRoot_t *agRoot,
                      bit32      counters
                      )
{
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  bit32 i;

  for (i = 0; i < LL_COUNTERS; i++)
  {
    if (counters & (1 << i))
      saRoot->LLCounters.arrayIOCounter[i] = 0;
  }

  return AGSA_RC_SUCCESS;
}
#endif

