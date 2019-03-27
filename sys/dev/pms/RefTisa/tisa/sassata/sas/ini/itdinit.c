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
/** \file
 *
 *
 * This file contains initiator initialization functions
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#include <dev/pms/RefTisa/tisa/api/titypes.h>
#include <dev/pms/RefTisa/tisa/api/ostiapi.h>
#include <dev/pms/RefTisa/tisa/api/tiapi.h>
#include <dev/pms/RefTisa/tisa/api/tiglobal.h>

#ifdef FDS_SM
#include <dev/pms/RefTisa/sat/api/sm.h>
#include <dev/pms/RefTisa/sat/api/smapi.h>
#include <dev/pms/RefTisa/sat/api/tdsmapi.h>
#endif

#ifdef FDS_DM
#include <dev/pms/RefTisa/discovery/api/dm.h>
#include <dev/pms/RefTisa/discovery/api/dmapi.h>
#include <dev/pms/RefTisa/discovery/api/tddmapi.h>
#endif

#include <dev/pms/RefTisa/tisa/sassata/sas/common/tdtypes.h>
#include <dev/pms/freebsd/driver/common/osstring.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdutil.h>

#ifdef INITIATOR_DRIVER
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itdtypes.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itddefs.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itdglobl.h>
#endif

#ifdef TARGET_DRIVER
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdglobl.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdxchg.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdtypes.h>
#endif

#include <dev/pms/RefTisa/tisa/sassata/common/tdsatypes.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdproto.h>

/*****************************************************************************
*! \brief itdssGetResource
*
*  Purpose:  This function is called to determine the Transport 
*            Dependent Layer internal resource requirement for the initiator
*            side.
*
*  /param   tiRoot:            Pointer to driver/port instance.
*  /param   initiatorResource: Pointer to initiator functionality memory and 
*                              option requirement.
*
*  /return: None
*
*  /note - This function only return the memory requirement in the tiMem_t 
*          structure in initiatorResource. It does not allocated memory, so the
*          address fields in tiMem_t are not used.
*
*****************************************************************************/
osGLOBAL void 
itdssGetResource(
                 tiRoot_t * tiRoot, 
                 tiInitiatorResource_t * initiatorResource 
                 ) 
{
  itdssOperatingOption_t    OperatingOption;
  tiInitiatorMem_t          *iniMem;
  bit32 i;

  iniMem                  = &initiatorResource->initiatorMem;
  iniMem->count           = 1;          /* Only 1 memory descriptors are used */
  
  TI_DBG6(("itdssGetResource: start\n"));
  
  /*  other than [0], nothing is used 
   *  tdCachedMem[0]: cached mem for initiator TD Layer main functionality :
   *                  itdssIni_t
   *  tdCachedMem[1-5]: is availalbe 
  */
  
  /* 
   * Get default parameters from the OS Specific area
   * and reads parameters from the configuration file
   */ 
  itdssGetOperatingOptionParams(tiRoot, &OperatingOption);
  
  /* 
   * Cached mem for initiator Transport Dependent Layer main functionality 
   */
  
  iniMem->tdCachedMem[0].singleElementLength  = sizeof(itdsaIni_t);
  iniMem->tdCachedMem[0].numElements          = 1;
  iniMem->tdCachedMem[0].totalLength          =
    iniMem->tdCachedMem[0].singleElementLength *
    iniMem->tdCachedMem[0].numElements;
  iniMem->tdCachedMem[0].alignment            = sizeof (void *); /* 4 bytes */
  iniMem->tdCachedMem[0].type                 = TI_CACHED_MEM;
  iniMem->tdCachedMem[0].reserved             = 0;
  iniMem->tdCachedMem[0].virtPtr               = agNULL;
  iniMem->tdCachedMem[0].osHandle              = agNULL;
  iniMem->tdCachedMem[0].physAddrUpper         = 0;
  iniMem->tdCachedMem[0].physAddrLower         = 0;
  
  
  /*
   * Not used mem structure. Initialize them.
   */ 
  for (i = iniMem->count; i < 6; i++)
  {
    iniMem->tdCachedMem[i].singleElementLength  = 0;
    iniMem->tdCachedMem[i].numElements          = 0;
    iniMem->tdCachedMem[i].totalLength          = 0;
    iniMem->tdCachedMem[i].alignment            = 0;
    iniMem->tdCachedMem[i].type                 = TI_CACHED_MEM;
    iniMem->tdCachedMem[i].reserved             = 0;

    iniMem->tdCachedMem[i].virtPtr               = agNULL;
    iniMem->tdCachedMem[i].osHandle              = agNULL;
    iniMem->tdCachedMem[i].physAddrUpper         = 0;
    iniMem->tdCachedMem[i].physAddrLower         = 0;
    
  }
  
  /* 
   * Operating option of TISA
   * fills in tiInitiatorOption 
   */
  initiatorResource->initiatorOption.usecsPerTick       = OperatingOption.UsecsPerTick;  /* default value 1 sec*/

  initiatorResource->initiatorOption.pageSize           = 0;

  /* initialization */
  initiatorResource->initiatorOption.dynamicDmaMem.numElements          = 0;
  initiatorResource->initiatorOption.dynamicDmaMem.singleElementLength  = 0;
  initiatorResource->initiatorOption.dynamicDmaMem.totalLength          = 0;
  initiatorResource->initiatorOption.dynamicDmaMem.alignment            = 0;
  
  /* initialization */
  initiatorResource->initiatorOption.dynamicCachedMem.numElements         = 0;
  initiatorResource->initiatorOption.dynamicCachedMem.singleElementLength = 0;
  initiatorResource->initiatorOption.dynamicCachedMem.totalLength         = 0;
  initiatorResource->initiatorOption.dynamicCachedMem.alignment           = 0;

  
  /* This is not used in OS like Linux which supports dynamic memeory allocation
     In short, this is for Windows, which does not support dynamic memory allocation */
  /* ostiallocmemory(..... ,agFALSE) is supported by the following code eg) sat.c
     The memory is DMA capable(uncached)
   */
#ifdef CCBUILD_EncryptionDriver
  /* extend the DMA memory for supporting two encryption DEK tables */
  initiatorResource->initiatorOption.dynamicDmaMem.numElements          = 128 + DEK_MAX_TABLE_ENTRIES / 2;
#else
  initiatorResource->initiatorOption.dynamicDmaMem.numElements          = 128;
#endif
  /* worked 
     initiatorResource->initiatorOption.dynamicDmaMem.singleElementLength  = sizeof(tdIORequestBody_t);
  */
  initiatorResource->initiatorOption.dynamicDmaMem.singleElementLength  = 512;
  initiatorResource->initiatorOption.dynamicDmaMem.totalLength          =
    initiatorResource->initiatorOption.dynamicDmaMem.numElements *
    initiatorResource->initiatorOption.dynamicDmaMem.singleElementLength;
  initiatorResource->initiatorOption.dynamicDmaMem.alignment            = sizeof(void *);

  
  /* This is not used in OS like Linux which supports dynamic memeory allocation
     In short, this is for Windows, which does not support dynamic memory allocation */
  /* ostiallocmemory(..... ,agTRUE) is supported by the following code eg) sat.c
     The memory is DMA incapable(cached)
   */
  initiatorResource->initiatorOption.dynamicCachedMem.numElements = 1024 + 256;
  /* worked 
  initiatorResource->initiatorOption.dynamicCachedMem.singleElementLength = sizeof(tdIORequestBody_t);
  initiatorResource->initiatorOption.dynamicCachedMem.singleElementLength = sizeof(tdssSMPRequestBody_t);
  */
  initiatorResource->initiatorOption.dynamicCachedMem.singleElementLength = 512;
  initiatorResource->initiatorOption.dynamicCachedMem.totalLength         = 
        initiatorResource->initiatorOption.dynamicCachedMem.numElements *
        initiatorResource->initiatorOption.dynamicCachedMem.singleElementLength;
  initiatorResource->initiatorOption.dynamicCachedMem.alignment           = sizeof(void *);
  
  /*
   * set the I/O request body size
   */
  initiatorResource->initiatorOption.ioRequestBodySize  = sizeof(tdIORequestBody_t);
  TI_DBG6(("itdssGetResource: sizeof(tdssSMPRequestBody_t) %d\n", (int)sizeof(tdssSMPRequestBody_t)));
  TI_DBG6(("itdssGetResource: end\n"));
  
  return;
}


