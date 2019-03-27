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
*
* $FreeBSD$
*
********************************************************************************/
/*******************************************************************************/
/** \file
 *
 *
 * #define for SAS intiator in SAS/SATA TD layer
 *
 */


#ifndef __ITDDEFS_H__
#define __ITDDEFS_H__
/* discovery related state */
#define ITD_DSTATE_NOT_STARTED                 0 
#define ITD_DSTATE_STARTED                     1
#define ITD_DSTATE_COMPLETED                   2

/* SAS/SATA discovery status */
#define DISCOVERY_NOT_START                       0                       /**< status indicates discovery not started */
#define DISCOVERY_UP_STREAM                       1                       /**< status indicates discover upstream */
#define DISCOVERY_DOWN_STREAM                     2                       /**< status indicates discover downstream */
#define DISCOVERY_CONFIG_ROUTING                  3                       /**< status indicates discovery config routing table */
#define DISCOVERY_SAS_DONE                        4                       /**< status indicates discovery done */
#define DISCOVERY_REPORT_PHY_SATA                 5                       /**< status indicates discovery report phy sata */
#endif /* __ITDDEFS_H__ */
