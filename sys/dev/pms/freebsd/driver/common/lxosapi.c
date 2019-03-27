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

*******************************************************************************/


MALLOC_DEFINE( M_PMC_OSTI, "osti_cacheable", "allocated from ostiAllocMemory as cacheable memory" );


/******************************************************************************
ostiAllocMemory()
Purpose:
  TD layer calls to get dma memory
Parameters: 
  tiRoot_t *ptiRoot (IN)            Pointer refers to the current root  
  void **osMemHandle (IN_OUT)       Pointer To OS Mem handle to fill in
  void **agVirtAddr (IN_OUT)        Pointer to allocated memory address
  U32  *agPhysUpper32 (IN_OUT)      Pointer to Up 32 bit mem phys addr.
  U32  *agPhysLower32 (IN_OUT)      Pointer to low 32 bit mem phys addr.
  U32  alignment (IN)               Alignment requirement
  U32  allocLength (IN)             Required memory length
  agBOOLEAN isChacheable (IN)       Required memory type
Return:
  tiSuccess - success
  tiMemoryTooLarge - requested memory size too large
  tiMemoryNotAvail - no dma memory available 
Note:
  for sata use.
  where a cacheable allocation inherently may be swapped, the values
   agPhysUpper32 and agPhysLower32 are understood to mean nothing when the
   value isCacheable is set to true.  these phys values must not be used by
   the caller.
******************************************************************************/
osGLOBAL U32 ostiAllocMemory( tiRoot_t *ptiRoot,
                              void    **osMemHandle,
                              void    **agVirtAddr,
                              U32      *agPhysUpper32,
                              U32      *agPhysLower32,
                              U32       alignment,
                              U32       allocLength,
                              agBOOLEAN isCacheable )
{
  ag_card_info_t *pCardInfo = TIROOT_TO_CARDINFO( ptiRoot );
  ag_dma_addr_t  *pMem;
  struct agtiapi_softc  *pCard;
  pCard = TIROOT_TO_CARD(ptiRoot);

  AGTIAPI_PRINTK( "ostiAllocMemory: debug, cache? %d size %d alloc algn %d ### \n",
          isCacheable, allocLength, alignment );

  if( pCardInfo->topOfFreeDynamicMem == 0 ) {
    AGTIAPI_PRINTK( "ostiAllocMemory: No space left, increase "
	    "AGTIAPI_DYNAMIC_MAX! ERROR\n" );
    return tiMemoryNotAvail;
  }

  pMem = pCardInfo->freeDynamicMem[pCardInfo->topOfFreeDynamicMem - 1];

  // where this memory has bee preallocated, be sure requirements do not
  //  exceed the limits of resources available
  if( allocLength > 4096 ) {
    AGTIAPI_PRINTK( "ostiAllocMemory: no-cache size 0x%x alloc NOT AVAILABLE\n",
            allocLength );
    return tiMemoryNotAvail;
  }
  if( alignment > 32 ) {
    AGTIAPI_PRINTK( "ostiAllocMemory: no-cache alignment 0x%x NOT AVAILABLE\n",
            alignment );
    return tiMemoryNotAvail;
  }
    
  pMem->dmaPhysAddr = pMem->nocache_busaddr;
  pMem->dmaVirtAddr = pMem->nocache_mem;
  pMem->memSize     = allocLength;
  *agVirtAddr  = pMem->dmaVirtAddr;

  *agPhysUpper32 = HIGH_32_BITS( pMem->dmaPhysAddr );    
  *agPhysLower32 = LOW_32_BITS( pMem->dmaPhysAddr );

  mtx_lock(&pCard->memLock);
  pCardInfo->topOfFreeDynamicMem--;
  *osMemHandle = (void *)pMem; // virtAddr;
  mtx_unlock(&pCard->memLock);

  return tiSuccess;
}

