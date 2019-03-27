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
 * tdport.c
 * This file contains port realted functions such as tiCOMPortStart()
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
#include <dev/pms/RefTisa/sallsdk/spc/sadefs.h>
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

#ifndef TURN_OFF_HDA
#include <dev/pms/RefTisa/sallsdk/hda/64k/aap1img.h> /* SPC HDA */
#include <dev/pms/RefTisa/sallsdk/hda/64k/ilaimg.h>
#include <dev/pms/RefTisa/sallsdk/hda/64k/iopimg.h>
#include <dev/pms/RefTisa/sallsdk/hda/64k/istrimg.h>

#include <dev/pms/RefTisa/sallsdk/hda/64k/aap18008.h>	/* SPCv HDA */
#include <dev/pms/RefTisa/sallsdk/hda/64k/iop8008.h>

#include <dev/pms/RefTisa/sallsdk/hda/64k/ila8008.h> /* Ila common to SPCv SPCvp versions */

#include <dev/pms/RefTisa/sallsdk/hda/64k/raae8070.h>	/* SPCv 12g HDA */
#include <dev/pms/RefTisa/sallsdk/hda/64k/iop8070.h>
#include <dev/pms/RefTisa/sallsdk/hda/64k/ila8070.h> /* Ila 12g  SPCv SPCvp versions */

#endif /* TURN_OFF_HDA */


bit32 gSSC_Disable = 0;
bit32 volatile sgpioResponseSet = 0;

#ifdef ECHO_TESTING
/* temporary to test saEchoCommand() */
bit8 gEcho;
#endif
bit32 tiCOMConfigureSgpio(
                        tiRoot_t    *tiRoot,
                        bit8        enableSgpio
                        );


/*****************************************************************************
*! \brief tdsaGetSwConfigParams
*
*  Purpose:  This function reads software configuration parameters from the
*            configuration file
*
*  \param  tiRoot:            Pointer to driver/port instance.
*
*  \return: None
*
*  \note -
*
*****************************************************************************/
osGLOBAL void
tdsaGetSwConfigParams(
                      tiRoot_t *tiRoot
                      )
{
  tdsaRoot_t     *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t  *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
  agsaSwConfig_t *SwConfig;
  agsaQueueConfig_t   *QueueConfig;
  char           *buffer;
  bit32          buffLen;
  bit32          lenRecv = 0;
  char           *pLastUsedChar = agNULL;
  char           tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
  char           globalStr[]     = "Global";
  char           iniParmsStr[]   = "InitiatorParms";
  char           SwParmsStr[]   = "SWParms";
  char           OBQueueProps[] = "OBQueueProps";
  char           IBQueueProps[] = "IBQueueProps";
  char           IBQueueSize[40];
  char           OBQueueSize[40];
  char           IBQueueEleSize[40];
  char           OBQueueEleSize[40];
  char           OBQueueInterruptCount[40];
  char           OBQueueInterruptDelay[40];
  char           OBQueueInterruptEnable[40];
  char           IBQueuePriority[40];
  char           *cardNum = tdsaAllShared->CardIDString;
  bit32          i;
  bit32          enableDIF;
  bit32          enableEncryption;
#ifdef SA_CONFIG_MDFD_REGISTRY
  bit32          disableMDF;
#endif

#ifdef FDS_DM
  dmSwConfig_t   *dmSwConfig;
#endif
#ifdef FDS_SM
  smSwConfig_t   *smSwConfig;
#endif

  TI_DBG6(("tdsaGetSwConfigParams: start\n"));
  TI_DBG6(("tdsaGetSwConfigParams: tdsaRoot %p tdsaAllShared %p \n",tdsaRoot, tdsaAllShared));

  buffer = tmpBuffer;
  buffLen = sizeof(tmpBuffer);

  osti_memset(buffer, 0, buffLen);

  /* the followings are the default values */
  SwConfig = (agsaSwConfig_t *)&(tdsaAllShared->SwConfig);
  QueueConfig = (agsaQueueConfig_t *)&(tdsaAllShared->QueueConfig);

#ifdef FDS_DM
  dmSwConfig = (dmSwConfig_t *)&(tdsaAllShared->dmSwConfig);
#endif
#ifdef FDS_SM
  smSwConfig = (smSwConfig_t *)&(tdsaAllShared->smSwConfig);
#endif

  /*
    just default values
    and are overwritten later by the configuration file contents
  */
  SwConfig->numDevHandles = DEFAULT_MAX_DEV;

  SwConfig->maxActiveIOs = DEFAULT_MAX_ACTIVE_IOS;
  SwConfig->smpReqTimeout = DEFAULT_SMP_TIMEOUT; /* DEFAULT_VALUE; */
  SwConfig->numberOfEventRegClients = DEFAULT_NUM_REG_CLIENTS;
  SwConfig->sizefEventLog1 = HOST_EVENT_LOG_SIZE;
  SwConfig->sizefEventLog2 = HOST_EVENT_LOG_SIZE;
  SwConfig->eventLog1Option = DEFAULT_EVENT_LOG_OPTION;
  SwConfig->eventLog2Option = DEFAULT_EVENT_LOG_OPTION;
  SwConfig->fatalErrorInterruptEnable = 1;
  SwConfig->fatalErrorInterruptVector = 0; /* Was 1 */
  SwConfig->hostDirectAccessSupport = 0;
  SwConfig->hostDirectAccessMode = 0;
  SwConfig->FWConfig = 0;
  SwConfig->enableDIF = agFALSE;
  SwConfig->enableEncryption = agFALSE;

#ifdef SA_CONFIG_MDFD_REGISTRY
  SwConfig->disableMDF = agFALSE;
#endif

  SwConfig->param1 = tdsaAllShared->tdDeviceIdVendId;
  SwConfig->param2 = tdsaAllShared->tdSubVendorId;


#if defined(SALLSDK_DEBUG)
  SwConfig->sallDebugLevel = 1; /* DEFAULT_VALUE; */
#endif
#if defined(DM_DEBUG)
  dmSwConfig->DMDebugLevel = 1; /* DEFAULT_VALUE; */
#endif
#if defined(SM_DEBUG)
  smSwConfig->SMDebugLevel = 1; /* DEFAULT_VALUE; */
#endif

  tdsaAllShared->portTMO = PORT_RECOVERY_TIMEOUT;   /* default 5 sec */
  tdsaAllShared->stp_idle_time = STP_IDLE_TIME;     /* default 5 us */
  tdsaAllShared->itNexusTimeout = IT_NEXUS_TIMEOUT; /* default 2000 ms */

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

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
      SwConfig->numDevHandles = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->numDevHandles = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: MaxTargets  %d\n",SwConfig->numDevHandles  ));
  }

  /*
   * read the NumInboundQueue parameter
   */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  QueueConfig->numInboundQueues = DEFAULT_NUM_INBOUND_QUEUE;  /* default 1 Inbound queue */

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "NumInboundQueues", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      QueueConfig->numInboundQueues = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      QueueConfig->numInboundQueues = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
    }

    if (QueueConfig->numInboundQueues > AGSA_MAX_INBOUND_Q)
    {
      QueueConfig->numInboundQueues = AGSA_MAX_INBOUND_Q;
    }
  }

  /*
   * read the NumOutboundQueue parameter
   */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  QueueConfig->numOutboundQueues = DEFAULT_NUM_OUTBOUND_QUEUE;  /* default 1 Outbound queue */

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "NumOutboundQueues", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      QueueConfig->numOutboundQueues = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      QueueConfig->numOutboundQueues = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
    }

    if (QueueConfig->numOutboundQueues > AGSA_MAX_OUTBOUND_Q)
    {
      QueueConfig->numOutboundQueues = AGSA_MAX_OUTBOUND_Q;
    }
  }

  /*
   * read the outbound queue option
   */

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  tdsaAllShared->QueueOption = DEFAULT_QUEUE_OPTION;  /* default 0 Outbound queue element */

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "QueueOption", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->QueueOption = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->QueueOption = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  /*
   * read the MaxActiveIO parameter
   */

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "MaxActiveIO", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      SwConfig->maxActiveIOs = osti_strtoul (buffer, &pLastUsedChar, 0);
      TI_DBG6(("tdsaGetSwConfigParams: maxactiveio 1 !!!\n"));
    }
    else
    {
      SwConfig->maxActiveIOs = osti_strtoul (buffer, &pLastUsedChar, 10);
      TI_DBG6(("tdsaGetSwConfigParams: maxactiveio 2 !!!\n"));
    }
    TI_DBG6(("tdsaGetSwConfigParams: maxactiveio 3 !!!\n"));
  }



  /*
   * read the SMPTO parameter (SMP Timeout)
   */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SMPTO", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      SwConfig->smpReqTimeout = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->smpReqTimeout = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }


  /*
   * read the NumRegClients parameter (SMP Timeout)
   */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "NumRegClients", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      SwConfig->numberOfEventRegClients = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->numberOfEventRegClients = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

#if defined(SALLSDK_DEBUG)
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "LLDebugLevel", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      SwConfig->sallDebugLevel = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->sallDebugLevel = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
#endif

#if defined(DM_DEBUG)
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "DMDebugLevel", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      dmSwConfig->DMDebugLevel = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      dmSwConfig->DMDebugLevel = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
#endif

#if defined(SM_DEBUG)
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SMDebugLevel", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      smSwConfig->SMDebugLevel = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      smSwConfig->SMDebugLevel = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
#endif

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  for (i=0;i<QueueConfig->numInboundQueues;i++)
  {
    osti_sprintf(IBQueueSize,"IBQueueNumElements%d", i);
    osti_sprintf(IBQueueEleSize,"IBQueueElementSize%d", i);
    osti_sprintf(IBQueuePriority,"IBQueuePriority%d", i);

    /*
     * read the IBQueueSize
     */

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    tdsaAllShared->InboundQueueSize[i] = DEFAULT_INBOUND_QUEUE_SIZE;  /* default 256 Inbound queue size */

    if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             IBQueueSize, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->InboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->InboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: queue number %d IB queue size %d\n", i, tdsaAllShared->InboundQueueSize[i]));
      }
    }


    /*
     * read the IBQueueEleSize
     */

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    tdsaAllShared->InboundQueueEleSize[i] = DEFAULT_INBOUND_QUEUE_ELE_SIZE;  /* default 128 Inbound queue element */

    if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             IBQueueEleSize, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->InboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->InboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: queue number %d IB queue ele size %d\n", i, tdsaAllShared->InboundQueueEleSize[i]));
      }
    }

    /*
     * read the IBQueuePriority
     */

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    tdsaAllShared->InboundQueuePriority[i] = DEFAULT_INBOUND_QUEUE_PRIORITY; /* default 0 Inbound queue priority */

    if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             IBQueuePriority, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->InboundQueuePriority[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->InboundQueuePriority[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: queue number %d priority %d\n", i, tdsaAllShared->InboundQueuePriority[i]));
      }
    }

    /**********************************************/
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
  }/* end of loop */



  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  for (i=0;i<QueueConfig->numOutboundQueues;i++)
  {
    osti_sprintf(OBQueueSize,"OBQueueNumElements%d", i);
    osti_sprintf(OBQueueEleSize,"OBQueueElementSize%d", i);
    osti_sprintf(OBQueueInterruptDelay,"OBQueueInterruptDelay%d", i);
    osti_sprintf(OBQueueInterruptCount,"OBQueueInterruptCount%d", i);
    osti_sprintf(OBQueueInterruptEnable,"OBQueueInterruptEnable%d", i);

    /*
     * read the OBQueueSize
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;


    tdsaAllShared->OutboundQueueSize[i] = DEFAULT_OUTBOUND_QUEUE_SIZE;  /* default 256 Outbound queue size */

    if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             OBQueueSize, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->OutboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->OutboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: queue number %d OB queue size %d\n", i, tdsaAllShared->OutboundQueueSize[i]));

      }
    }


    /*
     * read the OBQueueEleSize
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;


    tdsaAllShared->OutboundQueueEleSize[i] = DEFAULT_OUTBOUND_QUEUE_ELE_SIZE;  /* default 128 Outbound queue element */

    if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             OBQueueEleSize, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->OutboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->OutboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: queue number %d OB queue ele size %d\n", i, tdsaAllShared->OutboundQueueEleSize[i]));

      }
    }


    /*
     * read the OBQueueInterruptDelay
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;


    tdsaAllShared->OutboundQueueInterruptDelay[i] = DEFAULT_OUTBOUND_QUEUE_INTERRUPT_DELAY;  /* default 1 Outbound interrupt delay */

    if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             OBQueueInterruptDelay, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->OutboundQueueInterruptDelay[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->OutboundQueueInterruptDelay[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: queue number %d interrupt delay %d\n", i, tdsaAllShared->OutboundQueueInterruptDelay[i]));

      }
    }

    /*
     * read the OBQueueInterruptCount
     */

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    tdsaAllShared->OutboundQueueInterruptCount[i] = DEFAULT_OUTBOUND_QUEUE_INTERRUPT_COUNT;  /* default 1 Outbound interrupt count */

    if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             OBQueueInterruptCount, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->OutboundQueueInterruptCount[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->OutboundQueueInterruptCount[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: queue number %d interrupt count %d\n", i, tdsaAllShared->OutboundQueueInterruptCount[i]));
      }
    }


    /*
     * read the OBQueueInterruptEnable
     */

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    tdsaAllShared->OutboundQueueInterruptEnable[i] = DEFAULT_OUTBOUND_INTERRUPT_ENABLE;  /* default 1 Outbound interrupt is enabled */

    if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             OBQueueInterruptEnable, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->OutboundQueueInterruptEnable[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->OutboundQueueInterruptEnable[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: queue number %d interrupt enable %d\n", i, tdsaAllShared->OutboundQueueInterruptEnable[i]));
      }
    }

    /**********************************************/
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

  }/* end of loop */



  /********************READ CARD SPECIFIC *******************************************************/

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  for (i=0;i<QueueConfig->numInboundQueues;i++)
  {
    osti_sprintf(IBQueueSize,"IBQueueNumElements%d", i);
    osti_sprintf(IBQueueEleSize,"IBQueueElementSize%d", i);
    osti_sprintf(IBQueuePriority,"IBQueuePriority%d", i);

    /*
     * read the IBQueueSize
     */

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    if ((ostiGetTransportParam(
                             tiRoot,
                             cardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             IBQueueSize, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->InboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->InboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: queue number %d IB queue size %d\n", i, tdsaAllShared->InboundQueueSize[i]));
      }
    }

    /*
     * read the IBQueueEleSize
     */

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    if ((ostiGetTransportParam(
                             tiRoot,
                             cardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             IBQueueEleSize, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->InboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->InboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: queue number %d IB queue ele size %d\n", i, tdsaAllShared->InboundQueueEleSize[i]));
      }
    }

    /*
     * read the IBQueuePriority
     */

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    if ((ostiGetTransportParam(
                             tiRoot,
                             cardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             IBQueuePriority, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->InboundQueuePriority[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->InboundQueuePriority[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: card number %s queue number %d priority %d\n", cardNum, i, tdsaAllShared->InboundQueuePriority[i]));
      }
    }

    /**********************************************/
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
  }/* end of loop */



  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  for (i=0;i<QueueConfig->numOutboundQueues;i++)
  {
    osti_sprintf(OBQueueSize,"OBQueueNumElements%d", i);
    osti_sprintf(OBQueueEleSize,"OBQueueElementSize%d", i);
    osti_sprintf(OBQueueInterruptDelay,"OBQueueInterruptDelay%d", i);
    osti_sprintf(OBQueueInterruptCount,"OBQueueInterruptCount%d", i);
    osti_sprintf(OBQueueInterruptEnable,"OBQueueInterruptEnable%d", i);

    /*
     * read the OBQueueSize
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    if ((ostiGetTransportParam(
                             tiRoot,
                             cardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             OBQueueSize, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->OutboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->OutboundQueueSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: queue number %d OB queue size %d\n", i, tdsaAllShared->OutboundQueueSize[i]));

      }
    }

    /*
     * read the OBQueueEleSize
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;


    if ((ostiGetTransportParam(
                             tiRoot,
                             cardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             OBQueueEleSize, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->OutboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->OutboundQueueEleSize[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: queue number %d OB queue ele size %d\n", i, tdsaAllShared->OutboundQueueEleSize[i]));

      }
    }

    /*
     * read the OBQueueInterruptDelay
     */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;


    if ((ostiGetTransportParam(
                             tiRoot,
                             cardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             OBQueueInterruptDelay, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->OutboundQueueInterruptDelay[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->OutboundQueueInterruptDelay[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: card number %s queue number %d interrupt delay %d\n", cardNum, i, tdsaAllShared->OutboundQueueInterruptDelay[i]));

      }
    }

    /*
     * read the OBQueueInterruptCount
     */

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    if ((ostiGetTransportParam(
                             tiRoot,
                             cardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             OBQueueInterruptCount, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->OutboundQueueInterruptCount[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->OutboundQueueInterruptCount[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: card number %s queue number %d interrupt count %d\n", cardNum, i, tdsaAllShared->OutboundQueueInterruptCount[i]));
      }
    }


    /*
     * read the OBQueueInterruptEnable
     */

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    if ((ostiGetTransportParam(
                             tiRoot,
                             cardNum,   /* key */
                             SwParmsStr,  /* subkey1 */
                             OBQueueProps,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             OBQueueInterruptEnable, /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->OutboundQueueInterruptEnable[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->OutboundQueueInterruptEnable[i] = (bit16) osti_strtoul (buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetSwConfigParams: card number %s queue number %d interrupt enable %d\n", cardNum, i, tdsaAllShared->OutboundQueueInterruptEnable[i]));
      }
    }


    /**********************************************/
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

  }/* end of loop */

  /* process event log related parameters */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "EventLogSize1", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      SwConfig->sizefEventLog1 = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->sizefEventLog1 = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "EventLogOption1", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      SwConfig->eventLog1Option = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->eventLog1Option = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "EventLogSize2", /* valueName *//* IOP size in K dWords   */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      SwConfig->sizefEventLog2 = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->sizefEventLog2 = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "EventLogOption2", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      SwConfig->eventLog2Option = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->eventLog2Option = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
  /* end of event log related parameters */

  /*
    HDA parameters
  */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "HDASupport", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      SwConfig->hostDirectAccessSupport = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->hostDirectAccessSupport = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "HDAMode", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      SwConfig->hostDirectAccessMode = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->hostDirectAccessMode = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
  /* the end of HDA parameters */

  /* FW configuration */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "FWConfig", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      SwConfig->FWConfig = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->FWConfig = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
  /* The end of FW configuration */

  /* IQ Normal priority and High priority */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "IQNQDepth", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        QueueConfig->iqNormalPriorityProcessingDepth = (bit8) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        QueueConfig->iqNormalPriorityProcessingDepth = (bit8) osti_strtoul (buffer, &pLastUsedChar, 10);
      }
    }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             IBQueueProps,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "IQHQDepth", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        QueueConfig->iqHighPriorityProcessingDepth = (bit8) osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        QueueConfig->iqHighPriorityProcessingDepth = (bit8) osti_strtoul (buffer, &pLastUsedChar, 10);
      }
    }
  /* End IQ Normal priority and High priority */

  /* Start port timeout value */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "PortTMO", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->portTMO = osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->portTMO = osti_strtoul (buffer, &pLastUsedChar, 10);
      }
    }
  /* End port timeout value */

#ifdef SA_ENABLE_TRACE_FUNCTIONS
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "TraceDestination", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      SwConfig->TraceDestination = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->TraceDestination = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: SwConfig->TraceDestination %d\n",SwConfig->TraceDestination));
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "TraceMask", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      SwConfig->TraceMask = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->TraceMask = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: SwConfig->TraceMask %d %X\n",SwConfig->TraceMask,SwConfig->TraceMask));
  }
#endif /*# SA_ENABLE_TRACE_FUNCTIONS */

#ifdef AGTIAPI_CTL
  /*
   * read the SAS Connection Time Limit parameter
   */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  tdsaAllShared->SASConnectTimeLimit = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SASCTL",    /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
      tdsaAllShared->SASConnectTimeLimit = osti_strtoul (buffer, &pLastUsedChar, 0);
    else
      tdsaAllShared->SASConnectTimeLimit = osti_strtoul (buffer, &pLastUsedChar, 10);
  }
#endif

  /* Start FCA value */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  tdsaAllShared->FCA = 1; /* No FCA by default */

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             "InitiatorParms",  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "FCA", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->FCA = osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->FCA = osti_strtoul (buffer, &pLastUsedChar, 10);
      }
    }
  /* End FCA value */

  /* Start ResetInDiscovery value */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  tdsaAllShared->ResetInDiscovery = 0; /* No ResetInDiscovery by default */

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             "InitiatorParms",  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "ResetInDiscovery", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        tdsaAllShared->ResetInDiscovery = osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        tdsaAllShared->ResetInDiscovery = osti_strtoul (buffer, &pLastUsedChar, 10);
      }
    }
  /* End ResetInDiscovery value */

  /* Start MCN value */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  tdsaAllShared->MCN = 1; /* default MCN */

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "MCN", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->MCN = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->MCN = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG6(("tdsaGetSwConfigParams: MCN %d\n", tdsaAllShared->MCN));
  }
  /* End MCN value */

  /* Start sflag value */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  tdsaAllShared->sflag = 0; /* default sflag */

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "Sflag",     /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->sflag = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->sflag = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG1(("tdsaGetSwConfigParams: sflag %d\n", tdsaAllShared->sflag));
  }
  /* End sflag value */

  /* Start enable DIF */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "enableDIF", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      enableDIF = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      enableDIF = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG6(("tdsaGetSwConfigParams: enableDIF %d\n", enableDIF));
    if (enableDIF)
    {
      SwConfig->enableDIF = agTRUE;
    }
    else
    {
      SwConfig->enableDIF = agFALSE;
    }
    TI_DBG6(("tdsaGetSwConfigParams: SwConfig->enableDIF %d\n", SwConfig->enableDIF));
  }
  /* End enable DIF */


  /* Start enable Encryption */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "enableEncryption", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      enableEncryption = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      enableEncryption = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG6(("tdsaGetSwConfigParams: enableEncryption %d\n", enableEncryption));
    if (enableEncryption)
    {
      SwConfig->enableEncryption = agTRUE;
    }
    else
    {
      SwConfig->enableEncryption = agFALSE;
    }
    TI_DBG6(("tdsaGetSwConfigParams: SwConfig->enableEncryption %d\n", SwConfig->enableEncryption));
  }
  /* End enable Encryption */

  /* Start allow connection rate change */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  tdsaAllShared->RateAdjust = 0; /* No rate adjust by default */
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "RateAdjust", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->RateAdjust = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->RateAdjust = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG6(("tdsaGetSwConfigParams: tdsaAllShared->RateAdjust %d\n", tdsaAllShared->RateAdjust));
  }
  /* End allow connection rate change */


#ifdef SA_CONFIG_MDFD_REGISTRY
  /* Start disable MDF */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "disableMDF", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      disableMDF = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      disableMDF = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG6(("tdsaGetSwConfigParams: disableMDF %d\n", disableMDF));
    if (disableMDF)
    {
      SwConfig->disableMDF = agTRUE;
    }
    else
    {
      SwConfig->disableMDF = agFALSE;
    }
    TI_DBG6(("tdsaGetSwConfigParams: SwConfig->disableMDF %d\n", SwConfig->disableMDF));
  }
  /* End disable MDF */
#endif /*SA_CONFIG_MDFD_REGISTRY*/

  /* Start IT_NEXUS_TIMEOUT */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "IT_NEXUS_TIMEOUT", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->itNexusTimeout = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->itNexusTimeout = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG1(("tdsaGetSwConfigParams: tdsaAllShared->itNexusTimeout %d\n", tdsaAllShared->itNexusTimeout));
  }
  /* End IT_NEXUS_TIMEOUT */

  /* Start stp idle time */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "STPIdleTime", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->stp_idle_time = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->stp_idle_time = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: STPIdleTime %d\n", tdsaAllShared->stp_idle_time));
  }
  /* End stp idle time */

  /* Start STP_MCT_TMO */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  tdsaAllShared->STP_MCT_TMO = 32;
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SAS_STP_MCT_TMO", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->STP_MCT_TMO = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->STP_MCT_TMO = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: STP_MCT_TMO %d\n", tdsaAllShared->STP_MCT_TMO));
  }
  /* End  STP_MCT_TMO */

  /* Start SSP_MCT_TMO */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  tdsaAllShared->SSP_MCT_TMO = 32;
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SAS_SSP_MCT_TMO", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->SSP_MCT_TMO = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->SSP_MCT_TMO = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: SSP_MCT_TMO %d\n", tdsaAllShared->SSP_MCT_TMO));
  }
  /* End  SSP_MCT_TMO */


  /* Start MAX_OPEN_TIME */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  tdsaAllShared->MAX_OPEN_TIME = 5;
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SAS_MAX_OPEN_TIME", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->MAX_OPEN_TIME = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->MAX_OPEN_TIME = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: MAX_OPEN_TIME %d\n", tdsaAllShared->MAX_OPEN_TIME));
  }
  /* End  MAX_OPEN_TIME */


  /* Start SMP_MAX_CONN_TIMER */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  tdsaAllShared->SMP_MAX_CONN_TIMER = 0xFF;
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SAS_SMP_MAX_CONN_TIMER", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->SMP_MAX_CONN_TIMER = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->SMP_MAX_CONN_TIMER = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: SMP_MAX_CONN_TIMER %d\n", tdsaAllShared->SMP_MAX_CONN_TIMER));
  }
  /* End  SMP_MAX_CONN_TIMER */

  /* Start STP_FRM_TMO */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  tdsaAllShared->STP_FRM_TMO = 0;
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SAS_STP_FRM_TMO", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->STP_FRM_TMO = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->STP_FRM_TMO = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: STP_FRM_TMO %d\n", tdsaAllShared->STP_FRM_TMO));
  }
  /* End  STP_FRM_TMO */

  /* Start MFD_OPNRJT_RTRY_INTVL */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  tdsaAllShared->MFD = 1; /* disabled  by default */
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SAS_MFD", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->MFD = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->MFD = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: MFD %d\n", tdsaAllShared->MFD));
  }
  /* End  MFD_OPNRJT_RTRY_INTVL */

  /* Start MFD_OPNRJT_RTRY_INTVL */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  tdsaAllShared->OPNRJT_RTRY_INTVL = 2;
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SAS_OPNRJT_RTRY_INTVL", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->OPNRJT_RTRY_INTVL = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->OPNRJT_RTRY_INTVL = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: OPNRJT_RTRY_INTVL %d\n", tdsaAllShared->OPNRJT_RTRY_INTVL));
  }
  /* End  MFD_OPNRJT_RTRY_INTVL */

  /* Start DOPNRJT_RTRY_TMO */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  tdsaAllShared->DOPNRJT_RTRY_TMO = 128;
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SAS_DOPNRJT_RTRY_TMO", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->DOPNRJT_RTRY_TMO = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->DOPNRJT_RTRY_TMO = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: DOPNRJT_RTRY_TMO %d\n", tdsaAllShared->DOPNRJT_RTRY_TMO));
  }
  /* End  DOPNRJT_RTRY_TMO */

  /* Start COPNRJT_RTRY_TMO */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
//  tdsaAllShared->COPNRJT_RTRY_TMO = 32;
  tdsaAllShared->COPNRJT_RTRY_TMO = 128;
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SAS_COPNRJT_RTRY_TMO", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->COPNRJT_RTRY_TMO = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->COPNRJT_RTRY_TMO = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: COPNRJT_RTRY_TMO %d\n", tdsaAllShared->COPNRJT_RTRY_TMO));
  }
  /* End  COPNRJT_RTRY_TMO */

  /* Start DOPNRJT_RTRY_THR */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
//  tdsaAllShared->DOPNRJT_RTRY_THR = 16; /* FW default */
  /*
    Making ORR bigger than IT NEXUS LOSS which is 2000000us = 2 second.
    Assuming a bigger value 3 second, 3000000/128 = 23437.5 where 128 is tdsaAllShared->DOPNRJT_RTRY_TMO
  */
  tdsaAllShared->DOPNRJT_RTRY_THR = 23438;
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SAS_DOPNRJT_RTRY_THR", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->DOPNRJT_RTRY_THR = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->DOPNRJT_RTRY_THR = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: DOPNRJT_RTRY_THR %d\n", tdsaAllShared->DOPNRJT_RTRY_THR));
  }
  /* End  DOPNRJT_RTRY_THR */

  /* Start COPNRJT_RTRY_THR */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
