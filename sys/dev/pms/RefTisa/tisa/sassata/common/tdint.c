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
 * This file contains interrupt related functions in the SAS/SATA TD layer
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
*! \biref  tiCOMInterruptHandler
*
*  Purpose: This function is called to service the hardware interrupt of the
*           hardware.
*
*  \param tiRoot:   Pointer to initiator specific root data structure  for this
*                   instance of the driver.
*
*  \param channelNum: The zero-base channel number of the controller.
*                     0xFFFFFFFF indicates that the OS-App Specific layer does 
*                     not provide the channel number. The TD/LL Layer needs to 
*                     discover of any of its own channels that are causing the 
*                     interrupt.
*
*  \return None
*
*  \note - The only thing that this API will do is to acknowledge and mask
*          the necessary hardware interrupt register. The actual processing
*          of the interrupt handler is done in tiCOMDelayedInterruptHandler().
*
*****************************************************************************/
FORCEINLINE bit32 
tiCOMInterruptHandler(
                      tiRoot_t * tiRoot,
                      bit32      channelNum)
{
  tdsaRoot_t      *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t   *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
  agsaRoot_t      *agRoot = &(tdsaAllShared->agRootNonInt);
  bit32           interruptPending = agFALSE;

  interruptPending = saInterruptHandler(agRoot, channelNum);
  
  return interruptPending;
  
} /* tiCOMInterruptHandler() */


/*****************************************************************************
*! \brief tiCOMDelayedInterruptHandler
*
*  Purpose: This function is called to process the task associated with the
*           interrupt handler. The task that this handler needs to do includes:
*           completion of I/O, login event, error event, etc
*
*  \param tiRoot:     Pointer to initiator specific root data structure for
*                     this instance of the driver.
*  \param channelNum: The zero-base channel number of the controller.
*                     0xFFFFFFFF indicates that the OS-App Specific layer does 
*                     not provide the channel number. The TD/LL Layer needs to 
*                     discover of any of its own channels that are causing the 
*                     interrupt.
*  \param count:      Count on how many items (such as IO completion) need to
*                     be processed in this context.
*  \param interruptContext: The thread/process context within which this 
*                           function is called.
*
*             tiInterruptContext:     this function is called within an
*                                     interrupt context.
*             tiNonInterruptContext:  this function is called outside an
*                                     interrupt context.
*  \return None
*
*****************************************************************************/
FORCEINLINE 
bit32 
tiCOMDelayedInterruptHandler(
                             tiRoot_t  *tiRoot,
                             bit32     channelNum,
                             bit32     count, 
                             bit32     context
                             )
{
  tdsaRoot_t      *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t   *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
  agsaRoot_t      *agRoot = agNULL;
  bit32            completed = 0;

  TDSA_OUT_ENTER(tiRoot);

  if(context == tiInterruptContext)
  {
    agRoot = &(tdsaAllShared->agRootInt);
  }
  else
  {
    agRoot = &(tdsaAllShared->agRootNonInt);
  }

  completed = saDelayedInterruptHandler(agRoot, channelNum, count);

  if(completed == 0)
  {
    TI_DBG3(("tiCOMDelayedInterruptHandler: processedMsgCount zero\n"));
  }

  
  TDSA_OUT_LEAVE(tiRoot);

  return(completed);
} /* tiCOMDelayedInterruptHandler() */


/*****************************************************************************
*! \brief tiCOMSystemInterruptsActive
*
*  Purpose: This function is called to indicate whether interrupts are 
*           active or not from this point in time.
*
*  \param tiRoot:        Pointer to initiator specific root data structure for
*                        this instance of the driver.
*  \param sysIntsActive: Boolean value either true or false
*
*  \return None
*
*****************************************************************************/
osGLOBAL void
tiCOMSystemInterruptsActive(
                            tiRoot_t * tiRoot, 
                            bit32 sysIntsActive
                            )
{

  tdsaRoot_t     *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t  *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
  agsaRoot_t     *agRoot;
  agRoot = &(tdsaAllShared->agRootNonInt);

#ifdef SPC_POLLINGMODE
  if(sysIntsActive)  return;
#endif /* SPC_POLLINGMODE */

  tdsaAllShared->flags.sysIntsActive = sysIntsActive;

  TI_DBG6(("tiCOMSystemInterruptsActive: start\n"));
  /* enable low level interrupts */
  if(agRoot->sdkData != agNULL)
  {
    saSystemInterruptsActive(
                             agRoot, 
                             (agBOOLEAN) tdsaAllShared->flags.sysIntsActive
                             );
  }
  
  TI_DBG6(("tiCOMSystemInterruptsActive: end\n"));
} /* tiCOMSystemInterruptsActive */


osGLOBAL void
tiComCountActiveIORequests(
                            tiRoot_t * tiRoot 
                          )
{
  tdsaRoot_t     *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t  *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
  agsaRoot_t     *agRoot;
  agRoot = &(tdsaAllShared->agRootNonInt);
  saCountActiveIORequests(agRoot );
}

/*****************************************************************************
*! \brief tiCOMInterruptEnable
*
*  Purpose: This function is called to enable an interrupts on the specified channel 
*           active or not from this point in time.
*
*  \param tiRoot:        Pointer to initiator specific root data structure for
*                        this instance of the driver.
*  \param : channelNum   vector number for MSIX  Zero for legacy interrupt 
*
*  \return None
*
*****************************************************************************/
osGLOBAL FORCEINLINE 
void
tiCOMInterruptEnable(
                      tiRoot_t * tiRoot,
                      bit32      channelNum)
{
  tdsaRoot_t     *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t  *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
  agsaRoot_t     *agRoot;
  agRoot = &(tdsaAllShared->agRootNonInt);

  saSystemInterruptsEnable(agRoot, channelNum);
}