/******************************************************************************
ostiIOCTLWaitForSignal()  
Purpose:
  Function to wait semaphore during ioctl
Parameters: 
  tiRoot_t *ptiRoot (IN)     Pointer to the current HBA  
  void **agParam1 (IN_OUT)   Pointer to context to be passed
  void **agParam2 (IN_OUT)   Pointer to context to be passed
  void **agParam (IN_OUT)    Pointer to context to be passed
Return:
Note: 
******************************************************************************/
osGLOBAL void
ostiIOCTLWaitForSignal(tiRoot_t *ptiRoot,
                       void *agParam1,
                       void *agParam2,
                       void *agParam3)
{
  struct agtiapi_softc  *pCard;
  pCard = TIROOT_TO_CARD(ptiRoot);

  pCard->down_count++;
  sema_wait (pCard->pIoctlSem);
}

/* Below function has to be changed to use wait for completion */
osGLOBAL void
ostiIOCTLWaitForComplete(tiRoot_t *ptiRoot,
                       void *agParam1,
                       void *agParam2,
                       void *agParam3)
{
  struct agtiapi_softc  *pCard;
  pCard = TIROOT_TO_CARD(ptiRoot);

  pCard->down_count++;
  sema_wait (pCard->pIoctlSem);
}


/******************************************************************************
ostiChipConfigReadBit32()
Purpose:
  Read 32-bit value from PCI configuration register
Parameters:
  tiRoot_t *ptiRoot (IN)     Pointer to tiRoot structure
  U32 chipConfigOffset (IN)  Offset to PCI configuration register
Return:
  32 bit data
******************************************************************************/
U32 ostiChipConfigReadBit32( tiRoot_t *ptiRoot, U32 chipConfigOffset )
{
  device_t lDev = TIROOT_TO_PCIDEV(ptiRoot);
  u_int32_t lData = 0;

  lData = pci_read_config( lDev, chipConfigOffset, 4 );

  return (U32)lData;
}


/******************************************************************************
ostiChipConfigWriteBit32()
Purpose:
  Write 32-bit value to PCI configuration register
Parameters:
  tiRoot_t *ptiRoot (IN)     Pointer to tiRoot structure
  U32 chipConfigOffset (IN)  Offset to PCI configuration register    
  U32 chipConfigValue (IN)   Value to be written
Return: none
******************************************************************************/
void ostiChipConfigWriteBit32( tiRoot_t *ptiRoot,
			       U32       chipConfigOffset,
			       U32       chipConfigValue   )
{
  device_t lDev = TIROOT_TO_PCIDEV(ptiRoot);
  pci_write_config( lDev, chipConfigOffset, chipConfigValue, 4 );
}

/******************************************************************************
ostiChipReadBit32()
Purpose:
  Read 32-bit value from PCI address register
Parameters:
  tiRoot_t *ptiRoot (IN)  Pointer to tiRoot structure
  U32 chipOffset (IN)     Offset to PCI configuration register    
Return:
  32 bit data
******************************************************************************/
U32 ostiChipReadBit32(tiRoot_t *ptiRoot, U32 chipOffset)
{
  U32  data;
  ag_card_info_t *pCardInfo;

  pCardInfo = TIROOT_TO_CARDINFO(ptiRoot);
  data = *(U32 *)(pCardInfo->pciMemVirtAddr + chipOffset);
  return data;
}

/******************************************************************************
ostiChipWriteBit32()
Purpose:
  Write 32-bit value to PCI address register
Parameters:
  tiRoot_t *ptiRoot (IN)  Pointer to tiRoot structure
  U32 chipOffset (IN)     Offset to PCI configuration register    
  U32 chipValue (IN)      Value to be written
Return: none
******************************************************************************/
void ostiChipWriteBit32( tiRoot_t *ptiRoot, U32 chipOffset, U32 chipValue )
{
  ag_card_info_t *pCardInfo;
  pCardInfo = TIROOT_TO_CARDINFO(ptiRoot);
  *(U32 *)(pCardInfo->pciMemVirtAddr + chipOffset) = chipValue;
}