//  tdsaAllShared->COPNRJT_RTRY_THR = 1024; /* FW default */
  /*
    Making ORR bigger than IT NEXUS LOSS which is 2000000us = 2 second.
    Assuming a bigger value 3 second, 3000000/128 =  23437.5 where 128 is tdsaAllShared->COPNRJT_RTRY_TMO
  */
  tdsaAllShared->COPNRJT_RTRY_THR = 23438;
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SAS_COPNRJT_RTRY_THR", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->COPNRJT_RTRY_THR = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->COPNRJT_RTRY_THR = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: COPNRJT_RTRY_THR %d\n", tdsaAllShared->COPNRJT_RTRY_THR));
  }
  /* End  COPNRJT_RTRY_THR */

  /* Start MAX_AIP */
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;
  tdsaAllShared->MAX_AIP = 0x200000;
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,/* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "SAS_MAX_AIP", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->MAX_AIP = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->MAX_AIP = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG2(("tdsaGetSwConfigParams: MAX_AIP %d\n", tdsaAllShared->MAX_AIP));
  }
  /* End  MAX_AIP */

  /***********************************************************************/
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;


    /*
    typedef struct agsaMPIContext_s
    {
      bit32   MPITableType;
      bit32   offset;
      bit32   value;
    } agsaMPIContext_t;
    */

  {
    bit32 MpiContextvalue  = 0;
    SwConfig->mpiContextTable = agNULL;
    SwConfig->mpiContextTablelen = 0;

    if ((ostiGetTransportParam(
                               tiRoot,
                               globalStr,   /* key */
                               SwParmsStr,  /* subkey1 */
                               agNULL,      /* subkey2 */
                               agNULL,
                               agNULL,
                               agNULL,      /* subkey5 */
                               "MpiContext", /* valueName */
                               buffer,
                               buffLen,
                               &lenRecv
                               ) == tiSuccess) && (lenRecv != 0))
    {

      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        MpiContextvalue = osti_strtoul (buffer, &pLastUsedChar, 0);
      }
      else
      {
        MpiContextvalue =  osti_strtoul (buffer, &pLastUsedChar, 10);
      }


      if (MpiContextvalue  == 0)
      {
        SwConfig->mpiContextTable = agNULL;
        SwConfig->mpiContextTablelen = 0;
      }
      else
      {
        tdsaRoot_t    *tdsaRoot = (tdsaRoot_t *)tiRoot->tdData;
        if(agNULL != tdsaRoot)
        {
          tdsaRoot->tdsaAllShared.MpiOverride.MPITableType = 0xFF;
          tdsaRoot->tdsaAllShared.MpiOverride.offset = 0;
          tdsaRoot->tdsaAllShared.MpiOverride.value = 0;

          SwConfig->mpiContextTable = &(tdsaRoot->tdsaAllShared.MpiOverride);
          SwConfig->mpiContextTablelen = sizeof(agsaMPIContext_t);
        }

        TI_DBG1(("tdsaGetSwConfigParams: MpiContext %p Len %d\n", SwConfig->mpiContextTable, SwConfig->mpiContextTablelen));

      }
    }

    if(SwConfig->mpiContextTable != agNULL )
    {
      tdsaRoot_t    *tdsaRoot = (tdsaRoot_t *)tiRoot->tdData;
      if(agNULL != tdsaRoot)
      {
        if ((ostiGetTransportParam(
                                   tiRoot,
                                   globalStr,   /* key */
                                   SwParmsStr,  /* subkey1 */
                                   agNULL,      /* subkey2 */
                                   agNULL,
                                   agNULL,
                                   agNULL,      /* subkey5 */
                                   "MpiTableType", /* valueName */
                                   buffer,
                                   buffLen,
                                   &lenRecv
                                   ) == tiSuccess) && (lenRecv != 0))
        {
          if (osti_strncmp(buffer, "0x", 2) == 0)
          {
            tdsaRoot->tdsaAllShared.MpiOverride.MPITableType = osti_strtoul (buffer, &pLastUsedChar, 0);
          }
          else
          {
            tdsaRoot->tdsaAllShared.MpiOverride.MPITableType =  osti_strtoul (buffer, &pLastUsedChar, 10);
          }
        TI_DBG1(("tdsaGetSwConfigParams: MpiOverride.MPITableType  0x%X\n",tdsaRoot->tdsaAllShared.MpiOverride.MPITableType ));
        }

        if ((ostiGetTransportParam(
                                   tiRoot,
                                   globalStr,   /* key */
                                   SwParmsStr,  /* subkey1 */
                                   agNULL,      /* subkey2 */
                                   agNULL,
                                   agNULL,
                                   agNULL,      /* subkey5 */
                                   "MpiTableOffset", /* valueName */
                                   buffer,
                                   buffLen,
                                   &lenRecv
                                   ) == tiSuccess) && (lenRecv != 0))
        {
          if (osti_strncmp(buffer, "0x", 2) == 0)
          {
            tdsaRoot->tdsaAllShared.MpiOverride.offset = osti_strtoul (buffer, &pLastUsedChar, 0);
          }
          else
          {
            tdsaRoot->tdsaAllShared.MpiOverride.offset =  osti_strtoul (buffer, &pLastUsedChar, 10);
          }

        TI_DBG1(("tdsaGetSwConfigParams: MpiOverride.offset 0x%X\n",tdsaRoot->tdsaAllShared.MpiOverride.offset ));
        }

        if ((ostiGetTransportParam(
                                   tiRoot,
                                   globalStr,   /* key */
                                   SwParmsStr,  /* subkey1 */
                                   agNULL,      /* subkey2 */
                                   agNULL,
                                   agNULL,
                                   agNULL,      /* subkey5 */
                                   "MpiTableValue", /* valueName */
                                   buffer,
                                   buffLen,
                                   &lenRecv
                                   ) == tiSuccess) && (lenRecv != 0))
        {
          if (osti_strncmp(buffer, "0x", 2) == 0)
          {
            tdsaRoot->tdsaAllShared.MpiOverride.value = osti_strtoul (buffer, &pLastUsedChar, 0);
          }
          else
          {
            tdsaRoot->tdsaAllShared.MpiOverride.value =  osti_strtoul (buffer, &pLastUsedChar, 10);
          }
          TI_DBG1(("tdsaGetSwConfigParams: MpiOverride.value 0x%X\n",tdsaRoot->tdsaAllShared.MpiOverride.value ));
        }
      }
    }
  }
  /***********************************************************************/

#ifdef SA_ENABLE_PCI_TRIGGER

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot, 
                             globalStr,   /* key */
                             SwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL, 
                             agNULL,      /* subkey5 */
                             "PciTrigger", /* valueName */
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {

    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      SwConfig->PCI_trigger = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      SwConfig->PCI_trigger = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG1(("tdsaGetSwConfigParams: PciTrigger %d\n",SwConfig->PCI_trigger));        
  }
#endif /* SA_ENABLE_PCI_TRIGGER */


  TI_DBG6(("tdsaGetSwConfigParams: $$$$$$$$$$$$$$$$$ merge $$$$$$$$$$$$$\n"));
#if defined(SALLSDK_DEBUG)
  TI_DBG2(("tdsaGetSwConfigParams: SwConfig->sallDebugLevel %d\n", SwConfig->sallDebugLevel));
#endif

#ifdef SA_ENABLE_PCI_TRIGGER
  TI_DBG1(("tdsaGetSwConfigParams: SwConfig->PCI_trigger  0x%x   0x%x\n",SwConfig->PCI_trigger, tdsaRoot->itdsaIni->tdsaAllShared->SwConfig.PCI_trigger));
#endif /* SA_ENABLE_PCI_TRIGGER */


#ifdef AGTIAPI_CTL
  TI_DBG6(("tdsaLoLevelGetResource: SASConnectTimeLimit 0x%x\n",
           tdsaAllShared->SASConnectTimeLimit));
#endif

  return;
}

/*****************************************************************************
*! \brief  tdsaParseLinkRateMode
*
*  Purpose:  This function parses link rate and mode.
*
*  \param   LinkRate: Link rate specified by user.
*  \param   Mode: Link rate specified by user.
*
*  \return:
*           Value combined with Linkrate and Mode
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdsaParseLinkRateMode(
                      tiRoot_t *tiRoot,
                      bit32 index,
                      bit32 LinkRateRead,
                      bit32 ModeRead,
                      bit32 OpticalModeRead,
                      bit32 LinkRate,
                      bit32 Mode,
                      bit32 OpticalMode
                      )
{
  tdsaRoot_t     *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t  *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);

  TI_DBG3(("tdsaParseLinkRateMode: index 0x%X\n",index));
  TI_DBG3(("tdsaParseLinkRateMode: LinkRateRead 0x%X    LinkRate 0x%X\n",LinkRateRead,LinkRate));
  TI_DBG3(("tdsaParseLinkRateMode: ModeRead 0x%X        Mode 0x%X\n",ModeRead,Mode));
  TI_DBG3(("tdsaParseLinkRateMode: OpticalModeRead 0x%X OpticalMode 0x%X\n",OpticalModeRead,OpticalMode));


  if (LinkRateRead == agTRUE)
  {
    /* link rate */
    if (LinkRate & 0x1)
    {
      tdsaAllShared->Ports[index].agPhyConfig.phyProperties = tdsaAllShared->Ports[index].agPhyConfig.phyProperties | 0x1;
    }
    if (LinkRate & 0x2)
    {
      tdsaAllShared->Ports[index].agPhyConfig.phyProperties = tdsaAllShared->Ports[index].agPhyConfig.phyProperties | 0x2;
    }
    if (LinkRate & 0x4)
    {
      tdsaAllShared->Ports[index].agPhyConfig.phyProperties = tdsaAllShared->Ports[index].agPhyConfig.phyProperties | 0x4;
    }
    if (LinkRate & 0x8)
    {
      tdsaAllShared->Ports[index].agPhyConfig.phyProperties = tdsaAllShared->Ports[index].agPhyConfig.phyProperties | 0x8;
    }
    if (LinkRate == 0 || LinkRate > 0xF )
    {
      /* not allowed, set the rate to default 1.5 G */
      tdsaAllShared->Ports[index].agPhyConfig.phyProperties = 0;
      tdsaAllShared->Ports[index].agPhyConfig.phyProperties = tdsaAllShared->Ports[index].agPhyConfig.phyProperties | 0x1;
      TI_DBG1(("tdsaParseLinkRateMode:  LinkRate == 0 || LinkRate >= 0x%x\n",tdsaAllShared->Ports[index].agPhyConfig.phyProperties));
    }
    TI_DBG2(("tdsaParseLinkRateMode:A index 0x%x LinkRate 0x%x Mode 0x%x\n",index,LinkRate,Mode));

  }

  if ( ModeRead == agTRUE)
  {
    /* mode */
    if (Mode & 0x1)
    {
      tdsaAllShared->Ports[index].agPhyConfig.phyProperties = tdsaAllShared->Ports[index].agPhyConfig.phyProperties | 0x10;
    }
    if (Mode & 0x2)
    {
      tdsaAllShared->Ports[index].agPhyConfig.phyProperties = tdsaAllShared->Ports[index].agPhyConfig.phyProperties | 0x20;
    }
    if (Mode == 0 || Mode >= 4 )
    {
      /* not allowed, set the mode to default SAS/SATA */
      tdsaAllShared->Ports[index].agPhyConfig.phyProperties = tdsaAllShared->Ports[index].agPhyConfig.phyProperties & 0xf;
      tdsaAllShared->Ports[index].agPhyConfig.phyProperties = tdsaAllShared->Ports[index].agPhyConfig.phyProperties | 0x30;
    }
    TI_DBG2(("tdsaParseLinkRateMode:1 index 0x%x Mode 0x%x\n",index,tdsaAllShared->Ports[index].agPhyConfig.phyProperties));
  }

  if ( OpticalModeRead == agTRUE)
  {
    /* setting bit20 */
    agsaRoot_t     *agRoot = &tdsaAllShared->agRootInt;

    if (OpticalMode == 0)
    {
      TI_DBG1(("tdsaParseLinkRateMode: OpticalMode 0  phy %d phyProperties 0x%x\n",index,tdsaAllShared->Ports[index].agPhyConfig.phyProperties));
    } 
    else if(OpticalMode == 1)
    {
      if(tIsSPCV12or6G(agRoot))
      {
        TI_DBG1(("tdsaParseLinkRateMode: OpticalMode 1  phy %d phyProperties 0x%x\n",index,tdsaAllShared->Ports[index].agPhyConfig.phyProperties));
        tdsaAllShared->Ports[index].agPhyConfig.phyProperties = tdsaAllShared->Ports[index].agPhyConfig.phyProperties | (1 << 22);
      }
      else
      {
        tdsaAllShared->Ports[index].agPhyConfig.phyProperties = tdsaAllShared->Ports[index].agPhyConfig.phyProperties | (1 << 22);
        tdsaAllShared->Ports[index].agPhyConfig.phyProperties &= 0xFFFFFFF0;
        tdsaAllShared->Ports[index].agPhyConfig.phyProperties |= 0x4;
      }
    }
    else if(OpticalMode == 2 )
    {
      if(tIsSPCV12or6G(agRoot))
      {
        TI_DBG1(("tdsaParseLinkRateMode: OpticalMode 2  phy %d phyProperties 0x%x\n",index,tdsaAllShared->Ports[index].agPhyConfig.phyProperties));
        tdsaAllShared->Ports[index].agPhyConfig.phyProperties = tdsaAllShared->Ports[index].agPhyConfig.phyProperties | (1 << 20);
      }
      else
      {
        TD_ASSERT(0, "SPC optical mode 2");
      }

      TI_DBG1(("tdsaParseLinkRateMode: OpticalMode %d phy %d phyProperties 0x%x\n",OpticalMode,index,tdsaAllShared->Ports[index].agPhyConfig.phyProperties));
    }
    else
    {
       TI_DBG1(("tdsaParseLinkRateMode: OpticalMode unknown %d  phy %d phyProperties 0x%x\n",OpticalMode,index,tdsaAllShared->Ports[index].agPhyConfig.phyProperties));
    }
  }
  else
  {
    TI_DBG1(("tdsaParseLinkRateMode: OpticalMode off phy %d phyProperties 0x%x\n",index,tdsaAllShared->Ports[index].agPhyConfig.phyProperties));
  }

  TI_DBG1(("tdsaParseLinkRateMode: phy %d phyProperties 0x%x\n",index,tdsaAllShared->Ports[index].agPhyConfig.phyProperties));


  return;
}


/*****************************************************************************
*! \brief tdsaGetHwConfigParams
*
*  Purpose:  This function reads hardware configuration parameters from the
*            configuration file
*
*  \param  tiRoot:            Pointer to driver/port instance.
*
*  \return: None
*
*  \note -
*
*****************************************************************************/
osGLOBAL void
tdsaGetHwConfigParams(
                      tiRoot_t *tiRoot
                      )
{
  tdsaRoot_t     *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t  *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
  agsaHwConfig_t *HwConfig;
  char           *buffer;
  bit32          buffLen;
  bit32          lenRecv = 0;
  char           *pLastUsedChar = agNULL;
  char           tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
  char           globalStr[]     = "Global";
  char           HwParmsStr[]   = "HWParms";
  char           phyReg[10];
  int            i,j;
  agsaPhyAnalogSetupTable_t *phyRegTable;

  TI_DBG6(("tdsaGetHwConfigParams: start\n"));
  TI_DBG6(("tdsaGetHwConfigParams: tdsaRoot %p tdsaAllShared %p \n",tdsaRoot, tdsaAllShared));

  buffer = tmpBuffer;
  buffLen = sizeof(tmpBuffer);

  osti_memset(buffer, 0, buffLen);

  HwConfig = (agsaHwConfig_t *)&(tdsaAllShared->HwConfig);
  phyRegTable = (agsaPhyAnalogSetupTable_t *)&(HwConfig->phyAnalogConfig);

  osti_memset(HwConfig, 0, sizeof(agsaHwConfig_t));

  /*
    just default values
    and are overwritten later by the configuration file contents
    turning off hw control interrupt coalescing
  */
  tdsaAllShared->FWMaxPorts = DEFAULT_FW_MAX_PORTS; /* 8, applicable only to SPC not to SPCv */
  HwConfig->phyCount = TD_MAX_NUM_PHYS;
  HwConfig->hwInterruptCoalescingTimer = 1;
  HwConfig->hwInterruptCoalescingControl = 0;
  tdsaAllShared->phyCalibration = 0;
  HwConfig->hwOption = 0; /* default: PI/CI addresses are 32-bit */

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             HwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "HwIntCoalTimer", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      HwConfig->hwInterruptCoalescingTimer = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      HwConfig->hwInterruptCoalescingTimer = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             HwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "HwIntCoalControl", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      HwConfig->hwInterruptCoalescingControl = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      HwConfig->hwInterruptCoalescingControl = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  /* For hwInterruptCoalescingTimer, 0 disables interrrupt, not allowed */
  if (HwConfig->hwInterruptCoalescingControl == 1 && HwConfig->hwInterruptCoalescingTimer == 0)
  {
    HwConfig->hwInterruptCoalescingTimer = 1;
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  /* interrupt reassetion field*/
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             HwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "IntReassertionOpt", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      HwConfig->intReassertionOption = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      HwConfig->intReassertionOption = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  /* interrupt reassetion field*/
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             HwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "HwOption", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      HwConfig->hwOption = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      HwConfig->hwOption = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  /* interrupt reassetion field*/
  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             HwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "MaxFWPorts", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->FWMaxPorts = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->FWMaxPorts = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  if ((ostiGetTransportParam(
                             tiRoot,
                             globalStr,   /* key */
                             HwParmsStr,  /* subkey1 */
                             agNULL,      /* subkey2 */
                             agNULL,
                             agNULL,
                             agNULL,      /* subkey5 */
                             "phyCalibration", /* valueName */
                             buffer,
                             buffLen,
                             &lenRecv
                             ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->phyCalibration = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->phyCalibration = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;


  /* phy calibration */
  for (i=0;i<MAX_INDEX;i++)
  {
    for(j=0;j<10;j++)
    {
      osti_sprintf(phyReg,"spaReg%d%d",i,j);
      TI_DBG6(("tdsaGetHwConfigParams: phyReg %s\n", phyReg));

      if (j == 0)
      {
        if ((ostiGetTransportParam(
                               tiRoot,
                               globalStr,   /* key */
                               HwParmsStr,  /* subkey1 */
                               agNULL,      /* subkey2 */
                               agNULL,
                               agNULL,
                               agNULL,      /* subkey5 */
                               phyReg, /* valueName */
                               buffer,
                               buffLen,
                               &lenRecv
                               ) == tiSuccess) && (lenRecv != 0))
      {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister0 = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister0 = osti_strtoul (buffer, &pLastUsedChar, 10);
        }
      }

      osti_memset(buffer, 0, buffLen);
      lenRecv = 0;
      }
      else if (j == 1)
      {
        if ((ostiGetTransportParam(
                               tiRoot,
                               globalStr,   /* key */
                               HwParmsStr,  /* subkey1 */
                               agNULL,      /* subkey2 */
                               agNULL,
                               agNULL,
                               agNULL,      /* subkey5 */
                               phyReg, /* valueName */
                               buffer,
                               buffLen,
                               &lenRecv
                               ) == tiSuccess) && (lenRecv != 0))
      {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister1 = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister1 = osti_strtoul (buffer, &pLastUsedChar, 10);
        }
      }

      osti_memset(buffer, 0, buffLen);
      lenRecv = 0;
      }
      else if (j == 2)
      {
        if ((ostiGetTransportParam(
                               tiRoot,
                               globalStr,   /* key */
                               HwParmsStr,  /* subkey1 */
                               agNULL,      /* subkey2 */
                               agNULL,
                               agNULL,
                               agNULL,      /* subkey5 */
                               phyReg, /* valueName */
                               buffer,
                               buffLen,
                               &lenRecv
                               ) == tiSuccess) && (lenRecv != 0))
      {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister2 = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister2 = osti_strtoul (buffer, &pLastUsedChar, 10);
        }
      }

      osti_memset(buffer, 0, buffLen);
      lenRecv = 0;
      }
      else if (j == 3)
      {
        if ((ostiGetTransportParam(
                               tiRoot,
                               globalStr,   /* key */
                               HwParmsStr,  /* subkey1 */
                               agNULL,      /* subkey2 */
                               agNULL,
                               agNULL,
                               agNULL,      /* subkey5 */
                               phyReg, /* valueName */
                               buffer,
                               buffLen,
                               &lenRecv
                               ) == tiSuccess) && (lenRecv != 0))
      {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister3 = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister3 = osti_strtoul (buffer, &pLastUsedChar, 10);
        }
      }

      osti_memset(buffer, 0, buffLen);
      lenRecv = 0;
      }
      else if (j == 4)
      {
        if ((ostiGetTransportParam(
                               tiRoot,
                               globalStr,   /* key */
                               HwParmsStr,  /* subkey1 */
                               agNULL,      /* subkey2 */
                               agNULL,
                               agNULL,
                               agNULL,      /* subkey5 */
                               phyReg, /* valueName */
                               buffer,
                               buffLen,
                               &lenRecv
                               ) == tiSuccess) && (lenRecv != 0))
      {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister4 = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister4 = osti_strtoul (buffer, &pLastUsedChar, 10);
        }
      }

      osti_memset(buffer, 0, buffLen);
      lenRecv = 0;
      }
      else if (j == 5)
      {
        if ((ostiGetTransportParam(
                               tiRoot,
                               globalStr,   /* key */
                               HwParmsStr,  /* subkey1 */
                               agNULL,      /* subkey2 */
                               agNULL,
                               agNULL,
                               agNULL,      /* subkey5 */
                               phyReg, /* valueName */
                               buffer,
                               buffLen,
                               &lenRecv
                               ) == tiSuccess) && (lenRecv != 0))
      {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister5 = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister5 = osti_strtoul (buffer, &pLastUsedChar, 10);
        }
      }

      osti_memset(buffer, 0, buffLen);
      lenRecv = 0;
      }
      else if (j == 6)
      {
        if ((ostiGetTransportParam(
                               tiRoot,
                               globalStr,   /* key */
                               HwParmsStr,  /* subkey1 */
                               agNULL,      /* subkey2 */
                               agNULL,
                               agNULL,
                               agNULL,      /* subkey5 */
                               phyReg, /* valueName */
                               buffer,
                               buffLen,
                               &lenRecv
                               ) == tiSuccess) && (lenRecv != 0))
      {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister6 = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister6 = osti_strtoul (buffer, &pLastUsedChar, 10);
        }
      }

      osti_memset(buffer, 0, buffLen);
      lenRecv = 0;
      }
      else if (j == 7)
      {
        if ((ostiGetTransportParam(
                               tiRoot,
                               globalStr,   /* key */
                               HwParmsStr,  /* subkey1 */
                               agNULL,      /* subkey2 */
                               agNULL,
                               agNULL,
                               agNULL,      /* subkey5 */
                               phyReg, /* valueName */
                               buffer,
                               buffLen,
                               &lenRecv
                               ) == tiSuccess) && (lenRecv != 0))
      {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister7 = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister7 = osti_strtoul (buffer, &pLastUsedChar, 10);
        }
      }

      osti_memset(buffer, 0, buffLen);
      lenRecv = 0;
      }
      else if (j == 8)
      {
        if ((ostiGetTransportParam(
                               tiRoot,
                               globalStr,   /* key */
                               HwParmsStr,  /* subkey1 */
                               agNULL,      /* subkey2 */
                               agNULL,
                               agNULL,
                               agNULL,      /* subkey5 */
                               phyReg, /* valueName */
                               buffer,
                               buffLen,
                               &lenRecv
                               ) == tiSuccess) && (lenRecv != 0))
      {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister8 = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister8 = osti_strtoul (buffer, &pLastUsedChar, 10);
        }
      }

      osti_memset(buffer, 0, buffLen);
      lenRecv = 0;
      }
      else if (j == 9)
      {
        if ((ostiGetTransportParam(
                               tiRoot,
                               globalStr,   /* key */
                               HwParmsStr,  /* subkey1 */
                               agNULL,      /* subkey2 */
                               agNULL,
                               agNULL,
                               agNULL,      /* subkey5 */
                               phyReg, /* valueName */
                               buffer,
                               buffLen,
                               &lenRecv
                               ) == tiSuccess) && (lenRecv != 0))
      {
        if (osti_strncmp(buffer, "0x", 2) == 0)
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister9 = osti_strtoul (buffer, &pLastUsedChar, 0);
        }
        else
        {
          phyRegTable->phyAnalogSetupRegisters[i].spaRegister9 = osti_strtoul (buffer, &pLastUsedChar, 10);
        }
      }

      osti_memset(buffer, 0, buffLen);
      lenRecv = 0;
      }

    } /* inner loop */
  } /* outer loop */
  return;
}
/*****************************************************************************
*! \brief tdsaGetCardPhyParams
*
*  Purpose:  This function reads phy-related configuration parameters from the
*            configuration file
*
*  \param  tiRoot:            Pointer to driver/port instance.
*
*  \return: None
*
*  \note - just a place holder for now
*
*****************************************************************************/
osGLOBAL void
tdsaGetCardPhyParams(
                 tiRoot_t *tiRoot
                 )
{
  tdsaRoot_t     *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t  *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
  agsaRoot_t     *agRoot = &tdsaAllShared->agRootInt;
  char           *buffer;
  bit32          buffLen;
  bit32          lenRecv = 0;
  char           *pLastUsedChar = agNULL;
  char           tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
  char           *globalStr = tdsaAllShared->CardIDString;
  char           phyParmsStr[12];
  int            i;
  bit32          LinkRate = 15, Mode = 3, OpticalMode = 0; //VG
  bit32          LinkRateRead = agTRUE, ModeRead = agFALSE, OpticalModeRead = agFALSE;
  bit32          flag = agFALSE; /* true only for PM8008 or PM8009 (SPCv and SPCve) controller */

  TI_DBG6(("tdsaGetCardPhyParams: start \n"));
  TI_DBG6(("tdsaGetCardPhyParams: tdsaRoot %p tdsaAllShared %p \n", tdsaRoot,tdsaAllShared));

  if (tiIS_8PHY(agRoot))
  {
    TI_DBG6(("tdsaGetCardPhyParams: SPCv or SPCve \n"));
    flag = agTRUE;
  }
  TI_DBG6(("tdsaGetCardPhyParams: flag %d\n", flag));

#ifdef REMOVED
#ifdef FPGA_CARD
  for (i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    /* setting default phy properties */
    OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
    OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x05060708);
    /* 1.5G only, SAS/SATA, no spin-up control */
    tdsaAllShared->Ports[i].agPhyConfig.phyProperties = 0x31; /* 49 */
  }
#else
#ifdef INITIATOR_DRIVER
  /* ASIC */
  for (i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    /* setting default phy properties */
    OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
    OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x05060708);
    /* 1.5G/3G , SAS/SATA, no spin-up control */
    tdsaAllShared->Ports[i].agPhyConfig.phyProperties = 0x37; /* 55 */
    TI_DBG6(("tdsaGetCardPhyParams: phy %d hi 0x%x lo 0x%x\n", i, SA_IDFRM_GET_SAS_ADDRESSHI(&(tdsaAllShared->Ports[i].SASID)), SA_IDFRM_GET_SAS_ADDRESSLO(&(tdsaAllShared->Ports[i].SASID))));
  }
#endif

#ifdef TARGET_DRIVER
  /* ASIC */
  for (i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    /* setting default phy properties */
    OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
    OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x05050500+i);
    /* 1.5G/3G , SAS/SATA, no spin-up control */
    tdsaAllShared->Ports[i].agPhyConfig.phyProperties = 0x37; /* 55 */
    TI_DBG6(("tdsaGetCardPhyParams: phy %d hi 0x%x lo 0x%x\n", i, SA_IDFRM_GET_SAS_ADDRESSHI(&(tdsaAllShared->Ports[i].SASID)), SA_IDFRM_GET_SAS_ADDRESSLO(&(tdsaAllShared->Ports[i].SASID))));
  }
#endif

#endif
#endif /* REMOVED */


  buffer = tmpBuffer;
  buffLen = sizeof(tmpBuffer);

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  for (i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    if (flag == agFALSE)
    {
      osti_sprintf(phyParmsStr,"PhyParms%d", i);
    }
    else
    {
      if (i >= 4)
      {
        osti_sprintf(phyParmsStr,"PhyParms%d", i+4);
      }
      else
      {
        osti_sprintf(phyParmsStr,"PhyParms%d", i);
      }
    }

    TI_DBG6(("tdsaGetCardPhyParams: i %d PhyParms %s\n", i, phyParmsStr));

    TI_DBG2(("tdsaGetCardPhyParams: phy %d phyProperties %d\n", i, tdsaAllShared->Ports[i].agPhyConfig.phyProperties));


    if ((ostiGetTransportParam (
                                tiRoot,
                                globalStr,
                                phyParmsStr,
                                agNULL,
                                agNULL,
                                agNULL,
                                agNULL,
                                "AddrHi",
                                buffer,
                                buffLen,
                                &lenRecv
                                ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, osti_strtoul(buffer, &pLastUsedChar, 0));
        TI_DBG6(("tdsaGetCardPhyParams: phy %d hi 0x%x \n", i, SA_IDFRM_GET_SAS_ADDRESSHI(&(tdsaAllShared->Ports[i].SASID))));
      }
      else
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, osti_strtoul(buffer, &pLastUsedChar, 10));
        TI_DBG6(("tdsaGetCardPhyParams: phy %d hi %d \n", i, SA_IDFRM_GET_SAS_ADDRESSHI(&(tdsaAllShared->Ports[i].SASID))));
      }
    }


    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    if ((ostiGetTransportParam (
                                tiRoot,
                                globalStr,
                                phyParmsStr,
                                agNULL,
                                agNULL,
                                agNULL,
                                agNULL,
                                "AddrLow",
                                buffer,
                                buffLen,
                                &lenRecv
                                ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, osti_strtoul(buffer, &pLastUsedChar, 0));
        TI_DBG6(("tdsaGetCardPhyParams: phy %d lo 0x%x\n", i, SA_IDFRM_GET_SAS_ADDRESSLO(&(tdsaAllShared->Ports[i].SASID))));
      }
      else
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, osti_strtoul(buffer, &pLastUsedChar, 10));
        TI_DBG6(("tdsaGetCardPhyParams: phy %d lo %d\n", i, SA_IDFRM_GET_SAS_ADDRESSLO(&(tdsaAllShared->Ports[i].SASID))));
      }
    }
    TI_DBG6(("tdsaGetCardPhyParams: loop phy %d hi 0x%x lo 0x%x\n", i, SA_IDFRM_GET_SAS_ADDRESSHI(&(tdsaAllShared->Ports[i].SASID)), SA_IDFRM_GET_SAS_ADDRESSLO(&(tdsaAllShared->Ports[i].SASID))));

    /* phy properties */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
