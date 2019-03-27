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
/*! \file saframe.c
 *  \brief The file implements the functions to read frame content
 */


/******************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/RefTisa/sallsdk/spc/saglobal.h>
#ifdef SA_ENABLE_TRACE_FUNCTIONS
#ifdef siTraceFileID
#undef siTraceFileID
#endif
#define siTraceFileID 'D'
#endif

/******************************************************************************/
/*! \brief Read 32 bits from a frame
 *
 *  Read 32 bits from a frame
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param agFrame      The frame handler
 *  \param frameOffset  Offset in bytes from the beginning of valid frame bytes or IU
                        to the 32-bit value to read
 *
 *  \return The read value
 *
 */
/*******************************************************************************/
GLOBAL bit32 saFrameReadBit32(
  agsaRoot_t          *agRoot,
  agsaFrameHandle_t   agFrame,
  bit32               frameOffset
  )
{
  bit8                    *payloadAddr;
  bit32                   value = 0;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "zr");

  if ( agNULL != agFrame )
  {
    /* Find the address of the payload */
    payloadAddr = (bit8 *)(agFrame) + frameOffset;

    /* read one DW Data */
    value = *(bit32 *)payloadAddr;
  }


  /* (5) return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "zr");
  return value;
}

/******************************************************************************/
/*! \brief Read a block from a frame
 *
 *  Read a block from a frame
 *
 *  \param agRoot         Handles for this instance of SAS/SATA LLL
 *  \param agFrame        The frame handler
 *  \param frameOffset    The offset of the frame to start read
 *  \param frameBuffer    The pointer to the destination of data read from the frame
 *  \param frameBufLen    Number of bytes to read from the frame
 *
 *  \return -void-
 *
 */
/*******************************************************************************/
GLOBAL void saFrameReadBlock (
  agsaRoot_t          *agRoot,
  agsaFrameHandle_t   agFrame,
  bit32               frameOffset,
  void                *frameBuffer,
  bit32               frameBufLen
  )
{
  bit8                    *payloadAddr;
  bit32                   i;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "zi");

  /* Sanity check */
  SA_ASSERT(frameBufLen < 4096, "saFrameReadBlock read more than 4k");

  if ( agNULL != agFrame )
  {
    /* Find the address of the payload */
    payloadAddr = (bit8 *)(agFrame) + frameOffset;
    /* Copy the frame data to the destination frame buffer */
    for ( i = 0; i < frameBufLen; i ++ )
    {
      *(bit8 *)((bit8 *)frameBuffer + i) = *(bit8 *)(payloadAddr + i);
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "zi");
}