/******************************************************************************
ostiChipReadBit32Ext()
Purpose:
  Read 32-bit value from PCI address register
Parameters:
  tiRoot_t *ptiRoot (IN)  Pointer to tiRoot structure
  busBaseNumber            PCI BAR number
  U32 chipOffset (IN)     Offset to PCI configuration register    
Return:
  32 bit data
******************************************************************************/
U32 ostiChipReadBit32Ext( tiRoot_t *ptiRoot,
			  U32 busBaseNumber,
			  U32 chipOffset )
{
  U32  data;
  ag_card_info_t *pCardInfo;

  pCardInfo = TIROOT_TO_CARDINFO(ptiRoot);
  data = *(U32 *)((pCardInfo->pciMemVirtAddrSpc[busBaseNumber]) + chipOffset );
  return data;
}

/******************************************************************************
ostiChipWriteBit32Ext()
Purpose:
  Write 32-bit value to PCI address register
Parameters:
  tiRoot_t *ptiRoot (IN)  Pointer to tiRoot structure
  busBaseNumber           PCI BAR number  
  U32 chipOffset (IN)     Offset to PCI configuration register    
  U32 chipValue (IN)      Value to be written
Return: none
******************************************************************************/
void ostiChipWriteBit32Ext( tiRoot_t *ptiRoot,
			    U32 busBaseNumber,
			    U32 chipOffset,
			    U32 aData )
{
  ag_card_info_t *pCardInfo;
  pCardInfo = TIROOT_TO_CARDINFO(ptiRoot);
  *(U32 *)((pCardInfo->pciMemVirtAddrSpc[busBaseNumber]) + chipOffset ) = aData;
}

/******************************************************************************
ostiChipReadBit8()
Purpose:
  Read 8-bit value from PCI address register
Parameters:
  tiRoot_t *ptiRoot (IN)  Pointer to tiRoot structure
  U32 chipOffset (IN)     Offset to PCI configuration register    
Return:
  8 bit data
******************************************************************************/
U08 ostiChipReadBit8( tiRoot_t *ptiRoot, U32 chipOffset )
{
  ag_card_info_t *pCardInfo;
  pCardInfo = TIROOT_TO_CARDINFO(ptiRoot);
  return *(U08 *)( pCardInfo->pciMemVirtAddr + chipOffset );
}

/******************************************************************************
ostiChipWriteBit8()
Purpose:
  Write 8-bit value to PCI address register
Parameters:
  tiRoot_t *ptiRoot (IN)  Pointer to tiRoot structure
  U32 chipOffset (IN)     Offset to PCI configuration register    
  U8 chipValue (IN)       Value to be written
Return: none
******************************************************************************/
void ostiChipWriteBit8( tiRoot_t *ptiRoot, U32 chipOffset, U08 chipValue )
{
  ag_card_info_t *pCardInfo;
  pCardInfo = TIROOT_TO_CARDINFO(ptiRoot);
  *(U08 *)( pCardInfo->pciMemVirtAddr + chipOffset ) = chipValue;
}


void ostiFlashReadBlock(tiRoot_t *ptiRoot,
                   U32      offset,
                   void     *bufPtr,
                   U32      nbytes)
{
  AGTIAPI_PRINTK( "ostiFlashReadBlock: No support for iscsi device\n" );
}

/******************************************************************************
ostiFreeMemory()
Purpose:
  TD layer calls to free allocated dma memory
Parameters: 
  tiRoot_t *ptiRoot (IN)  Pointer refers to the current root  
  void *osMemHandle (IN)  Pointer to OS mem handle to be released
  u32  allocLength (IN)   Aloocated memory length in byte
Return:
  tiSuccess       - success
  tiInvalidHandle - handle is invalid
******************************************************************************/
osGLOBAL U32 ostiFreeMemory( tiRoot_t *ptiRoot,
                             void *osMemHandle,
                             U32 allocLength )
{
  ag_card_info_t *pCardInfo = TIROOT_TO_CARDINFO( ptiRoot );
  ag_dma_addr_t  *pMem = (ag_dma_addr_t*)osMemHandle;
  struct agtiapi_softc  *pCard;
  pCard = TIROOT_TO_CARD(ptiRoot);

  if( !osMemHandle ) {
      AGTIAPI_PRINTK( "ostiFreeMemory: NULL handle ERROR\n" );
      return tiInvalidHandle;
  }

  AGTIAPI_PRINTK( "ostiFreeMemory: debug messsage %p ### \n",
                  (void*)pMem->dmaPhysAddr );

  // mark as unused
  pMem->memSize = 0;
  pMem->dmaVirtAddr = NULL;
  pMem->dmaPhysAddr = 0;

  if (pCardInfo->topOfFreeDynamicMem == AGTIAPI_DYNAMIC_MAX) {
    AGTIAPI_PRINTK( "ostiFreeMemory: too many free slots ERROR\n" );
    return tiInvalidHandle;
  }

  mtx_lock(&pCard->memLock);
  pCardInfo->freeDynamicMem[pCardInfo->topOfFreeDynamicMem++] = pMem;
  mtx_unlock(&pCard->memLock);

  return tiSuccess;
}