/*
    if ((ostiGetTransportParam (
                                tiRoot,
                                globalStr,
                                phyParmsStr,
                                agNULL,
                                agNULL,
                                agNULL,
                                agNULL,
                                "LinkRate",
                                buffer,
                                buffLen,
                                &lenRecv
                                ) == tiSuccess) && (lenRecv != 0))
    {
      LinkRateRead = agTRUE;
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        LinkRate = osti_strtoul(buffer, &pLastUsedChar, 0);
        TI_DBG6(("tdsaGetCardPhyParams: phy %d linkrate 0x%x \n", i, LinkRate));
      }
      else
      {
        LinkRate = osti_strtoul(buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetCardPhyParams: phy %d linkrate %d \n", i, LinkRate));
      }
    }

    TI_DBG2(("tdsaGetCardPhyParams: phy %d linkrate %d \n", i, LinkRate));
*/

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    if ((ostiGetTransportParam (
                                tiRoot,
                                globalStr,
                                phyParmsStr,
                                agNULL,
                                agNULL,
                                agNULL,
                                agNULL,
                                "Mode",
                                buffer,
                                buffLen,
                                &lenRecv
                                ) == tiSuccess) && (lenRecv != 0))
    {
      ModeRead = agTRUE;
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        Mode = osti_strtoul(buffer, &pLastUsedChar, 0);
        TI_DBG6(("tdsaGetCardPhyParams: phy %d Mode 0x%x \n", i, Mode));
      }
      else
      {
        Mode = osti_strtoul(buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetCardPhyParams: phy %d Mode %d \n", i, Mode));
      }
    }

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    if ((ostiGetTransportParam (
                                tiRoot,
                                globalStr,
                                phyParmsStr,
                                agNULL,
                                agNULL,
                                agNULL,
                                agNULL,
                                "OpticalMode",
                                buffer,
                                buffLen,
                                &lenRecv
                                ) == tiSuccess) && (lenRecv != 0))
    {
      OpticalModeRead = agTRUE;
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        OpticalMode = osti_strtoul(buffer, &pLastUsedChar, 0);
        TI_DBG6(("tdsaGetCardPhyParams: phy %d OpticalMode 0x%x \n", i, OpticalMode));
      }
      else
      {
        OpticalMode = osti_strtoul(buffer, &pLastUsedChar, 10);
        TI_DBG6(("tdsaGetCardPhyParams: phy %d OpticalMode %d \n", i, OpticalMode));
      }
    }

    tdsaParseLinkRateMode(tiRoot, i, LinkRateRead, ModeRead, OpticalModeRead, LinkRate, Mode, OpticalMode);

    TI_DBG2(("tdsaGetCardPhyParams: phy %d phyProperties %d\n", i, tdsaAllShared->Ports[i].agPhyConfig.phyProperties));


    /**********************************************/
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
    LinkRateRead = agTRUE;//VG
    ModeRead = agFALSE;
    OpticalModeRead = agFALSE;

  } /* end for */
  return;
}





/*****************************************************************************
*! \brief tdsaGetGlobalPhyParams
*
*  Purpose:  This function reads phy-related configuration parameters from the
*            configuration file
*
*  \param  tiRoot:            Pointer to driver/port instance.
*
*  \return: None
*
*  \note - just a place holder for now
*
*****************************************************************************/
osGLOBAL void
tdsaGetGlobalPhyParams(
                 tiRoot_t *tiRoot
                 )
{
  tdsaRoot_t     *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t  *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
  agsaRoot_t     *agRoot = &tdsaAllShared->agRootInt;
  char           *buffer;
  bit32          buffLen;
  bit32          lenRecv = 0;
  char           *pLastUsedChar = agNULL;
  char           tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
  char           globalStr[]     = "Global";
  char           phyParmsStr[12];
  int            i;
  bit32          LinkRate = 15/*7*/, Mode = 3, OpticalMode = 0;
  bit32          LinkRateRead = agFALSE, ModeRead = agFALSE, OpticalModeRead = agFALSE;
  bit32          flag = agFALSE; /* true only for PM8008 or PM8009 (SPCv and SPCve) controller */

  TI_DBG6(("tdsaGetGlobalPhyParams: start \n"));
  TI_DBG6(("tdsaGetGlobalPhyParams: tdsaRoot %p tdsaAllShared %p \n", tdsaRoot,tdsaAllShared));

  if (tiIS_8PHY(agRoot) )
  {
    TI_DBG6(("tdsaGetGlobalPhyParams: SPCv or SPCve \n"));
    flag = agTRUE;
  }

  TI_DBG6(("tdsaGetGlobalPhyParams: flag %d\n", flag));

#ifdef FPGA_CARD
  for (i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    /* setting default phy properties */
    OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
    OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x05060708);
    /* 1.5G only, SAS/SATA, no spin-up control */
    tdsaAllShared->Ports[i].agPhyConfig.phyProperties = 0x31; /* 49 */
  }
#else
  /* ASIC */
#ifdef INITIATOR_DRIVER
  for (i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    /* setting default phy properties */
    if (flag == agFALSE) /* SPC or SPCv+ */
    {
      if (0 <= i && i <= 7)
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x05060708);
      }
      else
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01010101);
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x02020202);
      }
    }
    else /* SPCv or SPCve */
    {
      if (0 <= i && i <= 3)
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x05060708);
      }
      else if (4 <= i && i <= 7)
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x02020202);
      }
      else /* don't care */
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01010101);
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x0f0f0f0f);
      }

    }
    /* 1.5G/3G , SAS/SATA, no spin-up control */
    tdsaAllShared->Ports[i].agPhyConfig.phyProperties = 0x31; /* 55 */
    TI_DBG6(("tdsaGetGlobalPhyParams: phy %d hi 0x%x lo 0x%x\n", i, SA_IDFRM_GET_SAS_ADDRESSHI(&(tdsaAllShared->Ports[i].SASID)), SA_IDFRM_GET_SAS_ADDRESSLO(&(tdsaAllShared->Ports[i].SASID))));

  }
#endif
#ifdef TARGET_DRIVER
   for (i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    /* setting default phy properties */
    /* SPC; narrow ports; 8 ports
       SPCv, SPCve wide port; 8 ports
       SPCv+ wide port; 16 ports
    */
    if (tiIS_SPC(agRoot))
    {
       if (0 <= i && i <= 7)
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x05050500+i);
      }
      else
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x06060600+i);
      }
    }
    else if (tiIS_16PHY(agRoot))
    {
       if (0 <= i && i <= 7)
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x05050500+i);
      }
      else
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x06060600+i);
      }
    }
    else
    {
      if (0 <= i && i <= 3)
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x05050500+i);
      }
      else if (4 <= i && i <= 7)
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x06060600+i);
      }
      else /* don't care */
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, 0x01020304);
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, 0x0f0f0f0f+i);
      }
    }
    /* 1.5G/3G , SAS/SATA, no spin-up control */
    tdsaAllShared->Ports[i].agPhyConfig.phyProperties = 0x31; /* 49 The default is 1.5G and will be changed based on the registry value */
    TI_DBG6(("tdsaGetGlobalPhyParams: phy %d hi 0x%x lo 0x%x\n", i, SA_IDFRM_GET_SAS_ADDRESSHI(&(tdsaAllShared->Ports[i].SASID)), SA_IDFRM_GET_SAS_ADDRESSLO(&(tdsaAllShared->Ports[i].SASID))));

  }
#endif
#endif


  buffer = tmpBuffer;
  buffLen = sizeof(tmpBuffer);

  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  /* needs to read Phy's id frame */
  for (i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    if (flag == agFALSE)
    {
      osti_sprintf(phyParmsStr,"PhyParms%d", i);
    }
    else
    {
      if (i >= 4)
      {
        osti_sprintf(phyParmsStr,"PhyParms%d", i+4);
      }
      else
      {
        osti_sprintf(phyParmsStr,"PhyParms%d", i);
      }
    }

    TI_DBG6(("tdsaGetGlobalPhyParams: i %d PhyParms %s\n", i, phyParmsStr));


    if ((ostiGetTransportParam (
                                tiRoot,
                                globalStr,
                                phyParmsStr,
                                agNULL,
                                agNULL,
                                agNULL,
                                agNULL,
                                "AddrHi",
                                buffer,
                                buffLen,
                                &lenRecv
                                ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, osti_strtoul(buffer, &pLastUsedChar, 0));
        TI_DBG6(("tdsaGetGlobalPhyParams: phy %d hi 0x%x \n", i, SA_IDFRM_GET_SAS_ADDRESSHI(&(tdsaAllShared->Ports[i].SASID))));
      }
      else
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressHi, 0, osti_strtoul(buffer, &pLastUsedChar, 10));
        TI_DBG6(("tdsaGetGlobalPhyParams: phy %d hi %d \n", i, SA_IDFRM_GET_SAS_ADDRESSHI(&(tdsaAllShared->Ports[i].SASID))));

      }
    }


    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    if ((ostiGetTransportParam (
                                tiRoot,
                                globalStr,
                                phyParmsStr,
                                agNULL,
                                agNULL,
                                agNULL,
                                agNULL,
                                "AddrLow",
                                buffer,
                                buffLen,
                                &lenRecv
                                ) == tiSuccess) && (lenRecv != 0))
    {
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, osti_strtoul(buffer, &pLastUsedChar, 0));
        TI_DBG6(("tdsaGetGlobalPhyParams: phy %d lo 0x%x\n", i, SA_IDFRM_GET_SAS_ADDRESSLO(&(tdsaAllShared->Ports[i].SASID))));
      }
      else
      {
        OSSA_WRITE_BE_32(agRoot, tdsaAllShared->Ports[i].SASID.sasAddressLo, 0, osti_strtoul(buffer, &pLastUsedChar, 10));
        TI_DBG6(("tdsaGetGlobalPhyParams: phy %d lo %d\n", i, SA_IDFRM_GET_SAS_ADDRESSLO(&(tdsaAllShared->Ports[i].SASID))));

      }
    }
    TI_DBG6(("tdsaGetGlobalPhyParams: loop phy %d hi 0x%x lo 0x%x\n", i, SA_IDFRM_GET_SAS_ADDRESSHI(&(tdsaAllShared->Ports[i].SASID)), SA_IDFRM_GET_SAS_ADDRESSLO(&(tdsaAllShared->Ports[i].SASID))));

    /* phy properties */
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
/*
    if ((ostiGetTransportParam (
                                tiRoot,
                                globalStr,
                                phyParmsStr,
                                agNULL,
                                agNULL,
                                agNULL,
                                agNULL,
                                "LinkRate",
                                buffer,
                                buffLen,
                                &lenRecv
                                ) == tiSuccess) && (lenRecv != 0))
    {
      LinkRateRead = agTRUE;
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        LinkRate = osti_strtoul(buffer, &pLastUsedChar, 0);
        TI_DBG2(("tdsaGetGlobalPhyParams: phy %d linkrate 0x%x \n", i, LinkRate));
      }
      else
      {
        LinkRate = osti_strtoul(buffer, &pLastUsedChar, 10);
        TI_DBG2(("tdsaGetGlobalPhyParams: phy %d linkrate %d \n", i, LinkRate));
      }
    }
*/

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    if ((ostiGetTransportParam (
                                tiRoot,
                                globalStr,
                                phyParmsStr,
                                agNULL,
                                agNULL,
                                agNULL,
                                agNULL,
                                "Mode",
                                buffer,
                                buffLen,
                                &lenRecv
                                ) == tiSuccess) && (lenRecv != 0))
    {
      ModeRead = agTRUE;
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        Mode = osti_strtoul(buffer, &pLastUsedChar, 0);
        TI_DBG2(("tdsaGetGlobalPhyParams: phy %d Mode 0x%x \n", i, Mode));
      }
      else
      {
        Mode = osti_strtoul(buffer, &pLastUsedChar, 10);
        TI_DBG2(("tdsaGetGlobalPhyParams: phy %d Mode %d \n", i, Mode));
      }
    }

    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;

    if ((ostiGetTransportParam (
                                tiRoot,
                                globalStr,
                                phyParmsStr,
                                agNULL,
                                agNULL,
                                agNULL,
                                agNULL,
                                "OpticalMode",
                                buffer,
                                buffLen,
                                &lenRecv
                                ) == tiSuccess) && (lenRecv != 0))
    {
      OpticalModeRead = agTRUE;
      if (osti_strncmp(buffer, "0x", 2) == 0)
      {
        OpticalMode = osti_strtoul(buffer, &pLastUsedChar, 0);
        TI_DBG2(("tdsaGetGlobalPhyParams: phy %d OpticalMode 0x%x \n", i, OpticalMode));
      }
      else
      {
        OpticalMode = osti_strtoul(buffer, &pLastUsedChar, 10);
        TI_DBG2(("tdsaGetGlobalPhyParams: phy %d OpticalMode %d \n", i, OpticalMode));
      }
    }

    TI_DBG2(("tdsaGetGlobalPhyParams:A phy %d phyProperties %d\n", i, tdsaAllShared->Ports[i].agPhyConfig.phyProperties));
    tdsaParseLinkRateMode(tiRoot, i, LinkRateRead, ModeRead, OpticalModeRead, LinkRate, Mode, OpticalMode);

    TI_DBG2(("tdsaGetGlobalPhyParams:B phy %d phyProperties %d\n", i, tdsaAllShared->Ports[i].agPhyConfig.phyProperties));



    /**********************************************/
    osti_memset(buffer, 0, buffLen);
    lenRecv = 0;
    /* restore default */
    LinkRate = 15;
    Mode = 3;
    OpticalMode = 0;
    LinkRateRead = agTRUE;//VG
    ModeRead = agFALSE;
    OpticalModeRead = agFALSE;


  } /* end for */

  return;
}

/*****************************************************************************
*! \brief  tdsaGetPortParams
*
*  Purpose:  This function reads port-related configuration parameters from the
*            configuration file
*
*  \param  tiRoot:            Pointer to driver/port instance.
*
*  \return:     None
*
*  \note - just a place holder for now
*
*****************************************************************************/
osGLOBAL void
tdsaGetPortParams(
                  tiRoot_t *tiRoot
                  )
{
  tdsaRoot_t     *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t  *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);


  char    *buffer;
  bit32   buffLen;
  bit32   lenRecv = 0;
  char    *pLastUsedChar = agNULL;
  char    tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
  char    globalStr[]     = "Global";
  char    portParmsStr[] = "PortParms";

  TI_DBG6(("tdsaGetPortParams: start \n"));
  TI_DBG6(("tdsaGetPortParams: tdsaRoot %p tdsaAllShared %p \n", tdsaRoot,tdsaAllShared));

  buffer = tmpBuffer;
  buffLen = sizeof(tmpBuffer);
  osti_memset(buffer, 0, buffLen);

  if ((ostiGetTransportParam (
                              tiRoot,
                              globalStr,
                              portParmsStr,
                              agNULL,
                              agNULL,
                              agNULL,
                              agNULL,
                              "InterruptDelay",
                              buffer,
                              buffLen,
                              &lenRecv
                              ) == tiSuccess) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    {
      tdsaAllShared->currentInterruptDelay = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      tdsaAllShared->currentInterruptDelay = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
    TI_DBG6(("tdsaGetPortParams: in \n"));
  }
  osti_memset(buffer, 0, buffLen);
  lenRecv = 0;

  TI_DBG6(("tdsaGetPortParams: out \n"));

  /* and more .... */


  return;
}

#ifdef FW_EVT_LOG_TST
void saLogDump(agsaRoot_t *agRoot,
               U32    *eventLogSize,
               U32   **eventLogAddress);

void tiLogDump(tiRoot_t *tiRoot, U32 *size, U32 **addr)
{
  tdsaRoot_t    *tdsaRoot;
  tdsaContext_t *tdsaAllShared;

  tdsaRoot = (tdsaRoot_t*)tiRoot->tdData;
  tdsaAllShared = (tdsaContext_t*)&(tdsaRoot->tdsaAllShared);
  saLogDump(&tdsaAllShared->agRootNonInt, size, addr);
}
#endif



/*****************************************************************************
*! \brief  tiCOMPortInit
*
*  Purpose: This function is called to initialize the port hardware.
*           This call could only be called until after the successful
*           completion tiCOMInit().
*
*  \param   tiRoot:         Pointer to root data structure.
*  \param   sysIntsActive:  system interrupt flag
*
*  \return:
*           tiSuccess:      Successful.
*           Others:             Fail.
*
*  \note -
*
*****************************************************************************/
osGLOBAL bit32
tiCOMPortInit(
              tiRoot_t      *tiRoot,
              bit32         sysIntsActive
              )
{
  tdsaRoot_t          *tdsaRoot;
  tdsaContext_t       *tdsaAllShared;
  agsaRoot_t          *agRoot;
  tiLoLevelResource_t *loResource;
  bit32                status = tiError;
  bit32                i;

  agsaQueueConfig_t   *QueueConfig;

#ifdef CONTROLLER_STATUS_TESTING
  static agsaControllerStatus_t  agcontrollerStatus;
#endif /* CONTROLLER_STATUS_TESTING */

#ifdef CONTROLLER_INFO_TESTING
  static agsaControllerInfo_t  agcontrollerInfo;
#endif /* CONTROLLER_INFO_TESTING */

#ifdef CONTROLLER_ENCRYPT_TESTING
  static  agsaEncryptInfo_t       agsaEncryptInfo;
#endif /* CONTROLLER_INFO_TESTING */

  static agsaMemoryRequirement_t agMemoryRequirement;
#ifdef ECHO_TESTING
  /* temp */
  static   bit8                     payload[56];
#endif

#if defined(FDS_DM) || defined(FDS_SM)
  static agsaMemoryRequirement_t memRequirement;
  bit32                          maxQueueSets = 0;
  bit32                          LLMemCount = 0;
  bit32                          usecsPerTick = 0;
  static agsaSwConfig_t          tmpLLSwConfig;
#endif

#ifdef FDS_DM
   static  dmRoot_t                       *dmRoot = agNULL;
#ifdef FDS_SM
   static dmSwConfig_t                   dmSwConfig;
#endif
  static dmMemoryRequirement_t   dmMemRequirement;
  bit32                          DMMemCount = 0;
#endif

#if defined(FDS_DM) && defined(FDS_SM)
  bit32                          dmUsecsPerTick = 0;
  bit32                          dmMaxNumLocks = 0;
#endif

#ifdef FDS_SM
  smRoot_t                       *smRoot = agNULL;
//  smSwConfig_t                   smSwConfig;
  static smMemoryRequirement_t   smMemRequirement;
  bit32                          SMMemCount = 0;
#endif

#ifndef TURN_OFF_HDA
  static agsaFwImg_t                    HDAImg;
#endif /*  TURN_OFF_HDA */

  TI_DBG3(("tiCOMPortInit: start\n"));
  TI_DBG6(("tiCOMPortInit: sizeof agsaMemoryRequirement_t %d\n", (int)sizeof(agsaMemoryRequirement_t)));

  tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);

  osti_memset(&agMemoryRequirement, 0, sizeof(agsaMemoryRequirement_t));
  /*
   * don't do anything if reset is in progress
   */
  if (tdsaAllShared->flags.resetInProgress == agTRUE)
  {
    TI_DBG1(("tiCOMPortInit: resetinProgress error\n"));
    return tiError;
  }

  loResource = &(tdsaAllShared->loResource);

  agRoot = &(tdsaAllShared->agRootNonInt);
  tdsaAllShared->flags.sysIntsActive    = sysIntsActive;

  /*
     gets port-related parameters; not in use for now
     tdsaGetPortParams(tiRoot);
   */

  /* call these before agroot is created  for testing */


#ifdef CONTROLLER_STATUS_TESTING
  TI_DBG1(("tiCOMPortInit: saGetControllerStatus returns 0x%X\n",saGetControllerStatus(agRoot,&agcontrollerStatus ) ));
#endif /* CONTROLLER_INFO_TESTING */

#ifdef CONTROLLER_INFO_TESTING
  TI_DBG1(("tiCOMPortInit: saGetControllerInfo returns 0x%X\n",saGetControllerInfo(agRoot,&agcontrollerInfo ) ));
#endif /* CONTROLLER_INFO_TESTING */

#ifdef CONTROLLER_ENCRYPT_TESTING
  TI_DBG1(("tiCOMPortInit: saEncryptGetMode returns 0x%X\n",saEncryptGetMode(agRoot,agNULL, &agsaEncryptInfo ) ));
#endif /* CONTROLLER_INFO_TESTING */


  tdsaGetSwConfigParams(tiRoot);
  tdsaPrintSwConfig(&(tdsaAllShared->SwConfig));

  /* setting interrupt requirements */
  tdsaAllShared->SwConfig.max_MSIX_InterruptVectors = loResource->loLevelOption.maxInterruptVectors;
  tdsaAllShared->SwConfig.max_MSI_InterruptVectors = loResource->loLevelOption.max_MSI_InterruptVectors;
  tdsaAllShared->SwConfig.legacyInt_X = loResource->loLevelOption.flag;
  TI_DBG2(("tiCOMPortInit: got max_MSIX_InterruptVectors %d \n", tdsaAllShared->SwConfig.max_MSIX_InterruptVectors));
  TI_DBG2(("tiCOMPortInit: got max_MSI_InterruptVectors %d \n", tdsaAllShared->SwConfig.max_MSI_InterruptVectors));
  TI_DBG2(("tiCOMPortInit: got flag - legacyInt_X %d \n", tdsaAllShared->SwConfig.legacyInt_X));

  /* error checking for interrupt types */
  if (
      ((tdsaAllShared->SwConfig.max_MSIX_InterruptVectors == 0) &&
       (tdsaAllShared->SwConfig.max_MSI_InterruptVectors == 0)  &&
       (tdsaAllShared->SwConfig.legacyInt_X == 0))
      ||
      ((tdsaAllShared->SwConfig.max_MSIX_InterruptVectors != 0) &&
       (tdsaAllShared->SwConfig.max_MSI_InterruptVectors == 0)  &&
       (tdsaAllShared->SwConfig.legacyInt_X == 0))
      ||
      ((tdsaAllShared->SwConfig.max_MSIX_InterruptVectors == 0) &&
       (tdsaAllShared->SwConfig.max_MSI_InterruptVectors != 0)  &&
       (tdsaAllShared->SwConfig.legacyInt_X == 0))
      ||
      ((tdsaAllShared->SwConfig.max_MSIX_InterruptVectors == 0) &&
       (tdsaAllShared->SwConfig.max_MSI_InterruptVectors == 0)  &&
       (tdsaAllShared->SwConfig.legacyInt_X != 0))
     )
  {
    /* do nothing */
  }
  else
  {
    TI_DBG1(("tiCOMPortInit: incorrect interrupt\n"));
    return tiError;
  }

  QueueConfig = &tdsaAllShared->QueueConfig;

  for(i=0;i<QueueConfig->numInboundQueues;i++)
  {
    QueueConfig->inboundQueues[i].elementCount = tdsaAllShared->InboundQueueSize[i];
    QueueConfig->inboundQueues[i].elementSize = tdsaAllShared->InboundQueueEleSize[i];
    QueueConfig->inboundQueues[i].priority = tdsaAllShared->InboundQueuePriority[i];
    QueueConfig->inboundQueues[i].reserved = 0;
    TI_DBG6(("tiCOMPortInit: InboundQueuePriroity %d \n", tdsaAllShared->InboundQueuePriority[i]));
  }
  for(i=0;i<QueueConfig->numOutboundQueues;i++)
  {
    QueueConfig->outboundQueues[i].elementCount = tdsaAllShared->OutboundQueueSize[i];
    QueueConfig->outboundQueues[i].elementSize = tdsaAllShared->OutboundQueueEleSize[i];
    QueueConfig->outboundQueues[i].interruptDelay = tdsaAllShared->OutboundQueueInterruptDelay[i]; /* default 0; no interrupt delay */
    QueueConfig->outboundQueues[i].interruptCount = tdsaAllShared->OutboundQueueInterruptCount[i]; /* default 1 */
    QueueConfig->outboundQueues[i].interruptEnable = tdsaAllShared->OutboundQueueInterruptEnable[i]; /* default 1 */
    QueueConfig->outboundQueues[i].interruptVectorIndex = 0;
    if (tdsaAllShared->SwConfig.max_MSIX_InterruptVectors != 0)
    {
      QueueConfig->outboundQueues[i].interruptVectorIndex = i % tdsaAllShared->SwConfig.max_MSIX_InterruptVectors;
    }
    else if (tdsaAllShared->SwConfig.max_MSI_InterruptVectors != 0)
    {
      QueueConfig->outboundQueues[i].interruptVectorIndex = i % tdsaAllShared->SwConfig.max_MSI_InterruptVectors;
    }
    else
    {
      QueueConfig->outboundQueues[i].interruptVectorIndex = 0;
    }
    TI_DBG6(("tiCOMPortInit: OutboundQueueInterruptDelay %d OutboundQueueInterruptCount %d OutboundQueueInterruptEnable %d\n", tdsaAllShared->OutboundQueueInterruptDelay[i], tdsaAllShared->OutboundQueueInterruptCount[i], tdsaAllShared->OutboundQueueInterruptEnable[i]));

  }
  /* queue option */
  QueueConfig->queueOption = tdsaAllShared->QueueOption;

  tdsaAllShared->SwConfig.param3 = (void *)QueueConfig;
  tdsaAllShared->SwConfig.stallUsec = 10;

  /* finds a first high priority queue for SMP */
  tdsaAllShared->SMPQNum = 0; /* default */
  for(i=0;i<QueueConfig->numInboundQueues;i++)
  {
    if (QueueConfig->inboundQueues[i].priority != DEFAULT_INBOUND_QUEUE_PRIORITY) /* 0 */
    {
      tdsaAllShared->SMPQNum = i;
      break;
    }
  }

  tdsaGetHwConfigParams(tiRoot);

  tdsaPrintHwConfig(&(tdsaAllShared->HwConfig));

#ifdef TARGET_DRIVER
  /* target, not yet */
  if (tdsaAllShared->currentOperation & TD_OPERATION_TARGET)
  {
    ttdssGetTargetParams(tiRoot);
  }
#endif

#if defined(FDS_DM) && defined(FDS_SM)
  /*
    needs to call saGetRequirements() to find out agMemoryRequirement.count requested by LL
  */
  osti_memcpy(&tmpLLSwConfig, &(tdsaAllShared->SwConfig), sizeof(agsaSwConfig_t));

  saGetRequirements(agRoot,
                    &tmpLLSwConfig,
                    &memRequirement,
                    &usecsPerTick,
                    &maxQueueSets
                    );
  TI_DBG1(("tiCOMPortInit: usecsPerTick %d\n", usecsPerTick));

  TI_DBG1(("tiCOMPortInit: LL memRequirement.count %d\n", memRequirement.count));
  TI_DBG1(("tiCOMPortInit: loResource->loLevelMem.count %d\n", loResource->loLevelMem.count));
  LLMemCount = memRequirement.count;

  /*
    needs to call dmGetRequirements() to find out dmMemoryRequirement.count requested by DM
  */

  dmGetRequirements(dmRoot,
                    &dmSwConfig,
                    &dmMemRequirement,
                    &dmUsecsPerTick,
                    &dmMaxNumLocks
                    );

  TI_DBG1(("tiCOMPortInit: DM dmmemRequirement.count %d\n", dmMemRequirement.count));
  TI_DBG1(("tiCOMPortInit: loResource->loLevelMem.count %d\n", loResource->loLevelMem.count));

  DMMemCount = dmMemRequirement.count;
  SMMemCount = loResource->loLevelMem.count - LLMemCount - DMMemCount;
  agMemoryRequirement.count =  LLMemCount;

  TI_DBG1(("tiCOMPortInit: SMMemCount %d\n", SMMemCount));


#elif defined(FDS_DM)
  /*
    needs to call saGetRequirements() to find out agMemoryRequirement.count requested by LL
  */
  osti_memcpy(&tmpLLSwConfig, &(tdsaAllShared->SwConfig), sizeof(agsaSwConfig_t));

  saGetRequirements(agRoot,
                    &tmpLLSwConfig,
                    &memRequirement,
                    &usecsPerTick,
                    &maxQueueSets
                    );

  TI_DBG1(("tiCOMPortInit: memRequirement.count %d\n", memRequirement.count));
  TI_DBG1(("tiCOMPortInit: loResource->loLevelMem.count %d\n", loResource->loLevelMem.count));

  LLMemCount = memRequirement.count;
  DMMemCount = loResource->loLevelMem.count - LLMemCount;

  agMemoryRequirement.count =  LLMemCount;

#elif defined(FDS_SM)
  osti_memcpy(&tmpLLSwConfig, &(tdsaAllShared->SwConfig), sizeof(agsaSwConfig_t));

  saGetRequirements(agRoot,
                    &tmpLLSwConfig,
                    &memRequirement,
                    &usecsPerTick,
                    &maxQueueSets
                    );

  TI_DBG1(("tiCOMPortInit: memRequirement.count %d\n", memRequirement.count));
  TI_DBG1(("tiCOMPortInit: loResource->loLevelMem.count %d\n", loResource->loLevelMem.count));

  LLMemCount = memRequirement.count;
  SMMemCount = loResource->loLevelMem.count - LLMemCount;

  agMemoryRequirement.count =  LLMemCount;

#else

  agMemoryRequirement.count = loResource->loLevelMem.count;

#endif

#if defined(FDS_DM) && defined(FDS_SM)
  /* for debugging */
  for(i=0;i<(int)(LLMemCount + DMMemCount + SMMemCount);i++)
  {
    TI_DBG2(("tiCOMPortInit: index %d phyAddrUpper 0x%x phyAddrLower 0x%x totalLength %d alignment %d\n", i, loResource->loLevelMem.mem[i].physAddrUpper, loResource->loLevelMem.mem[i].physAddrLower, loResource->loLevelMem.mem[i].totalLength, loResource->loLevelMem.mem[i].alignment));
    TI_DBG2(("tiCOMPortInit: index %d virtPtr %p\n",i, loResource->loLevelMem.mem[i].virtPtr));
  }
