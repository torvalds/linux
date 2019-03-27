/******************************************************************************
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

******************************************************************************/
/* $FreeBSD$ */
/******************************************************************************
This program is part of PMC-Sierra initiator/target device driver. 
The functions here are commonly used by different type of drivers that support
PMC-Sierra storage network initiator hardware. 
******************************************************************************/


MALLOC_DEFINE( M_PMC_MMAL, "agtiapi_MemAlloc malloc",
               "allocated from agtiapi_MemAlloc as simple malloc case" );


/*****************************************************************************
agtiapi_DelayMSec()

Purpose:
  Busy wait for number of mili-seconds
Parameters:
  U32 MiliSeconds (IN)  Number of mili-seconds to delay
Return:
Note:
*****************************************************************************/
STATIC void agtiapi_DelayMSec( U32 MiliSeconds )
{
  DELAY(MiliSeconds * 1000);  // DELAY takes in usecs
}

/******************************************************************************
agtiapi_typhAlloc()
Purpose:
  Preallocation handling
  Allocate DMA memory which will be divided among proper pointers in
   agtiapi_MemAlloc() later
Parameters:
  ag_card_info_t *thisCardInst (IN)
Return:
  AGTIAPI_SUCCESS - success
  AGTIAPI_FAIL    - fail
******************************************************************************/
STATIC agBOOLEAN agtiapi_typhAlloc( ag_card_info_t *thisCardInst )
{
  struct agtiapi_softc *pmsc = thisCardInst->pCard;
  int wait = 0;

  if( bus_dma_tag_create( bus_get_dma_tag(pmsc->my_dev), // parent
                          32,                          // alignment
                          0,                           // boundary
                          BUS_SPACE_MAXADDR,           // lowaddr
                          BUS_SPACE_MAXADDR,           // highaddr
                          NULL,                        // filter
                          NULL,                        // filterarg
                          pmsc->typhn,                 // maxsize (size)
                          1,                           // number of segments
                          pmsc->typhn,                 // maxsegsize
                          0,                           // flags
                          NULL,                        // lockfunc
                          NULL,                        // lockarg
                          &pmsc->typh_dmat ) ) {
    printf( "agtiapi_typhAlloc: Can't create no-cache mem tag\n" );
    return AGTIAPI_FAIL;
  }

  if( bus_dmamem_alloc( pmsc->typh_dmat,
                        &pmsc->typh_mem,
                        BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_NOCACHE,
                        &pmsc->typh_mapp ) ) {
    printf( "agtiapi_typhAlloc: Cannot allocate cache mem %d\n",
            pmsc->typhn );
    return AGTIAPI_FAIL;
  }

  if ( bus_dmamap_load( pmsc->typh_dmat,
                        pmsc->typh_mapp,
                        pmsc->typh_mem,
                        pmsc->typhn,
                        agtiapi_MemoryCB, // try reuse of CB for same goal
                        &pmsc->typh_busaddr,
                        0 ) || !pmsc->typh_busaddr ) {
    for( ; wait < 20; wait++ ) {
      if( pmsc->typh_busaddr ) break;
      DELAY( 50000 );
    }

    if( ! pmsc->typh_busaddr ) {
      printf( "agtiapi_typhAlloc: cache mem won't load %d\n",
              pmsc->typhn );
      return AGTIAPI_FAIL;
    }
  }

  pmsc->typhIdx = 0;
  pmsc->tyPhsIx = 0;

  return AGTIAPI_SUCCESS;
}