/******************************************************************************
ostiMakeParamString()
Purpose:
  Utility function to simplify flow in ostiGetTransportParam().  Produces
  a string handle constructed from ostiGetTransportParam() values:
  key, subkey1, subkey2, subkey3, subkey4, subkey5, and valueName.
Parameters:
  S08 *aKey (IN)             Pointer to 1st level parameter string
  S08 *aSubkey1 (IN)         Pointer to 2nd level parameter string
  S08 *aSubkey2 (IN)         Pointer to 3rd level parameter string
  S08 *aSubkey3 (IN)         Pointer to 4th level parameter string
  S08 *aSubkey4 (IN)         Pointer to 5th level parameter string
  S08 *aSubkey5 (IN)         Pointer to 6th level parameter string
  S08 *aValueName (IN)       Pointer to name string of the value under keys
  S08 *aFullKey (OUT)        Pointer to returned key-value-handle buffer
  U32 *apLenFullKey (OUT)    String length in the key-value-handle buffer
Return:
  tiSuccess - Success
  tiError   - Failed
Note:
  If all input strings are NULL, tiError will return with zero in apLenFullKey
*****************************************************************************/
inline static U32 ostiMakeParamString( S08 *aKey,
                                       S08 *aSubkey1,
                                       S08 *aSubkey2,
                                       S08 *aSubkey3,
                                       S08 *aSubkey4,
                                       S08 *aSubkey5,
                                       S08 *aValueName,
                                       S08 *aFullKey,
                                       U32 *apLenFullKey )
{
  // preliminary sanity checks
  if( agNULL == aKey ) {
    *apLenFullKey = 0;
    printf( "ostiGetTransportParam called with no key.  how odd.\n" );
    return tiError;
  }
  if( agNULL == aValueName ) {
    *apLenFullKey = 0;
    printf( "ostiGetTransportParam called with no value-name.  how odd.\n" );
    return tiError;
  }

  strcpy( aFullKey, "DPMC_" );  // start at the beginning of the string
  strcat( aFullKey, aKey );

  int lIdx;
  S08 *lStrIdx = agNULL;
  for( lIdx = 1; lIdx <= 5; lIdx++ ) {
    if( 1 == lIdx) lStrIdx = aSubkey1;
    if( 2 == lIdx) lStrIdx = aSubkey2;
    if( 3 == lIdx) lStrIdx = aSubkey3;
    if( 4 == lIdx) lStrIdx = aSubkey4;
    if( 5 == lIdx) lStrIdx = aSubkey5;
    if( agNULL == lStrIdx ) break; // no more key information
    // append key information
    strcat( aFullKey, "_" );
    strcat( aFullKey, lStrIdx );
  }

  // only the value name is left to append
  strcat( aFullKey, "_" );
  strcat( aFullKey, aValueName );

  *apLenFullKey = strlen( aFullKey ); // 58 is max len seen; June 11, 2012
  // printf( "ostiMakeParamString: x%d out-str:%s\n", // debug print
  //        *apLenFullKey, aFullKey );

  return tiSuccess; // ship it chief
}