#endif

  /* initialize */
  TI_DBG6(("tiCOMPortInit: AGSA_NUM_MEM_CHUNKS %d\n", AGSA_NUM_MEM_CHUNKS));
  for(i=0;i<AGSA_NUM_MEM_CHUNKS;i++)
  {
    agMemoryRequirement.agMemory[i].virtPtr = agNULL;
    agMemoryRequirement.agMemory[i].osHandle = agNULL;
    agMemoryRequirement.agMemory[i].phyAddrUpper = 0;
    agMemoryRequirement.agMemory[i].phyAddrLower = 0;
    agMemoryRequirement.agMemory[i].totalLength = 0;
    agMemoryRequirement.agMemory[i].numElements = 0;
    agMemoryRequirement.agMemory[i].singleElementLength = 0;
    agMemoryRequirement.agMemory[i].alignment = 0;
    agMemoryRequirement.agMemory[i].type = 0;
    agMemoryRequirement.agMemory[i].reserved = 0;
  }

  for(i=0;i<(int)agMemoryRequirement.count;i++)
  {
    TI_DBG2(("tiCOMPortInit: LL copying loResource.loLevelMem to agsaMemoryRequirement_t index %d\n", i));
    agMemoryRequirement.agMemory[i].virtPtr = loResource->loLevelMem.mem[i].virtPtr;
    agMemoryRequirement.agMemory[i].osHandle = loResource->loLevelMem.mem[i].osHandle;
    agMemoryRequirement.agMemory[i].phyAddrUpper = loResource->loLevelMem.mem[i].physAddrUpper;
    agMemoryRequirement.agMemory[i].phyAddrLower = loResource->loLevelMem.mem[i].physAddrLower;
    agMemoryRequirement.agMemory[i].totalLength = loResource->loLevelMem.mem[i].totalLength;
    agMemoryRequirement.agMemory[i].numElements = loResource->loLevelMem.mem[i].numElements;
    agMemoryRequirement.agMemory[i].singleElementLength = loResource->loLevelMem.mem[i].singleElementLength;
    agMemoryRequirement.agMemory[i].alignment = loResource->loLevelMem.mem[i].alignment;
    if (loResource->loLevelMem.mem[i].type == TI_DMA_MEM)
    {
      agMemoryRequirement.agMemory[i].type = AGSA_DMA_MEM;
    }
    else if (loResource->loLevelMem.mem[i].type == TI_CACHED_MEM)
    {
      agMemoryRequirement.agMemory[i].type = AGSA_CACHED_MEM;

    }
    else if (loResource->loLevelMem.mem[i].type == TI_CACHED_DMA_MEM)
    {
      agMemoryRequirement.agMemory[i].type = AGSA_CACHED_DMA_MEM;
    }

    agMemoryRequirement.agMemory[i].reserved = loResource->loLevelMem.mem[i].reserved;
    TI_DBG2(("tiCOMPortInit: index %d virtPtr %p osHandle %p\n",i, loResource->loLevelMem.mem[i].virtPtr, loResource->loLevelMem.mem[i].osHandle));
    TI_DBG2(("tiCOMPortInit: index %d phyAddrUpper 0x%x phyAddrLower 0x%x totalLength %d numElements %d\n", i,
    loResource->loLevelMem.mem[i].physAddrUpper,
    loResource->loLevelMem.mem[i].physAddrLower,
    loResource->loLevelMem.mem[i].totalLength,
    loResource->loLevelMem.mem[i].numElements));
    TI_DBG2(("tiCOMPortInit: index %d singleElementLength 0x%x alignment 0x%x type %d reserved %d\n", i,
    loResource->loLevelMem.mem[i].singleElementLength,
    loResource->loLevelMem.mem[i].alignment,
    loResource->loLevelMem.mem[i].type,
    loResource->loLevelMem.mem[i].reserved));

  }
  osti_memset(&(tdsaAllShared->tdFWControlEx), 0, sizeof(tdFWControlEx_t));

  /*
   * Note: Be sure to call this only once since sallsdk does initialization only once
   * saInitialize(IN, IN, IN, IN, IN);
   */

  TI_DBG1(("tiCOMPortInit: tdsaAllShared->tdDeviceIdVendId %x\n",tdsaAllShared->tdDeviceIdVendId));
  TI_DBG1(("tiCOMPortInit: tdsaAllShared->tdSubVendorId= SUB_VEN_ID %x\n",tdsaAllShared->tdSubVendorId));

  TI_DBG1(("tiCOMPortInit: swConfig->param1 hwDEVICE_ID_VENDID %x\n", tdsaAllShared->SwConfig.param1 ));
  TI_DBG1(("tiCOMPortInit: swConfig->param2 hwSVID             %x\n", tdsaAllShared->SwConfig.param2));

  /*
    1. Read hostDirectAccessSupport
    2. If set, read HDA images based on chip ID
 */

  t_MacroCheck(agRoot);

#ifndef TURN_OFF_HDA
  if (tdsaAllShared->SwConfig.hostDirectAccessSupport != 0)
  {
    osti_memset(&HDAImg, 0, sizeof(HDAImg));
    if (tiIS_SPC(agRoot))
    {
      TI_DBG1(("tiCOMPortInit: SPC HDA\n"));
      HDAImg.aap1Img = (bit8*)(&aap1array);
      HDAImg.aap1Len = sizeof( aap1array);
      HDAImg.iopImg  = (bit8*)(&ioparray);
      HDAImg.iopLen  = sizeof(  ioparray);
      HDAImg.istrImg = (bit8*)(&istrarray);
      HDAImg.istrLen = sizeof( istrarray);

      HDAImg.ilaImg  = (bit8*)(&ilaarray);
      HDAImg.ilaLen  = sizeof(ilaarray);
    }
    else if (tiIS_SPC6V(agRoot))
    {
      TI_DBG1(("tiCOMPortInit: SPCv HDA\n"));
      HDAImg.aap1Img = (bit8*)(&spcv_aap1array);
      HDAImg.aap1Len =  sizeof( spcv_aap1array);
      HDAImg.iopImg  = (bit8*)(&spcv_ioparray);
      HDAImg.iopLen  = sizeof(  spcv_ioparray);

      HDAImg.ilaImg  = (bit8*)(&spcv_ilaarray);
      HDAImg.ilaLen  = sizeof(spcv_ilaarray);
    }
    else if (tIsSPCV12G(agRoot))
    {
      TI_DBG1(("tiCOMPortInit: SPCv12G HDA\n"));
      HDAImg.aap1Img = (bit8*)(&spcv12g_raaearray);
      HDAImg.aap1Len =  sizeof( spcv12g_raaearray);
      HDAImg.iopImg  = (bit8*)(&spcv12g_ioparray);
      HDAImg.iopLen  = sizeof(  spcv12g_ioparray);

      HDAImg.ilaImg  = (bit8*)(&spcv12g_ilaarray);
      HDAImg.ilaLen  = sizeof(spcv12g_ilaarray);
    }
    else
    {
      TI_DBG1(("tiCOMPortInit: HDA Mode Unknown chip type 0x%08x\n",ossaHwRegReadConfig32(agRoot,0 )));
      return tiError;
    }
    TI_DBG1(("tiCOMPortInit: HDA aap1Len 0x%08x iopLen 0x%08x ilaLen 0x%08x\n",HDAImg.aap1Len,HDAImg.iopLen,HDAImg.ilaLen ));
    tdsaAllShared->SwConfig.param4 = &(HDAImg);
  }
  else
  {
    TI_DBG1(("tiCOMPortInit: HDA off\n"));
    tdsaAllShared->SwConfig.param4 = agNULL;
  }
#endif /*  TURN_OFF_HDA */

  if (tiIS_SPC(agRoot))
  {
    /* FW config is only for SPC */
    tdsaAllShared->SwConfig.FWConfig = 0;
    /* default port recovery timer 0x32 = 50 = 5000ms and port reset timer 3 (300 ms)*/
    tdsaAllShared->SwConfig.PortRecoveryResetTimer = 0x30032;
    TI_DBG1(("tiCOMPortInit:only for SPC FWConfig set\n"));
  }

  tdsaAllShared->SwConfig.fatalErrorInterruptVector = loResource->loLevelOption.maxInterruptVectors > 31 ? 31 : loResource->loLevelOption.maxInterruptVectors -1;

  TI_DBG1(("tiCOMPortInit: SwConfig->FWConfig 0x%x\n", tdsaAllShared->SwConfig.FWConfig));
  TI_DBG1(("tiCOMPortInit: SwConfig->fatalErrorInterruptVector 0x%x\n", tdsaAllShared->SwConfig.fatalErrorInterruptVector));
  TI_DBG1(("tiCOMPortInit: loResource->loLevelOption.usecsPerTick %d\n", loResource->loLevelOption.usecsPerTick));

  status = saInitialize(agRoot,
                        &agMemoryRequirement,
                        &(tdsaAllShared->HwConfig),
                        /* &temp_HwConfig, */
                        &(tdsaAllShared->SwConfig),
                        loResource->loLevelOption.usecsPerTick);

  TI_DBG6(("tiCOMPortInit: loResource->loLevelOption.usecsPerTick %d 0x%x\n", loResource->loLevelOption.usecsPerTick, loResource->loLevelOption.usecsPerTick));

  /*TI_DBG6(("tiCOMPortInit: tdsaAllShared->SwConfig.enableDIF %d\n", tdsaAllShared->SwConfig.enableDIF)); */
  /*TI_DBG6(("tiCOMPortInit: tdsaAllShared->SwConfig.enableEncryption %d\n", tdsaAllShared->SwConfig.enableEncryption)); */

  if(status == AGSA_RC_FAILURE )
  {
    TI_DBG1(("tiCOMPortInit: saInitialize AGSA_RC_FAILURE, status 0x%x\n", status));
    return tiError;
  }

  if( status == AGSA_RC_VERSION_INCOMPATIBLE)
  {
    TI_DBG1(("tiCOMPortInit: saInitialize AGSA_RC_VERSION_INCOMPATIBLE, status 0x%x\n", status));
    return tiError;
  }

  /* let's make sdkData same for Int and Non-int agRoots */
  tdsaAllShared->agRootInt.sdkData = tdsaAllShared->agRootNonInt.sdkData;


  /* mark the port as initialized */
  for(i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    tdsaAllShared->Ports[i].flags.portInitialized = agTRUE;
  }

#ifdef INITIATOR_DRIVER                 //ini. only in stsdkll spec (TP)
  /* register device registration callback function */
  TI_DBG6(("tiCOMPortInit: calling saRegisterEventCallback for device registration\n"));
  status = saRegisterEventCallback(agRoot, OSSA_EVENT_SOURCE_DEVICE_HANDLE_ADDED, (void *)ossaDeviceRegistrationCB);
  if (status == AGSA_RC_FAILURE)
  {
    TI_DBG6(("tiCOMPortInit: saRegisterEventCallback Device Register failed\n"));
  }
  else
  {
    TI_DBG6(("tiCOMPortInit: saRegisterEventCallback Device Register succeeded\n"));
  }
#endif

  /* register device deregistration callback function */
  TI_DBG6(("tiCOMPortInit: calling saRegisterEventCallback for device de-registration\n"));
  status = saRegisterEventCallback(agRoot, OSSA_EVENT_SOURCE_DEVICE_HANDLE_REMOVED, (void *)ossaDeregisterDeviceHandleCB);
  if (status == AGSA_RC_FAILURE)
  {
    TI_DBG6(("tiCOMPortInit: saRegisterEventCallback Device Deregister failed\n"));
  }
  else
  {
    TI_DBG6(("tiCOMPortInit: saRegisterEventCallback Device Deregister succeeded\n"));
  }

#ifdef ECHO_TESTING
  /* temporary to test saEchoCommand() */
  /*
    send echo
  */
  payload[0] = gEcho;
  payload[55] = gEcho;
  TI_DBG2(("tiCOMPortInit: calling saEchoCommand gEcho %d\n", gEcho));
  saEchoCommand(agRoot, agNULL, tdsaRotateQnumber(tiRoot, agNULL), (void *)&payload);
  gEcho++;
#endif

#ifdef CONTROLLER_STATUS_TESTING
  TI_DBG1(("tiCOMPortInit: saGetControllerStatus returns 0x%X\n",saGetControllerStatus(agRoot,&agcontrollerStatus ) ));
#endif /* CONTROLLER_INFO_TESTING */

#ifdef CONTROLLER_INFO_TESTING
  TI_DBG1(("tiCOMPortInit: saGetControllerInfo returns 0x%X\n",saGetControllerInfo(agRoot,&agcontrollerInfo ) ));
#endif /* CONTROLLER_INFO_TESTING */

#ifdef CONTROLLER_ENCRYPT_TESTING
  TI_DBG1(("tiCOMPortInit: saEncryptGetMode returns 0x%X\n",saEncryptGetMode(agRoot,agNULL,&agsaEncryptInfo ) ));
#endif /* CONTROLLER_INFO_TESTING */


#ifdef VPD_TESTING
  /* temporary to test saSetVPDCommand() and saGetVPDCommand */
  tdsaVPDSet(tiRoot);
#endif /* VPD_TESTING */

#if defined(FDS_DM) && defined(FDS_SM)
  /* initialize DM then SM */
  /* DM */
  dmRoot = &(tdsaAllShared->dmRoot);
  osti_memset(&dmMemRequirement, 0, sizeof(dmMemoryRequirement_t));

  dmMemRequirement.count = DMMemCount;

  for(i=LLMemCount;i<(int)(LLMemCount + DMMemCount);i++)
  {
    TI_DBG2(("tiCOMPortInit: DM copying loResource.loLevelMem to agsaMemoryRequirement_t index %d\n", i));
    dmMemRequirement.dmMemory[i-LLMemCount].virtPtr = loResource->loLevelMem.mem[i].virtPtr;
    dmMemRequirement.dmMemory[i-LLMemCount].osHandle = loResource->loLevelMem.mem[i].osHandle;
    dmMemRequirement.dmMemory[i-LLMemCount].physAddrUpper = loResource->loLevelMem.mem[i].physAddrUpper;
    dmMemRequirement.dmMemory[i-LLMemCount].physAddrLower = loResource->loLevelMem.mem[i].physAddrLower;
    dmMemRequirement.dmMemory[i-LLMemCount].totalLength = loResource->loLevelMem.mem[i].totalLength;
    dmMemRequirement.dmMemory[i-LLMemCount].numElements = loResource->loLevelMem.mem[i].numElements;
    dmMemRequirement.dmMemory[i-LLMemCount].singleElementLength = loResource->loLevelMem.mem[i].singleElementLength;
    dmMemRequirement.dmMemory[i-LLMemCount].alignment = loResource->loLevelMem.mem[i].alignment;
    dmMemRequirement.dmMemory[i-LLMemCount].type = loResource->loLevelMem.mem[i].type;
    dmMemRequirement.dmMemory[i-LLMemCount].reserved = loResource->loLevelMem.mem[i].reserved;
    TI_DBG2(("tiCOMPortInit: index %d virtPtr %p osHandle %p\n",i, loResource->loLevelMem.mem[i].virtPtr, loResource->loLevelMem.mem[i].osHandle));
    TI_DBG2(("tiCOMPortInit: index %d phyAddrUpper 0x%x phyAddrLower 0x%x totalLength %d numElements %d\n", i,
    loResource->loLevelMem.mem[i].physAddrUpper,
    loResource->loLevelMem.mem[i].physAddrLower,
    loResource->loLevelMem.mem[i].totalLength,
    loResource->loLevelMem.mem[i].numElements));
    TI_DBG2(("tiCOMPortInit: index %d singleElementLength 0x%x alignment 0x%x type %d reserved %d\n", i,
    loResource->loLevelMem.mem[i].singleElementLength,
    loResource->loLevelMem.mem[i].alignment,
    loResource->loLevelMem.mem[i].type,
    loResource->loLevelMem.mem[i].reserved));

  }

  status = dmInitialize(dmRoot,
                        agRoot,
                        &dmMemRequirement,
                        &(tdsaAllShared->dmSwConfig), //&dmSwConfig, /* start here */
                        loResource->loLevelOption.usecsPerTick);

  if(status == DM_RC_FAILURE || status == DM_RC_VERSION_INCOMPATIBLE)
  {
    TI_DBG1(("tiCOMPortInit: dmInitialize FAILED, status 0x%x\n", status));
    return tiError;
  }

  /* SM */
  smRoot = &(tdsaAllShared->smRoot);
  osti_memset(&smMemRequirement, 0, sizeof(smMemoryRequirement_t));

  smMemRequirement.count = SMMemCount;

  for(i=(LLMemCount + DMMemCount);i<(int)(LLMemCount + DMMemCount + SMMemCount);i++)
  {
    TI_DBG1(("tiCOMPortInit: SM copying loResource.loLevelMem to agsaMemoryRequirement_t index %d\n", i));
    smMemRequirement.smMemory[i-LLMemCount-DMMemCount].virtPtr = loResource->loLevelMem.mem[i].virtPtr;
    smMemRequirement.smMemory[i-LLMemCount-DMMemCount].osHandle = loResource->loLevelMem.mem[i].osHandle;
    smMemRequirement.smMemory[i-LLMemCount-DMMemCount].physAddrUpper = loResource->loLevelMem.mem[i].physAddrUpper;
    smMemRequirement.smMemory[i-LLMemCount-DMMemCount].physAddrLower = loResource->loLevelMem.mem[i].physAddrLower;
    smMemRequirement.smMemory[i-LLMemCount-DMMemCount].totalLength = loResource->loLevelMem.mem[i].totalLength;
    smMemRequirement.smMemory[i-LLMemCount-DMMemCount].numElements = loResource->loLevelMem.mem[i].numElements;
    smMemRequirement.smMemory[i-LLMemCount-DMMemCount].singleElementLength = loResource->loLevelMem.mem[i].singleElementLength;
    smMemRequirement.smMemory[i-LLMemCount-DMMemCount].alignment = loResource->loLevelMem.mem[i].alignment;
    smMemRequirement.smMemory[i-LLMemCount-DMMemCount].type = loResource->loLevelMem.mem[i].type;
    smMemRequirement.smMemory[i-LLMemCount-DMMemCount].reserved = loResource->loLevelMem.mem[i].reserved;
    TI_DBG2(("tiCOMPortInit: index %d virtPtr %p osHandle %p\n",i, loResource->loLevelMem.mem[i].virtPtr, loResource->loLevelMem.mem[i].osHandle));
    TI_DBG2(("tiCOMPortInit: index %d phyAddrUpper 0x%x phyAddrLower 0x%x totalLength %d numElements %d\n", i,
    loResource->loLevelMem.mem[i].physAddrUpper,
    loResource->loLevelMem.mem[i].physAddrLower,
    loResource->loLevelMem.mem[i].totalLength,
    loResource->loLevelMem.mem[i].numElements));
    TI_DBG2(("tiCOMPortInit: index %d singleElementLength 0x%x alignment 0x%x type %d reserved %d\n", i,
    loResource->loLevelMem.mem[i].singleElementLength,
    loResource->loLevelMem.mem[i].alignment,
    loResource->loLevelMem.mem[i].type,
    loResource->loLevelMem.mem[i].reserved));

  }

  status = smInitialize(smRoot,
                        agRoot,
                        &smMemRequirement,
                        &(tdsaAllShared->smSwConfig), //&smSwConfig, /* start here */
                        loResource->loLevelOption.usecsPerTick);

  if(status == SM_RC_FAILURE || status == SM_RC_VERSION_INCOMPATIBLE)
  {
    TI_DBG1(("tiCOMPortInit: smInitialize FAILED, status 0x%x\n", status));
    return tiError;
  }

#elif defined(FDS_DM)
  dmRoot = &(tdsaAllShared->dmRoot);
  osti_memset(&dmMemRequirement, 0, sizeof(dmMemoryRequirement_t));

  dmMemRequirement.count = DMMemCount;

  for(i=LLMemCount;i<(int)(LLMemCount + DMMemCount);i++)
  {
    TI_DBG6(("tiCOMPortInit: copying loResource.loLevelMem to agsaMemoryRequirement_t index %d\n", i));
    dmMemRequirement.dmMemory[i-LLMemCount].virtPtr = loResource->loLevelMem.mem[i].virtPtr;
    dmMemRequirement.dmMemory[i-LLMemCount].osHandle = loResource->loLevelMem.mem[i].osHandle;
    dmMemRequirement.dmMemory[i-LLMemCount].physAddrUpper = loResource->loLevelMem.mem[i].physAddrUpper;
    dmMemRequirement.dmMemory[i-LLMemCount].physAddrLower = loResource->loLevelMem.mem[i].physAddrLower;
    dmMemRequirement.dmMemory[i-LLMemCount].totalLength = loResource->loLevelMem.mem[i].totalLength;
    dmMemRequirement.dmMemory[i-LLMemCount].numElements = loResource->loLevelMem.mem[i].numElements;
    dmMemRequirement.dmMemory[i-LLMemCount].singleElementLength = loResource->loLevelMem.mem[i].singleElementLength;
    dmMemRequirement.dmMemory[i-LLMemCount].alignment = loResource->loLevelMem.mem[i].alignment;
    dmMemRequirement.dmMemory[i-LLMemCount].type = loResource->loLevelMem.mem[i].type;
    dmMemRequirement.dmMemory[i-LLMemCount].reserved = loResource->loLevelMem.mem[i].reserved;
    TI_DBG6(("tiCOMPortInit: index %d phyAddrUpper 0x%x phyAddrLower 0x%x totalLength %d alignment %d\n", i, loResource->loLevelMem.mem[i].physAddrUpper, loResource->loLevelMem.mem[i].physAddrLower, loResource->loLevelMem.mem[i].totalLength, loResource->loLevelMem.mem[i].alignment));
    TI_DBG6(("tiCOMPortInit: index %d virtPtr %p\n",i, loResource->loLevelMem.mem[i].virtPtr));

  }

  status = dmInitialize(dmRoot,
                        agRoot,
                        &dmMemRequirement,
                        &(tdsaAllShared->dmSwConfig), //&dmSwConfig, /* start here */
                        loResource->loLevelOption.usecsPerTick);

  if(status == DM_RC_FAILURE || status == DM_RC_VERSION_INCOMPATIBLE)
  {
    TI_DBG1(("tiCOMPortInit: dmInitialize FAILED, status 0x%x\n", status));
    return tiError;
  }

#elif defined(FDS_SM)
  smRoot = &(tdsaAllShared->smRoot);
  osti_memset(&smMemRequirement, 0, sizeof(smMemoryRequirement_t));

  smMemRequirement.count = SMMemCount;

  for(i=LLMemCount;i<(int)(LLMemCount + SMMemCount);i++)
  {
    TI_DBG6(("tiCOMPortInit: copying loResource.loLevelMem to agsaMemoryRequirement_t index %d\n", i));
    smMemRequirement.smMemory[i-LLMemCount].virtPtr = loResource->loLevelMem.mem[i].virtPtr;
    smMemRequirement.smMemory[i-LLMemCount].osHandle = loResource->loLevelMem.mem[i].osHandle;
    smMemRequirement.smMemory[i-LLMemCount].physAddrUpper = loResource->loLevelMem.mem[i].physAddrUpper;
    smMemRequirement.smMemory[i-LLMemCount].physAddrLower = loResource->loLevelMem.mem[i].physAddrLower;
    smMemRequirement.smMemory[i-LLMemCount].totalLength = loResource->loLevelMem.mem[i].totalLength;
    smMemRequirement.smMemory[i-LLMemCount].numElements = loResource->loLevelMem.mem[i].numElements;
    smMemRequirement.smMemory[i-LLMemCount].singleElementLength = loResource->loLevelMem.mem[i].singleElementLength;
    smMemRequirement.smMemory[i-LLMemCount].alignment = loResource->loLevelMem.mem[i].alignment;
    smMemRequirement.smMemory[i-LLMemCount].type = loResource->loLevelMem.mem[i].type;
    smMemRequirement.smMemory[i-LLMemCount].reserved = loResource->loLevelMem.mem[i].reserved;
    TI_DBG6(("tiCOMPortInit: index %d phyAddrUpper 0x%x phyAddrLower 0x%x totalLength %d alignment %d\n", i, loResource->loLevelMem.mem[i].physAddrUpper, loResource->loLevelMem.mem[i].physAddrLower, loResource->loLevelMem.mem[i].totalLength, loResource->loLevelMem.mem[i].alignment));
    TI_DBG6(("tiCOMPortInit: index %d virtPtr %p\n",i, loResource->loLevelMem.mem[i].virtPtr));

  }

  status = smInitialize(smRoot,
                        agRoot,
                        &smMemRequirement,
                        &(tdsaAllShared->smSwConfig), //&smSwConfig, /* start here */
                        loResource->loLevelOption.usecsPerTick);

  if(status == SM_RC_FAILURE || status == SM_RC_VERSION_INCOMPATIBLE)
  {
    TI_DBG1(("tiCOMPortInit: smInitialize FAILED, status 0x%x\n", status));
    return tiError;
  }
#else
  /* nothing */
#endif /* FDS_DM && FDS_SM */

  /* call these again after agroot is created  for testing */
#ifdef CONTROLLER_STATUS_TESTING
  TI_DBG1(("tiCOMPortInit:again saGetControllerStatus returns 0x%X\n",saGetControllerStatus(agRoot,&agcontrollerStatus ) ));
#endif /* CONTROLLER_INFO_TESTING */

#ifdef CONTROLLER_INFO_TESTING
  TI_DBG1(("tiCOMPortInit:again saGetControllerInfo returns 0x%X\n",saGetControllerInfo(agRoot,&agcontrollerInfo ) ));
#endif /* CONTROLLER_INFO_TESTING */

#ifdef CONTROLLER_ENCRYPT_TESTING
  TI_DBG1(("tiCOMPortInit:again saEncryptGetMode returns 0x%X\n",saEncryptGetMode(agRoot,agNULL,&agsaEncryptInfo ) ));
#endif /* CONTROLLER_INFO_TESTING */

/* Enable SGPIO */
  if (tiSuccess == tiCOMConfigureSgpio(tiRoot, agTRUE))
  {
    TI_DBG2(("tiCOMPortInit: Successfully sent request to enable SGPIO\n"));
  }
  else
  {
    TI_DBG1(("tiCOMPortInit: Failed to enable SGPIO\n"));
  }

  return tiSuccess;
}

/*****************************************************************************
*! \brief SendSgpioRequest
*
*  Purpose: This function is used to send SGPIO request during initialization
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   regType: Register Type
*  \param   regIndex: Register Index
*  \param   regCount: Register Count
*  \param   writeData: Part of the request
*                  
*  \return:
*           tiSuccess on success
*           Other status on failure
*
*****************************************************************************/   
static bit32 SendSgpioRequest(
                            tiRoot_t    *tiRoot,
                            bit8        regType,
                            bit8        regIndex,
                            bit8        regCount,
                            bit32       *writeData
                            )
{
    static bit32            buffer[128] = {0};
    bit32                   status = tiError;
    bit32		    retVal = IOCTL_CALL_FAIL;
    tiIOCTLPayload_t        *agIOCTLPayload = (tiIOCTLPayload_t *) buffer;
    agsaSGpioReqResponse_t  *pSGpioReq = (agsaSGpioReqResponse_t *) &agIOCTLPayload->FunctionSpecificArea[0];
    agsaSGpioReqResponse_t  *pSgpioResponse = (agsaSGpioReqResponse_t *) &agIOCTLPayload->FunctionSpecificArea[0];

    do{
  
    /* Frame the Ioctl payload */
    agIOCTLPayload->MajorFunction = IOCTL_MJ_SGPIO;
    agIOCTLPayload->Length = sizeof(agsaSGpioReqResponse_t);
    
    /* Frame the SGPIO request */
    pSGpioReq->smpFrameType = SMP_REQUEST;
    pSGpioReq->function = AGSA_WRITE_SGPIO_REGISTER;
    pSGpioReq->registerType = regType;
    pSGpioReq->registerIndex = regIndex;
    pSGpioReq->registerCount = regCount;
    memcpy(pSGpioReq->readWriteData, writeData, regCount * 4);

    /* Send the SGPIO request */
    sgpioResponseSet = 0;
    retVal = tdsaSGpioIoctlSetup(tiRoot, agNULL, agIOCTLPayload, agNULL, agNULL);
    if (retVal != IOCTL_CALL_PENDING)
    {
	break;
    }
    /* Waiting for SGPIO Response */
    while(!sgpioResponseSet)
    {
	tiCOMDelayedInterruptHandler(tiRoot, 0, 1, tiNonInterruptContext);
    }
    sgpioResponseSet = 0;
    /* Check the ioctl result */
    if(agIOCTLPayload->Status != IOCTL_ERR_STATUS_OK)
    {
	break;
    }
    /* Check the sgpio function result */
    if(pSgpioResponse->functionResult != 0x00)
    {
	break;
    }

    status = tiSuccess;
    
  }while(0);
    
    return status;
}

/*****************************************************************************
*! \brief tiCOMConfigureSgpio
*
*  Purpose: This function is used to configure SGPIO during initialization
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   enableSgpio: Enable / Disable SGPIO
*                  
*  \return:
*           tiSuccess on success
*           Other status on failure
*
*****************************************************************************/
bit32 tiCOMConfigureSgpio(
                        tiRoot_t    *tiRoot,
                        bit8        enableSgpio
                        )
{
    bit32	    status = tiError;
    bit32	    i;
    bit8            regCount;
    bit32           writeData[OSSA_SGPIO_MAX_READ_DATA_COUNT] = {0};
    agsaSGpioCfg0_t *pCfg0 = (agsaSGpioCfg0_t *) &writeData[0];
    agsaSGpioCfg1_t *pCfg1 = (agsaSGpioCfg1_t *) &writeData[1];
    tdsaRoot_t	    *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t   *tdsaAllShared = (tdsaContext_t *) &tdsaRoot->tdsaAllShared;
    bit32	    phyCount = tdsaAllShared->phyCount;

    if (enableSgpio)
    {
        /* Configure both CFG[0] and CFG[1] */
        regCount = 2;

        /* Enable SGPIO */
        pCfg0->gpioEnable = 1;

        /* The following are the default values for CFG[1] suggested by SFF-8485 spec */
        /* Maximum Activity ON: 2 */
        /* Forced Activity OFF: 1 */
        pCfg1->maxActOn = 2;
        pCfg1->forceActOff = 1;
    }
    else
    {
        /* Configure CFG[0] only */
        regCount = 1;

        /* Disable SGPIO */
        pCfg0->gpioEnable = 0;
    }

    status = SendSgpioRequest(tiRoot, AGSA_SGPIO_CONFIG_REG, 0, regCount, writeData);
    if((tiSuccess == status) && (enableSgpio))
    {
	 /* Write default values to transmit registers */ 
	 /* RegisterCount = Number of phys present in HBA / 4 */
	 regCount = phyCount / 4;
         for(i = 0; i < regCount; i++)
	 {
	   /* Following are the default values specified in SFF-8485 spec */
	   /* Activity: 5 */
	   /* Locate: 0 */
	   /* Error: 0 */
	   writeData[i] = 0xA0A0A0A0;
  	}
	status = SendSgpioRequest(tiRoot, AGSA_SGPIO_DRIVE_BY_DRIVE_TRANSMIT_REG, 0, regCount, writeData);
   }
    
   return status;
}


