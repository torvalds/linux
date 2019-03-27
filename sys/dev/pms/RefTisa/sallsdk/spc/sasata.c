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
/*! \file sasata.c
 *  \brief The file implements the functions to SATA IO
 *
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
#define siTraceFileID 'M'
#endif

/******************************************************************************/
/*! \brief Start SATA command
 *
 *  Start SATA command
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param queueNum
 *  \param agIORequest
 *  \param agDevHandle
 *  \param agRequestType
 *  \param agSATAReq
 *  \param agTag
 *  \param agCB
 *
 *  \return If command is started successfully
 *          - \e AGSA_RC_SUCCESS command is started successfully
 *          - \e AGSA_RC_FAILURE command is not started successfully
 */
/*******************************************************************************/
GLOBAL bit32 saSATAStart(
  agsaRoot_t                  *agRoot,
  agsaIORequest_t             *agIORequest,
  bit32                       queueNum,
  agsaDevHandle_t             *agDevHandle,
  bit32                       agRequestType,
  agsaSATAInitiatorRequest_t  *agSATAReq,
  bit8                        agTag,
  ossaSATACompletedCB_t       agCB
  )

{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  mpiICQueue_t        *circularQ = agNULL;
  agsaDeviceDesc_t    *pDevice   = agNULL;
  agsaPort_t          *pPort     = agNULL;
  agsaIORequestDesc_t *pRequest  = agNULL;
  void                *pMessage  = agNULL;
  agsaSgl_t           *pSgl      = agNULL;
  bit32               *payload   = agNULL;
  bit32               deviceIndex = 0;
  bit32               ret = AGSA_RC_SUCCESS, retVal = 0;
  bit32               AtapDir = 0;
  bit32               encryptFlags = 0;
  bit16               size = 0;
  bit16               opCode = 0;
  bit8                inq = 0, outq = 0;

  OSSA_INP_ENTER(agRoot);
  smTraceFuncEnter(hpDBG_VERY_LOUD, "8a");

  SA_DBG3(("saSATAStart: in\n"));
  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "(saSATAStart) agRoot is NULL");
  SA_ASSERT((agNULL != agIORequest), "(saSATAStart) agIORequest is NULL");
  SA_ASSERT((agNULL != agDevHandle), "(saSATAStart) agDevHandle is NULL");
  SA_ASSERT((agNULL != agSATAReq), "(saSATAStart) agSATAReq is NULL");

  /* Assign inbound and outbound queue */
  inq = (bit8)(queueNum & MPI_IB_NUM_MASK);
  outq = (bit8)((queueNum & MPI_OB_NUM_MASK) >> MPI_OB_SHIFT);
  SA_ASSERT((AGSA_MAX_INBOUND_Q > inq), "The IBQ Number is out of range.");

  /* Find the outgoing port for the device */
  pDevice = (agsaDeviceDesc_t *) (agDevHandle->sdkData);
  SA_ASSERT((agNULL != pDevice), "(saSATAStart) pDevice is NULL");

  pPort = pDevice->pPort;
  SA_ASSERT((agNULL != pPort), "(saSATAStart) pPort is NULL");

  /* SATA DIF is obsolete */
  if (agSATAReq->option & AGSA_SATA_ENABLE_DIF)
  {
    return AGSA_RC_FAILURE;
  }

  /* find deviceID for IOMB */
  deviceIndex = pDevice->DeviceMapIndex;

  /*  Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));
  if ( agNULL != pRequest )
  {
    /* If free IOMB avaliable */
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));

    /* Add the request to the pendingSTARequests list of the device */
    pRequest->valid = agTRUE;
    saLlistIOAdd(&(pDevice->pendingIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    if ((agSATAReq->option & AGSA_SATA_ENABLE_ENCRYPTION) ||
          (agSATAReq->option & AGSA_SATA_ENABLE_DIF))
    {
        opCode = OPC_INB_SATA_DIF_ENC_OPSTART;
        size = IOMB_SIZE128;
    }
    else
    {
        opCode = OPC_INB_SATA_HOST_OPSTART;
        if (agRequestType == AGSA_SATA_PROTOCOL_NON_PKT ||
            agRequestType == AGSA_SATA_PROTOCOL_H2D_PKT ||
            agRequestType == AGSA_SATA_PROTOCOL_D2H_PKT)
            size = IOMB_SIZE128;
        else
            size = IOMB_SIZE64;
    }
    /* If LL IO request entry avaliable */
    /* set up pRequest */
    pRequest->pIORequestContext = agIORequest;
    pRequest->pDevice = pDevice;
    pRequest->pPort = pPort;
    pRequest->requestType = agRequestType;
    pRequest->startTick = saRoot->timeTick;
    pRequest->completionCB = (ossaSSPCompletedCB_t)agCB;
    /* Set request to the sdkData of agIORequest */
    agIORequest->sdkData = pRequest;

    /* save tag and IOrequest pointer to IOMap */
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;

#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

    /* get a free inbound queue entry */
    circularQ = &saRoot->inboundQueue[inq];
    retVal    = mpiMsgFreeGet(circularQ, size, &pMessage);

    if (AGSA_RC_FAILURE == retVal)
    {
#ifdef SA_LL_IBQ_PROTECT
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */
      /* if not sending return to free list rare */
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
      pRequest->valid = agFALSE;
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      SA_DBG3(("saSATAStart, error when get free IOMB\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "8a");
      ret = AGSA_RC_FAILURE;
      goto ext;
    }

    /* return busy if inbound queue is full */
    if (AGSA_RC_BUSY == retVal)
    {
#ifdef SA_LL_IBQ_PROTECT
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */
      /* if not sending return to free list rare */
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
      pRequest->valid = agFALSE;
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      SA_DBG1(("saSATAStart, no more IOMB\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "8a");
      ret = AGSA_RC_BUSY;
      goto ext;
    }

  }
  else   /* If no LL IO request entry available */
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saSATAStart, No request from free list\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "8a");
    ret = AGSA_RC_BUSY;
    goto ext;
  }

  payload = (bit32 *)pMessage;
  SA_DBG4(("saSATAStart: Payload offset 0x%X\n", (unsigned int)(payload - (bit32 *)pMessage)));


  switch ( agRequestType )
  {
  case AGSA_SATA_PROTOCOL_FPDMA_READ:
  case AGSA_SATA_PROTOCOL_FPDMA_WRITE:
  case AGSA_SATA_PROTOCOL_FPDMA_READ_M:
  case AGSA_SATA_PROTOCOL_FPDMA_WRITE_M:
    pSgl = &(agSATAReq->agSgl);
    AtapDir = agRequestType & (AGSA_DIR_MASK | AGSA_SATA_ATAP_MASK);
    if (agRequestType & AGSA_MSG)
    {
      /* set M bit */
      AtapDir |= AGSA_MSG_BIT;
    }
    break;
  case AGSA_SATA_PROTOCOL_DMA_READ:
  case AGSA_SATA_PROTOCOL_DMA_WRITE:
  case AGSA_SATA_PROTOCOL_DMA_READ_M:
  case AGSA_SATA_PROTOCOL_DMA_WRITE_M:
  case AGSA_SATA_PROTOCOL_PIO_READ_M:
  case AGSA_SATA_PROTOCOL_PIO_WRITE_M:
  case AGSA_SATA_PROTOCOL_PIO_READ:
  case AGSA_SATA_PROTOCOL_PIO_WRITE:
  case AGSA_SATA_PROTOCOL_H2D_PKT:
  case AGSA_SATA_PROTOCOL_D2H_PKT:
    agTag = 0; /* agTag not valid for these requests */
    pSgl = &(agSATAReq->agSgl);
    AtapDir = agRequestType & (AGSA_DIR_MASK | AGSA_SATA_ATAP_MASK);
    if (agRequestType & AGSA_MSG)
    {
      /* set M bit */
      AtapDir |= AGSA_MSG_BIT;
    }
    break;

  case AGSA_SATA_PROTOCOL_NON_DATA:
  case AGSA_SATA_PROTOCOL_NON_DATA_M:
  case AGSA_SATA_PROTOCOL_NON_PKT:
    agTag = 0; /* agTag not valid for these requests */
    AtapDir = agRequestType & (AGSA_DIR_MASK | AGSA_SATA_ATAP_MASK);
    if (agRequestType & AGSA_MSG)
    {
      /* set M bit */
      AtapDir |= AGSA_MSG_BIT;
    }
    break;

  case AGSA_SATA_PROTOCOL_SRST_ASSERT:
    agTag = 0; /* agTag not valid for these requests */
    AtapDir = AGSA_SATA_ATAP_SRST_ASSERT;
    break;

  case AGSA_SATA_PROTOCOL_SRST_DEASSERT:
    agTag = 0; /* agTag not valid for these requests */
    AtapDir = AGSA_SATA_ATAP_SRST_DEASSERT;
    break;

  case AGSA_SATA_PROTOCOL_DEV_RESET:
  case AGSA_SATA_PROTOCOL_DEV_RESET_M: /* TestBase */
    agTag = 0; /* agTag not valid for these requests */
    AtapDir = AGSA_SATA_ATAP_PKT_DEVRESET;
    if (agRequestType & AGSA_MSG)
    {
      /* set M bit */
      AtapDir |= AGSA_MSG_BIT; /* TestBase */
    }
    break;

  default:
    SA_DBG1(("saSATAStart: (Unknown agRequestType) 0x%X \n",agRequestType));
    SA_ASSERT((0), "saSATAStart: (Unknown agRequestType)");

    break;
  }

  if ((AGSA_SATA_PROTOCOL_SRST_ASSERT == agRequestType) ||
       (AGSA_SATA_PROTOCOL_SRST_DEASSERT == agRequestType) ||
       (AGSA_SATA_PROTOCOL_DEV_RESET == agRequestType))
  {

    SA_DBG3(("saSATAStart:AGSA_SATA_PROTOCOL_SRST_DEASSERT AGSA_SATA_PROTOCOL_SRST_ASSERT\n"));

    si_memset((void *)payload, 0, sizeof(agsaSATAStartCmd_t));
    /* build IOMB DW 1 */
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t, tag), pRequest->HTag);
    /* DWORD 2 */
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,deviceId ), deviceIndex);
    /* DWORD 3 */
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,dataLen ), 0 );
    /* DWORD 4 */
    OSSA_WRITE_LE_32(agRoot, 
                    payload, 
                    OSSA_OFFSET_OF(agsaSATAStartCmd_t,optNCQTagataProt ),
                    (((agSATAReq->option & SATA_FIS_MASK) << SHIFT24)    |
                    (agTag << SHIFT16)                                   |
                    AtapDir));

   si_memcpy((void *)(payload+4), (void *)&agSATAReq->fis.fisRegHostToDev, sizeof(agsaFisRegHostToDevice_t));
  }
  else
  {
    /* build IOMB DW 1 */
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t, tag), pRequest->HTag);
    /* DWORD 2 */
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,deviceId ), deviceIndex);
    /* DWORD 3 */
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,dataLen ),  agSATAReq->dataLength );

     /* Since we are writing the payload in order, check for any special modes now. */
    if (agSATAReq->option & AGSA_SATA_ENABLE_ENCRYPTION)
    {
        SA_ASSERT((opCode == OPC_INB_SATA_DIF_ENC_OPSTART), "opcode");
        SA_DBG4(("saSATAStart: 1 Payload offset 0x%X\n", (unsigned int)(payload - (bit32 *)pMessage)));
        AtapDir |= AGSA_ENCRYPT_BIT;
    }

    if (agSATAReq->option & AGSA_SATA_ENABLE_DIF)
    {
        SA_ASSERT((opCode == OPC_INB_SATA_DIF_ENC_OPSTART), "opcode");
        AtapDir |= AGSA_DIF_BIT;
    }
