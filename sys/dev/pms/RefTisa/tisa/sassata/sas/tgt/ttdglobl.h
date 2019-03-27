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
 * $RCSfile: ttdglobl.h,v $
 *
 * Copyright 2006 PMC-Sierra, Inc.
 *
 * $Author: vempatin $
 * $Revision: 113679 $
 * $Date: 2012-04-16 14:35:19 -0700 (Mon, 16 Apr 2012) $
 *
 * #define for SAS target in SAS/SATA TD layer
 *
 */

    
#ifndef __TTD_GLOBALS_H__
    
#define __TTD_GLOBALS_H__
    
/* 
 * Transport Target specific default parameters.
 */ 
#define DEFAULT_XCHGS                   256
#define DEFAULT_TGT_TIMER_TICK          1000000     /* 1 second */
#define DEFAULT_MAX_TARGETS             256
#define DEFAULT_BLOCK_SIZE              512


/* Exchange field accessors */
#define TD_XCHG_CONTEXT(ti_root)                    (&TD_GET_TITGT_CONTEXT(ti_root)->ttdsaXchgData)
#define TD_XCHG_CONTEXT_MAX_NUM_XCHGS(ti_root)      (TD_XCHG_CONTEXT(ti_root)->maxNumXchgs) 
#define TD_XCHG_CONTEXT_NO_USED(ti_root)            (TD_XCHG_CONTEXT(ti_root)->noUsed)
#define TD_XCHG_CONTEXT_NO_FREED(ti_root)           (TD_XCHG_CONTEXT(ti_root)->noFreed)
#define TD_XCHG_CONTEXT_NO_CMD_RCVD(ti_root)        (TD_XCHG_CONTEXT(ti_root)->noCmdRcvd)
#define TD_XCHG_CONTEXT_NO_START_IO(ti_root)        (TD_XCHG_CONTEXT(ti_root)->noStartIo)
#define TD_XCHG_CONTEXT_NO_SEND_RSP(ti_root)        (TD_XCHG_CONTEXT(ti_root)->noSendRsp)
#define TD_XCHG_CONTEXT_NO_IO_COMPLETED(ti_root)    (TD_XCHG_CONTEXT(ti_root)->noCompleted)

#define TD_XCHG_GET_CONTEXT(ti_request)             ((ttdsaXchg_t *)(ti_request)->tdData)
#define TD_XCHG_GET_STATE(xchg)                     (xchg->state)
#define TD_XCHG_SET_STATE(xchg, val)                (xchg->state) = (val)

#define TD_XCHG_STATE_ACTIVE                        1
#define TD_XCHG_STATE_INACTIVE                      0

#define READ_GOOD_RESPONSE                          0x1
#define WRITE_GOOD_RESPONSE                         0x2

#endif  /* __TTD_GLOBALS_H__ */