/*****************************************************************************
*! \brief  tiCOMPortStart
*
*  Purpose: This function is called to bring the port hardware online. This
*           call could only be called until after the successful completion
*           tiCOMPortInit().
*
*  \param  tiRoot:          Pointer to root data structure.
*  \param  portID:          A ID for this portal to be used by the TD Layer
*                           to get the portal configuration information.
*  \param  portalContext:   Pointer to the context for this portal.
*  \param  option:          An option for starting a port
*
*  \return:
*          tiSuccess:      Successful.
*          Others:             Fail.
*
*  \note -
*   If sas or sata initiator, this will be called 8 (the number of phys) times.
*   If both sas and sata initiator, this will be called 16 times
*
*****************************************************************************/
/* portID is used as PhyID
   Should return always tiSuccess. PortStarted is returned in ossaHwCB()
*/
osGLOBAL bit32
tiCOMPortStart(
               tiRoot_t          * tiRoot,
               bit32               portID,
               tiPortalContext_t * portalContext,
               bit32               option
               )
{
  tdsaRoot_t    *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t *agRoot = &tdsaAllShared->agRootInt;
  agsaSASProtocolTimerConfigurationPage_t SASConfigPage;
  bit32 status = AGSA_RC_FAILURE;
  static bit32 IsSendSASConfigPage = agFALSE;


  TI_DBG3(("tiCOMPortStart: start portID %d\n", portID));

  /*
   * return error if reset is in progress
   */
  if (tdsaAllShared->flags.resetInProgress == agTRUE)
  {
    TI_DBG1(("tiCOMPortStart: reset error\n"));
    return tiError;
  }

  /*
   *
   * port is not initialized, return error
   */
  if (tdsaAllShared->Ports[portID].flags.portInitialized == agFALSE)
  {
    TI_DBG1(("tiCOMPortStart: not intialized error\n"));
    return tiError;
  }

  /* portal has been started. */
  if (portalContext->tdData != NULL)
  {
    if (tdsaAllShared->Ports[portID].flags.portStarted == agTRUE)
    {
      TI_DBG3 (("tiCOMPortStart : Cannot start port again: Port has already been started\n"));
      ostiPortEvent (
                     tiRoot,
                     tiPortStarted,
                     tiSuccess,
                     (void *) tdsaAllShared->Ports[portID].tiPortalContext
                     );
      return tiSuccess;
    }
  }

  portalContext->tdData =  &(tdsaAllShared->Ports[portID]);
  TI_DBG4 (("tiCOMPortStart : saving portalconext portID %d tdsaAllShared %p\n", portID, tdsaAllShared));
  /* saving tiportalContext_t */
  tdsaAllShared->Ports[portID].tiPortalContext = portalContext;
  TI_DBG4(("tiCOMPortStart : portID/phyID %d tiPortalContext %p\n", portID, tdsaAllShared->Ports[portID].tiPortalContext));

  /*
    where is "tdsaAllShared->Ports[0].portContext" set?
    in ossaHWCB
  */
  if (tdsaAllShared->Ports[portID].flags.portStarted == agTRUE)
  {
    TI_DBG1(("tiCOMPortStart: port already has been started \n"));
    return tiSuccess;
  }

  
  /*
    hardcode sasID frame. It should be read by ostigettransportparams later from configuration file
  */
#ifdef INITIATOR_DRIVER

  tdsaAllShared->Ports[portID].SASID.target_ssp_stp_smp = 0;
  tdsaAllShared->Ports[portID].SASID.initiator_ssp_stp_smp
      = SA_IDFRM_SSP_BIT | SA_IDFRM_STP_BIT | SA_IDFRM_SMP_BIT;
  tdsaAllShared->Ports[portID].SASID.deviceType_addressFrameType = AGSA_DEV_TYPE_END_DEVICE;

  tdsaAllShared->Ports[portID].SASID.phyIdentifier = (bit8)portID;

#endif

#ifdef TARGET_DRIVER

  tdsaAllShared->Ports[portID].SASID.target_ssp_stp_smp = SA_IDFRM_SSP_BIT;
  tdsaAllShared->Ports[portID].SASID.initiator_ssp_stp_smp = 0;
  tdsaAllShared->Ports[portID].SASID.deviceType_addressFrameType = AGSA_DEV_TYPE_END_DEVICE;
  tdsaAllShared->Ports[portID].SASID.phyIdentifier = (bit8)portID;

#endif

#if defined (INITIATOR_DRIVER) && defined (TARGET_DRIVER)

  /* for combo testing */
  tdsaAllShared->Ports[portID].SASID.target_ssp_stp_smp = SA_IDFRM_SSP_BIT;
  tdsaAllShared->Ports[portID].SASID.initiator_ssp_stp_smp
      = SA_IDFRM_SSP_BIT | SA_IDFRM_STP_BIT | SA_IDFRM_SMP_BIT;
  tdsaAllShared->Ports[portID].SASID.deviceType_addressFrameType = AGSA_DEV_TYPE_END_DEVICE;

  tdsaAllShared->Ports[portID].SASID.phyIdentifier = (bit8)portID;

#endif


  TI_DBG6(("tiCOMPortStart: before pid %d\n", portID));
  tdssPrintSASIdentify(&(tdsaAllShared->Ports[portID].SASID));

  TI_DBG6(("tiCOMPortStart: sysIntsActive %s\n",
           (tdsaAllShared->flags.sysIntsActive == agTRUE) ? "agTRUE" : "agFALSE"));

  /* Read global configuration first then card-specific configuration */

  /* the following must be processed only once */
  if ( tdsaAllShared->first_process == agFALSE)
  {
    tdsaGetGlobalPhyParams(tiRoot);
    tdsaGetCardPhyParams(tiRoot);
    tdsaAllShared->first_process = agTRUE;
  }

  TI_DBG6(("tiCOMPortStart: after pid %d\n", portID));
  tdssPrintSASIdentify(&(tdsaAllShared->Ports[portID].SASID));

  /*
     Phy Calibration
  */
  if (tdsaAllShared->phyCalibration)
  {
    /* Change default phy calibration */
    tdsaAllShared->Ports[portID].agPhyConfig.phyProperties =
      (tdsaAllShared->Ports[portID].agPhyConfig.phyProperties) | 0x80;
    /* Setting index of phy calibration table index
       portID is used as phy calibration table index
    */
    tdsaAllShared->Ports[portID].agPhyConfig.phyProperties =
      (tdsaAllShared->Ports[portID].agPhyConfig.phyProperties) | (portID << 8);
  }
  TI_DBG2(("tiCOMPortStart: tdsaAllShared->Ports[0x%x].agPhyConfig.phyProperties 0x%x\n",
    portID, tdsaAllShared->Ports[portID].agPhyConfig.phyProperties));


  if(gSSC_Disable)
  {
    tdsaAllShared->Ports[portID].agPhyConfig.phyProperties = tdsaAllShared->Ports[portID].agPhyConfig.phyProperties | 0x40000;
    TI_DBG1(("tiCOMPortStart:gSSC_Disable tdsaAllShared->Ports[portID].agPhyConfig.phyProperties 0x%x\n", tdsaAllShared->Ports[portID].agPhyConfig.phyProperties));

  }

  if(tIsSPCV12or6G(agRoot) && !IsSendSASConfigPage) /* Not SPC */
  {
    /* call saSetControllerConfig() to set STP_IDLE_TIME; All others are the defaults */
    osti_memset(&SASConfigPage, 0, sizeof(agsaSASProtocolTimerConfigurationPage_t));
    SASConfigPage.pageCode        =  AGSA_SAS_PROTOCOL_TIMER_CONFIG_PAGE;
    SASConfigPage.MST_MSI         =  3 << 15; /* enables both MCT for SSP target and initiator */
    SASConfigPage.STP_SSP_MCT_TMO =  (tdsaAllShared->STP_MCT_TMO << 16) | tdsaAllShared->SSP_MCT_TMO; /* default of 3200 us for STP and SSP maximum connection time */
    SASConfigPage.STP_FRM_TMO     = (tdsaAllShared->MAX_OPEN_TIME << 24) | (tdsaAllShared->SMP_MAX_CONN_TIMER << 16) | tdsaAllShared->STP_FRM_TMO; /* MAX_OPEN_TIME, SMP_MAX_CONN_TIMER, STP frame timeout */
    SASConfigPage.STP_IDLE_TMO    =  tdsaAllShared->stp_idle_time;
    if (SASConfigPage.STP_IDLE_TMO > 0x3FFFFFF)
    {
      SASConfigPage.STP_IDLE_TMO = 0x3FFFFFF;
    }
    SASConfigPage.OPNRJT_RTRY_INTVL =         (tdsaAllShared->MFD << 16)              | tdsaAllShared->OPNRJT_RTRY_INTVL; /* Multi Data Fetach enabled and 2 us for Open Reject Retry interval */
    SASConfigPage.Data_Cmd_OPNRJT_RTRY_TMO =  (tdsaAllShared->DOPNRJT_RTRY_TMO << 16) | tdsaAllShared->COPNRJT_RTRY_TMO; /* 128 us for ORR Timeout for DATA phase and 32 us for ORR Timeout for command phase */
    SASConfigPage.Data_Cmd_OPNRJT_RTRY_THR =  (tdsaAllShared->DOPNRJT_RTRY_THR << 16) | tdsaAllShared->COPNRJT_RTRY_THR; /* 16 for ORR backoff threshold for DATA phase and 1024 for ORR backoff threshold for command phase */
    SASConfigPage.MAX_AIP =  tdsaAllShared->MAX_AIP; /* MAX AIP. Default is  0x200000 */

    TI_DBG1(("tiCOMPortStart: saSetControllerConfig SASConfigPage.pageCode                 0x%08x\n",SASConfigPage.pageCode));
    TI_DBG1(("tiCOMPortStart: saSetControllerConfig SASConfigPage.MST_MSI                  0x%08x\n",SASConfigPage.MST_MSI));
    TI_DBG1(("tiCOMPortStart: saSetControllerConfig SASConfigPage.STP_SSP_MCT_TMO          0x%08x\n",SASConfigPage.STP_SSP_MCT_TMO));
    TI_DBG1(("tiCOMPortStart: saSetControllerConfig SASConfigPage.STP_FRM_TMO              0x%08x\n",SASConfigPage.STP_FRM_TMO));
    TI_DBG1(("tiCOMPortStart: saSetControllerConfig SASConfigPage.STP_IDLE_TMO             0x%08x\n",SASConfigPage.STP_IDLE_TMO));
    TI_DBG1(("tiCOMPortStart: saSetControllerConfig SASConfigPage.OPNRJT_RTRY_INTVL        0x%08x\n",SASConfigPage.OPNRJT_RTRY_INTVL));
    TI_DBG1(("tiCOMPortStart: saSetControllerConfig SASConfigPage.Data_Cmd_OPNRJT_RTRY_TMO 0x%08x\n",SASConfigPage.Data_Cmd_OPNRJT_RTRY_TMO));
    TI_DBG1(("tiCOMPortStart: saSetControllerConfig SASConfigPage.Data_Cmd_OPNRJT_RTRY_THR 0x%08x\n",SASConfigPage.Data_Cmd_OPNRJT_RTRY_THR));
    TI_DBG1(("tiCOMPortStart: saSetControllerConfig SASConfigPage.MAX_AIP                  0x%08x\n",SASConfigPage.MAX_AIP));

    status = saSetControllerConfig(agRoot,
                                    0,
                                    AGSA_SAS_PROTOCOL_TIMER_CONFIG_PAGE,
                                    sizeof(agsaSASProtocolTimerConfigurationPage_t),
                                    &SASConfigPage,
                                    agNULL);
    if (status != AGSA_RC_SUCCESS)
    {
      TI_DBG1(("tiCOMPortStart: calling saSetControllerConfig() failed\n"));
    }
    else
    {
      TI_DBG2(("tiCOMPortStart: calling saSetControllerConfig() is OK\n"));
    }
    IsSendSASConfigPage = agTRUE;
  }
  else
  {
    TI_DBG1(("tiCOMPortStart: saSetControllerConfig not called tIsSPCV12or6G %d IsSendSASConfigPage %d\n",tIsSPCV12or6G(agRoot),IsSendSASConfigPage));
  }

  /* maps portID to phyID */
  status = saPhyStart(agRoot,
                      agNULL,
                      0,
                      portID,
                      &(tdsaAllShared->Ports[portID].agPhyConfig),
                      &(tdsaAllShared->Ports[portID].SASID)
                      );

  TI_DBG6(("tiCOMPortStart: saPhyStart status %d\n", status));

  if (status == AGSA_RC_SUCCESS)
  {
    TI_DBG3(("tiCOMPortStart : calling portstarted\n"));
    ostiPortEvent(
                  tiRoot,
                  tiPortStarted,
                  tiSuccess,
                  (void *) tdsaAllShared->Ports[portID].tiPortalContext
                  );
    return tiSuccess;
  }
  else
  {
    TI_DBG3(("tiCOMPortStart : cant' start port\n"));
    return tiError;
  }

}

/*****************************************************************************
*! \brief  tiCOMPortStop
*
*  Purpose: This function is called to bring the port hardware down.
*
*  \param  tiRoot:          Pointer to root data structure.
*  \param  portalContext:   Pointer to the context for this portal.
*
*  \return:
*          tiSuccess:      Successful.
*          Others:             Fail.
*
*  \note -
*
*****************************************************************************/
osGLOBAL bit32
tiCOMPortStop (
  tiRoot_t          *tiRoot,
  tiPortalContext_t *portalContext
  )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdList_t          *PortContextList;
  tdsaPortContext_t *onePortContext = agNULL;
  agsaRoot_t        *agRoot = agNULL;
  bit32             i;
  bit32             found = agFALSE;

#ifdef CONTROLLER_STATUS_TESTING
  agsaControllerStatus_t  agcontrollerStatus;
#endif /* CONTROLLER_STATUS_TESTING */


  TI_DBG3(("tiCOMPortStop: start\n"));
  /*
    find the portcontext
    find phys belonging to that portcotext
    call saPhyStop for all those phys
    call saPhyStop()
    remove the portcontext from the portcontext list
  */

  agRoot = &(tdsaAllShared->agRootNonInt);

#ifdef CONTROLLER_STATUS_TESTING
  TI_DBG1(("tiCOMPortStop: saGetControllerStatus returns 0x%X\n",saGetControllerStatus(agRoot,&agcontrollerStatus ) ));
#endif /* CONTROLLER_INFO_TESTING */

  if (TDLIST_EMPTY(&(tdsaAllShared->MainPortContextList)))
  {
    TI_DBG1(("tiCOMPortStop: empty tdsaPortContext\n"));
    return tiError;
  }

  /* find a right portcontext */
  PortContextList = tdsaAllShared->MainPortContextList.flink;
  while (PortContextList != &(tdsaAllShared->MainPortContextList))
  {
    onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, PortContextList);
    if (onePortContext == agNULL)
    {
      TI_DBG1(("tiCOMPortStop: onePortContext is NULL!!!\n"));
      return tiError;
    }
    if (onePortContext->tiPortalContext == portalContext)
    {
      TI_DBG6(("tiCOMPortStop: found; oneportContext ID %d\n", onePortContext->id));
      found = agTRUE;
      break;
    }
    PortContextList = PortContextList->flink;
  }

  if (found == agFALSE)
  {
    TI_DBG1(("tiCOMPortStop: No corressponding tdsaPortContext\n"));
    return tiError;
  }

#ifdef INITIATOR_DRIVER
  /* reset the fields of portcontext */
  onePortContext->DiscoveryState = ITD_DSTATE_NOT_STARTED;
  onePortContext->discoveryOptions = AG_SA_DISCOVERY_OPTION_FULL_START;
#endif /* INITIATOR_DRIVER */

  onePortContext->Count = 0;
  onePortContext->agContext.osData = onePortContext;
  for(i=0;i<TD_MAX_NUM_PHYS;i++)
  {
    if (onePortContext->PhyIDList[i] == agTRUE)
    {
      tdsaAllShared->Ports[i].portContext = agNULL;
#ifdef CCFLAGS_PHYCONTROL_COUNTS
      if( tiIS_SPC(agRoot) )
      {

        saLocalPhyControl(agRoot,&onePortContext->agContext,0 , i, AGSA_PHY_GET_ERROR_COUNTS, agNULL);
        saLocalPhyControl(agRoot,&onePortContext->agContext,0 , i, AGSA_PHY_CLEAR_ERROR_COUNTS, agNULL);
        saLocalPhyControl(agRoot,&onePortContext->agContext,0 , i, AGSA_PHY_GET_BW_COUNTS, agNULL);
      }
      else
      {
        TI_DBG1(("\ntiCOMPortStop: CCFLAGS_PHYCONTROL_COUNTS PHY %d\n",i));
        saGetPhyProfile( agRoot,&onePortContext->agContext,0, AGSA_SAS_PHY_ERR_COUNTERS_PAGE, i);
        saGetPhyProfile( agRoot,&onePortContext->agContext,0, AGSA_SAS_PHY_BW_COUNTERS_PAGE,i);
        saGetPhyProfile( agRoot,&onePortContext->agContext,0, AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE,i);
        saGetPhyProfile( agRoot,&onePortContext->agContext,0, AGSA_SAS_PHY_GENERAL_STATUS_PAGE,i);
        saGetPhyProfile( agRoot,&onePortContext->agContext,0, AGSA_SAS_PHY_ERR_COUNTERS_CLR_PAGE,i);

        TI_DBG1(("tiCOMPortStop: CCFLAGS_PHYCONTROL_COUNTS PHY %d\n",i));

      }

#endif /* CCFLAGS_PHYCONTROL_COUNTS */
      saPhyStop(agRoot, &onePortContext->agContext, 0, i);
    }
  }

  return tiSuccess;
}


/*****************************************************************************
*! \brief tiCOMGetPortInfo
*
*  Purpose:  This function is called to return information about the specific
*            port instant
*
*
*  \param   tiRoot:        Pointer to driver/port instance.
*  \param   portalContext  Pointer to the context for this portal.
*  \param   tiPortInfo:    Pointer to port information structure.
*
*  \Return: tiSuccess
*
*****************************************************************************/
/*
  can't find tdsaportcontext in this function
  since discovery has not been called by OS layer yet
  Therefore, hardcoded value are being returned for now
*/
osGLOBAL bit32 tiCOMGetPortInfo(
                                tiRoot_t            *tiRoot,
                                tiPortalContext_t   *portalContext,
                                tiPortInfo_t        *tiPortInfo
                                )

{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdList_t          *PortContextList;
  tdsaPortContext_t *onePortContext = agNULL;
  bit32             found = agFALSE;
  static bit8       localname[68], remotename[68];
  
  TI_DBG6(("tiCOMGetPortInfo: start\n"));

 
  tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
  if (TDLIST_EMPTY(&(tdsaAllShared->MainPortContextList)))
  {
    tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
    TI_DBG1(("tiCOMGetPortInfo: No tdsaPortContext\n"));
    return tiError;
  }
  else
  {
    tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
  }

  /* find a corresponding portcontext */
  PortContextList = tdsaAllShared->MainPortContextList.flink;
  while (PortContextList != &(tdsaAllShared->MainPortContextList))
  {
    onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, PortContextList);
    TI_DBG3(("tiCOMGetPortInfo: oneportContext pid %d\n", onePortContext->id));
    if (onePortContext->tiPortalContext == portalContext && onePortContext->valid == agTRUE)
    {
      TI_DBG3(("tiCOMGetPortInfo: found; oneportContext pid %d\n", onePortContext->id));
      found = agTRUE;
      break;
    }
    PortContextList = PortContextList->flink;
  }

  if (found == agFALSE)
  {
    TI_DBG1(("tiCOMGetPortInfo: First, No corresponding tdsaPortContext\n"));
    return tiError;
  }
  
  if (onePortContext == agNULL)
  {
    TI_DBG1(("tiCOMGetPortInfo: Second, No corressponding tdsaPortContext\n"));
    return tiError;
  }
  
  osti_memset(localname, 0, sizeof(localname));
  osti_memset(remotename, 0, sizeof(remotename));
  
  /*
    Parse the type of port then fill in the information
  */
  if ( onePortContext->sasRemoteAddressHi == 0xFFFFFFFF && onePortContext->sasRemoteAddressLo == 0xFFFFFFFF)
  {
    /* directly attached SATA port */
    osti_memcpy(localname, &(onePortContext->sasLocalAddressHi), 4);
    osti_memcpy(&(localname[4]), &(onePortContext->sasLocalAddressLo), 4);
    tiPortInfo->localNameLen = 8;
    /* information is from SATA ID device data. remoteName is serial number, firmware version, model number */
    osti_memcpy(remotename, onePortContext->remoteName, 68);
    tiPortInfo->remoteNameLen = 68;    
  }
  else
  {
    /* copy hi address and low address */
    osti_memcpy(localname, &(onePortContext->sasLocalAddressHi), 4);
    osti_memcpy(&(localname[4]), &(onePortContext->sasLocalAddressLo), 4);
    tiPortInfo->localNameLen = 8;
    osti_memcpy(remotename, &(onePortContext->sasRemoteAddressHi), 4);
    osti_memcpy(&(remotename[4]), &(onePortContext->sasRemoteAddressLo), 4);
    tiPortInfo->remoteNameLen = 8;
  }  
  
  tiPortInfo->localName = (char *)&localname;
  tiPortInfo->remoteName = (char *)&remotename;  

  
  return tiSuccess;

}

/*****************************************************************************
*
* tiCOMSetControllerConfig
*
*  Purpose:  This function is called to set the controller's advanced configuration.
*            The status is reported via ostiPortEvent().
*
*  Parameters:
*
*    tiRoot:        Pointer to driver/port instance.
*
*  Return:
*     tiSuccess:  The setting controller configuration was started.
*     tiError:    The setting controller configuration was not started.
*
*****************************************************************************/
osGLOBAL bit32  tiCOMSetControllerConfig (
         tiRoot_t    *tiRoot,
         bit32       modePage,
         bit32       length,
         void        *buffer,
         void        *context)
{
   agsaRoot_t      *agRoot;
   bit32           returnCode = AGSA_RC_BUSY;
   bit32           tiStatus = tiSuccess;
   tdsaRoot_t      *tdsaRoot ;
   tdsaContext_t   *tdsaAllShared ;

   TD_ASSERT(tiRoot, "tiRoot");
   tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
   TD_ASSERT(tdsaRoot, "tdsaRoot");

   tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
   TD_ASSERT(tdsaAllShared, "tdsaAllShared");
   agRoot = &(tdsaAllShared->agRootNonInt);
   TD_ASSERT(agRoot, "agRoot");

   agRoot = &(tdsaAllShared->agRootNonInt);

   TI_DBG1(("tiCOMSetControllerConfig:\n" ));

   /*do some sanity checking */
   if ( ((modePage == TI_INTERRUPT_CONFIGURATION_PAGE)   && (length != sizeof(tiInterruptConfigPage_t )))   ||
        ((modePage == TI_ENCRYPTION_GENERAL_CONFIG_PAGE) && (length != sizeof(tiEncryptGeneralPage_t  )))    ||
        ((modePage == TI_ENCRYPTION_DEK_CONFIG_PAGE)     && (length != sizeof(tiEncryptDekConfigPage_t)))    ||
        ((modePage == TI_ENCRYPTION_CONTROL_PARM_PAGE)  && (length != sizeof(tiEncryptControlParamPage_t ))) ||
        ((modePage == TI_ENCRYPTION_HMAC_CONFIG_PAGE)  && (length != sizeof(tiEncryptHMACConfigPage_t )))    ||
        ((modePage == TI_SAS_PROTOCOL_TIMER_CONFIG_PAGE) && (length != sizeof(tiSASProtocolTimerConfigurationPage_t )))  )
   {
       tiStatus = tiError;
   }
   else
   {
       returnCode = saSetControllerConfig(agRoot, 0, modePage, length, buffer, context);

       if (returnCode == AGSA_RC_SUCCESS)
       {
           tiStatus = tiSuccess;
       }
       else if (returnCode == AGSA_RC_BUSY)
       {
           tiStatus = tiBusy;
       }
       else
       {
           tiStatus = tiError;
       }
   }
   return(tiStatus);
}

/*****************************************************************************
*
* tiCOMGetControllerConfig
*
*  Purpose:  This function is called to get the controller's advanced configuration.
*            The status is reported via ostiPortEvent().
*
*  Parameters:
*
*    tiRoot:        Pointer to driver/port instance.
*    flag:          Interrupt  Vector Mask
*                   This parameter is valid only when modePage is set to TI_INTERRUPT_CONFIGURATION_PAGE.
*                   When the modePage field is set to TI_INTERRUPT_CONFIGURATION_PAGE,
*                   this field contains a bitmap of interrupt vectors for which interrupt coalescing parameters are retrieved.
*  Return:
*     tiSuccess:  The controller configuration retrival was started.
*     tiError:    The controller configuration retrival was not started.
*
*****************************************************************************/
osGLOBAL bit32  tiCOMGetControllerConfig (
         tiRoot_t    *tiRoot,
         bit32       modePage,
         bit32       flag,
         void        *context)

{
  agsaRoot_t      *agRoot;
  bit32           returnCode = AGSA_RC_BUSY;
  bit32           tiStatus = tiSuccess;
  tdsaRoot_t      *tdsaRoot ;
  tdsaContext_t   *tdsaAllShared ;

  TD_ASSERT(tiRoot, "tiRoot");
  tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  TD_ASSERT(tdsaRoot, "tdsaRoot");

  tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  TD_ASSERT(tdsaAllShared, "tdsaAllShared");
  agRoot = &(tdsaAllShared->agRootNonInt);
  TD_ASSERT(agRoot, "agRoot");

  agRoot = &(tdsaAllShared->agRootNonInt);

  TI_DBG1(("tiCOMGetControllerConfig: modePage 0x%x context %p\n",modePage,context ));

  returnCode = saGetControllerConfig(agRoot, 0, modePage, flag, 0, context);

  if (returnCode == AGSA_RC_SUCCESS)
  {
    tiStatus = tiSuccess;
    TI_DBG1(("tiCOMGetControllerConfig:modePage 0x%x tiSuccess\n",modePage ));
  }
  else if (returnCode == AGSA_RC_BUSY)
  {
    tiStatus = tiBusy;
    TI_DBG1(("tiCOMGetControllerConfig:modePage 0x%x tiBusy\n",modePage ));
  }
  else
  {
    tiStatus = tiError;
    TI_DBG1(("tiCOMGetControllerConfig:modePage 0x%x tiError\n",modePage ));
  }

   return(tiStatus);
}

/*****************************************************************************
*
* tiCOMEncryptGetInfo
*
*  Purpose:  This function is called to return information about the encryption
*            engine for the specified port.
*
*  Parameters:
*
*    tiRoot:        Pointer to driver/port instance.
*
*  Return:
*   tiSuccess       The request is being processed
*   tiNotSupported  Encryption is not supported
*
*****************************************************************************/
osGLOBAL bit32 tiCOMEncryptGetInfo(tiRoot_t *tiRoot)
{
  tiEncryptInfo_t tiEncryptInfo;
  agsaEncryptInfo_t agsaEncryptInfo;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  agsaRoot_t      *agRoot;
  tiEncryptPort_t tiEncryptPort;
  bit32           returnCode;
  bit32           tiStatus;

  agRoot = &(tdsaAllShared->agRootNonInt);

  returnCode = saEncryptGetMode(agRoot,agNULL, &agsaEncryptInfo);
  TI_DBG1(("tiCOMEncryptGetInfo: returnCode 0x%x\n", returnCode));

  if (returnCode == AGSA_RC_SUCCESS)
  {
      tiStatus = tiSuccess;

      /*
       * The data encoded in the agsaEncryptInfo must be converted
       * to match the fields of the tiEncryptInfo structure.
       *
       * No sector information is currently available.
       */
      osti_memset(&tiEncryptInfo, 0x0, sizeof(tiEncryptInfo_t));

      /* cipher mode */
      if (agsaEncryptInfo.encryptionCipherMode == agsaEncryptCipherModeXTS)
      {
         tiEncryptInfo.securityCipherMode = TI_ENCRYPT_ATTRIB_CIPHER_XTS;
      }
      /* security mode */
      if (agsaEncryptInfo.encryptionSecurityMode == agsaEncryptSMF)
      {
         tiEncryptInfo.securityCipherMode |= TI_ENCRYPT_SEC_MODE_FACT_INIT;
      }
      else if (agsaEncryptInfo.encryptionSecurityMode == agsaEncryptSMA)
      {
         tiEncryptInfo.securityCipherMode |= TI_ENCRYPT_SEC_MODE_A;
      }
      else if (agsaEncryptInfo.encryptionSecurityMode == agsaEncryptSMB)
      {
         tiEncryptInfo.securityCipherMode |= TI_ENCRYPT_SEC_MODE_B;
      }

      tiEncryptInfo.status = agsaEncryptInfo.status;

      tiEncryptPort.pData = &tiEncryptInfo;

      /* The low level returns synchronously, so fake a port event now.*/
      tiEncryptPort.encryptEvent = tiEncryptGetInfo;
      tiEncryptPort.subEvent = 0;

      ostiPortEvent(tiRoot,tiEncryptOperation,tiStatus,&tiEncryptPort);
  }
  else
  {
    if (returnCode == AGSA_RC_NOT_SUPPORTED)
    {
      tiStatus = tiNotSupported;
      TI_DBG1(("tiCOMEncryptGetInfo: tiNotSupported\n"));
    }
    else
    {
      TI_DBG1(("tiCOMEncryptGetInfo: tiError returnCode 0x%x\n",returnCode));
      tiStatus = tiError;
    }

    tiEncryptPort.pData = NULL;
  }

  return(tiStatus);
}