/******************************************************************************
agtiapi_InitResource()
Purpose:
  Mapping PCI memory space
  Allocate and initialize per card based resource
Parameters: 
  ag_card_info_t *pCardInfo (IN)  
Return:
  AGTIAPI_SUCCESS - success
  AGTIAPI_FAIL    - fail
Note:    
******************************************************************************/
STATIC agBOOLEAN agtiapi_InitResource( ag_card_info_t *thisCardInst )
{
  struct agtiapi_softc *pmsc = thisCardInst->pCard;
  device_t devx = thisCardInst->pPCIDev;

  //AGTIAPI_PRINTK( "agtiapi_InitResource: begin; pointer values %p / %p \n",
  //        devx, thisCardInst );
  // no IO mapped card implementation, we'll implement memory mapping

  if( agtiapi_typhAlloc( thisCardInst ) == AGTIAPI_FAIL ) {
    printf( "agtiapi_InitResource: failed call to agtiapi_typhAlloc \n" );
    return AGTIAPI_FAIL;
  }

  AGTIAPI_PRINTK( "agtiapi_InitResource: dma alloc MemSpan %p -- %p\n",
                  (void*) pmsc->typh_busaddr,
                  (void*) ( (U32_64)pmsc->typh_busaddr + pmsc->typhn ) );

  //  logical BARs for SPC:
  //    bar 0 and 1 - logical BAR0
  //    bar 2 and 3 - logical BAR1
  //    bar4 - logical BAR2
  //    bar5 - logical BAR3
  //    Skiping the assignments for bar 1 and bar 3 (making bar 0, 2 64-bit):
  U32 bar;
  U32 lBar = 0; // logicalBar
  for (bar = 0; bar < PCI_NUMBER_BARS; bar++) {
    if ((bar==1) || (bar==3))
      continue;
    thisCardInst->pciMemBaseRIDSpc[lBar] = PCIR_BAR(bar);
    thisCardInst->pciMemBaseRscSpc[lBar] =
      bus_alloc_resource_any( devx,
                              SYS_RES_MEMORY,
                              &(thisCardInst->pciMemBaseRIDSpc[lBar]),
                              RF_ACTIVE );
    AGTIAPI_PRINTK( "agtiapi_InitResource: bus_alloc_resource_any rtn %p \n",
                    thisCardInst->pciMemBaseRscSpc[lBar] );
    if ( thisCardInst->pciMemBaseRscSpc[lBar] != NULL ) {
      thisCardInst->pciMemVirtAddrSpc[lBar] =
        (caddr_t)rman_get_virtual(
          thisCardInst->pciMemBaseRscSpc[lBar] );
      thisCardInst->pciMemBaseSpc[lBar]  =
        bus_get_resource_start( devx, SYS_RES_MEMORY,
                                thisCardInst->pciMemBaseRIDSpc[lBar]);
      thisCardInst->pciMemSizeSpc[lBar]  =
        bus_get_resource_count( devx, SYS_RES_MEMORY,
                                thisCardInst->pciMemBaseRIDSpc[lBar] );
      AGTIAPI_PRINTK( "agtiapi_InitResource: PCI: bar %d, lBar %d "
                      "VirtAddr=%lx, len=%d\n", bar, lBar,
                      (long unsigned int)thisCardInst->pciMemVirtAddrSpc[lBar],
                      thisCardInst->pciMemSizeSpc[lBar] );
    }
    else {
      thisCardInst->pciMemVirtAddrSpc[lBar] = 0;
      thisCardInst->pciMemBaseSpc[lBar]  = 0;
      thisCardInst->pciMemSizeSpc[lBar]  = 0;
    }
    lBar++;
  }
  thisCardInst->pciMemVirtAddr = thisCardInst->pciMemVirtAddrSpc[0];
  thisCardInst->pciMemSize = thisCardInst->pciMemSizeSpc[0];
  thisCardInst->pciMemBase = thisCardInst->pciMemBaseSpc[0];

  // Allocate all TI data structure required resources.
  // tiLoLevelResource
  U32 numVal;
  ag_resource_info_t *pRscInfo;
  pRscInfo = &thisCardInst->tiRscInfo;
  pRscInfo->tiLoLevelResource.loLevelOption.pciFunctionNumber =
    pci_get_function( devx );

  struct timeval tv;
  tv.tv_sec  = 1;
  tv.tv_usec = 0;
  int ticksPerSec;
  ticksPerSec = tvtohz( &tv );
  int uSecPerTick = 1000000/USEC_PER_TICK;

  if (pRscInfo->tiLoLevelResource.loLevelMem.count != 0) {
    //AGTIAPI_INIT("agtiapi_InitResource: loLevelMem count = %d\n",
    // pRscInfo->tiLoLevelResource.loLevelMem.count);

    // adjust tick value to meet Linux requirement
    pRscInfo->tiLoLevelResource.loLevelOption.usecsPerTick = uSecPerTick;
    AGTIAPI_PRINTK( "agtiapi_InitResource: "
                    "pRscInfo->tiLoLevelResource.loLevelOption.usecsPerTick"
                    " 0x%x\n",
                    pRscInfo->tiLoLevelResource.loLevelOption.usecsPerTick );
    for( numVal = 0; numVal < pRscInfo->tiLoLevelResource.loLevelMem.count;
         numVal++ ) {
      if( pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].totalLength ==
          0 ) {
        AGTIAPI_PRINTK("agtiapi_InitResource: skip ZERO %d\n", numVal);
        continue;
      }

      // check for 64 bit alignment
      if ( pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].alignment <
           AGTIAPI_64BIT_ALIGN ) {
        AGTIAPI_PRINTK("agtiapi_InitResource: set ALIGN %d\n", numVal);
        pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].alignment =
          AGTIAPI_64BIT_ALIGN;
      }
      if( ((pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].type
            & (BIT(0) | BIT(1))) == TI_DMA_MEM)  ||
          ((pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].type
            & (BIT(0) | BIT(1))) == TI_CACHED_DMA_MEM)) {
        if ( thisCardInst->dmaIndex >=
             sizeof(thisCardInst->tiDmaMem) /
             sizeof(thisCardInst->tiDmaMem[0]) ) {
          AGTIAPI_PRINTK( "Invalid dmaIndex %d ERROR\n",
                          thisCardInst->dmaIndex );
          return AGTIAPI_FAIL;
        }
        thisCardInst->tiDmaMem[thisCardInst->dmaIndex].type =
#ifdef CACHED_DMA
          pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].type
          & (BIT(0) | BIT(1));