/******************************************************************************
ostiGetTransportParam()
Purpose:
  Call back function from lower layer to get parameters.
Parameters:
  tiRoot_t *ptiRoot (IN)     Pointer to driver root data structure
  S08 *key (IN)              Pointer to 1st level parameter
  S08 *subkey1 (IN)          Pointer to 2nd level parameter
  S08 *subkey2 (IN)          Pointer to 3rd level parameter
  S08 *subkey3 (IN)          Pointer to 4th level parameter
  S08 *subkey4 (IN)          Pointer to 5th level parameter
  S08 *subkey5 (IN)          Pointer to 6th level parameter
  S08 *valueName (IN)        Pointer to name of the value under keys
  S08 *buffer (OUT)          Pointer to returned information buffer
  U32 bufferLen (OUT)        Buffer length
  U32 *lenReceived (OUT)     String length in the buffer
Return:
  tiSuccess - Success
  Other     - Failed
Note:
  The scheme of searching adjustable parameter tree is the following:
  key
    - subkey1
      - subkey2
        - subkey3
          - subkey4
            - subkey5
              - value
  If no match in any case, tiError will return with zero length.

  Where there is no indication of max key and subkey length,
  an upper limit guess of 200 is used.
  Perhaps a prudent revision would be to add some argument(s) to be
  able to manage/check these "key" string lengths.
  This function does no checking of buffer being a valid pointer.
*****************************************************************************/
U32 ostiGetTransportParam( tiRoot_t *ptiRoot,
                           S08      *key,
                           S08      *subkey1,
                           S08      *subkey2,
                           S08      *subkey3,
                           S08      *subkey4,
                           S08      *subkey5,
                           S08      *valueName,
                           S08      *buffer,
                           U32       bufferLen,
                           U32      *lenReceived )
{
  S08 lFullKey[200];
  U32 lLenFullKey = 0;
  *lenReceived = 0;

  if( bufferLen > 1 )
    strcpy( buffer, "" );
  else {
    printf( "ostiGetTransportParam: buffer too small at only %d",
            bufferLen );
    return tiError; // not a reasonable buffer to work with
  }
  ostiMakeParamString( key, subkey1, subkey2, subkey3, subkey4, subkey5,
                       valueName, lFullKey, &lLenFullKey );
  if( lLenFullKey )  // clean ParamString extraction
    TUNABLE_STR_FETCH( lFullKey, buffer, bufferLen );
  else
    return tiError;  // not working out, bail now

  *lenReceived = strlen( buffer );

  //if( *lenReceived ) // handy debug print
  //  printf( "ostiGetTransportParam: sz%d val:%s hdl-str:%s\n",
  //          *lenReceived, buffer, lFullKey );

  return tiSuccess;  // ship it chief
}


/******************************************************************************
ostiIOCTLClearSignal()

Purpose:
  Function to clear or reset semaphore during ioctl
Parameters: 
  tiRoot_t *ptiRoot (IN)     Pointer to the current HBA  
  void **agParam1 (IN_OUT)   Pointer to context to be passed
  void **agParam2 (IN_OUT)   Pointer to context to be passed
  void **agParam (IN_OUT)    Pointer to context to be passed
Return:
Note:    
  TBD, need more work for card based semaphore.  Also needs to 
  consider the calling sequence.
******************************************************************************/
osGLOBAL void 
ostiIOCTLClearSignal(tiRoot_t *ptiRoot,
                     void **agParam1,
                     void **agParam2, 
                     void **agParam3)
{
}
 

/******************************************************************************
ostiIOCTLSetSignal()  ### function currently stubbed out
Purpose:
  Function to set semaphore during ioctl
Parameters: 
  tiRoot_t *ptiRoot (IN)     Pointer to the current HBA  
  void **agParam1 (IN_OUT)   Pointer to context to be passed
  void **agParam2 (IN_OUT)   Pointer to context to be passed
  void **agParam (IN_OUT)    Pointer to context to be passed
Return:
Note:    
******************************************************************************/
osGLOBAL void 
ostiIOCTLSetSignal(tiRoot_t *ptiRoot,
                   void *agParam1,
                   void *agParam2, 
                   void *agParam3)
{
  struct agtiapi_softc  *pCard;
  pCard = TIROOT_TO_CARD(ptiRoot);
  if (pCard->down_count != pCard->up_count)
  {
    pCard->up_count++;
    sema_post (pCard->pIoctlSem);
  }
}