/*****************************************************************************
*! \brief  itdssGetOperatingOptionParams
*
*  Purpose: This function is called to get default parameters from the 
*           OS Specific area. This function is called in the context of 
*           tiCOMGetResource() and tiCOMInit().
*
*
*  \param  tiRoot:   Pointer to initiator driver/port instance.
*  \param  option:   Pointer to the Transport Dependent options.
*
*  \return: None
*
*  \note -
*
*****************************************************************************/
osGLOBAL void 
itdssGetOperatingOptionParams(
                      tiRoot_t                *tiRoot, 
                      itdssOperatingOption_t  *OperatingOption
                      ) 
{
  char    *key = agNULL;
  char    *subkey1 = agNULL;
  char    *subkey2 = agNULL;
  char    *buffer;
  bit32   buffLen;
  bit32   lenRecv = 0;
  char    *pLastUsedChar = agNULL;
  char    tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
  char    globalStr[]     = "Global";
  char    iniParmsStr[]   = "InitiatorParms";

  TI_DBG6(("itdssGetOperatingOptionParams: start\n"));
  
  /* 
     first set the values to Default values
     Then, overwrite them using ostiGetTransportParam()
  */


  /* to remove compiler warnings */ 
  pLastUsedChar   = pLastUsedChar;
  lenRecv         = lenRecv;
  subkey2         = subkey2;
  subkey1         = subkey1;
  key             = key;
  buffer          = &tmpBuffer[0];
  buffLen         = sizeof (tmpBuffer);

  osti_memset(buffer, 0, buffLen);

  
  
  /* default values */
  OperatingOption->MaxTargets = DEFAULT_MAX_DEV; /* DEFAULT_MAX_TARGETS; */ /* 256 */
  OperatingOption->UsecsPerTick = DEFAULT_INI_TIMER_TICK; /* 1 sec */

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  /* defaults are overwritten in the following */
  /* Get MaxTargets */ 
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,
                             iniParmsStr,
                             agNULL,
                             agNULL,
                             agNULL, 
                             agNULL, 
                             "MaxTargets",
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      OperatingOption->MaxTargets = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      OperatingOption->MaxTargets = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("itdssGetOperatingOptionParams: MaxTargets  %d\n",  OperatingOption->MaxTargets ));
  }
  