#else
        TI_DMA_MEM;
#endif
        if( agtiapi_MemAlloc( thisCardInst,
              &thisCardInst->tiDmaMem[thisCardInst->dmaIndex].dmaVirtAddr,
              &thisCardInst->tiDmaMem[thisCardInst->dmaIndex].dmaPhysAddr,
              &pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].virtPtr,
              &pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].
              physAddrUpper,
              &pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].
              physAddrLower,
              pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].totalLength,
              thisCardInst->tiDmaMem[thisCardInst->dmaIndex].type,
              pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].alignment)
            != AGTIAPI_SUCCESS ) {
          return AGTIAPI_FAIL;
        }
        thisCardInst->tiDmaMem[thisCardInst->dmaIndex].memSize =
          pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].totalLength;
        //AGTIAPI_INIT("agtiapi_InitResource: LoMem %d dmaIndex=%d  DMA virt"
        //             " %p, phys 0x%x, length %d align %d\n",
        //       numVal, pCardInfo->dmaIndex,
        //     pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].virtPtr,
        //   pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].physAddrLower,
        //     pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].totalLength,
        //     pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].alignment);
        thisCardInst->dmaIndex++;
      }
      else if ( (pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].type &
                 (BIT(0) | BIT(1))) == TI_CACHED_MEM) {
        if (thisCardInst->cacheIndex >=
            sizeof(thisCardInst->tiCachedMem) /
            sizeof(thisCardInst->tiCachedMem[0])) {
          AGTIAPI_PRINTK( "Invalid cacheIndex %d ERROR\n",
                  thisCardInst->cacheIndex );
          return AGTIAPI_FAIL;
        }
        if ( agtiapi_MemAlloc( thisCardInst,
               &thisCardInst->tiCachedMem[thisCardInst->cacheIndex],
               (vm_paddr_t *)agNULL,
               &pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].virtPtr,
               (U32 *)agNULL,
               (U32 *)agNULL,
               pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].totalLength,
               TI_CACHED_MEM,
               pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].alignment)
             != AGTIAPI_SUCCESS ) {
          return AGTIAPI_FAIL;
        }

        //AGTIAPI_INIT("agtiapi_InitResource: LoMem %d cacheIndex=%d CACHED "
        //      "vaddr %p / %p, length %d align %d\n",
        //      numVal, pCardInfo->cacheIndex,
        //      pCardInfo->tiCachedMem[pCardInfo->cacheIndex],
        //      pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].virtPtr,
        //      pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].totalLength,
        //      pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].alignment);

        thisCardInst->cacheIndex++;
      }
      else if ( ((pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].type
                  & (BIT(0) | BIT(1))) == TI_DMA_MEM_CHIP)) {
        // not expecting this case, print warning that should get attention
        printf( "RED ALARM: we need a BAR for TI_DMA_MEM_CHIP, ignoring!" );
      }
      else {
        printf( "agtiapi_InitResource: Unknown required memory type %d "
                "ERROR!\n",
                pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].type);
        return AGTIAPI_FAIL;
      }
    }
  }
  // end: TI data structure resources ...

  // begin: tiInitiatorResource
  if ( pmsc->flags & AGTIAPI_INITIATOR ) {
    if ( pRscInfo->tiInitiatorResource.initiatorMem.count != 0 ) {
      //AGTIAPI_INIT("agtiapi_InitResource: initiatorMem count = %d\n",
      //         pRscInfo->tiInitiatorResource.initiatorMem.count);
      numVal =
        (U32)( pRscInfo->tiInitiatorResource.initiatorOption.usecsPerTick
               / uSecPerTick );
      if( pRscInfo->tiInitiatorResource.initiatorOption.usecsPerTick
          % uSecPerTick > 0 )
        pRscInfo->tiInitiatorResource.initiatorOption.usecsPerTick =
          (numVal + 1) * uSecPerTick;
      else
        pRscInfo->tiInitiatorResource.initiatorOption.usecsPerTick =
          numVal * uSecPerTick;
      for ( numVal = 0;
            numVal < pRscInfo->tiInitiatorResource.initiatorMem.count;
            numVal++ ) {
        // check for 64 bit alignment
        if( pRscInfo->tiInitiatorResource.initiatorMem.tdCachedMem[numVal].
            alignment < AGTIAPI_64BIT_ALIGN ) {
          pRscInfo->tiInitiatorResource.initiatorMem.tdCachedMem[numVal].
            alignment = AGTIAPI_64BIT_ALIGN;
        }
        if( thisCardInst->cacheIndex >=
            sizeof( thisCardInst->tiCachedMem) /
            sizeof( thisCardInst->tiCachedMem[0])) {
          AGTIAPI_PRINTK( "Invalid cacheIndex %d ERROR\n",
                  thisCardInst->cacheIndex );
          return AGTIAPI_FAIL;
        }
        // initiator memory is cached, no check is needed
        if( agtiapi_MemAlloc( thisCardInst,
              (void *)&thisCardInst->tiCachedMem[thisCardInst->cacheIndex],
              (vm_paddr_t *)agNULL,
              &pRscInfo->tiInitiatorResource.initiatorMem.
              tdCachedMem[numVal].virtPtr,
              (U32 *)agNULL,
              (U32 *)agNULL,
              pRscInfo->tiInitiatorResource.initiatorMem.tdCachedMem[numVal].
              totalLength,
              TI_CACHED_MEM,
              pRscInfo->tiInitiatorResource.initiatorMem.tdCachedMem[numVal].
              alignment)
            != AGTIAPI_SUCCESS) {
          return AGTIAPI_FAIL;
        }
        // AGTIAPI_INIT("agtiapi_InitResource: IniMem %d cacheIndex=%d CACHED "
        //      "vaddr %p / %p, length %d align 0x%x\n",
        //      numVal,
        //      pCardInfo->cacheIndex,
        //      pCardInfo->tiCachedMem[pCardInfo->cacheIndex],
        //      pRscInfo->tiInitiatorResource.initiatorMem.tdCachedMem[numVal].
        //       virtPtr,
        //pRscInfo->tiInitiatorResource.initiatorMem.tdCachedMem[numVal].
        //       totalLength,
        // pRscInfo->tiInitiatorResource.initiatorMem.tdCachedMem[numVal].
        //       alignment);
        thisCardInst->cacheIndex++;
      }
    }
  }
  // end: tiInitiatorResource   

  // begin: tiTdSharedMem
  if (pRscInfo->tiSharedMem.tdSharedCachedMem1.totalLength != 0) {
    // check for 64 bit alignment
    if( pRscInfo->tiSharedMem.tdSharedCachedMem1.alignment < 
	AGTIAPI_64BIT_ALIGN ) {
      pRscInfo->tiSharedMem.tdSharedCachedMem1.alignment = AGTIAPI_64BIT_ALIGN;
    }
    if( (pRscInfo->tiSharedMem.tdSharedCachedMem1.type & (BIT(0) | BIT(1))) 
	== TI_DMA_MEM )	{ 
      if( thisCardInst->dmaIndex >=
	  sizeof(thisCardInst->tiDmaMem) / sizeof(thisCardInst->tiDmaMem[0]) ) {
	AGTIAPI_PRINTK( "Invalid dmaIndex %d ERROR\n", thisCardInst->dmaIndex);
	return AGTIAPI_FAIL;
      }
      if( agtiapi_MemAlloc( thisCardInst, (void *)&thisCardInst->
			    tiDmaMem[thisCardInst->dmaIndex].dmaVirtAddr,
			    &thisCardInst->tiDmaMem[thisCardInst->dmaIndex].
			    dmaPhysAddr,
			    &pRscInfo->tiSharedMem.tdSharedCachedMem1.virtPtr, 
			    &pRscInfo->tiSharedMem.tdSharedCachedMem1.
			    physAddrUpper, 
			    &pRscInfo->tiSharedMem.tdSharedCachedMem1.
			    physAddrLower, 
			    pRscInfo->tiSharedMem.tdSharedCachedMem1.
			    totalLength, 
			    TI_DMA_MEM,
			    pRscInfo->tiSharedMem.tdSharedCachedMem1.alignment)
	  != AGTIAPI_SUCCESS )
	return AGTIAPI_FAIL;

      thisCardInst->tiDmaMem[thisCardInst->dmaIndex].memSize = 
        pRscInfo->tiSharedMem.tdSharedCachedMem1.totalLength + 
        pRscInfo->tiSharedMem.tdSharedCachedMem1.alignment;
      //    printf( "agtiapi_InitResource: SharedMem DmaIndex=%d DMA "
      //            "virt %p / %p, phys 0x%x, align %d\n", 
      //            thisCardInst->dmaIndex,
      //            thisCardInst->tiDmaMem[thisCardInst->dmaIndex].dmaVirtAddr,
      //            pRscInfo->tiSharedMem.tdSharedCachedMem1.virtPtr, 
      //            pRscInfo->tiSharedMem.tdSharedCachedMem1.physAddrLower, 
      //            pRscInfo->tiSharedMem.tdSharedCachedMem1.alignment);
      thisCardInst->dmaIndex++;
    }
    else if( (pRscInfo->tiSharedMem.tdSharedCachedMem1.type &
	      (BIT(0) | BIT(1)))
	     == TI_CACHED_MEM )	{
      if( thisCardInst->cacheIndex >=
	  sizeof(thisCardInst->tiCachedMem) /
	  sizeof(thisCardInst->tiCachedMem[0]) ) {
	AGTIAPI_PRINTK( "Invalid cacheIndex %d ERROR\n", thisCardInst->cacheIndex);
	return AGTIAPI_FAIL;
      }
      if( agtiapi_MemAlloc( thisCardInst, (void *)&thisCardInst->
			    tiCachedMem[thisCardInst->cacheIndex],
			    (vm_paddr_t *)agNULL,
			    &pRscInfo->tiSharedMem.tdSharedCachedMem1.virtPtr, 
			    (U32 *)agNULL,
			    (U32 *)agNULL,
			    pRscInfo->
			    tiSharedMem.tdSharedCachedMem1.totalLength, 
			    TI_CACHED_MEM,
			    pRscInfo->tiSharedMem.tdSharedCachedMem1.alignment)
	  != AGTIAPI_SUCCESS )
	return AGTIAPI_FAIL;
      //    printf( "agtiapi_InitResource: SharedMem cacheIndex=%d CACHED "
      //                 "vaddr %p / %p, length %d align 0x%x\n",
      //                 thisCardInst->cacheIndex,
      //                 thisCardInst->tiCachedMem[thisCardInst->cacheIndex],
      //                 pRscInfo->tiSharedMem.tdSharedCachedMem1.virtPtr,
      //                 pRscInfo->tiSharedMem.tdSharedCachedMem1.totalLength,
      //                 pRscInfo->tiSharedMem.tdSharedCachedMem1.alignment);
      AGTIAPI_PRINTK( "agtiapi_InitResource: SharedMem cacheIndex=%d CACHED "
                      "vaddr %p / %p, length %d align 0x%x\n",
                      thisCardInst->cacheIndex,
                      thisCardInst->tiCachedMem[thisCardInst->cacheIndex],
                      pRscInfo->tiSharedMem.tdSharedCachedMem1.virtPtr,
                      pRscInfo->tiSharedMem.tdSharedCachedMem1.totalLength,
                      pRscInfo->tiSharedMem.tdSharedCachedMem1.alignment );
      thisCardInst->cacheIndex++;
    }
    else {
      AGTIAPI_PRINTK( "agtiapi_InitResource: "
                      "Unknown required memory type ERROR!\n" );
      return AGTIAPI_FAIL;
    }
  }
  // end: tiTdSharedMem
  DELAY( 200000 ); // or use AGTIAPI_INIT_MDELAY(200);
  return AGTIAPI_SUCCESS;
} // agtiapi_InitResource() ends here

