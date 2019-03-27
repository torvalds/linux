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
*
* $FreeBSD$
*
********************************************************************************/
/*******************************************************************************/
/** \file
 *
 *
 * data structures for SAS intiator in SAS/SATA TD layer
 *
 */

#ifndef __ITDTYPES_H__
    
#define __ITDTYPES_H__
    
#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/tisa/api/titypes.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/common/tdtypes.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdlist.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itddefs.h>

/** \brief data structure for the options of SAS initiator
 *
 * This data structure contains options for SAS initiator such as the maximum
 * number of allowed targets and DIF capability
 *
 */
typedef struct itdssOperatingOption_s  {
  bit32 MaxTargets; /**< the maximum number of allowed targets */
  /* this is read from a file or #defined
     then passed to TD layer from tiInitiatorResource_t
   */
  bit32 UsecsPerTick;       /* in micro seconds */
} itdssOperatingOption_t;




#endif  /* __ITDTYPES_H__ */