#ifdef CCBUILD_TEST_EPL
    if(agSATAReq->encrypt.enableEncryptionPerLA)
        AtapDir |= (1 << SHIFT4);        // enable EPL
#endif
    /* DWORD 4 */
    OSSA_WRITE_LE_32(agRoot, 
                    payload, 
                    OSSA_OFFSET_OF(agsaSATAStartCmd_t,optNCQTagataProt ),
                  (((agSATAReq->option & SATA_FIS_MASK) << SHIFT24) |
                    (agTag << SHIFT16)                              |
                    AtapDir));

    /* DWORD 5 6 7 8 9 */
    si_memcpy((void *)(payload+4), (void *)&agSATAReq->fis.fisRegHostToDev, sizeof(agsaFisRegHostToDevice_t));
    /* DWORD 10 reserved */
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,reserved1 ),  0 );

    /* DWORD 11 reserved */
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,reserved2 ),  0 );

    SA_DBG4(("saSATAStart: 2 Payload offset 0x%X\n", (unsigned int)(payload - (bit32 *)pMessage)));
  }
  if (agSATAReq->option & AGSA_SATA_ENABLE_ENCRYPTION)
  {
    /* Write 10 dwords of zeroes as payload, skipping all DIF fields */
    SA_DBG4(("saSATAStart: 2a Payload offset 0x%X\n", (unsigned int)(payload - (bit32 *)pMessage)));
    if (opCode == OPC_INB_SATA_DIF_ENC_OPSTART)
    {
      /* DW 11 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,Res_EPL_DESCL ),0 );
      /* DW 12 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,resSKIPBYTES ),0 );
       /* DW 13 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,Res_DPL_DESCL_NDPLR ),0 );
      /* DW 14 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,Res_EDPL_DESCH ),0 );
      /* DW 15 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,DIF_flags ),0 );
      /* DW 16 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,udt ),0 );
      /* DW 17 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,udtReplacementLo ),0 );
      /* DW 18 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,udtReplacementHi ),0 );
      /* DW 19 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,DIF_seed ),0 );
    }

    if (agSATAReq->option & AGSA_SATA_ENABLE_ENCRYPTION)
    {
        SA_ASSERT((opCode == OPC_INB_SATA_DIF_ENC_OPSTART), "opcode");

        SA_DBG4(("saSATAStart: 3 Payload offset 0x%X\n", (unsigned int)(payload - (bit32 *)pMessage)));
        /* Configure DWORD 20 */
        encryptFlags = 0;

        if (agSATAReq->encrypt.keyTagCheck == agTRUE)
        {
          encryptFlags |= AGSA_ENCRYPT_KEY_TAG_BIT;
        }

        if( agSATAReq->encrypt.cipherMode == agsaEncryptCipherModeXTS )
        {
          encryptFlags |= AGSA_ENCRYPT_XTS_Mode << SHIFT4;
        }

        encryptFlags |= agSATAReq->encrypt.dekInfo.dekTable << SHIFT2;

        encryptFlags |= (agSATAReq->encrypt.dekInfo.dekIndex & 0xFFFFFF) << SHIFT8;
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,encryptFlagsLo ),encryptFlags );

        /* Configure DWORD 21*/
        /* This information is available in the sectorSizeIndex */
        encryptFlags = agSATAReq->encrypt.sectorSizeIndex;
        /*
         * Set Region0 sectors count
         */
        if(agSATAReq->encrypt.enableEncryptionPerLA)
        {
            encryptFlags |= (agSATAReq->encrypt.EncryptionPerLRegion0SecCount << SHIFT16);
        }

        encryptFlags |= (agSATAReq->encrypt.kekIndex) << SHIFT5;
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,encryptFlagsHi ),encryptFlags );

        /* Configure DWORD 22*/
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,keyTagLo ),  agSATAReq->encrypt.keyTag_W0 );
        /* Configure DWORD 23 */
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,keyTagHi ),  agSATAReq->encrypt.keyTag_W1 );
        /* Configure DWORD 24 */
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,tweakVal_W0 ), agSATAReq->encrypt.tweakVal_W0  );
        /* Configure DWORD 25 */
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,tweakVal_W1 ), agSATAReq->encrypt.tweakVal_W1  );
        /* Configure DWORD 26 */
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,tweakVal_W2 ), agSATAReq->encrypt.tweakVal_W2  );
        /* Configure DWORD 27 */
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,tweakVal_W3 ), agSATAReq->encrypt.tweakVal_W3  );
    }
    else
    {
      /* Write 8 dwords of zeros as payload, skipping all encryption fields */
      if (opCode == OPC_INB_SATA_DIF_ENC_OPSTART)
      {
        /* Configure DWORD 22*/
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,keyTagLo ), 0 );
        /* Configure DWORD 23 */
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,keyTagHi ), 0 );
        /* Configure DWORD 24 */
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,tweakVal_W0 ), 0  );
        /* Configure DWORD 25 */
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,tweakVal_W1 ), 0  );
        /* Configure DWORD 26 */
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,tweakVal_W2 ), 0  );
        /* Configure DWORD 27 */
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,tweakVal_W3 ), 0  );
      }
    }

    SA_DBG4(("saSATAStart: 4 Payload offset 0x%X\n", (unsigned int)(payload - (bit32 *)pMessage)));

    /* DWORD 11 13 14*/
    if(agSATAReq->encrypt.enableEncryptionPerLA)
    {
      /* DWORD 11 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t, Res_EPL_DESCL),
                         agSATAReq->encrypt.EncryptionPerLAAddrLo);
      /* DWORD 13 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t, Res_DPL_DESCL_NDPLR), 0);
      /* DWORD 14 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t, Res_EDPL_DESCH),
                      agSATAReq->encrypt.EncryptionPerLAAddrHi);
    }
    else
    {
      /* DWORD 11 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t, Res_EPL_DESCL),0);
      /* DW 13 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t, Res_DPL_DESCL_NDPLR), 0);
      /* DWORD 14 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,Res_EDPL_DESCH ),0 );
    }

    /* Configure DWORD 28 for encryption*/
    if (pSgl)
    {
      /* Configure DWORD 28 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,AddrLow0 ),  pSgl->sgLower );
      /* Configure DWORD 29 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,AddrHi0 ), pSgl->sgUpper  );
      /* Configure DWORD 30 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,Len0 ),  pSgl->len );
      /* Configure DWORD 31 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,E0 ), pSgl->extReserved  );
    }
    else
    {
      /* Configure DWORD 28 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,AddrLow0 ),  0 );
      /* Configure DWORD 29 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,AddrHi0 ),  0 );
      /* Configure DWORD 30 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,Len0 ),  0 );
      /* Configure DWORD 31 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAEncryptStartCmd_t,E0 ),  0 );
    }

  }
  else
  {
    SA_ASSERT((opCode == OPC_INB_SATA_HOST_OPSTART), "opcode");
    if (pSgl)
    {
      /* Configure DWORD 12 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,AddrLow0 ),  pSgl->sgLower );
      /* Configure DWORD 13 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,AddrHi0 ), pSgl->sgUpper  );
      /* Configure DWORD 14 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,Len0 ),  pSgl->len );
      /* Configure DWORD 15 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,E0 ), pSgl->extReserved  );
    }
    else
    {
      /* Configure DWORD 12 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,AddrLow0 ),  0 );
      /* Configure DWORD 13 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,AddrHi0 ),  0 );
      /* Configure DWORD 14 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,Len0 ),  0 );
      /* Configure DWORD 15 */
      OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,E0 ),  0 );
    }
    /* support ATAPI packet command */
    if ((agRequestType == AGSA_SATA_PROTOCOL_NON_PKT ||
        agRequestType == AGSA_SATA_PROTOCOL_H2D_PKT ||
        agRequestType == AGSA_SATA_PROTOCOL_D2H_PKT))
     {
         /*DWORD 16 - 19 as SCSI CDB for support ATAPI Packet command*/
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,ATAPICDB ),
                        (bit32)(agSATAReq->scsiCDB[0]|(agSATAReq->scsiCDB[1]<<8)|(agSATAReq->scsiCDB[2]<<16)|(agSATAReq->scsiCDB[3]<<24)));
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,ATAPICDB )+ 4,
                        (bit32)(agSATAReq->scsiCDB[4]|(agSATAReq->scsiCDB[5]<<8)|(agSATAReq->scsiCDB[6]<<16)|(agSATAReq->scsiCDB[7]<<24)));
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,ATAPICDB )+ 8,
                        (bit32)(agSATAReq->scsiCDB[8]|(agSATAReq->scsiCDB[9]<<8)|(agSATAReq->scsiCDB[10]<<16)|(agSATAReq->scsiCDB[11]<<24)));
        OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAStartCmd_t,ATAPICDB )+ 12,
                        (bit32)(agSATAReq->scsiCDB[12]|(agSATAReq->scsiCDB[13]<<8)|(agSATAReq->scsiCDB[14]<<16)|(agSATAReq->scsiCDB[15]<<24)));
     }
  }

  /* send IOMB to SPC */
  ret = mpiMsgProduce(circularQ,
                      (void *)pMessage,
                      MPI_CATEGORY_SAS_SATA,
                      opCode,
                      outq,
                      (bit8)circularQ->priority);