/*****************************************************************************
*
* tiCOMEncryptSetMode
*
*  Purpose:  This function is called to set the encryption security and cipher modes
*            for the encryption engine.
*
*  Parameters:
*
*    tiRoot:        Pointer to driver/port instance.
*
*  Return:
*   tiSuccess       The request is being processed
*   tiError         The encryption engine is not in factory init mode or multiple
*                   security modes were specified.
*
*****************************************************************************/

osGLOBAL bit32 tiCOMEncryptSetMode(tiRoot_t            *tiRoot,
                                   bit32               securityCipherMode)
{
  bit32                         returnCode;
  bit32                         tiStatus;
  agsaEncryptInfo_t mode;
  agsaEncryptInfo_t *pmode = &mode;

  tdsaRoot_t        *tdsaRoot ;
  tdsaContext_t     *tdsaAllShared;
  agsaRoot_t        *agRoot;

  TD_ASSERT(tiRoot, "tiRoot");
  tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  TD_ASSERT(tdsaRoot, "tdsaRoot");

  tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  TD_ASSERT(tdsaAllShared, "tdsaAllShared");
  agRoot = &(tdsaAllShared->agRootNonInt);
  TD_ASSERT(agRoot, "agRoot");

  pmode->encryptionSecurityMode = 0;
  pmode->encryptionCipherMode = 0;
  pmode->status = 0;

  TI_DBG1(("tiCOMEncryptSetMode:\n"));


  if(( securityCipherMode & TI_ENCRYPT_SEC_MODE_A ) == TI_ENCRYPT_SEC_MODE_A)
  {
    pmode->encryptionSecurityMode = agsaEncryptSMA;
  }

  if(( securityCipherMode & TI_ENCRYPT_SEC_MODE_B ) == TI_ENCRYPT_SEC_MODE_B)
  {
    pmode->encryptionSecurityMode = agsaEncryptSMB;
  }

  if( (securityCipherMode & TI_ENCRYPT_ATTRIB_CIPHER_XTS)  == TI_ENCRYPT_ATTRIB_CIPHER_XTS)
  {
    pmode->encryptionCipherMode |= agsaEncryptCipherModeXTS;
  }

  /* ECB is not supported in SPCv */
  if(( securityCipherMode & TI_ENCRYPT_ATTRIB_CIPHER_ECB)  == TI_ENCRYPT_ATTRIB_CIPHER_ECB)
  {
    return tiError;
  }

  returnCode = saEncryptSetMode(agRoot,agNULL, 0, pmode );

  if (returnCode == AGSA_RC_SUCCESS)
  {
    tiStatus = tiSuccess;
  }
  else if (returnCode == AGSA_RC_BUSY)
  {
    TI_DBG1(("tiCOMEncryptSetMode:tiBusy\n"));
    tiStatus = tiBusy;
  }
  else
  {
    TI_DBG1(("tiCOMEncryptSetMode:tiError\n"));
    tiStatus = tiError;
  }

  return(tiStatus);
}

/*****************************************************************************
*
* tiCOMEncryptDekAdd
*
*  Purpose:  This function is called to add a DEK to the controller cache.
*
*  Parameters:
*
*    tiRoot:          Pointer to driver/port instance.
*    kekIndext:         Index of the KEK table
*    dekTableSelect:  Number of the DEK table receiving a new entry
*    dekAddrHi:       Upper 32-bits of the DEK table physical address
*    dekAddrLo:       Lower 32-bits of the DEK table physical address
*    dekIndex:        Number of the first entry in the DEK table that will inserted in the cache
*    dekNumberOfEntries: Number of entries to be inserted in the cache
*    dekBlobFormat:     Specifies the DEK blob format
*    dekTableKeyEntrySize: Encoded value for DEK Entry Size in the DEK Table
*
*  Return:
*   tiSuccess       The request is being processed
*   tiError         An invalid parameter was specified
*
*****************************************************************************/
osGLOBAL bit32 tiCOMEncryptDekAdd(tiRoot_t            *tiRoot,
                                  bit32               kekIndex,
                                  bit32               dekTableSelect,
                                  bit32               dekAddrHi,
                                  bit32               dekAddrLo,
                                  bit32               dekIndex,
                                  bit32               dekNumberOfEntries,
                                  bit32               dekBlobFormat,
                                  bit32               dekTableKeyEntrySize

                                  )
{
    agsaRoot_t        *agRoot;
    bit32           returnCode;
    bit32           tiStatus;
    tdsaRoot_t        *tdsaRoot ;
    tdsaContext_t     *tdsaAllShared ;

    TD_ASSERT(tiRoot, "tiRoot");
    tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
    TD_ASSERT(tdsaRoot, "tdsaRoot");

    tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    TD_ASSERT(tdsaAllShared, "tdsaAllShared");
    agRoot = &(tdsaAllShared->agRootNonInt);
    TD_ASSERT(agRoot, "agRoot");


    agRoot = &(tdsaAllShared->agRootNonInt);


    TI_DBG1(("tiCOMEncryptDekAdd:\n" ));

    returnCode = saEncryptDekCacheUpdate(agRoot,
                                    agNULL,
                                    0,
                                    kekIndex,
                                    dekTableSelect,
                                    dekAddrHi,
                                    dekAddrLo,
                                    dekIndex,
                                    dekNumberOfEntries,
                                    dekBlobFormat,
                                    dekTableKeyEntrySize
                                    );

    if (returnCode == AGSA_RC_SUCCESS)
    {
        tiStatus = tiSuccess;
    }
    else if (returnCode == AGSA_RC_BUSY)
    {
        tiStatus = tiBusy;
    }
    else
    {
        tiStatus = tiError;
    }

    return(tiStatus);
}

/*****************************************************************************
*
* tiCOMEncryptDekInvalidate
*
*  Purpose:  This function is called to remove a DEK entry from the hardware cache.
*
*  Parameters:
*
*    tiRoot:        Pointer to driver/port instance.
*    dekTable       DEK table that will be affected
*    dekIndex:      DEK table entry that will be affected. The value 0xfffffff clears the cache.
*
*  Return:
*   tiSuccess       The request is being processed
*   tiError         An invalid parameter was specified
*   tiBusy          An operation is already in progress
*
*****************************************************************************/

osGLOBAL bit32 tiCOMEncryptDekInvalidate(tiRoot_t            *tiRoot,
                                         bit32               dekTable,
                                         bit32               dekIndex)
{

    tdsaRoot_t        *tdsaRoot;
    tdsaContext_t     *tdsaAllShared;

    agsaRoot_t        *agRoot;
    tiEncryptPort_t tiEncryptPort;
    tiEncryptDek_t  tiEncryptDek;
    bit32           returnCode;
    bit32           tiStatus;

    TD_ASSERT(tiRoot, "tiRoot");
    tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
    TD_ASSERT(tdsaRoot, "tdsaRoot");

    tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    TD_ASSERT(tdsaAllShared, "tdsaAllShared");
    agRoot = &(tdsaAllShared->agRootNonInt);
    TD_ASSERT(agRoot, "agRoot");

    TI_DBG1(("tiCOMEncryptDekInvalidate:dekTable 0x%x dekIndex 0x%x\n", dekTable, dekIndex));

    returnCode = saEncryptDekCacheInvalidate(agRoot, agNULL, 0, dekTable, dekIndex);

    if (returnCode == AGSA_RC_SUCCESS)
    {
        tiStatus = tiSuccess;
    }
    else
    {
        if (returnCode == AGSA_RC_NOT_SUPPORTED)
        {
            tiStatus = tiNotSupported;
        }
        else if (returnCode == AGSA_RC_BUSY)
        {
            tiStatus = tiBusy;
        }
        else
        {
            tiStatus = tiError;
        }

        tiEncryptDek.dekTable = dekTable;
        tiEncryptDek.dekIndex = dekIndex;

        tiEncryptPort.encryptEvent = tiEncryptDekInvalidate;
        tiEncryptPort.subEvent = 0;
        tiEncryptPort.pData = (void *) &tiEncryptDek;

        ostiPortEvent(tiRoot,tiEncryptOperation,tiStatus,&tiEncryptPort);
    }

    return(tiStatus);
}

/*****************************************************************************
*
* tiCOMEncryptKekAdd
*
*  Purpose:  This function is called to add a KEK in the register specified by
*            the index parameter.
*
*  Parameters:
*
*    tiRoot:           Pointer to driver/port instance.
*    kekIndex:         KEK table entry that will be affected
*    wrapperKekIndex   KEK table entry that encrypt the KEK blob
*    encryptKekBlob    KEK blob that will be added
*
*  Return:
*   tiSuccess       The request is being processed
*   tiError         An invalid parameter was specified
*   tiBusy          A KEK operation is already in progress
*
*****************************************************************************/

osGLOBAL bit32 tiCOMEncryptKekAdd(tiRoot_t            *tiRoot,
                                  bit32               kekIndex,
                                  bit32               wrapperKekIndex,
                                  bit32               blobFormat,
                                  tiEncryptKekBlob_t *encryptKekBlob)
{
  tdsaRoot_t        *tdsaRoot;
  tdsaContext_t     *tdsaAllShared;
  agsaRoot_t        *agRoot;

  bit32           returnCode= AGSA_RC_BUSY;
  bit32           tiStatus= tiError;

  tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

  agRoot = &(tdsaAllShared->agRootNonInt);

  TI_DBG1(("tiCOMEncryptDekInvalidate: kekIndex 0x%x wrapperKekIndex 0x%x\n", kekIndex , wrapperKekIndex));

  returnCode = saEncryptKekUpdate(agRoot,
                                  agNULL,
                                  0,
                                  AGSA_ENCRYPT_STORE_NVRAM,
                                  kekIndex,
                                  wrapperKekIndex,
                                  blobFormat,
                                  (agsaEncryptKekBlob_t *) encryptKekBlob);

  if (returnCode == AGSA_RC_SUCCESS)
  {
    tiStatus = tiSuccess;
  }
  else if (returnCode == AGSA_RC_BUSY)
  {
    tiStatus = tiBusy;
  }
  else
  {
    tiStatus = tiError;
  }

  return(tiStatus);
}
#ifdef HIALEAH_ENCRYPTION

osGLOBAL bit32 tiCOMEncryptHilSet(tiRoot_t            *tiRoot )
{
  tdsaRoot_t        *tdsaRoot;
  tdsaContext_t     *tdsaAllShared;
  agsaRoot_t        *agRoot;
  agsaEncryptInfo_t agsaEncryptInfo;

  bit32           returnCode= tiBusy;
  bit32           tiStatus= tiError;

  tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

  agRoot = &(tdsaAllShared->agRootNonInt);


  returnCode = saEncryptGetMode(agRoot,agNULL, &agsaEncryptInfo);
  TI_DBG1(("tiCOMEncryptHilSet: saEncryptGetMode returnCode 0x%x agsaEncryptInfo status 0x%x Smode 0x%x CMode 0x%x\n",
          returnCode,
          agsaEncryptInfo.status,
          agsaEncryptInfo.encryptionSecurityMode,
          agsaEncryptInfo.encryptionCipherMode ));

  if (returnCode == AGSA_RC_FAILURE)
  {
   TI_DBG1(("tiCOMEncryptHilSet:agsaEncryptInfo.status 0x%x\n",agsaEncryptInfo.status ));
    if(agsaEncryptInfo.status == 0x81)
    {
   	  TI_DBG1(("tiCOMEncryptHilSet: status 0x80 KEY CARD MISMATCH agsaEncryptInfo.status 0x%x\n",agsaEncryptInfo.status ));
      returnCode = saEncryptHilUpdate(agRoot,
                                      agNULL,
                                      0 );
      if (returnCode == AGSA_RC_SUCCESS)
      {
        TI_DBG1(("tiCOMEncryptHilSet:AGSA_RC_SUCCESS\n"));
      }
    }
	else if(agsaEncryptInfo.status == 0x80)
    {
   		ostidisableEncryption(tiRoot);
		TI_DBG1(("tiCOMEncryptHilSet: status 0x80 KEY CARD MISSING agsaEncryptInfo.status 0x%x\n",agsaEncryptInfo.status ));
    	returnCode = AGSA_RC_SUCCESS;
	}
    else
    {
      TI_DBG1(("tiCOMEncryptHilSet: not status 0x81 agsaEncryptInfo.status 0x%x\n",agsaEncryptInfo.status ));
      returnCode = AGSA_RC_FAILURE;
    }
  }

  if (returnCode == AGSA_RC_SUCCESS)
  {
    tiStatus = tiSuccess;
  }
  else if (returnCode == AGSA_RC_BUSY)
  {
    TI_DBG1(("tiCOMEncryptHilSet:AGSA_RC_BUSY\n"));
    tiStatus = tiBusy;
  }
  else
  {
    TI_DBG1(("tiCOMEncryptHilSet:tiError\n"));
    tiStatus = tiError;
  }

  return(tiStatus);
}
#endif /* HIALEAH_ENCRYPTION */

/*****************************************************************************
*
* tiCOMEncryptKekStore
*
*  Purpose:  This function is called to store a KEK in NVRAM. If -1 is specified
*            as the KEK index, then all KEKs will be stored.
*
*  Parameters:
*
*    tiRoot:        Pointer to driver/port instance.
*    kekIndex:      The KEK to be stored in NVRAM
*
*  Return:
*   tiSuccess       The request is being processed
*   tiError         An invalid parameter was specified
*   tiBusy          A KEK operation is already in progress
*
*****************************************************************************/

osGLOBAL bit32 tiCOMEncryptKekStore(tiRoot_t  *tiRoot,
                                    bit32      kekIndex)
{
#ifdef NOT_YET
    tdsaRoot_t        *tdsaRoot;
    tdsaContext_t     *tdsaAllShared;
    agsaRoot_t        *agRoot;
#endif
/*
    bit32           returnCode= AGSA_RC_BUSY;
*/
    bit32           tiStatus = tiError;

#ifdef NOT_YET
    tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
    tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

    agRoot = &(tdsaAllShared->agRootNonInt);
#endif

    TI_DBG1(("tiCOMEncryptKekStore: Needs code !!!! kekIndex 0x%x\n", kekIndex ));
/*
    returnCode = fcEncryptKekStore(agRoot, kekIndex);

    if (returnCode == AGSA_RC_SUCCESS)
    {
        tiStatus = tiSuccess;
    }
    else if (returnCode == AGSA_RC_BUSY)
    {
        tiStatus = tiBusy;
    }
    else
    {
        tiStatus;
    }
*/
    return(tiStatus);
}

/*****************************************************************************
*
* tiCOMEncryptKekLoad
*
*  Purpose:  This function is called to load a KEK from NVRAM. If -1 is specified
*            as the KEK index, then all KEKs will be loaded.
*
*  Parameters:
*
*    tiRoot:        Pointer to driver/port instance.
*    kekIndex:      The KEK to be loaded in NVRAM
*
*  Return:
*   tiSuccess       The request is being processed
*   tiError         An invalid parameter was specified
*   tiBusy          A KEK operation is already in progress
*
*****************************************************************************/

osGLOBAL bit32 tiCOMEncryptKekLoad(tiRoot_t            *tiRoot,
                                   bit32               kekIndex)
{
#ifdef NOT_YET
    tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    agsaRoot_t      *agRoot;
    //bit32           returnCode;
#endif
    bit32           tiStatus = tiError;

#ifdef NOT_YET
    agRoot = &(tdsaAllShared->agRootNonInt);
#endif
/*
    returnCode = fcEncryptKekLoad(agRoot, kekIndex);

    if (returnCode == AGSA_RC_SUCCESS)
    {
        tiStatus = tiSuccess;
    }
    else if (returnCode == AGSA_RC_BUSY)
    {
        tiStatus = tiBusy;
    }
    else
    {
        tiStatus = tiError;
    }
*/
    return(tiStatus);
}

/*****************************************************************************
*
* tiCOMEncryptSelfTest
*
*  Purpose:  This function starts the encryption self test. For the encryption self test, IOs must be quiesced.
*                The completion of this function is via ostiPortEvent().
*
*  Parameters:
*
*    tiRoot:      Pointer to driver/port instance.
*    type:        Types of test
                      0x1: tiBISTTest
                      0x2: tiHMACTest
                      Others are reserved.
*    length:
                   Size of the test descriptor in bytes, e.g.,
                   Sizeof(tiEncryptSelfTestDescriptor_t)
                   Sizeof(tiEncryptHMACTestDescriptor_t)
*    TestDescriptor       address of the test descriptor structure.
*
*  Return:
*   tiSuccess     The request is being processed
*   tiError          An invalid parameter was specified
*   tiBusy          A encrytion operation is already in progress
*
*****************************************************************************/
osGLOBAL bit32 tiCOMEncryptSelfTest(
                        tiRoot_t  *tiRoot,
                        bit32      type,
                        bit32      length,
                        void      *TestDescriptor
                        )
{
  tdsaRoot_t     *tdsaRoot       = agNULL;
  tdsaContext_t  *tdsaAllShared  = agNULL;
  agsaRoot_t     *agRoot         = agNULL;

  bit32           returnCode     = AGSA_RC_BUSY;
  bit32           tiStatus       = tiError;

  tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  TD_ASSERT(tdsaRoot != agNULL, "tdsaRoot is NULL !!!");

  tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  TD_ASSERT(tdsaAllShared != agNULL, "tdsaAllShared is NULL !!!");

  agRoot = &(tdsaAllShared->agRootNonInt);
  TD_ASSERT(agRoot != agNULL, "agRoot is NULL !!!");

  TI_DBG1(("tiCOMEncryptSelfTest: type =  0x%x length = 0x%x\n", type, length));

  /*do some sanity checking */
  if ( ((type == TI_ENCRYPTION_TEST_TYPE_BIST) && (length != sizeof(tiEncryptSelfTestDescriptor_t )))  ||
       ((type == TI_ENCRYPTION_TEST_TYPE_HMAC) && (length != sizeof(tiEncryptHMACTestDescriptor_t))) )
  {
    TI_DBG1(("tiCOMEncryptSelfTest: type or length error, type 0x%x length 0x%x\n", type, length));
    tiStatus = tiError;
  }
  else
  {
    returnCode = saEncryptSelftestExecute(agRoot,
                                      agNULL,
                                      0,
                                      type,
                                      length,
                                      TestDescriptor
                                      );

    if (returnCode == AGSA_RC_SUCCESS)
    {
      tiStatus = tiSuccess;
    }
    else if (returnCode == AGSA_RC_BUSY)
    {
      tiStatus = tiBusy;
    }
    else
    {
      tiStatus = tiError;
    }
  }

  return(tiStatus);
}

/*****************************************************************************
*
* tiCOMSetOperator
*
*  Purpose:  This function is called to login to or logout out from the controller by an operator.
                  The status is reported via ostiPortEvent().
*
*  Parameters:
*
*    tiRoot:      Pointer to driver/port instance.
*    flag:         operator flag.
                     Bits 0-3: Access type (ACS)
                       0x1: Login
                       0x2: Logout
                       All others are reserved.
                     Bit 4: KEYopr pinned in the KEK table (PIN)
                       0: Not pinned. Operator ID table will be searched during authentication.
                       1: Pinned. OPRIDX is referenced to unwrap the certificate.
                     Bits 5-7: Reserved
                     Bits 8-15: KEKopr Index in the KEK Table (OPRIDX). If KEKopr is pinned in the KEK table, OPRIDX is to reference the KEK for authentication
                     Bits 16-31: Reserved.

     cert:         The pointer to the operator's certificate. The size of the certificate is 40 bytes.
*
*  Return:
*   tiSuccess     Log in or log out was started.
*   tiError          Log in or log out was not started.
*   tiBusy          A operator management operation is already in progress
*
*****************************************************************************/
osGLOBAL bit32 tiCOMSetOperator(
                        tiRoot_t      *tiRoot,
                        bit32          flag,
                        void          *cert
                        )
{
  tdsaRoot_t     *tdsaRoot       = agNULL;
  tdsaContext_t  *tdsaAllShared  = agNULL;
  agsaRoot_t     *agRoot         = agNULL;

  bit32           returnCode     = AGSA_RC_FAILURE;
  bit32           tiStatus       = tiError;

  tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  TD_ASSERT(tdsaRoot != agNULL, "tdsaRoot is NULL !!!");

  tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  TD_ASSERT(tdsaAllShared != agNULL, "tdsaAllShared is NULL !!!");

  agRoot = &(tdsaAllShared->agRootNonInt);
  TD_ASSERT(agRoot != agNULL, "agRoot is NULL !!!");

  TI_DBG1(("tiCOMSetOperator: flag =  0x%x \n", flag));

  returnCode = saSetOperator(agRoot,
                             agNULL,
                             0,
                             flag,
                             cert);

  if (returnCode == AGSA_RC_SUCCESS)
  {
    tiStatus = tiSuccess;
  }
  else if (returnCode == AGSA_RC_BUSY)
  {
    tiStatus = tiBusy;
  }
  else
  {
    tiStatus = tiError;
  }

  return(tiStatus);
}

/*****************************************************************************
*
* tiCOMGetOperator
*
*  Purpose:  This function is used to retrieve the role and ID of the current operator or all operators.
                  The status is reported via ostiPortEvent().
*
*  Parameters:
*
*    tiRoot:      Pointer to driver/port instance.
*    option:     Types of get operations
                       0x1: Current operator only
                       0x2: All operators
                       All others are reserved.
      AddrHi      Upper 32-bit host physical address to store operator certificates.
                    This field is used only when option is 0x2
      AddrLo     Lower 32-bit host physical address to store operator certificates.
                    This field is used only when option is 0x2
*
*  Return:
*   tiSuccess     The operation was started..
*   tiError          The operation was not started.
*   tiBusy          A operator management operation is already in progress
*
*****************************************************************************/
osGLOBAL bit32 tiCOMGetOperator(
                           tiRoot_t   *tiRoot,
                           bit32       option,
                           bit32       AddrHi,
                           bit32       AddrLo
                           )
{
  tdsaRoot_t     *tdsaRoot       = agNULL;
  tdsaContext_t  *tdsaAllShared  = agNULL;
  agsaRoot_t     *agRoot         = agNULL;

  bit32           returnCode     = AGSA_RC_FAILURE;
  bit32           tiStatus       = tiError;

  tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  TD_ASSERT(tdsaRoot != agNULL, "tdsaRoot is NULL !!!");

  tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  TD_ASSERT(tdsaAllShared != agNULL, "tdsaAllShared is NULL !!!");

  agRoot = &(tdsaAllShared->agRootNonInt);
  TD_ASSERT(agRoot != agNULL, "agRoot is NULL !!!");

  TI_DBG1(("tiCOMGetOperator: option = 0x%x \n", option));

  returnCode = saGetOperator(agRoot,
                             agNULL,
                             0,
                             option,
                             AddrHi,
                             AddrLo);

  if (returnCode == AGSA_RC_SUCCESS)
  {
    tiStatus = tiSuccess;
  }
  else if (returnCode == AGSA_RC_BUSY)
  {
    tiStatus = tiBusy;
  }
  else
  {
    tiStatus = tiError;
  }

  return(tiStatus);
}

/*****************************************************************************
*
* tiCOMOperationManagement
*
*  Purpose:  this function is used to manage operators,  e.g. adding or deleting an operator..
*
*  Parameters:
*
*   tiRoot:      Pointer to driver/port instance.
*   flag:         operation flag.
                    Bits 0-7: Operator Management Operation(OMO)
                       0: Add an operator.
                       1: Delete an operator.
                       2: Delete all operators.
                       Others are reserved.
                    Bit 8: Pinned to KEK RAM (PKR)
                      0: Operator's KEK is stored in the operator ID table(OID_TLB) only.
                      1: Operator's KEK is pinned to the internal KEK RAM (1 of the 16 entries) and is also stored in OID_TLB.
                    Bits 9-10: KEKopr blob format (KBF)
                      00b: Reserved.
                      01b: AGSA_ENCRYPTED_KEK_PMCA.
                      10b: AGSA_ENCRYPTED_KEK_PMCB.
                      11b: Reserved.
                    Bits 11-15: Reserved
                    Bits 16-23: KEKauth Index in the KEK Table (AUTIDX)
                    Bits 24-31: KEKopr Index in the KEK Table (OPRIDX). This field is valid only when PKR is 1.

       role        Role
                       01b: Crypto officer role.
                       10b: User role.
                       All others are reserved.

*    idString:         Pointer to the tiID_t structure describing the ID string
*    kekBlob          Pointer to the tiEncryptKekBlob_t structure describing KBLOB.
*
*  Return:
*   tiSuccess     The request is being processed
*   tiError          An invalid parameter was specified
*   tiBusy          A operator management operation is already in progress
*
*****************************************************************************/
osGLOBAL bit32 tiCOMOperatorManagement(
                        tiRoot_t            *tiRoot,
                        bit32                flag,
                        bit8                 role,
                        tiID_t              *idString,
                        tiEncryptKekBlob_t  *kekBlob
                        )
{
  tdsaRoot_t     *tdsaRoot       = agNULL;
  tdsaContext_t  *tdsaAllShared  = agNULL;
  agsaRoot_t     *agRoot         = agNULL;

  bit32           returnCode     = AGSA_RC_BUSY;
  bit32           tiStatus       = tiError;

  tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  TD_ASSERT(tdsaRoot != agNULL, "tdsaRoot is NULL !!!");

  tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  TD_ASSERT(tdsaAllShared != agNULL, "tdsaAllShared is NULL !!!");

  agRoot = &(tdsaAllShared->agRootNonInt);
  TD_ASSERT(agRoot != agNULL, "agRoot is NULL !!!");

  TI_DBG1(("tiCOMOperatorManagement: flag =  0x%x role = 0x%x\n", flag, role));

  returnCode = saOperatorManagement(agRoot,
                                    agNULL,
                                    0,
                                    flag,
                                    role,
                                    (agsaID_t*)idString,
                                    (agsaEncryptKekBlob_t *)kekBlob
                                    );

  if (returnCode == AGSA_RC_SUCCESS)
  {
    tiStatus = tiSuccess;
  }
  else if (returnCode == AGSA_RC_BUSY)
  {
    tiStatus = tiBusy;
  }
  else
  {
    tiStatus = tiError;
  }

  return(tiStatus);
}

/*****************************************************************************
*! \brief tdssRemoveSASSATAFromSharedcontext
*
*  Purpose:  This function removes all discovered devices belonging to
*            a given portcontext from device list
*
*
*  \param   agRoot                   Pointer to the root data structure of
*                                    TD and Lower layer
*  \param   tsddPortContext_Instance Pointer to the target port context
*
*  \Return: none
*
*****************************************************************************/
#ifdef INITIATOR_DRIVER                     /*TBD: added to compile tgt_drv. (TP)*/
osGLOBAL void
tdssRemoveSASSATAFromSharedcontext(
                          agsaRoot_t           *agRoot,
                          tdsaPortContext_t    *PortContext_Instance
                          )
{
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

  TI_DBG1(("tdssRemoveSASSATAFromSharedcontext: start\n"));
  TI_DBG1(("tdssRemoveSASSATAFromSharedcontext: pid %d\n", PortContext_Instance->id));

  /* find oneDeviceData belonging to the portcontext */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tdssRemoveSASSATAFromSharedcontext: oneDeviceData is NULL!!!\n"));
      return;
    }
    if (oneDeviceData->tdPortContext == PortContext_Instance)
    {
      TI_DBG1(("tdssRemoveSASSATAFromSharedcontext: pid %d did %d\n", PortContext_Instance->id, oneDeviceData->id));
      TI_DBG1(("tdssRemoveSASSATAFromSharedcontext: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
      TI_DBG1(("tdssRemoveSASSATAFromSharedcontext: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

      /* reset valid bit */
      oneDeviceData->valid = agFALSE;
      oneDeviceData->valid2 = agFALSE;
      oneDeviceData->registered = agFALSE;
      /* notify only reported devices to OS layer*/
      if ( DEVICE_IS_SSP_TARGET(oneDeviceData) ||
           DEVICE_IS_STP_TARGET(oneDeviceData) ||
           DEVICE_IS_SATA_DEVICE(oneDeviceData)
        )
      {
        ostiInitiatorEvent(
                         tiRoot,
                         PortContext_Instance->tiPortalContext,
                         &(oneDeviceData->tiDeviceHandle),
                         tiIntrEventTypeDeviceChange,
                         tiDeviceRemoval,
                         agNULL
                         );
      }
      DeviceListList = DeviceListList->flink;
    /* to-do: deregister */
#ifdef REMOVED  /* don't remove device from the device list. May screw up ordering */
      TDLIST_DEQUEUE_THIS(&(oneDeviceData->MainLink));
      TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));
#endif
    }
    else
    {
      TI_DBG6(("tdssRemoveSASSATAFromSharedcontext: move to the next\n"));
      DeviceListList = DeviceListList->flink;
    }
  } /* while */

  return;
}