#ifdef REMOVED
  /* get UsecsPerTick */
  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,
                             iniParmsStr,
                             agNULL,
                             agNULL,
                             agNULL, 
                             agNULL, 
                             "UsecsPerTick",
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      OperatingOption->UsecsPerTick = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      OperatingOption->UsecsPerTick = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
#endif

  return;
}


/*****************************************************************************
*! \brief  itdssInit
*
*  Purpose: This function is called to initialize the initiator specific 
*           Transport Dependent Layer. 
*           This function is not directly called by OS Specific module, 
*           as it is internally called by tiCOMInit().
*
*  /param tiRoot:            Pointer to driver/port instance.
*  /param initiatorResource: Pointer to initiator functionality memory
*                            and option requirement.
*  /param tdSharedMem:       Pointer to cached memory required by the 
*                            target/initiator shared functionality.
*
*  /return: 
*   tiSuccess   OK
*   others      not OK
*
*  /note -
*
*****************************************************************************/
osGLOBAL bit32 
itdssInit(
          tiRoot_t              *tiRoot, 
          tiInitiatorResource_t *initiatorResource,
          tiTdSharedMem_t       *tdSharedMem 
          ) 
{
  tiInitiatorMem_t          *iniMem;
  itdsaIni_t                *Initiator;
  itdssOperatingOption_t    *OperatingOption;
  tdsaRoot_t                *tdsaRoot;

  TI_DBG6(("itdssInit: start\n"));
  iniMem = &initiatorResource->initiatorMem;
  tdsaRoot = (tdsaRoot_t *)tiRoot->tdData;
  /* 
   * Cached mem for initiator Transport Dependent Layer main functionality 
   */ 
  Initiator = iniMem->tdCachedMem[0].virtPtr;

  /* 
   * Get default parameters from the OS Specific area 
   */ 
  OperatingOption = &Initiator->OperatingOption;

  /* 
   * Get default parameters from the OS Specific area
   * and reads parameters from the configuration file
   */ 

  itdssGetOperatingOptionParams(tiRoot, OperatingOption);
  /*
   * Update TD operating options with OS-layer-saved value
   * Only UsecsPerTick is updated
   */
  OperatingOption->UsecsPerTick =
    initiatorResource->initiatorOption.usecsPerTick;
    
  Initiator->NumIOsActive             = 0;

  /* 
   *  tdCachedMem[0]: cached mem for initiator TD Layer main functionality :
   *                   itdssIni_t
   *  tdCachedMem[1-5]: not in use
  */

  /* initialize the timerlist */
  itdssInitTimers(tiRoot);

  
  /* Initialize the tdsaAllShared, tdssSASShared pointers */
  
  Initiator->tdsaAllShared = &(tdsaRoot->tdsaAllShared);
    
  TI_DBG6(("itdssInit: end\n"));
  return (tiSuccess);

}

/*****************************************************************************
*! \brief
*  itdssInitTimers
*
*  Purpose: This function is called to initialize the timers
*           for initiator
*
*  \param   tiRoot: pointer to the driver instance
*
*  \return: None
*
*  \note -
*
*****************************************************************************/
osGLOBAL void 
itdssInitTimers( 
                tiRoot_t *tiRoot 
                ) 
{
  tdsaRoot_t     *tdsaRoot = (tdsaRoot_t *)(tiRoot->tdData);
  tdsaContext_t  *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t     *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  
  /* initialize the timerlist */
  TDLIST_INIT_HDR(&(Initiator->timerlist));

  return;
}
