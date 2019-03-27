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
/********************************************************************************
**
** Version Control Information:
**
**
*******************************************************************************/
/********************************************************************************
**    
**   tiscsi.h
**
**   Abstract:   This module contains SCSI related data structure definition.
**     
********************************************************************************/

#ifndef TISCSI_H
#define TISCSI_H


/*
 * SCSI Sense Data
 */
typedef struct 
{
  bit8       snsRespCode;
  bit8       snsSegment;
  bit8       senseKey;          /* sense key                                */
  bit8       info[4];
  bit8       addSenseLen;       /* 11 always                                */
  bit8       cmdSpecific[4];
  bit8       addSenseCode;      /* additional sense code                    */
  bit8       senseQual;         /* additional sense code qualifier          */
  bit8       fru;
  bit8       skeySpecific[3];
} scsiRspSense_t;



#endif  /* TISCSI_H */