/******************************************************************************
agtiapi_ScopeDMARes()
Purpose:
  Determine the amount of DMA (non-cache) memory resources which will be
  required for a card ( and necessarily allocated in agtiapi_InitResource() )
Parameters: 
  ag_card_info_t *thisCardInst (IN)  
Return:
  size of DMA memory which call to agtiapi_InitResource() will consume  
Note:
  this funcion mirrors the flow of agtiapi_InitResource()
  results are stored in agtiapi_softc fields
******************************************************************************/
STATIC int agtiapi_ScopeDMARes( ag_card_info_t *thisCardInst )
{
  struct agtiapi_softc *pmsc = thisCardInst->pCard;
  U32 lAllMem = 0; // total memory count; typhn
  U32 lTmpAlign, lTmpType, lTmpLen;

  // tiLoLevelResource
  U32 numVal;
  ag_resource_info_t *pRscInfo;
  pRscInfo = &thisCardInst->tiRscInfo;

  if (pRscInfo->tiLoLevelResource.loLevelMem.count != 0) {
    for( numVal = 0; numVal < pRscInfo->tiLoLevelResource.loLevelMem.count;
         numVal++ ) {
      if( pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].totalLength ==
          0 ) {
        printf( "agtiapi_ScopeDMARes: skip ZERO %d\n", numVal );
        continue;
      }
      // check for 64 bit alignment
      lTmpAlign = pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].alignment;
      if( lTmpAlign < AGTIAPI_64BIT_ALIGN ) {
        AGTIAPI_PRINTK("agtiapi_ScopeDMARes: set ALIGN %d\n", numVal);
        //pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].alignment =
        lTmpAlign = AGTIAPI_64BIT_ALIGN;
      }
      if( ((pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].type
            & (BIT(0) | BIT(1))) == TI_DMA_MEM)  ||
          ((pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].type
            & (BIT(0) | BIT(1))) == TI_CACHED_DMA_MEM)) {
        //thisCardInst->tiDmaMem[thisCardInst->dmaIndex].type =
        lTmpType =
#ifdef CACHED_DMA
          pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].type
          & (BIT(0) | BIT(1));