/*****************************************************************************
*! \brief tdssRemoveSASSATAFromSharedcontextByReset
*
*  Purpose:  This function removes all ports and discovered devices
*
*
*  \param   agRoot                   Pointer to the root data structure of
*                                    TD and Lower layer
*
*  \Return: none
*
*****************************************************************************/
osGLOBAL void
tdssRemoveSASSATAFromSharedcontextByReset(
                                          agsaRoot_t           *agRoot
                                          )
{
  tdsaPortContext_t *onePortContext = agNULL;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *PortContextList;
  tdList_t          *DeviceListList;
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
#ifdef FDS_DM
  dmRoot_t          *dmRoot = agNULL;
  dmPortContext_t   *dmPortContext = agNULL;
  dmPortInfo_t      dmPortInfo;
#endif
#ifdef FDS_SM
  smRoot_t          *smRoot = &(tdsaAllShared->smRoot);
  smDeviceHandle_t  *smDeviceHandle = agNULL;
  agsaDevHandle_t   *agDevHandle = agNULL;
#endif

  TI_DBG1(("tdssRemoveSASSATAFromSharedcontextByReset: start\n"));

#ifdef FDS_DM
  dmRoot = &(tdsaAllShared->dmRoot);
#endif
  /* looping throuhg all portcontext */
  PortContextList = tdsaAllShared->MainPortContextList.flink;
  while (PortContextList != &(tdsaAllShared->MainPortContextList))
  {
    onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, PortContextList);
    if (onePortContext == agNULL)
    {
      TI_DBG1(("tdssRemoveSASSATAFromSharedcontextByReset: onePortContext is NULL!!!\n"));
      return;
    }
    TI_DBG1(("tdssRemoveSASSATAFromSharedcontextByReset: oneportContext pid %d\n", onePortContext->id));
    TI_DBG3(("tdssRemoveSASSATAFromSharedcontextByReset: sasAddressHi 0x%08x\n", onePortContext->sasLocalAddressHi));
    TI_DBG3(("tdssRemoveSASSATAFromSharedcontextByReset: sasAddressLo 0x%08x\n", onePortContext->sasLocalAddressLo));
#ifdef FDS_DM
    if (onePortContext->UseDM == agTRUE)
    {
      dmPortContext = &(onePortContext->dmPortContext);
      dmDestroyPort(dmRoot, dmPortContext, &dmPortInfo);
    }
#endif

    tdsaPortContextReInit(tiRoot, onePortContext);

    PortContextList = PortContextList->flink;
    tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
    TDLIST_DEQUEUE_THIS(&(onePortContext->MainLink));
    TDLIST_ENQUEUE_AT_TAIL(&(onePortContext->FreeLink), &(tdsaAllShared->FreePortContextList));
    tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
  }

  /* reinitialize the device data belonging to this portcontext */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tdssRemoveSASSATAFromSharedcontextByReset: oneDeviceData is NULL!!!\n"));
      return;
    }

    TI_DBG1(("tdssRemoveSASSATAFromSharedcontextByReset: did %d\n", oneDeviceData->id));
    TI_DBG1(("tdssRemoveSASSATAFromSharedcontextByReset: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
    TI_DBG1(("tdssRemoveSASSATAFromSharedcontextByReset: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

#ifdef FDS_SM
    agDevHandle = oneDeviceData->agDevHandle;
    smDeviceHandle = (smDeviceHandle_t *)&(oneDeviceData->smDeviceHandle);
    smDeregisterDevice(smRoot, agDevHandle, smDeviceHandle);
#endif

    tdsaDeviceDataReInit(tiRoot, oneDeviceData);

    DeviceListList = DeviceListList->flink;
    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    osti_memset(&(oneDeviceData->satDevData.satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));
    TDLIST_DEQUEUE_THIS(&(oneDeviceData->MainLink));
    TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceList));
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    /* no dequeue from Mainlink for consistant ordering of devices */
  }


  return;
}

#endif


/*****************************************************************************
*! \brief tdssAddSASToSharedcontext
*
*  Purpose:  This function adds a discovered device to a device list of
*            a shared context
*
*  \param   tsddPortContext_Instance Pointer to the target port context
*  \param   agRoot                   Pointer to the root data structure of
*                                    TD and Lower layer
*  \param   agDevHandle              Pointer to a device handle
*
*  \Return: none
*
*****************************************************************************/
osGLOBAL void
tdssAddSASToSharedcontext(
                          tdsaPortContext_t    *tdsaPortContext_Instance,
                          agsaRoot_t           *agRoot,
                          agsaDevHandle_t      *agDevHandle, /* this is NULL */
                          tdsaSASSubID_t       *agSASSubID,
                          bit32                 registered, /* no longer in use */
                          bit8                  phyID,
                          bit32                 flag
                          )
{

  tdsaPortContext_t *onePortContext = agNULL;
  tdList_t          *PortContextList;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  bit32             new_device = agTRUE;
  bit32             Indenom = tdsaAllShared->QueueConfig.numInboundQueues;
  bit32             Outdenom = tdsaAllShared->QueueConfig.numOutboundQueues;
  bit8              dev_s_rate = 0;
  bit8              sasorsata = 1;
  bit8              connectionRate;
  bit32             found = agFALSE;

  TI_DBG3(("tdssAddSASToSharedcontext: start\n"));
  /*
    find a right portcontext
    then, get devicedata from FreeLink in DeviceList
    then, do pointer operations
    then, add the devicedata to the portcontext
  */

  /* find a right portcontext */
  PortContextList = tdsaAllShared->MainPortContextList.flink;
  while (PortContextList != &(tdsaAllShared->MainPortContextList))
  {
    onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, PortContextList);
    if (onePortContext == tdsaPortContext_Instance)
    {
      TI_DBG3(("tdssAddSASToSharedContext: found; oneportContext ID %d\n", onePortContext->id));
      found = agTRUE;
      break;
    }
    PortContextList = PortContextList->flink;
  }

  if (found == agTRUE)
  {
    TI_DBG3(("tdssAddSASToSharedcontext: found pid %d\n", onePortContext->id));
  }
  else
  {
    TI_DBG1(("tdssAddSASToSharedcontext: Error!!! no portcontext found!!!\n"));
    return;
  }

  /* find a device's existence */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tdssAddSASToSharedcontext: oneDeviceData is NULL!!!\n"));
      return;
    }
    if ((oneDeviceData->SASAddressID.sasAddressHi == agSASSubID->sasAddressHi) &&
        (oneDeviceData->SASAddressID.sasAddressLo == agSASSubID->sasAddressLo) &&
        (oneDeviceData->tdPortContext == onePortContext)
         )
    {
      TI_DBG1(("tdssAddSASToSharedcontext: pid %d did %d\n", onePortContext->id, oneDeviceData->id));
      new_device = agFALSE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }

  /* new device */
  if (new_device == agTRUE)
  {
    TI_DBG3(("tdssAddSASToSharedcontext: new device\n"));

    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    if (!TDLIST_NOT_EMPTY(&(tdsaAllShared->FreeDeviceList)))
    {
      tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
      TI_DBG1(("tdssAddSASToSharedContext: empty DeviceData FreeLink\n"));
      return;
    }

    TDLIST_DEQUEUE_FROM_HEAD(&DeviceListList, &(tdsaAllShared->FreeDeviceList));
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, FreeLink, DeviceListList);

    TI_DBG3(("tdssAddSASToSharedcontext: oneDeviceData %p\n", oneDeviceData));

    onePortContext->Count++;
    oneDeviceData->DeviceType = TD_SAS_DEVICE;
    oneDeviceData->agRoot = agRoot;

    if (flag == TD_OPERATION_TARGET)
    {
      oneDeviceData->agDevHandle = agDevHandle;
      agDevHandle->osData = oneDeviceData; /* TD layer */
    }

    /* saving sas address */
    oneDeviceData->SASAddressID.sasAddressLo = agSASSubID->sasAddressLo;
    oneDeviceData->SASAddressID.sasAddressHi = agSASSubID->sasAddressHi;
    oneDeviceData->initiator_ssp_stp_smp = agSASSubID->initiator_ssp_stp_smp;
    oneDeviceData->target_ssp_stp_smp = agSASSubID->target_ssp_stp_smp;

    oneDeviceData->tdPortContext = onePortContext;
    oneDeviceData->valid = agTRUE;

    /* new */
    oneDeviceData->directlyAttached = agTRUE;
    /* parse sasIDframe to fill in agDeviceInfo */
    DEVINFO_PUT_SMPTO(&oneDeviceData->agDeviceInfo, DEFAULT_SMP_TIMEOUT);
    DEVINFO_PUT_ITNEXUSTO(&oneDeviceData->agDeviceInfo, (bit16)tdsaAllShared->itNexusTimeout);
    DEVINFO_PUT_FBS(&oneDeviceData->agDeviceInfo, 0);
    /* enable TLR */
    DEVINFO_PUT_FLAG(&oneDeviceData->agDeviceInfo, 1);

    sasorsata = SAS_DEVICE_TYPE; /* SAS target (SAS disk or expander) */
    connectionRate = onePortContext->LinkRate; 
    dev_s_rate = (bit8)(dev_s_rate | (sasorsata << 4));
    dev_s_rate = (bit8)(dev_s_rate | connectionRate);
    DEVINFO_PUT_DEV_S_RATE(&oneDeviceData->agDeviceInfo, dev_s_rate);


    DEVINFO_PUT_SAS_ADDRESSLO(
                              &oneDeviceData->agDeviceInfo,
                              agSASSubID->sasAddressLo
                              );
    DEVINFO_PUT_SAS_ADDRESSHI(
                              &oneDeviceData->agDeviceInfo,
                              agSASSubID->sasAddressHi
                              );

    oneDeviceData->agContext.osData = oneDeviceData;
    oneDeviceData->agContext.sdkData = agNULL;

    if (flag == TD_OPERATION_INITIATOR)
    {
      if (oneDeviceData->registered == agFALSE )
      {
        if( tdsaAllShared->sflag )
        {
          if( ! DEVICE_IS_SMP_TARGET(oneDeviceData))
          {
            TI_DBG1(("tdssAddSASToSharedcontext: First, saRegisterNewDevice sflag %d\n", tdsaAllShared->sflag));
            oneDeviceData->agDeviceInfo.flag = oneDeviceData->agDeviceInfo.flag | TD_XFER_RDY_PRIORTY_DEVICE_FLAG;
          }
        }

        saRegisterNewDevice( /* tdssAddSASToSharedcontext */
                            agRoot,
                            &oneDeviceData->agContext,
                            0,
                            &oneDeviceData->agDeviceInfo,
                            onePortContext->agPortContext,
                            0
                           );
      }
    }
    oneDeviceData->phyID = phyID;
    oneDeviceData->InQID = oneDeviceData->id % Indenom;

#ifdef TARGET_DRIVER
    {
      bit32 localId = oneDeviceData->id;
      localId += 1;
      oneDeviceData->OutQID = localId % Outdenom;
      TI_DBG1(("tdssAddSASToSharedcontext: OutQID %d\n", oneDeviceData->OutQID)); /* tdsaRotateQnumber for tgt*/

    }
#endif /* TARGET_DRIVER */

    TI_DBG4(("tdssAddSASToSharedcontext: SSP target %d STP target %d SATA device %d\n", DEVICE_IS_SSP_TARGET(oneDeviceData), DEVICE_IS_STP_TARGET(oneDeviceData), DEVICE_IS_SATA_DEVICE(oneDeviceData)));
    /* add the devicedata to the portcontext */
    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->MainLink), &(tdsaAllShared->MainDeviceList));
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    TI_DBG4(("tdssAddSASToSharedContext: one case pid %d did %d \n", onePortContext->id, oneDeviceData->id));
    TI_DBG4(("tdssAddSASToSharedContext: new case pid %d did %d phyID %d\n", onePortContext->id, oneDeviceData->id, oneDeviceData->phyID));

  }
  else /* old device */
  {
    TI_DBG3(("tdssAddSASToSharedcontext: old device\n"));
    TI_DBG3(("tdssAddSASToSharedcontext: oneDeviceData %p\n", oneDeviceData));

    oneDeviceData->DeviceType = TD_SAS_DEVICE;
    oneDeviceData->agRoot = agRoot;

    if (flag == TD_OPERATION_TARGET)
    {
      oneDeviceData->agDevHandle = agDevHandle;
      agDevHandle->osData = oneDeviceData; /* TD layer */
    }

    /* saving sas address */
    oneDeviceData->SASAddressID.sasAddressLo = agSASSubID->sasAddressLo;
    oneDeviceData->SASAddressID.sasAddressHi = agSASSubID->sasAddressHi;
    oneDeviceData->initiator_ssp_stp_smp = agSASSubID->initiator_ssp_stp_smp;
    oneDeviceData->target_ssp_stp_smp = agSASSubID->target_ssp_stp_smp;


    oneDeviceData->tdPortContext = onePortContext;
    oneDeviceData->valid = agTRUE;

    oneDeviceData->directlyAttached = agTRUE;
    /* new */
    if (oneDeviceData->registered == agFALSE)
    {
      TI_DBG1(("tdssAddSASToSharedcontext: registering\n"));
      /* parse sasIDframe to fill in agDeviceInfo */
      DEVINFO_PUT_SMPTO(&oneDeviceData->agDeviceInfo, DEFAULT_SMP_TIMEOUT);
      DEVINFO_PUT_ITNEXUSTO(&oneDeviceData->agDeviceInfo, (bit16)tdsaAllShared->itNexusTimeout);
      DEVINFO_PUT_FBS(&oneDeviceData->agDeviceInfo, 0);
      DEVINFO_PUT_FLAG(&oneDeviceData->agDeviceInfo, 1);

      sasorsata = SAS_DEVICE_TYPE; /* SAS target (SAS disk or expander) */
      connectionRate = onePortContext->LinkRate; 
      dev_s_rate = (bit8)(dev_s_rate | (sasorsata << 4));
      dev_s_rate = (bit8)(dev_s_rate | connectionRate);
      DEVINFO_PUT_DEV_S_RATE(&oneDeviceData->agDeviceInfo, dev_s_rate);


      DEVINFO_PUT_SAS_ADDRESSLO(
                                &oneDeviceData->agDeviceInfo,
                                agSASSubID->sasAddressLo
                                );
      DEVINFO_PUT_SAS_ADDRESSHI(
                                &oneDeviceData->agDeviceInfo,
                                agSASSubID->sasAddressHi
                                );

      oneDeviceData->agContext.osData = oneDeviceData;
      oneDeviceData->agContext.sdkData = agNULL;

      if (flag == TD_OPERATION_INITIATOR)
      {
        if( tdsaAllShared->sflag )
        {
          if( ! DEVICE_IS_SMP_TARGET(oneDeviceData))
          {
            TI_DBG1(("tdssAddSASToSharedcontext: Second, saRegisterNewDevice sflag %d\n", tdsaAllShared->sflag));
            oneDeviceData->agDeviceInfo.flag = oneDeviceData->agDeviceInfo.flag | TD_XFER_RDY_PRIORTY_DEVICE_FLAG;
          }
        }
        saRegisterNewDevice( /* tdssAddSASToSharedcontext */
                            agRoot,
                            &oneDeviceData->agContext,
                            0,
                            &oneDeviceData->agDeviceInfo,
                            onePortContext->agPortContext,
                            0
                            );
      }
    }






    oneDeviceData->phyID = phyID;
    oneDeviceData->InQID = oneDeviceData->id % Indenom;
    oneDeviceData->OutQID = oneDeviceData->id % Outdenom;

    TI_DBG1(("tdssAddSASToSharedcontext: A OutQID %d\n", oneDeviceData->OutQID));
    TI_DBG4(("tdssAddSASToSharedcontext: SSP target %d STP target %d SATA device %d\n", DEVICE_IS_SSP_TARGET(oneDeviceData), DEVICE_IS_STP_TARGET(oneDeviceData), DEVICE_IS_SATA_DEVICE(oneDeviceData)));
    TI_DBG4(("tdssAddSASToSharedContext: old case pid %d did %d phyID %d\n", onePortContext->id, oneDeviceData->id, oneDeviceData->phyID));
  }

  return;
}




/*****************************************************************************
*! \brief tdssRemoveDevicedataFromSharedcontext
*
*  Purpose:  This function removes a discovered device from a device list of
*            a port context
*
*  \param   tsddPortContext_Ins      Pointer to the target port context
*  \param   tdsaDeviceData_Ins       Pointer to the target device
*  \param   agRoot                   Pointer to the root data structure of
*                                    TD and Lower layer

*
*  \Return: none
*
*****************************************************************************/
osGLOBAL void
tdssRemoveSASFromSharedcontext(
                               tdsaPortContext_t *tdsaPortContext_Ins,
                               tdsaDeviceData_t  *tdsaDeviceData_Ins,
                               agsaRoot_t        *agRoot
                               )
{
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaPortContext_t *onePortContext = agNULL;
  tdList_t          *PortContextList;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  bit32             found = agTRUE;

  TI_DBG3(("tdssRemoveSASFromSharedcontext: start\n"));
  /* find a right portcontext */
  PortContextList = tdsaAllShared->MainPortContextList.flink;
  while (PortContextList != &(tdsaAllShared->MainPortContextList))
  {
    onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, PortContextList);
    if (onePortContext == agNULL)
    {
      TI_DBG1(("tdssRemoveDevicedataFromSharedcontext: onePortContext is NULL!!!\n"));
      return;
    }
    if (onePortContext == tdsaPortContext_Ins)
    {
      TI_DBG4(("tdssRemoveDevicedataFromSharedcontext: found; oneportContext ID %d\n", onePortContext->id));
      break;
    }
    PortContextList = PortContextList->flink;
  }

  /* find a device's existence */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tdssRemoveDevicedataFromSharedcontext: oneDeviceData is NULL!!!\n"));
      return;
    }
    if ((oneDeviceData->SASAddressID.sasAddressHi
         == SA_DEVINFO_GET_SAS_ADDRESSHI(&tdsaDeviceData_Ins->agDeviceInfo))
        &&
        (oneDeviceData->SASAddressID.sasAddressLo ==
         SA_DEVINFO_GET_SAS_ADDRESSLO(&tdsaDeviceData_Ins->agDeviceInfo)))
    {
      TI_DBG4(("tdssRemoveDevicedataFromSharedcontext: pid %d did %d\n", onePortContext->id, oneDeviceData->id));
      found = agFALSE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }

  if (found == agFALSE)
  {
    TI_DBG6(("tdssRemoveDevicedataFromSharedcontext: can't find the right devicedata in MainLink\n"));
    return;
  }

  /* remove it and put it back to FreeLink of Devicedata */
  TI_DBG6(("tdssRemoveDevicedataFromSharedcontext: removing ... pid %d did %d\n", onePortContext->id, oneDeviceData->id));

  /* invalidate the device but keep it on the list for persistency */
  oneDeviceData->valid = agFALSE;

  return;
}

/*****************************************************************************
*! \brief tdssRemoveAllDevicedataFromPortcontext
*
*  Purpose:  This function removes all discovered devices from a device list of
*            a port context
*
*  \param   tdsaDeviceData           Pointer to a device header
*
*  \Return: none
*
*****************************************************************************/
osGLOBAL void
tdssRemoveAllDevicelistFromPortcontext(
                                       tdsaPortContext_t *PortContext_Ins,
                                       agsaRoot_t        *agRoot
                                       )
{

  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;

  TI_DBG6(("tdssRemoveAllDevicedataFromPortcontext: start\n"));

  /*
    loop through device list and find the matching portcontext. Then invalidate the
    matching devices
  */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tdssRemoveAllDevicelistFromPortcontext: oneDeviceData is NULL!!!\n"));
      return;
    }
    if (oneDeviceData->tdPortContext == PortContext_Ins)
    {
      TI_DBG4(("tdssRemoveAllDevicelistFromPortcontext: pid %d did %d\n", PortContext_Ins->id, oneDeviceData->id));
      PortContext_Ins->Count--;
      oneDeviceData->valid = agFALSE;
    }
    DeviceListList = DeviceListList->flink;
  }

  return;
}


