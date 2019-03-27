/*******************************************************************************
**
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
 * $RCSfile: ttdtimer.c,v $
 *
 * Copyright 2006 PMC-Sierra, Inc.
 *
 * $Author: hasungwo $
 * $Revision: 112322 $
 * $Date: 2012-01-04 19:23:42 -0800 (Wed, 04 Jan 2012) $
 *
 * This file contains initiator IO related functions in TD layer
 *
 */
#include <osenv.h>
#include <ostypes.h>
#include <osdebug.h>

#include <sa.h>
#include <saapi.h>
#include <saosapi.h>

#include <titypes.h>
#include <ostiapi.h>
#include <tiapi.h>
#include <tiglobal.h>

#include <tdtypes.h>
#include <osstring.h>
#include <tdutil.h>

#ifdef INITIATOR_DRIVER
#include <itdtypes.h>
#include <itddefs.h>
#include <itdglobl.h>
#endif

#ifdef TARGET_DRIVER
#include "ttdglobl.h"
#include "ttdtxchg.h"
#include "ttdtypes.h"
#endif

#include <tdsatypes.h>
#include <tdproto.h>

/*****************************************************************************
*
* tiTargetTimerTick
*
*  Purpose:  This function is called by the os-specific module
*
*  Parameters:
*
*    tiRoot:            Pointer to driver/port instance.
*    
*
*  Return: None
*
* 
*****************************************************************************/

osGLOBAL void 
tiTGTTimerTick(tiRoot_t  *tiRoot)
{
  /* does nothing for now */
  return;
}