#else
        TI_DMA_MEM;
#endif
        if( lTmpType == TI_DMA_MEM ) {
          lTmpLen =
            pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].totalLength; 
          lAllMem += lTmpLen + lTmpAlign;
        }
        //printf( "agtiapi_ScopeDMARes: call 1 0x%x\n", lAllMem );
      }
      else if ( ( pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].type &
                  (BIT(0) | BIT(1)) ) == TI_CACHED_MEM ) {
        // these are not the droids we're looking for
        if( thisCardInst->cacheIndex >=
            sizeof(thisCardInst->tiCachedMem) /
            sizeof(thisCardInst->tiCachedMem[0]) ) {
          AGTIAPI_PRINTK( "agtiapi_ScopeDMARes: Invalid cacheIndex %d ERROR\n",
                          thisCardInst->cacheIndex );
          return lAllMem;
        }
      }
      else {
        printf( "agtiapi_ScopeDMARes: Unknown required memory type %d "
                "ERROR!\n",
                pRscInfo->tiLoLevelResource.loLevelMem.mem[numVal].type );
        return lAllMem;
      }
    }
  }
  // end: TI data structure resources ...

  // nothing for tiInitiatorResource

  // begin: tiTdSharedMem
  if (pRscInfo->tiSharedMem.tdSharedCachedMem1.totalLength != 0) {
    // check for 64 bit alignment
    lTmpAlign = pRscInfo->tiSharedMem.tdSharedCachedMem1.alignment;
    if( lTmpAlign < AGTIAPI_64BIT_ALIGN ) {
      //pRscInfo->tiSharedMem.tdSharedCachedMem1.alignment=AGTIAPI_64BIT_ALIGN;
       lTmpAlign = AGTIAPI_64BIT_ALIGN;
    }
    if( (pRscInfo->tiSharedMem.tdSharedCachedMem1.type & (BIT(0) | BIT(1))) 
        == TI_DMA_MEM )	{ 
      lTmpLen = pRscInfo->tiSharedMem.tdSharedCachedMem1.totalLength;
      lAllMem += lTmpLen + lTmpAlign;
      // printf( "agtiapi_ScopeDMARes: call 4D 0x%x\n", lAllMem );
    }
    else if( (pRscInfo->tiSharedMem.tdSharedCachedMem1.type &
              (BIT(0) | BIT(1)))
             != TI_CACHED_MEM )	{
      printf( "agtiapi_ScopeDMARes: Unknown required memory type ERROR!\n" );
    }
  }
  // end: tiTdSharedMem

  pmsc->typhn = lAllMem;
  return lAllMem;

} // agtiapi_ScopeDMARes() ends here