#ifdef SA_LL_IBQ_PROTECT
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

#ifdef SALL_API_TEST
  if (AGSA_RC_FAILURE != ret)
  {
    saRoot->LLCounters.IOCounter.numSataStarted++;
  }
#endif

  smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "8a");

ext:
  OSSA_INP_LEAVE(agRoot);
  return ret;
}

/******************************************************************************/
/*! \brief Abort SATA command
 *
 *  Abort SATA command
 *
 *  \param agRoot      handles for this instance of SAS/SATA hardware
 *  \param queueNum    inbound/outbound queue number
 *  \param agIORequest the IO Request descriptor
 *  \param agIOtoBeAborted
 *
 *  \return If command is aborted successfully
 *          - \e AGSA_RC_SUCCESS command is aborted successfully
 *          - \e AGSA_RC_FAILURE command is not aborted successfully
 */
/*******************************************************************************/
GLOBAL bit32 saSATAAbort(
  agsaRoot_t      *agRoot,
  agsaIORequest_t *agIORequest,
  bit32           queueNum,
  agsaDevHandle_t *agDevHandle,
  bit32           flag,
  void            *abortParam,
  ossaGenericAbortCB_t   agCB
  )
{
  bit32 ret = AGSA_RC_SUCCESS, retVal;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaIORequestDesc_t *pRequestABT = agNULL;
  agsaDeviceDesc_t    *pDevice = agNULL;
  agsaDeviceDesc_t    *pDeviceABT = NULL;
  agsaPort_t          *pPort = agNULL;
  mpiICQueue_t        *circularQ;
  void                *pMessage;
  agsaSATAAbortCmd_t  *payload;
  agsaIORequest_t     *agIOToBeAborted;
  bit8                inq, outq;
  bit32               flag_copy = flag;


  smTraceFuncEnter(hpDBG_VERY_LOUD,"8b");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  SA_ASSERT((agNULL != agIORequest), "");

  SA_DBG3(("saSATAAbort: Aborting request %p ITtoBeAborted %p\n", agIORequest, abortParam));

  /* Assign inbound and outbound Ring Buffer */
  inq = (bit8)(queueNum & MPI_IB_NUM_MASK);
  outq = (bit8)((queueNum & MPI_OB_NUM_MASK) >> MPI_OB_SHIFT);
  SA_ASSERT((AGSA_MAX_INBOUND_Q > inq), "The IBQ Number is out of range.");

  if( ABORT_SINGLE == (flag & ABORT_MASK) )
  {
    agIOToBeAborted = (agsaIORequest_t *)abortParam;
    /* Get LL IORequest entry for saSATAAbort() */
    pRequest = (agsaIORequestDesc_t *) (agIOToBeAborted->sdkData);
    if (agNULL == pRequest)
    {
      /* no pRequest found - can not Abort */
      SA_DBG1(("saSATAAbort: pRequest AGSA_RC_FAILURE\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "8b");
      return AGSA_RC_FAILURE;
    }
    /* Find the device the request sent to */
    pDevice = pRequest->pDevice;
    /* Get LL IORequest entry */
    pRequestABT = (agsaIORequestDesc_t *) (agIOToBeAborted->sdkData);
    /* Find the device the request sent to */
    if (agNULL == pRequestABT)
    {
      /* no pRequestABT - can not find pDeviceABT */
      SA_DBG1(("saSATAAbort: pRequestABT AGSA_RC_FAILURE\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "8b");
      return AGSA_RC_FAILURE;
    }
    pDeviceABT = pRequestABT->pDevice;

    if (agNULL == pDeviceABT)
    {
      /* no deviceID - can not build IOMB */
      SA_DBG1(("saSATAAbort: pDeviceABT AGSA_RC_FAILURE\n"));

      smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "8b");
      return AGSA_RC_FAILURE;
    }

    if (agNULL != pDevice)
    {
      /* Find the port the request was sent to */
      pPort = pDevice->pPort;
    }

    /* Get request from free IORequests */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));
  }
  else
  {
    if (ABORT_ALL == (flag & ABORT_MASK))
    {
      /* abort all */
      /* Find the outgoing port for the device */
      pDevice = (agsaDeviceDesc_t *) (agDevHandle->sdkData);
      pPort = pDevice->pPort;
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));
    }
    else
    {
      /* only support 00 and 01 for flag */
      SA_DBG1(("saSATAAbort: flag AGSA_RC_FAILURE\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "8b");
      return AGSA_RC_FAILURE;
    }
  }

  /* If no LL IO request entry avalable */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saSATAAbort, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "8b");
    return AGSA_RC_BUSY;
  }

  /* If free IOMB avaliable */
  /* Remove the request from free list */
  saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));

  SA_ASSERT((!pRequest->valid), "The pRequest is in use");
  /* Add the request to the pendingIORequests list of the device */
  pRequest->valid = agTRUE;
  saLlistIOAdd(&(pDevice->pendingIORequests), &(pRequest->linkNode));
  /* set up pRequest */

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  pRequest->pIORequestContext = agIORequest;
  pRequest->requestType = AGSA_SATA_REQTYPE;
  pRequest->pDevice = pDevice;
  pRequest->pPort = pPort;
  pRequest->completionCB = (void*)agCB;