osGLOBAL void 
ostiIOCTLComplete(tiRoot_t *ptiRoot,
                   void *agParam1,
                   void *agParam2, 
                   void *agParam3)
{
  struct agtiapi_softc  *pCard;
  pCard = TIROOT_TO_CARD(ptiRoot);
  if (pCard->down_count != pCard->up_count)
  {
    pCard->up_count++;
    sema_post (pCard->pIoctlSem);
  }
}

/******************************************************************************
ostiPortEvent()
Purpose:
  Call back function to inform OS the events of port state change.
Parameters:
  tiRoot_t *ptiRoot(IN)          Pointer to driver root data structure 
  tiPortEvent_t eventType (IN)   Type of port event:
                                 tiPortPanic
                                 tiPortResetComplete
                                 tiPortNameServerDown
                                 tiPortLinkDown
                                 tiPortLinkUp
                                 tiPortStarted
                                 tiPortStopped
                                 tiPortShutdown
                                 tiPortInitComplete
  void *pParm(IN)                Pointer to event specific structure
Return:
  None 
******************************************************************************/
void
ostiPortEvent(tiRoot_t      *ptiRoot, 
              tiPortEvent_t eventType, 
              U32           status, 
              void          *pParm)
{
  struct agtiapi_softc  *pCard;
  ag_portal_data_t *pPortalData;
  
  AGTIAPI_PRINTK("ostiPortEvent: start eventType 0x%x\n", eventType);

  pCard = TIROOT_TO_CARD(ptiRoot);

  switch (eventType) 
  {
  case tiPortStarted:
       pCard->flags |= AGTIAPI_CB_DONE;
       pPortalData = PORTAL_CONTEXT_TO_PORTALDATA(pParm);
       PORTAL_STATUS(pPortalData) |= AGTIAPI_PORT_START;
       AGTIAPI_PRINTK("PortStarted - portal %p, status %x\n",
                      pPortalData, PORTAL_STATUS(pPortalData));
       break;
  case tiPortLinkDown:
       pPortalData = PORTAL_CONTEXT_TO_PORTALDATA(pParm);
       PORTAL_STATUS(pPortalData) &= ~AGTIAPI_PORT_LINK_UP;
       AGTIAPI_PRINTK("PortLinkDown - portal %p\n", pPortalData);
       break;
  case tiPortLinkUp:
       pPortalData = PORTAL_CONTEXT_TO_PORTALDATA(pParm);
       PORTAL_STATUS(pPortalData) |= AGTIAPI_PORT_LINK_UP;
       AGTIAPI_PRINTK("PortLinkUp - portal %p\n", pPortalData);
#ifdef INITIATOR_DRIVER
#ifndef HOTPLUG_SUPPORT
       if (!(pCard->flags & AGTIAPI_INIT_TIME))
#endif
//         agtiapi_StartIO(pCard);
#endif
       break;
case tiPortDiscoveryReady:
       pCard->flags |= AGTIAPI_CB_DONE;
       pPortalData = PORTAL_CONTEXT_TO_PORTALDATA(pParm);
       PORTAL_STATUS(pPortalData) |= AGTIAPI_PORT_DISC_READY;
       AGTIAPI_PRINTK("PortDiscoveryReady - portal %p, status 0x%x\n",
                      pPortalData, PORTAL_STATUS(pPortalData));
#ifdef INITIATOR_DRIVER
#ifndef HOTPLUG_SUPPORT
       if (!(pCard->flags & AGTIAPI_INIT_TIME))
#endif
         tiINIDiscoverTargets(&pCard->tiRoot,
                              &pPortalData->portalInfo.tiPortalContext,
                              FORCE_PERSISTENT_ASSIGN_MASK);
#endif
       break;
  case tiPortNameServerDown:
       AGTIAPI_PRINTK("PortNameSeverDown\n");
       pPortalData = PORTAL_CONTEXT_TO_PORTALDATA(pParm);
       PORTAL_STATUS(pPortalData) &= ~AGTIAPI_NAME_SERVER_UP;
       break;
  case tiPortPanic:
       AGTIAPI_PRINTK("PortPanic\n");
       AGTIAPI_PRINTK( "## PortEvent\n" );
       pCard->flags |= AGTIAPI_PORT_PANIC;
       break;
  case tiPortResetComplete:
       AGTIAPI_PRINTK("PortResetComplete\n");
       pCard->flags |= AGTIAPI_CB_DONE;
       if (status == tiSuccess)
         pCard->flags |= AGTIAPI_RESET_SUCCESS;
       break;
  case tiPortShutdown:
       AGTIAPI_PRINTK("PortShutdown\n");
       pCard->flags |= AGTIAPI_CB_DONE;
       pCard->flags |= AGTIAPI_PORT_SHUTDOWN;
       break;
  case tiPortStopped:
       pCard->flags |= AGTIAPI_CB_DONE;
       pPortalData = PORTAL_CONTEXT_TO_PORTALDATA(pParm);
       PORTAL_STATUS(pPortalData) |= AGTIAPI_PORT_STOPPED;
       AGTIAPI_PRINTK("PortStopped - portal %p\n", pPortalData);
       break;
  case tiEncryptOperation:
       break;
  case tiModePageOperation:
       break;
  default:
       AGTIAPI_PRINTK("PortEvent - %d (Unknown)\n", eventType);
       break;
  }
  return;
}