STATIC void agtiapi_ReleasePCIMem( ag_card_info_t *pCardInfo ) {
  U32 bar = 0;
  int tmpRid = 0;
  struct resource *tmpRsc = NULL; 
  device_t dev;
  dev = pCardInfo->pPCIDev;

  for (bar=0; bar  < PCI_NUMBER_BARS; bar++) {  // clean up PCI resource
    tmpRid = pCardInfo->pciMemBaseRIDSpc[bar];
    tmpRsc = pCardInfo->pciMemBaseRscSpc[bar];
    if (tmpRsc != NULL) {   // Release PCI resources
      bus_release_resource( dev, SYS_RES_MEMORY, tmpRid, tmpRsc );
    }
  }
  return;
}


/******************************************************************************
agtiapi_MemAlloc()
Purpose:
  Handle various memory allocation requests.
Parameters: 
  ag_card_info_t *pCardInfo (IN)  Pointer to card info structure
  void **VirtAlloc (OUT)          Allocated memory virtual address  
  dma_addr_t *pDmaAddr (OUT)      Allocated dma memory physical address  
  void **VirtAddr (OUT)           Aligned memory virtual address  
  U32 *pPhysAddrUp (OUT)          Allocated memory physical upper 32 bits  
  U32 *pPhysAddrLow (OUT)         Allocated memory physical lower 32 bits  
  U32 MemSize (IN)                Allocated memory size
  U32 Type (IN)                   Type of memory required
  U32 Align (IN)                  Required memory alignment
Return:
  AGTIAPI_SUCCESS - success
  AGTIAPI_FAIL    - fail
******************************************************************************/
STATIC agBOOLEAN agtiapi_MemAlloc( ag_card_info_t *thisCardInst,
                                   void       **VirtAlloc,
                                   vm_paddr_t  *pDmaAddr,
                                   void       **VirtAddr,
                                   U32         *pPhysAddrUp,
                                   U32         *pPhysAddrLow,
                                   U32          MemSize,
                                   U32          Type,
                                   U32          Align )
{
  U32_64  alignOffset = 0;
  if( Align )
    alignOffset = Align - 1;

// printf( "agtiapi_MemAlloc: debug find mem TYPE, %d vs. CACHE %d, DMA %d \n",
//          ( Type & ( BIT(0) | BIT(1) ) ), TI_CACHED_MEM, TI_DMA_MEM );

  if ((Type & (BIT(0) | BIT(1))) == TI_CACHED_MEM) {
    *VirtAlloc = malloc( MemSize + Align, M_PMC_MMAL, M_ZERO | M_NOWAIT );
    *VirtAddr  = (void *)(((U32_64)*VirtAlloc + alignOffset) & ~alignOffset);
  }
  else {
    struct agtiapi_softc *pmsc = thisCardInst->pCard; // get card reference
    U32 residAlign = 0;
    // find virt index value
    *VirtAlloc = (void*)( (U64)pmsc->typh_mem + pmsc->typhIdx );
    *VirtAddr = (void *)( ( (U32_64)*VirtAlloc + alignOffset) & ~alignOffset );
    if( *VirtAddr != *VirtAlloc )
      residAlign = (U64)*VirtAddr - (U64)*VirtAlloc; // find alignment needed
    pmsc->typhIdx += residAlign + MemSize; // update index
    residAlign = 0; // reset variable for reuse
    // find phys index val
    pDmaAddr = (vm_paddr_t*)( (U64)pmsc->typh_busaddr + pmsc->tyPhsIx );
    vm_paddr_t *lPhysAligned =
      (vm_paddr_t*)( ( (U64)pDmaAddr + alignOffset ) & ~alignOffset );
    if( lPhysAligned != pDmaAddr )
      residAlign = (U64)lPhysAligned - (U64)pDmaAddr; // find alignment needed
    pmsc->tyPhsIx += residAlign + MemSize;  // update index
    *pPhysAddrUp  = HIGH_32_BITS( (U64)lPhysAligned );
    *pPhysAddrLow = LOW_32_BITS( (U64)lPhysAligned );
    //printf( "agtiapi_MemAlloc: physIx 0x%x size 0x%x resid:0x%x "
    //        "addr:0x%p addrAligned:0x%p Align:0x%x\n",
    //        pmsc->tyPhsIx, MemSize, residAlign, pDmaAddr, lPhysAligned,
    //        Align );
  }
  if ( !*VirtAlloc ) {
    AGTIAPI_PRINTK( "agtiapi_MemAlloc memory allocation ERROR x%x\n",
                    Type & (U32)(BIT(0) | BIT(1)));
    return AGTIAPI_FAIL;
  }
  return AGTIAPI_SUCCESS;
}