/* pRequest->abortCompletionCB = agCB; */
  pRequest->startTick = saRoot->timeTick;

  /* Set request to the sdkData of agIORequest */
  agIORequest->sdkData = pRequest;

  /* save tag and IOrequest pointer to IOMap */
  saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
  saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;

#ifdef SA_LL_IBQ_PROTECT
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

  /* If LL IO request entry avaliable */
  /* Get a free inbound queue entry */
  circularQ = &saRoot->inboundQueue[inq];
  retVal    = mpiMsgFreeGet(circularQ, IOMB_SIZE64, &pMessage);

  /* if message size is too large return failure */
  if (AGSA_RC_FAILURE == retVal)
  {
#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
    pRequest->valid = agFALSE;
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("saSATAAbort, error when get free IOMB\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "8b");
    return AGSA_RC_FAILURE;
  }

  /* return busy if inbound queue is full */
  if (AGSA_RC_BUSY == retVal)
  {
#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
    pRequest->valid = agFALSE;
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("saSATASAbort, no more IOMB\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'g', "8b");
    return AGSA_RC_BUSY;
  }


  /* setup payload */
  payload = (agsaSATAAbortCmd_t*)pMessage;
  OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAAbortCmd_t, tag), pRequest->HTag);

  if( ABORT_SINGLE == (flag & ABORT_MASK) )
  {
    /* If no device  */
    if ( agNULL == pDeviceABT )
    {
      #ifdef SA_LL_IBQ_PROTECT
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
      #endif /* SA_LL_IBQ_PROTECT */

      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
      pRequest->valid = agFALSE;
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      SA_DBG1(("saSATAAbort,no device\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'h', "8b");
      return AGSA_RC_FAILURE;
    }
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAAbortCmd_t, deviceId), pDeviceABT->DeviceMapIndex);
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAAbortCmd_t, HTagAbort), pRequestABT->HTag);
  }
  else
  {
    /* abort all */
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAAbortCmd_t, deviceId), pDevice->DeviceMapIndex);
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAAbortCmd_t, HTagAbort), 0);
  }

  if(flag & ABORT_TSDK_QUARANTINE)
  {
    if(smIS_SPCV(agRoot))
    {
      flag_copy &= ABORT_SCOPE;
      flag_copy |= ABORT_QUARANTINE_SPCV;
    }
  }
  OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSATAAbortCmd_t, abortAll), flag_copy);



  SA_DBG1(("saSATAAbort, HTag 0x%x HTagABT 0x%x deviceId 0x%x\n", payload->tag, payload->HTagAbort, payload->deviceId));

  /* post the IOMB to SPC */
  ret = mpiMsgProduce(circularQ, (void *)pMessage, MPI_CATEGORY_SAS_SATA, OPC_INB_SATA_ABORT, outq, (bit8)circularQ->priority);

