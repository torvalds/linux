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
*******************************************************************************/

/*******************************************************************************
Module Name:  
  lxproto.h
Abstract:  
  PMC-Sierra initiator/target driver common function prototype definition
Environment:  
  Kernel or loadable module  
Notes:

******************************************************************************/

#ifndef __LX_PROTO_H__
#define __LX_PROTO_H__

void       agtiapi_DelayMSec(U32);
void       agtiapi_DelaySec(U32);
void       agtiapi_DisplayRsc(ag_card_info_t *);
agBOOLEAN  agtiapi_InitResource(ag_card_info_t *);
agBOOLEAN  agtiapi_typhAlloc(ag_card_info_t *);
int        agtiapi_ScopeDMARes(ag_card_info_t *);
void       agtiapi_ReleasePCIMem(ag_card_info_t *);

STATIC agBOOLEAN agtiapi_MemAlloc( ag_card_info_t *thisCardInst,
                                   void          **VirtAlloc,
                                   vm_paddr_t     *pDmaAddr,
                                   void          **VirtAddr,
                                   U32            *pPhysAddrUp,
                                   U32            *pPhysAddrLow,
                                   U32             MemSize,
                                   U32             Type,
                                   U32             Align );

void       agtiapi_MemFree(ag_card_info_t *); 
U32        agtiapi_PCIMemSize(device_t, U32, U32);
void       agtiapi_Probe(void);
int        agtiapi_ProbeCard(device_t, ag_card_info_t *, int);
void       agtiapi_Setup(S08 *, S32 *);
 
#ifdef CHAR_DEVICE  
//int        agtiapi_Open(struct inode *, struct file *);
//int        agtiapi_Close(struct inode *, struct file *);
#endif

#ifdef AGTIAPI_INCLUDE_PROCS
static void agtiapi_ProcDel(ag_card_info_t *pInfo);
static int  agtiapi_ProcAdd(ag_card_info_t *pInfo);
#endif

#ifdef TEST_DUMP_FCTRACE_BUFFER
#if fcEnableTraceFunctions == 1
static void agtiapi_DumpTraceBuffer(ag_card_info_t *pInfo);
#endif
#endif


#endif
