/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/******************************************************************************
 @File          fm_sp.h

 @Description   FM SP  ...
*//***************************************************************************/
#ifndef __FM_SP_H
#define __FM_SP_H

#include "std_ext.h"
#include "error_ext.h"
#include "list_ext.h"

#include "fm_sp_common.h"
#include "fm_common.h"


#define __ERR_MODULE__  MODULE_FM_SP

typedef struct {
    t_FmBufferPrefixContent             bufferPrefixContent;
    e_FmDmaSwapOption                   dmaSwapData;
    e_FmDmaCacheOption                  dmaIntContextCacheAttr;
    e_FmDmaCacheOption                  dmaHeaderCacheAttr;
    e_FmDmaCacheOption                  dmaScatterGatherCacheAttr;
    bool                                dmaWriteOptimize;
    uint16_t                            liodnOffset;
    bool                                noScatherGather;
    t_FmBufPoolDepletion                *p_BufPoolDepletion;
    t_FmBackupBmPools                   *p_BackupBmPools;
    t_FmExtPools                        extBufPools;
} t_FmVspEntryDriverParams;

typedef struct {
    bool                        valid;
    volatile bool               lock;
    uint8_t                     pointedOwners;
    uint16_t                    absoluteSpId;
    uint8_t                     internalBufferOffset;
    t_FmSpBufMargins            bufMargins;
    t_FmSpIntContextDataCopy    intContext;
    t_FmSpBufferOffsets         bufferOffsets;
    t_Handle                    h_Fm;
    e_FmPortType                portType;           /**< Port type */
    uint8_t                     portId;             /**< Port Id - relative to type */
    uint8_t                     relativeProfileId;
    struct fm_pcd_storage_profile_regs *p_FmSpRegsBase;
    t_FmExtPools                extBufPools;
    t_FmVspEntryDriverParams    *p_FmVspEntryDriverParams;
} t_FmVspEntry;


#endif /* __FM_SP_H */