#ifdef SA_LL_IBQ_PROTECT
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

#ifdef SALL_API_TEST
  if (AGSA_RC_FAILURE != ret)
  {
    saRoot->LLCounters.IOCounter.numSataAborted++;
  }
#endif

  siCountActiveIORequestsOnDevice( agRoot,   payload->deviceId );

  smTraceFuncExit(hpDBG_VERY_LOUD, 'i', "8b");

  return ret;
}

/******************************************************************************/
/*! \brief Routine to handle for received SATA with data payload event
 *
 *  The handle for received SATA with data payload event
 *
 *  \param agRoot       handles for this instance of SAS/SATA hardware
 *  \param pRequest     the IO request descriptor
 *  \param agFirstDword pointer to the first Dword
 *  \param pResp        pointer to the rest of SATA response
 *  \param lengthResp   total length of SATA Response frame
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void siEventSATAResponseWtDataRcvd(
  agsaRoot_t              *agRoot,
  agsaIORequestDesc_t     *pRequest,
  bit32                   *agFirstDword,
  bit32                   *pResp,
  bit32                   lengthResp
  )
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaDeviceDesc_t    *pDevice;
#if defined(SALLSDK_DEBUG)
  agsaFrameHandle_t   frameHandle;
  /* get frame handle */
  frameHandle = (agsaFrameHandle_t)(pResp);