/******************************************************************************
ostiStallThread()
Purpose:
  Stall the thread (busy wait) for a number of microseconds.
Parameters:
  tiRoot_t *ptiRoot (IN)  Pointer to the tiRoot data structure
  U32 microseconds (IN)   Micro-seconds to be hold
Returns: none
******************************************************************************/
void ostiStallThread( tiRoot_t *ptiRoot, U32 microseconds )
{
  DELAY( microseconds );
}


/******************************************************************************
ostiTimeStamp()   ### stubbed out for now
Purpose:
  Time stamp
Parameters:
  tiRoot_t *ptiRoot (IN)  Pointer to the tiRoot data structure
Returns:
  Time stamp in milisecond
******************************************************************************/
U32
ostiTimeStamp(tiRoot_t *ptiRoot)
{
  return 0;
}

// meant as stubbed out 64 bit version.
U64 ostiTimeStamp64( tiRoot_t *ptiRoot )
{
  U64 retVal;
  retVal = ostiTimeStamp( ptiRoot );
  return retVal;
}

/******************************************************************************
ostiCacheFlush()    ### stubbed out for now
ostiCacheInvalidate()
ostiCachePreFlush()

Purpose:
  Cache-coherency APIs
Parameters:
  
Returns:
  
Note:
  These 3 functions are to support new cache coherency applications.
  Currently the APIs are implemented in FC for PPC platform. The 
  define CACHED_DMA enable for dma_cache_sync function call. However
  this define is restricted for certain version of linux, such as
  Linux 2.6.x and above, and certain platform such as PPC.

  DO NOT define the CACHED_DMA if the cache coherency is not required
  or the environment does not match.
******************************************************************************/
osGLOBAL void ostiCacheFlush(
                        tiRoot_t    *ptiRoot,
                        void        *osMemHandle,
                        void        *virtPtr,
                        bit32       length
                        )
{
}

osGLOBAL void ostiCacheInvalidate(
                        tiRoot_t    *ptiRoot,
                        void        *osMemHandle,
                        void        *virtPtr,
                        bit32       length
                        )
{
}

osGLOBAL void ostiCachePreFlush(
                        tiRoot_t    *tiRoot,
                        void    *osMemHandle,
                        void    *virtPtr,
                        bit32     length
                        )
{
}


/* 
   added for SAS/SATA
   this is called by ossaInterrruptEnable
*/
GLOBAL void ostiInterruptEnable( tiRoot_t  *ptiRoot, bit32 channelNum )
{
  // yep, really nothing.
}

/* 
   this is called by ossaInterrruptDisable
*/
GLOBAL void ostiInterruptDisable( tiRoot_t  *ptiRoot, bit32 channelNum )
{
  // yep, really nothing.
}