/******************************************************************************
agtiapi_MemFree()

Purpose:
  Free agtiapi_MemAlloc() allocated memory
Parameters: 
  ag_card_info_t *pCardInfo (IN)  Pointer to card info structure
Return: none
******************************************************************************/
STATIC void agtiapi_MemFree( ag_card_info_t *pCardInfo )
{
  U32 idx;

  // release memory vs. alloc in agtiapi_MemAlloc; cached case
  for( idx = 0; idx < pCardInfo->cacheIndex; idx++ ) {
    if( pCardInfo->tiCachedMem[idx] ) {
      free( pCardInfo->tiCachedMem[idx], M_PMC_MMAL );
      AGTIAPI_PRINTK( "agtiapi_MemFree: TI_CACHED_MEM Mem[%d] %p\n",
              idx, pCardInfo->tiCachedMem[idx] );
    }
  }

  // release memory vs. alloc in agtiapi_typhAlloc; used in agtiapi_MemAlloc
  struct agtiapi_softc *pmsc = pCardInfo->pCard; // get card reference
  if( pmsc->typh_busaddr != 0 ) {
    bus_dmamap_unload( pmsc->typh_dmat, pmsc->typh_mapp );
  }
  if( pmsc->typh_mem != NULL )  {
    bus_dmamem_free( pmsc->typh_dmat, pmsc->typh_mem, pmsc->typh_mapp );
  }
  if( pmsc->typh_dmat != NULL ) {
    bus_dma_tag_destroy( pmsc->typh_dmat );
  }
//reference values:
//  pCardInfo->dmaIndex
//  pCardInfo->tiDmaMem[idx].dmaVirtAddr
//  pCardInfo->tiDmaMem[idx].memSize
//  pCardInfo->tiDmaMem[idx].type == TI_CACHED_DMA_MEM
//  pCardInfo->tiDmaMem[idx].type == TI_DMA_MEM

/* This code is redundant.  Commenting out for now to maintain a placekeeper.
   Free actually takes place in agtiapi_ReleaseHBA as calls on osti_dmat. dm
  // release possible lower layer dynamic memory
  for( idx = 0; idx < AGTIAPI_DYNAMIC_MAX; idx++ ) {
    if( pCardInfo->dynamicMem[idx].dmaVirtAddr != NULL ) {
      printf( "agtiapi_MemFree: dynMem[%d] virtAddr"
	            " %p / %lx size: %d\n",
              idx, pCardInfo->dynamicMem[idx].dmaVirtAddr,
              (long unsigned int)pCardInfo->dynamicMem[idx].dmaPhysAddr,
              pCardInfo->dynamicMem[idx].memSize );
      if( pCardInfo->dynamicMem[idx].dmaPhysAddr )
	      some form of free call would go here  (
                    pCardInfo->dynamicMem[idx].dmaVirtAddr,
                    pCardInfo->dynamicMem[idx].memSize, ... );
      else
        free case for cacheable memory would go here
    }
  }
*/
  return;
}