#endif  /* SALLSDK_DEBUG */

  smTraceFuncEnter(hpDBG_VERY_LOUD,"8c");

  /* If the request is still valid */
  if ( agTRUE == pRequest->valid )
  {
    /* get device */
    pDevice = pRequest->pDevice;
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    /* Delete the request from the pendingIORequests */
    saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    (*(ossaSATACompletedCB_t)(pRequest->completionCB))(agRoot,
                                                       pRequest->pIORequestContext,
                                                       OSSA_IO_SUCCESS,
                                                       agFirstDword,
                                                       lengthResp,
                                                       (void *)pResp);

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "8c");

  return;
}

/******************************************************************************/
/*! \brief copy a SATA signature to another
 *
 *  copy a SATA signature to another
 *
 *  \param pDstSignature pointer to the destination signature
 *  \param pSrcSignature pointer to the source signature
 *
 *  \return If they match
 *          - \e agTRUE match
 *          - \e agFALSE  doesn't match
 */
/*******************************************************************************/
GLOBAL void siSATASignatureCpy(
  bit8  *pDstSignature,
  bit8  *pSrcSignature
  )
{
  bit32   i;

  for ( i = 0; i < 5; i ++ )
  {
    pDstSignature[i] = pSrcSignature[i];
  }

  return;
}