#ifdef INITIATOR_DRIVER
#ifdef TD_DISCOVER
/*****************************************************************************
*! \brief tdssNewAddSASToSharedcontext
*
*  Purpose:  This function adds a discovered SAS device to a device list of
*            a shared context. Used only in discovery.
*
*  \param   agRoot          Pointer to chip/driver Instance.
*  \param   onePortContext  Pointer to the target port context
*  \param   agSASSubID      Pointer to the SAS identification.
*
*  \Return:
*           Pointer to the device data
*
*****************************************************************************/
osGLOBAL tdsaDeviceData_t *
tdssNewAddSASToSharedcontext(
                             agsaRoot_t           *agRoot,
                             tdsaPortContext_t    *onePortContext,
                             tdsaSASSubID_t       *agSASSubID,
                             tdsaDeviceData_t     *oneExpDeviceData,
                             bit8                 phyID
                             )
{
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  bit32             new_device = agTRUE;

  TI_DBG3(("tdssNewAddSASToSharedcontext: start\n"));
  /*
    find a right portcontext
    then, get devicedata from FreeLink in DeviceList
    then, do pointer operations
    then, add the devicedata to the portcontext
  */


  TI_DBG3(("tdssNewAddSASToSharedcontext: oneportContext ID %d\n", onePortContext->id));
  /* find a device's existence */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("tdssNewAddSASToSharedcontext: oneDeviceData is NULL!!!\n"));
      return agNULL;
    }
    if ((oneDeviceData->SASAddressID.sasAddressHi == agSASSubID->sasAddressHi) &&
        (oneDeviceData->SASAddressID.sasAddressLo == agSASSubID->sasAddressLo) &&
        (oneDeviceData->tdPortContext == onePortContext)
        )
    {
      TI_DBG3(("tdssNewAddSASToSharedcontext: pid %d did %d\n", onePortContext->id, oneDeviceData->id));
      new_device = agFALSE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }

  /* new device */
  if (new_device == agTRUE)
  {
    TI_DBG3(("tdssNewAddSASToSharedcontext: new device\n"));
    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    if (!TDLIST_NOT_EMPTY(&(tdsaAllShared->FreeDeviceList)))
    {
      tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
      TI_DBG1(("tdssNewAddSASToSharedcontext: empty DeviceData FreeLink\n"));
      return agNULL;
    }

    TDLIST_DEQUEUE_FROM_HEAD(&DeviceListList, &(tdsaAllShared->FreeDeviceList));
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, FreeLink, DeviceListList);

    TI_DBG3(("tdssNewAddSASToSharedcontext: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));

    onePortContext->Count++;
    oneDeviceData->agRoot = agRoot;
    /* saving sas address */
    oneDeviceData->SASAddressID.sasAddressLo = agSASSubID->sasAddressLo;
    oneDeviceData->SASAddressID.sasAddressHi = agSASSubID->sasAddressHi;
    oneDeviceData->initiator_ssp_stp_smp = agSASSubID->initiator_ssp_stp_smp;
    oneDeviceData->target_ssp_stp_smp = agSASSubID->target_ssp_stp_smp;
    oneDeviceData->tdPortContext = onePortContext;
    /* handles both SAS target and STP-target, SATA-device */
    if (!DEVICE_IS_SATA_DEVICE(oneDeviceData) && !DEVICE_IS_STP_TARGET(oneDeviceData))
    {
      oneDeviceData->DeviceType = TD_SAS_DEVICE;
    }
    else
    {
      oneDeviceData->DeviceType = TD_SATA_DEVICE;
    }

    oneDeviceData->ExpDevice = oneExpDeviceData;
    /* set phyID only when it has initial value of 0xFF */
    if (oneDeviceData->phyID == 0xFF)
    {
      oneDeviceData->phyID = phyID;
    }
#ifdef FDS_DM
    oneDeviceData->valid = agTRUE;
#else

    /* incremental discovery */
    /* add device to incremental-related link. Report using this link
       when incremental discovery is done */
    if (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_INCREMENTAL_START)
    {
      TI_DBG3(("tdssNewAddSASToSharedcontext: incremental discovery\n"));
      TI_DBG3(("tdssNewAddSASToSharedcontext: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
      TI_DBG3(("tdssNewAddSASToSharedcontext: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
      oneDeviceData->valid2 = agTRUE;
    }
    else
    {
      TI_DBG3(("tdssNewAddSASToSharedcontext: full discovery\n"));
      TI_DBG3(("tdssNewAddSASToSharedcontext: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
      TI_DBG3(("tdssNewAddSASToSharedcontext: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
      oneDeviceData->valid = agTRUE;
    }
#endif
    /* add the devicedata to the portcontext */
    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->MainLink), &(tdsaAllShared->MainDeviceList));
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    TI_DBG4(("tdssNewAddSASToSharedcontext: one case pid %d did %d \n", onePortContext->id, oneDeviceData->id));
    TI_DBG4(("tdssNewAddSASToSharedcontext: new case pid %d did %d phyID %d\n", onePortContext->id, oneDeviceData->id, oneDeviceData->phyID));
  }
  else /* old device */
  {
    TI_DBG3(("tdssNewAddSASToSharedcontext: old device\n"));
    TI_DBG3(("tdssNewAddSASToSharedcontext: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));

    oneDeviceData->agRoot = agRoot;
    /* saving sas address */
    oneDeviceData->SASAddressID.sasAddressLo = agSASSubID->sasAddressLo;
    oneDeviceData->SASAddressID.sasAddressHi = agSASSubID->sasAddressHi;
    oneDeviceData->initiator_ssp_stp_smp = agSASSubID->initiator_ssp_stp_smp;
    oneDeviceData->target_ssp_stp_smp = agSASSubID->target_ssp_stp_smp;
    oneDeviceData->tdPortContext = onePortContext;
    /* handles both SAS target and STP-target, SATA-device */
    if (!DEVICE_IS_SATA_DEVICE(oneDeviceData) && !DEVICE_IS_STP_TARGET(oneDeviceData))
    {
      oneDeviceData->DeviceType = TD_SAS_DEVICE;
    }
    else
    {
      oneDeviceData->DeviceType = TD_SATA_DEVICE;
    }

    oneDeviceData->ExpDevice = oneExpDeviceData;
    /* set phyID only when it has initial value of 0xFF */
    if (oneDeviceData->phyID == 0xFF)
    {
      oneDeviceData->phyID = phyID;
    }

#ifdef FDS_DM
    oneDeviceData->valid = agTRUE;
#else
    if (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_INCREMENTAL_START)
    {
      TI_DBG3(("tdssNewAddSASToSharedcontext: incremental discovery\n"));
      TI_DBG3(("tdssNewAddSASToSharedcontext: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
      TI_DBG3(("tdssNewAddSASToSharedcontext: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
      oneDeviceData->valid2 = agTRUE;
    }
    else
    {
      TI_DBG3(("tdssNewAddSASToSharedcontext: full discovery\n"));
      TI_DBG3(("tdssNewAddSASToSharedcontext: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
      TI_DBG3(("tdssNewAddSASToSharedcontext: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
      oneDeviceData->valid = agTRUE;
    }
#endif
    TI_DBG4(("tdssNewAddSASToSharedcontext: old case pid %d did %d phyID %d\n", onePortContext->id, oneDeviceData->id, oneDeviceData->phyID));

  }
  return oneDeviceData;
}

/*****************************************************************************
*! \brief tdsaFindRegNValid
*
*  Purpose:  This function finds a device which is registered and valid in
*            the device list. Used only in incremental discovery.
*
*  \param   agRoot          Pointer to chip/driver Instance.
*  \param   onePortContext  Pointer to the target port context
*  \param   tdsaDeviceData  Pointer to a device list header
*  \param   agSASSubID      Pointer to the SAS identification.
*
*  \Return:
*           Pointer to the device data
*
*****************************************************************************/
osGLOBAL tdsaDeviceData_t *
tdsaFindRegNValid(
                  agsaRoot_t           *agRoot,
                  tdsaPortContext_t    *onePortContext,
                  tdsaSASSubID_t       *agSASSubID
                  )
{
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  bit32             found = agFALSE;

  TI_DBG3(("tdsaFindRegNValid: start\n"));

  /* find a device's existence */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  if (onePortContext->discovery.type == TDSA_DISCOVERY_OPTION_FULL_START)
  {
    TI_DBG3(("tdsaFindRegNValid: Full discovery\n"));
    while (DeviceListList != &(tdsaAllShared->MainDeviceList))
    {
      oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
      if (oneDeviceData == agNULL)
      {
        TI_DBG1(("tdsaFindRegNValid: oneDeviceData is NULL!!!\n"));
        return agNULL;
      }
      if ((oneDeviceData->SASAddressID.sasAddressHi == agSASSubID->sasAddressHi) &&
          (oneDeviceData->SASAddressID.sasAddressLo == agSASSubID->sasAddressLo) &&
          (oneDeviceData->registered == agTRUE) &&
          (oneDeviceData->valid == agTRUE) &&
          (oneDeviceData->tdPortContext == onePortContext)
          )
      {
        TI_DBG3(("tdsaFindRegNValid: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
        TI_DBG3(("tdsaFindRegNValid: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
        TI_DBG3(("tdsaFindRegNValid: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
        found = agTRUE;
        break;
      }
      DeviceListList = DeviceListList->flink;
    }
  }
  else
  {
    /* incremental discovery */
    TI_DBG3(("tdsaFindRegNValid: Incremental discovery\n"));
    while (DeviceListList != &(tdsaAllShared->MainDeviceList))
    {
      oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
      if (oneDeviceData == agNULL)
      {
        TI_DBG1(("tdsaFindRegNValid: oneDeviceData is NULL!!!\n"));
        return agNULL;
      }
      if ((oneDeviceData->SASAddressID.sasAddressHi == agSASSubID->sasAddressHi) &&
          (oneDeviceData->SASAddressID.sasAddressLo == agSASSubID->sasAddressLo) &&
          (oneDeviceData->registered == agTRUE) &&
          (oneDeviceData->valid2 == agTRUE) &&
          (oneDeviceData->tdPortContext == onePortContext)
          )
      {
        TI_DBG3(("tdsaFindRegNValid: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
        TI_DBG3(("tdsaFindRegNValid: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
        TI_DBG3(("tdsaFindRegNValid: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
        found = agTRUE;
        break;
      }
      DeviceListList = DeviceListList->flink;
    }
  }



  if (found == agFALSE)
  {
    TI_DBG3(("tdsaFindRegNValid: end returning NULL\n"));
    return agNULL;
  }
  else
  {
    TI_DBG3(("tdsaFindRegNValid: end returning NOT NULL\n"));
    return oneDeviceData;
  }

}

//registered to LL or not
/*****************************************************************************
*! \brief tdssNewSASorNot
*
*  Purpose:  This function finds whether a device is registered or not
*
*  \param   agRoot          Pointer to chip/driver Instance.
*  \param   onePortContext  Pointer to the target port context
*  \param   agSASSubID      Pointer to the SAS identification.
*
*  \Return:
*           agTRUE   Device is not registered (New device).
*           agFALSE  Device is registered (Old device).
*
*****************************************************************************/
bit32
tdssNewSASorNot(
                                 agsaRoot_t           *agRoot,
                                 tdsaPortContext_t    *onePortContext,
                                 tdsaSASSubID_t       *agSASSubID
                                 )
{
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  bit32             ret = agTRUE;

  TI_DBG3(("tdssNewSASorNot: start\n"));

  /* find a device's existence */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
    if ((oneDeviceData->SASAddressID.sasAddressHi == agSASSubID->sasAddressHi) &&
        (oneDeviceData->SASAddressID.sasAddressLo == agSASSubID->sasAddressLo) &&
        (oneDeviceData->registered == agTRUE) &&
        (oneDeviceData->tdPortContext == onePortContext)
        )
    {
      TI_DBG3(("tdssNewSASorNot: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
      ret = agFALSE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }



  TI_DBG3(("tdssNewSASorNot: end\n"));

  return ret;
}



/*****************************************************************************
*! \brief  tdssSASDiscoveringExpanderAlloc
*
*  Purpose:  This function allocates an expander from the pre-allocated memory
*            pool.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData:  Pointer to the device data.
*
*  \return:
*           Pointer to expander on success
*           agNULL              on failure
*
*   \note:
*
*****************************************************************************/
osGLOBAL tdsaExpander_t *
tdssSASDiscoveringExpanderAlloc(
                                tiRoot_t                 *tiRoot,
                                tdsaPortContext_t        *onePortContext,
                                tdsaDeviceData_t         *oneDeviceData
                                )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaExpander_t    *oneExpander = agNULL;
  tdList_t          *ExpanderList;

  /*
    move the expander from freeExpanderList
    and ground the expander by TDLIST_DEQUEUE_THIS
  */


  TI_DBG3(("tdssSASDiscoveringExpanderAlloc: start\n"));
  TI_DBG3(("tdssSASDiscoveringExpanderAlloc: did %d\n", oneDeviceData->id));
  TI_DBG3(("tdssSASDiscoveringExpanderAlloc: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  TI_DBG3(("tdssSASDiscoveringExpanderAlloc: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdssSASDiscoveringExpanderAlloc: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return agNULL;
  }

  tdsaDumpAllFreeExp(tiRoot);

  if (TDLIST_EMPTY(&(tdsaAllShared->freeExpanderList)))
  {
    TI_DBG1(("tdssSASDiscoveringExpanderAlloc: no free expanders\n"));
    return agNULL;
  }

  tdsaSingleThreadedEnter(tiRoot, TD_DISC_LOCK);
  TDLIST_DEQUEUE_FROM_HEAD(&ExpanderList, &(tdsaAllShared->freeExpanderList));
  tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
  //  oneExpander = TDLIST_OBJECT_BASE(tdsaContext_t, freeExpanderList, ExpanderList);
  oneExpander = TDLIST_OBJECT_BASE(tdsaExpander_t, linkNode, ExpanderList);

  if (oneExpander != agNULL)
  {
    TI_DBG3(("tdssSASDiscoveringExpanderAlloc: expander id %d\n", oneExpander->id));

    tdsaSingleThreadedEnter(tiRoot, TD_DISC_LOCK);
    TDLIST_DEQUEUE_THIS(&(oneExpander->linkNode));
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);

    oneExpander->tdDevice = oneDeviceData;
    oneExpander->tdUpStreamExpander = agNULL;
    oneExpander->tdCurrentDownStreamExpander = agNULL;
    oneExpander->tdReturnginExpander = agNULL;
    oneExpander->hasUpStreamDevice = agFALSE;
    oneExpander->numOfUpStreamPhys = 0;
    oneExpander->currentUpStreamPhyIndex = 0;
    oneExpander->discoveringPhyId = 0;
    oneExpander->underDiscovering = agFALSE;
    osti_memset( &(oneExpander->currentIndex), 0, sizeof(oneExpander->currentIndex));

    oneDeviceData->tdExpander = oneExpander;
  }

  return oneExpander;
}

/*****************************************************************************
*! \brief  tdssSASDiscoveringExpanderAdd
*
*  Purpose:  This function adds an expander to the expander list.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneExpander: Pointer to the expander data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdssSASDiscoveringExpanderAdd(
                              tiRoot_t                 *tiRoot,
                              tdsaPortContext_t        *onePortContext,
                              tdsaExpander_t           *oneExpander
                              )
{
#ifdef TD_INTERNAL_DEBUG
  tdList_t          *ExpanderList;
  tdsaExpander_t    *tempExpander;
#endif

  /* move the expander to discoveringExpanderList */

  TI_DBG3(("tdssSASDiscoveringExpanderAdd: start\n"));
  TI_DBG3(("tdssSASDiscoveringExpanderAdd: expander id %d\n", oneExpander->id));
  TI_DBG3(("tdssSASDiscoveringExpanderAdd: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdssSASDiscoveringExpanderAdd: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));

  if (onePortContext->valid == agFALSE)
  {
    TI_DBG1(("tdssSASDiscoveringExpanderAdd: aborting discovery\n"));
    tdsaSASDiscoverAbort(tiRoot, onePortContext);
    return;
  }


  if (onePortContext->discovery.status == DISCOVERY_UP_STREAM)
  {
    TI_DBG3(("tdssSASDiscoveringExpanderAdd: UPSTREAM\n"));
  }
  else if (onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
  {
    TI_DBG3(("tdssSASDiscoveringExpanderAdd: DOWNSTREAM\n"));
  }
  else
  {
    TI_DBG3(("tdssSASDiscoveringExpanderAdd: status %d\n", onePortContext->discovery.status));
  }

  TI_DBG3(("tdssSASDiscoveringExpanderAdd: BEFORE\n"));
  tdsaDumpAllExp(tiRoot, onePortContext, oneExpander);


  if ( oneExpander->underDiscovering == agFALSE)
  {
    TI_DBG3(("tdssSASDiscoveringExpanderAdd: ADDED \n"));

    oneExpander->underDiscovering = agTRUE;
    tdsaSingleThreadedEnter(tiRoot, TD_DISC_LOCK);
    TDLIST_ENQUEUE_AT_TAIL(&(oneExpander->linkNode), &(onePortContext->discovery.discoveringExpanderList));
    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
  }

  TI_DBG3(("tdssSASDiscoveringExpanderAdd: AFTER\n"));
  tdsaDumpAllExp(tiRoot, onePortContext, oneExpander);

#ifdef TD_INTERNAL_DEBUG
  /* debugging */
  if (TDLIST_EMPTY(&(onePortContext->discovery.discoveringExpanderList)))
  {
    TI_DBG3(("tdssSASDiscoveringExpanderAdd: empty discoveringExpanderList\n"));
    return;
  }
  ExpanderList = onePortContext->discovery.discoveringExpanderList.flink;
  while (ExpanderList != &(onePortContext->discovery.discoveringExpanderList))
  {
    tempExpander = TDLIST_OBJECT_BASE(tdsaExpander_t, linkNode, ExpanderList);
    TI_DBG3(("tdssSASDiscoveringExpanderAdd: expander id %d\n", tempExpander->id));
    ExpanderList = ExpanderList->flink;
  }
#endif

  return;
}

/* temp */
osGLOBAL bit32
tdssSASFindDiscoveringExpander(
               tiRoot_t                 *tiRoot,
               tdsaPortContext_t        *onePortContext,
               tdsaExpander_t           *oneExpander
              )
{
  tdList_t          *ExpanderList;
  tdsaExpander_t    *tempExpander;
  tdsaPortContext_t *tmpOnePortContext = onePortContext;
  bit32             ret = agFALSE;

  TI_DBG3(("tdssSASFindDiscoveringExpander: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdssSASFindDiscoveringExpander: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));

  if (TDLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
  {
    TI_DBG1(("tdssSASFindDiscoveringExpander: empty discoveringExpanderList\n"));
    return ret;
  }
  ExpanderList = tmpOnePortContext->discovery.discoveringExpanderList.flink;
  while (ExpanderList != &(tmpOnePortContext->discovery.discoveringExpanderList))
  {
    tempExpander = TDLIST_OBJECT_BASE(tdsaExpander_t, linkNode, ExpanderList);
    if (tempExpander == oneExpander)
    {
      TI_DBG3(("tdssSASFindDiscoveringExpander: match!!! expander id %d\n", tempExpander->id));
      TI_DBG3(("tdssSASFindDiscoveringExpander: exp addrHi 0x%08x\n", tempExpander->tdDevice->SASAddressID.sasAddressHi));
      TI_DBG3(("tdssSASFindDiscoveringExpander: exp addrLo 0x%08x\n", tempExpander->tdDevice->SASAddressID.sasAddressLo));
      ret = agTRUE;
      break;
    }

    ExpanderList = ExpanderList->flink;
  }


  return ret;

}
/* to be tested */
/* move the expander to freeExpanderList */
/*****************************************************************************
*! \brief  tdssSASDiscoveringExpanderRemove
*
*  Purpose:  This function removes an expander from the expander list.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneExpander: Pointer to the expander data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
tdssSASDiscoveringExpanderRemove(
                                 tiRoot_t                 *tiRoot,
                                 tdsaPortContext_t        *onePortContext,
                                 tdsaExpander_t           *oneExpander
                                 )
{
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared   = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
#ifdef TD_INTERNAL_DEBUG
  tdList_t          *ExpanderList;
  tdsaExpander_t    *tempExpander;
#endif

  TI_DBG3(("tdssSASDiscoveringExpanderRemove: start\n"));
  TI_DBG3(("tdssSASDiscoveringExpanderRemove: expander id %d\n", oneExpander->id));
  TI_DBG3(("tdssSASDiscoveringExpanderRemove: exp addrHi 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressHi));
  TI_DBG3(("tdssSASDiscoveringExpanderRemove: exp addrLo 0x%08x\n", oneExpander->tdDevice->SASAddressID.sasAddressLo));


  TI_DBG3(("tdssSASDiscoveringExpanderRemove: BEFORE\n"));
  tdsaDumpAllExp(tiRoot, onePortContext, oneExpander);
  tdsaDumpAllUpExp(tiRoot, onePortContext, oneExpander);
  tdsaDumpAllFreeExp(tiRoot);

#ifdef TD_INTERNAL_DEBUG
  /* debugging */
  TI_DBG3(("tdssSASDiscoveringExpanderRemove: BEFORE\n"));
  if (TDLIST_EMPTY(&(onePortContext->discovery.discoveringExpanderList)))
  {
    TI_DBG3(("tdssSASDiscoveringExpanderRemove: empty discoveringExpanderList\n"));
  }
  ExpanderList = onePortContext->discovery.discoveringExpanderList.flink;
  while (ExpanderList != &(onePortContext->discovery.discoveringExpanderList))
  {
    tempExpander = TDLIST_OBJECT_BASE(tdsaExpander_t, linkNode, ExpanderList);
    TI_DBG3(("tdssSASDiscoveringExpanderRemove: expander id %d\n", tempExpander->id));
    ExpanderList = ExpanderList->flink;
  }
#endif

  // if is temporary till smp problem is fixed
  if (tdssSASFindDiscoveringExpander(tiRoot, onePortContext, oneExpander) == agTRUE)
  {
    oneExpander->underDiscovering = agFALSE;
    oneExpander->discoveringPhyId = 0;
    tdsaSingleThreadedEnter(tiRoot, TD_DISC_LOCK);
    TDLIST_DEQUEUE_THIS(&(oneExpander->linkNode));

    if (onePortContext->discovery.status == DISCOVERY_UP_STREAM)
    {
      TI_DBG3(("tdssSASDiscoveringExpanderRemove: DISCOVERY_UP_STREAM\n"));
      TDLIST_ENQUEUE_AT_TAIL(&(oneExpander->upNode), &(onePortContext->discovery.UpdiscoveringExpanderList));
      onePortContext->discovery.NumOfUpExp++;
    }
    else
    {
      TI_DBG3(("tdssSASDiscoveringExpanderRemove: Status %d\n", onePortContext->discovery.status));
      TDLIST_ENQUEUE_AT_TAIL(&(oneExpander->linkNode), &(tdsaAllShared->freeExpanderList));
    }

    tdsaSingleThreadedLeave(tiRoot, TD_DISC_LOCK);
  } //end temp if
  else
  {
    TI_DBG1(("tdssSASDiscoveringExpanderRemove: !!! problem !!!\n"));
  }

  TI_DBG3(("tdssSASDiscoveringExpanderRemove: AFTER\n"));
  tdsaDumpAllExp(tiRoot, onePortContext, oneExpander);
  tdsaDumpAllUpExp(tiRoot, onePortContext, oneExpander);

  tdsaDumpAllFreeExp(tiRoot);

#ifdef TD_INTERNAL_DEBUG
  /* debugging */
  TI_DBG3(("tdssSASDiscoveringExpanderRemove: AFTER\n"));
  if (TDLIST_EMPTY(&(onePortContext->discovery.discoveringExpanderList)))
  {
    TI_DBG3(("tdssSASDiscoveringExpanderRemove: empty discoveringExpanderList\n"));
  }
  ExpanderList = onePortContext->discovery.discoveringExpanderList.flink;
  while (ExpanderList != &(onePortContext->discovery.discoveringExpanderList))
  {
    tempExpander = TDLIST_OBJECT_BASE(tdsaExpander_t, linkNode, ExpanderList);
    TI_DBG3(("tdssSASDiscoveringExpanderRemove: expander id %d\n", tempExpander->id));
    ExpanderList = ExpanderList->flink;
  }
#endif

  return;
}

#ifdef SATA_ENABLE

/*****************************************************************************
*! \brief tdssNewAddSATAToSharedcontext
*
*  Purpose:  This function adds a discovered SATA device to a device list of
*            a shared context. Used only in discovery.
*
*  \param   tiRoot  Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*  \param   agRoot          Pointer to chip/driver Instance.
*  \param   onePortContext  Pointer to the target port context
*  \param   tdsaDeviceData  Pointer to a device list header
*  \param   agSATADeviceInfo      Pointer to the SATA device information.
*  \param   Signature       Pointer to SATA signature
*  \param   pm              Port multiplier
*  \param   pmField         Port multiplier field
*  \param   connectionRate  Connection rate
*
*  \Return:
*           Pointer to the device data
*
*****************************************************************************/
osGLOBAL tdsaDeviceData_t *
tdssNewAddSATAToSharedcontext(tiRoot_t             *tiRoot,
                              agsaRoot_t           *agRoot,
                              tdsaPortContext_t    *onePortContext,
                              agsaSATADeviceInfo_t *agSATADeviceInfo,
                              bit8                    *Signature,
                              bit8                    pm,
                              bit8                    pmField,
                              bit32                   connectionRate,
                              tdsaDeviceData_t        *oneExpDeviceData,
                              bit8                    phyID
                              )
{
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  int               new_device = agTRUE;

  TI_DBG5(("tdssNewAddSATAToSharedcontext: start\n"));



  TI_DBG5(("tdssNewAddSATAToSharedcontext: oneportContext ID %d\n", onePortContext->id));


#ifdef RPM_SOC
  /* Find a device's existence */
  DeviceListList = tdsaAllShared->MainDeviceList.flink;
  while (DeviceListList != &(tdsaAllShared->MainDeviceList))
  {
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);

    if ((osti_memcmp (((char *)&oneDeviceData->satDevData.satIdentifyData),
                      ((char *)&agSATADeviceInfo->sataIdentifyData),
                      sizeof(agsaSATAIdentifyData_t)) == 0))
    {
      TI_DBG5(("tdssNewAddSATAToSharedcontext: pid %d did %d\n",
        onePortContext->id, oneDeviceData->id));
      new_device = agFALSE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }
#else
 

#endif

  /* New device */
  if (new_device == agTRUE)
  {
    TI_DBG5(("tdssNewAddSATAToSharedcontext: new device\n"));

    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    if (!TDLIST_NOT_EMPTY(&(tdsaAllShared->FreeDeviceList)))
    {
      tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
      TI_DBG1(("tdssNewAddSATAToSharedcontext: ERROR empty DeviceData FreeLink\n"));
      return oneDeviceData;
    }

    TDLIST_DEQUEUE_FROM_HEAD(&DeviceListList, &(tdsaAllShared->FreeDeviceList));
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, FreeLink, DeviceListList);

    onePortContext->Count++;
    oneDeviceData->DeviceType = TD_SATA_DEVICE;
    oneDeviceData->agRoot = agRoot;
    TI_DBG5(("tdssNewAddSATAToSharedcontext: oneDeviceData %p\n", oneDeviceData));
    TI_DBG5(("tdssNewAddSATAToSharedcontext: pSatDevData=%p\n", &oneDeviceData->satDevData));


    /* saving PortMultiplier(PM) field */
    oneDeviceData->satDevData.satPMField = pmField;

    /* saving signature */
    osti_memcpy(&(oneDeviceData->satDevData.satSignature), Signature, 8);

    /*
      saving device type
      ATA device type; here should be either ATA_ATA_DEVICE or ATA_ATAPI_DEVICE
    */
     oneDeviceData->satDevData.satDeviceType = tdssSATADeviceTypeDecode(agSATADeviceInfo->signature);
     TI_DBG3(("tdssNewAddSATAToSharedcontext: device type %d\n",  oneDeviceData->satDevData.satDeviceType));

#ifdef RPM_SOC_REMOVED
    /* print device signature - Word8 */
    TI_DBG3(("tdssNewAddSATAToSharedcontext: Word8 %x signature: %x %x %x %x %x %x %x %x\n",
             agSATADeviceInfo->sataIdentifyData.word1_9[7],
             agSATADeviceInfo->signature[0], agSATADeviceInfo->signature[1],
             agSATADeviceInfo->signature[2], agSATADeviceInfo->signature[3],
             agSATADeviceInfo->signature[4], agSATADeviceInfo->signature[5],
             agSATADeviceInfo->signature[6], agSATADeviceInfo->signature[7] ));
#endif



    oneDeviceData->tdPortContext = onePortContext;
    oneDeviceData->valid = agTRUE;

    oneDeviceData->ExpDevice = oneExpDeviceData;
    oneDeviceData->phyID = phyID;

    /* Add the devicedata to the portcontext */
    tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
    TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->MainLink), &(tdsaAllShared->MainDeviceList));
    tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
    TI_DBG5(("tdssNewAddSATAToSharedcontext: one case pid %d did %d \n",
      onePortContext->id, oneDeviceData->id));
  }
  else /* old device */
  {
    TI_DBG5(("tdssNewAddSATAToSharedcontext: old device\n"));

    onePortContext->Count++;

    oneDeviceData->DeviceType = TD_SATA_DEVICE;
    oneDeviceData->agRoot = agRoot;

    oneDeviceData->tdPortContext = onePortContext;
    oneDeviceData->valid = agTRUE;

    oneDeviceData->ExpDevice = oneExpDeviceData;
    oneDeviceData->phyID = phyID;

  }

  return oneDeviceData;

}
#endif /* SATA_ENABLE */
#endif /* TD_DISCOVER */
#endif /* INITIATOR_DRIVER */

#ifdef TARGET_DRIVER
/*****************************************************************************
*! \brief  tdssReportRemovals
*
*  Purpose:  This function goes through device list and removes all devices
*            belong to the portcontext. This function also deregiters those
*            devices. This function is called in case of incremental discovery
*            failure.
*
*  \param   agRoot        :  Pointer to chip/driver Instance.
*  \param   onePortContext: Pointer to the portal context instance.
*  \param   oneDeviceData: Pointer to the device data.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
ttdssReportRemovals(
                  agsaRoot_t           *agRoot,
                  tdsaPortContext_t    *onePortContext,
                  bit32                flag
                  )
{
  tdsaDeviceData_t  *oneDeviceData = agNULL;
  tdList_t          *DeviceListList;
  tdsaRootOsData_t  *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t          *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t        *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  bit32             removed = agFALSE;
  agsaEventSource_t *eventSource;
  bit32             PhyID;
  bit32             HwAckSatus;
  tdsaDeviceData_t  *tmpDeviceData = agNULL;

  TI_DBG1(("ttdssReportRemovals: start\n"));
  /* in case nothing was registered */
  PhyID = onePortContext->eventPhyID;
  if (tdsaAllShared->eventSource[PhyID].EventValid == agTRUE &&
      onePortContext->RegisteredDevNums == 0 &&
      PhyID != 0xFF
      )
  {
    TI_DBG1(("ttdssReportRemovals: calling saHwEventAck\n"));
    eventSource = &(tdsaAllShared->eventSource[PhyID].Source);
    HwAckSatus = saHwEventAck(
                              agRoot,
                              agNULL, /* agContext */
                              0,
                              eventSource, /* agsaEventSource_t */
                              0,
                              0
                              );
    if ( HwAckSatus != AGSA_RC_SUCCESS)
    {
      TI_DBG1(("ttdssReportRemovals: failing in saHwEventAck; status %d\n", HwAckSatus));
    }

    /* toggle */
    tdsaAllShared->eventSource[PhyID].EventValid = agFALSE;
    if (onePortContext->valid == agFALSE)
    {
      tdsaPortContextReInit(tiRoot, onePortContext);
      /*
        put all devices belonging to the onePortContext
        back to the free link
       */
      tdsaSingleThreadedEnter(tiRoot, TD_PORT_LOCK);
      TDLIST_DEQUEUE_THIS(&(onePortContext->MainLink));
      TDLIST_ENQUEUE_AT_TAIL(&(onePortContext->FreeLink), &(tdsaAllShared->FreePortContextList));
      tdsaSingleThreadedLeave(tiRoot, TD_PORT_LOCK);
    }
  }
  else
  {
    if (TDLIST_EMPTY(&(tdsaAllShared->MainDeviceList)))
    {
      TI_DBG1(("ttdssReportRemovals: empty device list\n"));
      return;
    }

    DeviceListList = tdsaAllShared->MainDeviceList.flink;
    while (DeviceListList != &(tdsaAllShared->MainDeviceList))
    {
      oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
      if( oneDeviceData == agNULL )
      {
        break;
      }
      TI_DBG1(("ttdssReportRemovals: loop did %d\n", oneDeviceData->id));
      TI_DBG1(("ttdssReportRemovals: sasAddrHi 0x%08x sasAddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));
      TI_DBG1(("ttdssReportRemovals: valid %d valid2 %d\n", oneDeviceData->valid, oneDeviceData->valid2));
      TI_DBG1(("ttdssReportRemovals: directlyAttached %d registered %d\n", oneDeviceData->directlyAttached, oneDeviceData->registered));
      if ( oneDeviceData->tdPortContext == onePortContext)
      {
        TI_DBG1(("ttdssReportRemovals: right portcontext pid %d\n", onePortContext->id));
        if (oneDeviceData->valid == agTRUE && oneDeviceData->registered == agTRUE)
        {
          TI_DBG1(("ttdssReportRemovals: removing\n"));

          /* notify only reported devices to OS layer*/
          removed = agTRUE;

          /* all targets except expanders */
          TI_DBG1(("ttdssReportRemovals: calling tdsaAbortAll\n"));
          TI_DBG1(("ttdssReportRemovals: did %d\n", oneDeviceData->id));
          TI_DBG1(("ttdssReportRemovals: sasAddrHi 0x%08x sasAddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));
          tmpDeviceData = oneDeviceData;
          ttdsaAbortAll(tiRoot, agRoot, oneDeviceData);


          /* reset valid bit */
          oneDeviceData->valid = agFALSE;
          oneDeviceData->valid2 = agFALSE;
          oneDeviceData->registered = agFALSE;
        }
        /* called by port invalid case */
        if (flag == agTRUE)
        {
          oneDeviceData->tdPortContext = agNULL;
        }
#ifdef REMOVED /* removed */
        /* directly attached SATA -> always remove it */
        if (oneDeviceData->DeviceType == TD_SATA_DEVICE &&
            oneDeviceData->directlyAttached == agTRUE)
        {
          TI_DBG1(("ttdssReportRemovals: device did %d\n", oneDeviceData->id));
          tdsaSingleThreadedEnter(tiRoot, TD_DEVICE_LOCK);
          TDLIST_DEQUEUE_THIS(&(oneDeviceData->MainLink));
          TDLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(tdsaAllShared->FreeDeviceLis));
          DeviceListList = tdsaAllShared->MainDeviceList.flink;
          if (TDLIST_EMPTY(&(tdsaAllShared->MainDeviceList)))
          {
            tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
            break;
          }
          else
          {
            tdsaSingleThreadedLeave(tiRoot, TD_DEVICE_LOCK);
          }
        }
        else
        {
          DeviceListList = DeviceListList->flink;
        }
#endif /* REMOVED */
        DeviceListList = DeviceListList->flink;
      }
      else
      {
        if (oneDeviceData->tdPortContext != agNULL)
        {
          TI_DBG1(("ttdssReportRemovals: different portcontext; oneDeviceData->tdPortContext pid %d oneportcontext pid %d\n", oneDeviceData->tdPortContext->id, onePortContext->id));
        }
        else
        {
          TI_DBG1(("ttdssReportRemovals: different portcontext; oneDeviceData->tdPortContext pid NULL oneportcontext pid %d\n", onePortContext->id));
        }
        DeviceListList = DeviceListList->flink;
      }
    }

    if (removed == agTRUE)
    {
      TI_DBG1(("ttdssReportRemovals: removed at the end\n"));
      ostiTargetEvent(
                      tiRoot,
                      onePortContext->tiPortalContext,
                      &(tmpDeviceData->tiDeviceHandle),
                      tiTgtEventTypeDeviceChange,
                      tiDeviceRemoval,
                      agNULL
                      );
    }
  } /* big else */
  return;
}
#endif /* TARGET_DRIVER */


/*****************************************************************************
*! \brief  tdsaRotateQnumber
*
*  Purpose:  This function generates inbound queue number.
*
*  \param   tiRoot: Pointer to the OS Specific module allocated tiRoot_t
*                   instance.
*
*  \return:
*           Queue number
*
*   \note:
*
*****************************************************************************/
FORCEINLINE bit32
tdsaRotateQnumber(tiRoot_t                *tiRoot,
                  tdsaDeviceData_t        *oneDeviceData )
{
  bit32             ret = 0;

  TI_DBG6(("tdsaRotateQnumber: start\n"));
  if (oneDeviceData == agNULL)
  {
    return 0;
  }
  ret = (oneDeviceData->OutQID << 16) | oneDeviceData->InQID;
  return ret;
}

osGLOBAL bit32
tdsaRotateQnumber1(tiRoot_t                *tiRoot,
                  tdsaDeviceData_t        *oneDeviceData )
{
  tdsaRoot_t        *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
//  static int Last_Q;
//  bit32             denom = tdsaAllShared->QueueConfig.numOutboundQueues;
  bit32             ret = 0;
  if (oneDeviceData == agNULL)
  {
//    Last_Q= 0;
    return 0;
  }

/* alway use highest Q number */
  ret = ((tdsaAllShared->QueueConfig.numOutboundQueues-1) << 16) | (tdsaAllShared->QueueConfig.numInboundQueues-1);

  return(ret);
}

#ifdef REMOVED
osGLOBAL bit32
tdsaRotateQnumber(tiRoot_t                *tiRoot,
                  tdsaDeviceData_t        *oneDeviceData )
{
  tdsaRoot_t        *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t     *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  bit32             denom = tdsaAllShared->QueueConfig.numInboundQueues;
  bit32             ret = 0;

  /* inbound queue number */
  tdsaAllShared->IBQnumber++;
  if (tdsaAllShared->IBQnumber % denom == 0) /* % Qnumber*/
  {
    tdsaAllShared->IBQnumber = 0;
  }
  TI_DBG6(("tdsaRotateQnumber: IBQnumber %d\n", tdsaAllShared->IBQnumber));


  /* outbound queue number */
  tdsaAllShared->OBQnumber++;
  denom = tdsaAllShared->QueueConfig.numOutboundQueues;
  if (tdsaAllShared->OBQnumber % denom == 0) /* % Qnumber*/
  {
    tdsaAllShared->OBQnumber = 0;
  }
  TI_DBG6(("tdsaRotateQnumber: OBQnumber %d\n", tdsaAllShared->OBQnumber));

  ret = (tdsaAllShared->OBQnumber << 16) | tdsaAllShared->IBQnumber;
  return ret;
}
#endif


void t_MacroCheck(  agsaRoot_t       *agRoot)
{
  TI_DBG1(("t_MacroCheck:tIsSPC           %d\n",tIsSPC(agRoot)));
  TI_DBG1(("t_MacroCheck:tIsSPCHIL        %d\n",tIsSPCHIL(agRoot)));
  TI_DBG1(("t_MacroCheck:tIsSPCv          %d\n",tIsSPCv(agRoot)));
  TI_DBG1(("t_MacroCheck:tIsSPCve         %d\n",tIsSPCve(agRoot)));
  TI_DBG1(("t_MacroCheck:tIsSPCvplus      %d\n",tIsSPCvplus(agRoot)));
  TI_DBG1(("t_MacroCheck:tIsSPCveplus     %d\n",tIsSPCveplus(agRoot)));
  TI_DBG1(("t_MacroCheck:tIsSPCADAPvplus  %d\n",tIsSPCADAPvplus(agRoot)));
  TI_DBG1(("t_MacroCheck:tIsSPCADAPveplus %d\n",tIsSPCADAPveplus(agRoot)));
  TI_DBG1(("t_MacroCheck:tIsSPC12Gv       %d\n",tIsSPC12Gv(agRoot)));
  TI_DBG1(("t_MacroCheck:tIsSPC12Gve      %d\n",tIsSPC12Gve(agRoot)));
  TI_DBG1(("t_MacroCheck:tIsSPC12Gvplus   %d\n",tIsSPC12Gvplus(agRoot)));
  TI_DBG1(("t_MacroCheck:tIsSPC12Gveplus  %d\n",tIsSPC12Gveplus(agRoot)));
  TI_DBG1(("t_MacroCheck:tiIS_SPC         %d\n",tiIS_SPC(agRoot)   ));
  TI_DBG1(("t_MacroCheck:tiIS_HIL         %d\n",tiIS_HIL(agRoot)   ));
  TI_DBG1(("t_MacroCheck:tiIS_SPC6V       %d\n",tiIS_SPC6V(agRoot) ));
  TI_DBG1(("t_MacroCheck:tiIS_SPC_ENC     %d\n",tiIS_SPC_ENC(agRoot) ));
  TI_DBG1(("t_MacroCheck:tIsSPCV12G       %d\n",tIsSPCV12G(agRoot) ));
}