/******************************************************************************
agtiapi_ProbeCard()
Purpose:
  sets thisCardInst->cardIdIndex to structure variant consistent with card.
  ag_card_type[idx].vendorId we already determined is PCI_VENDOR_ID_PMC_SIERRA.
Parameters:
  device_t dev,
  ag_card_info_t *thisCardInst,
  int thisCard
Return:
  0 - success
  other values are not as good
Note:
 This implementation is tailored to FreeBSD in alignment with the probe
 functionality of the FreeBSD environment.
******************************************************************************/
STATIC int agtiapi_ProbeCard( device_t dev,
			      ag_card_info_t *thisCardInst,
			      int thisCard )
{
  int idx;
  u_int16_t agtiapi_vendor; // PCI vendor ID
  u_int16_t agtiapi_dev; // PCI device ID
  AGTIAPI_PRINTK("agtiapi_ProbeCard: start\n");

  agtiapi_vendor = pci_get_vendor( dev ); // get PCI vendor ID
  agtiapi_dev = pci_get_device( dev ); // get PCI device ID
  for( idx = 0; idx < COUNT(ag_card_type); idx++ ) 
  {
    if ( ag_card_type[idx].deviceId == agtiapi_dev &&
	  ag_card_type[idx].vendorId == agtiapi_vendor) 
    { // device ID match
      memset( (void *)&agCardInfoList[ thisCard ], 0,
              sizeof(ag_card_info_t) );
      thisCardInst->cardIdIndex = idx;
      thisCardInst->pPCIDev = dev;
      thisCardInst->cardNameIndex = ag_card_type[idx].cardNameIndex;
      thisCardInst->cardID =
        pci_read_config( dev, ag_card_type[idx].membar, 4 ); // memAddr
      AGTIAPI_PRINTK("agtiapi_ProbeCard: We've got PMC SAS, probe successful %p / %p\n",
              thisCardInst->pPCIDev, thisCardInst );
      device_set_desc( dev, ag_card_names[ag_card_type[idx].cardNameIndex] );
      return 0;
    }
  }
  return 1;
}

